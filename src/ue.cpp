#include "ue.h"
#include "log.h"

#include <windows.h>
#include <MinHook.h>

#include <atomic>
#include <map>
#include <mutex>
#include <vector>

namespace ue {

bool SafeRead(const void* src, void* dst, size_t n)
{
    __try {
        memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

namespace {

// UE 5.x core object layout (engine ABI, not game members; identical across game patches, verified by SelfTest).
namespace off {
constexpr int ObjFlags = 0x8, ObjIndex = 0xC, ObjClass = 0x10, ObjName = 0x18, ObjOuter = 0x20;
constexpr int StructSuper = 0x40, StructChildren = 0x48, StructChildProps = 0x50, StructSize = 0x58;
constexpr int UFieldNext = 0x28;
constexpr int FFieldClass = 0x8, FFieldNext = 0x18, FFieldName = 0x20;
constexpr int PropFlags = 0x38, PropOffset = 0x44, PropExtra = 0x70; // extra: bool masks / struct / object class
constexpr int FuncFlags = 0xB0;
} // namespace off

// EObjectFlags (stable since UE4): objects with these are being torn down and must not be touched.
constexpr uint32_t RF_ClassDefaultObject = 0x10, RF_ArchetypeObject = 0x20, RF_BeginDestroyed = 0x8000,
                   RF_FinishDestroyed = 0x10000, RF_MirroredGarbage = 0x40000000;

using ProcessEventFn = void (*)(UObject*, UFunction*, void*);

struct {
    uint8_t* base;
    uint8_t** chunks;      // FChunkedFixedUObjectArray::Objects
    int32_t* numElements;  // live count, grows at runtime
    int32_t perChunk;
    int32_t itemSize;
    uint8_t** nameBlocks;  // FNameEntryAllocator::Blocks
    int lenShift;
    ProcessEventFn processEvent; // trampoline once hooked: calls never re-enter our hook
    unsigned gameThread;
    bool ready;
} g;

std::mutex g_cacheMutex;
struct CachedProp { Weak owner; Prop value; };
struct CachedFunction { Weak owner, function; };
std::map<std::pair<const void*, std::string>, CachedProp> g_props;
std::map<std::pair<const void*, std::string>, CachedFunction> g_funcs;
std::map<std::string, Weak> g_classes;

std::mutex g_taskMutex;
std::vector<std::function<void()>> g_tasks;
std::atomic<bool> g_tasksPending;
std::function<void()> g_tick, g_frameTick;

// GetTickCount64 moves in 15.6 ms steps: a ">= 16 ms" check on it really meant 32 Hz.
uint64_t PreciseMs()
{
    static const double toMs = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return 1000.0 / double(f.QuadPart);
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return uint64_t(double(c.QuadPart) * toMs);
}

template <class T> T Read(const void* p, int offset) { return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(p) + offset); }

bool Plausible(uint64_t v) { return v > 0x10000 && v < 0x7FFFFFFF0000 && !(v & 7); }

struct Range {
    uint8_t* begin = nullptr;
    uint8_t* end = nullptr;
};

Range Section(const char* name)
{
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(g.base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(g.base + dos->e_lfanew);
    IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
        if (strncmp(reinterpret_cast<const char*>(s->Name), name, 8) == 0)
            return {g.base + s->VirtualAddress, g.base + s->VirtualAddress + s->Misc.VirtualSize};
    return {};
}

// ------------------------------------------------------------------ names

// FNameEntry: 2-byte header (bIsWide in bit 0, length in the top bits) followed by the characters.
bool NameEntry(uint32_t index, const uint8_t** chars, int* len, bool* wide)
{
    uint32_t block = index >> 16;
    if (block >= 8192 || !g.nameBlocks[block]) return false;
    const uint8_t* entry = g.nameBlocks[block] + (index & 0xFFFF) * 2;
    uint16_t header = *reinterpret_cast<const uint16_t*>(entry);
    *wide = header & 1;
    *len = header >> g.lenShift;
    *chars = entry + 2;
    return *len > 0 && *len < 1024;
}

std::string NameString(const void* fname)
{
    uint32_t index = Read<uint32_t>(fname, 0), number = Read<uint32_t>(fname, 4);
    const uint8_t* chars;
    int len;
    bool wide;
    if (!NameEntry(index, &chars, &len, &wide)) return {};
    std::string s;
    if (wide) {
        int n = WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<const wchar_t*>(chars), len, nullptr, 0, nullptr, nullptr);
        s.resize(n);
        WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<const wchar_t*>(chars), len, s.data(), n, nullptr, nullptr);
    } else {
        s.assign(reinterpret_cast<const char*>(chars), len);
    }
    if (number) s += "_" + std::to_string(number - 1);
    return s;
}

// Allocation-free comparison, used when scanning every object.
bool NameEquals(const void* fname, const char* text)
{
    if (Read<uint32_t>(fname, 4) != 0) return NameString(fname) == text; // numbered names are rare
    const uint8_t* chars;
    int len;
    bool wide;
    if (!NameEntry(Read<uint32_t>(fname, 0), &chars, &len, &wide) || wide) return false;
    return size_t(len) == strlen(text) && memcmp(chars, text, len) == 0;
}

const void* SuperOf(const void* s) { return Read<const void*>(s, off::StructSuper); }

// ------------------------------------------------------------------ discovery

bool FindGObjects()
{
    Range data = Section(".data");
    for (uint8_t* p = data.begin; p + 0x20 <= data.end; p += 8) {
        auto chunks = Read<uint64_t>(p, 0);
        int32_t maxEl = Read<int32_t>(p, 0x10), numEl = Read<int32_t>(p, 0x14);
        int32_t maxChunks = Read<int32_t>(p, 0x18), numChunks = Read<int32_t>(p, 0x1C);
        if (!Plausible(chunks) || numChunks < 1 || numChunks > 0x40 || maxChunks < numChunks || maxChunks > 0x800) continue;
        if (numEl <= 0x800 || numEl > maxEl || maxEl % maxChunks) continue;
        int32_t perChunk = maxEl / maxChunks;
        if (perChunk < 0x4000 || perChunk > 0x80000 || numEl / perChunk + 1 != numChunks) continue;

        uint64_t chunk0 = 0;
        if (!SafeRead(reinterpret_cast<void*>(chunks), &chunk0, 8) || !Plausible(chunk0)) continue;
        for (int32_t itemSize : {0x18, 0x10, 0x20}) { // FUObjectItem size varies between engine versions
            bool ok = true;
            for (int32_t i = 1; i <= 8 && ok; i++) {
                uint64_t obj = 0;
                int32_t idx = -1;
                ok = SafeRead(reinterpret_cast<void*>(chunk0 + uint64_t(i) * itemSize), &obj, 8) && Plausible(obj) &&
                     SafeRead(reinterpret_cast<void*>(obj + off::ObjIndex), &idx, 4) && idx == i;
            }
            if (!ok) continue;
            g.chunks = reinterpret_cast<uint8_t**>(chunks);
            g.numElements = reinterpret_cast<int32_t*>(p + 0x14);
            g.perChunk = perChunk;
            g.itemSize = itemSize;
            km::Log("GObjects at exe+0x%llX: %d objects, %d per chunk, item size 0x%X",
                    static_cast<unsigned long long>(p - g.base), numEl, perChunk, itemSize);
            return true;
        }
    }
    return false;
}

bool FindGNames()
{
    // Blocks[0] starts with the entries "None" then "ByteProperty": look for a pointer to that in .data.
    Range data = Section(".data");
    for (uint8_t* p = data.begin + 0x10; p + 8 <= data.end; p += 8) {
        auto block0 = Read<uint64_t>(p, 0);
        uint8_t head[12];
        if (!Plausible(block0) || !SafeRead(reinterpret_cast<void*>(block0), head, sizeof(head))) continue;
        if (memcmp(head + 2, "None", 4) != 0 || memcmp(head + 8, "Byte", 4) != 0) continue;
        uint16_t byteHeader = *reinterpret_cast<uint16_t*>(head + 6);
        int shift = 0;
        while (shift < 16 && (byteHeader >> shift) != 12) shift++; // "ByteProperty" has 12 characters
        if (shift == 16) continue;
        g.nameBlocks = reinterpret_cast<uint8_t**>(p);
        g.lenShift = shift;
        // FNameEntryAllocator: Lock, CurrentBlock, CurrentByteCursor, Blocks[] (logged to diagnose odd layouts)
        km::Log("GNames at exe+0x%llX (blocks at +0x10), length shift %d, current block %u",
                static_cast<unsigned long long>(p - 0x10 - g.base), shift, Read<uint32_t>(p, -8));
        return true;
    }
    return false;
}

const uint8_t* FindInRange(const uint8_t* from, size_t len, const uint8_t* pat, size_t patLen, int wildcard)
{
    for (size_t i = 0; i + patLen <= len; i++) {
        size_t j = 0;
        while (j < patLen && (int(j) == wildcard || from[i + j] == pat[j])) j++;
        if (j == patLen) return from + i;
    }
    return nullptr;
}

ProcessEventFn oProcessEvent;

void InvokeTask(void* fn) { (*static_cast<std::function<void()>*>(fn))(); }

// SEH barrier: a feature hitting bad memory is logged and skipped instead of crashing the game.
bool Guarded(std::function<void()>& fn)
{
    __try {
        InvokeTask(&fn);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void GameThreadWork()
{
    static uint64_t lastTick, lastPoll, lastFrameTick;
    static int64_t lastFrame = -1;
    uint64_t now = PreciseMs();
    if (g_tasksPending.exchange(false)) {
        std::vector<std::function<void()>> tasks;
        {
            std::lock_guard lock(g_taskMutex);
            tasks.swap(g_tasks);
        }
        for (auto& t : tasks)
            if (!Guarded(t)) km::Log("A game-thread task faulted and was skipped");
    }
    if (g_tick && g.ready && now - lastTick >= 16) { // not while the self-test still waits for the engine
        lastTick = now;
        static int faults;
        if (!Guarded(g_tick) && faults++ < 5) km::Log("Tick faulted (feature skipped this frame)");
    }
    // Once per engine frame: poll the frame counter at most once a millisecond.
    if (g_frameTick && g.ready && now != lastPoll) {
        lastPoll = now;
        static UObject* ksl = FindObject("Default__KismetSystemLibrary");
        Call count(ksl, "GetFrameCount");
        int64_t frame = count && count.Run() ? count.Out<int64_t>("ReturnValue") : -1;
        if (frame != lastFrame && now - lastFrameTick >= 4) {
            lastFrame = frame;
            lastFrameTick = now;
            static int faults;
            if (!Guarded(g_frameTick) && faults++ < 5) km::Log("Frame tick faulted (skipped this frame)");
        }
    }
}

void hkProcessEvent(UObject* obj, UFunction* fn, void* params)
{
    static bool inside; // game thread only
    if (!inside && GetCurrentThreadId() == g.gameThread) {
        inside = true;
        GameThreadWork();
        inside = false;
    }
    oProcessEvent(obj, fn, params);
}

bool FindAndHookProcessEvent()
{
    UObject* first = nullptr;
    for (int32_t i = 0; i < 16 && !first; i++) first = ObjectAt(i);
    if (!first) return false;
    Range text = Section(".text");
    void** vtable = Read<void**>(first, 0);
    // ProcessEvent tests Function->FunctionFlags against FUNC_Native (0x400) and FUNC_HasOutParms (0x400000).
    const uint8_t native[] = {0xF7, 0x00, off::FuncFlags, 0, 0, 0, 0x00, 0x04, 0x00, 0x00};
    const uint8_t outParms[] = {0xF7, 0x00, off::FuncFlags, 0, 0, 0, 0x00, 0x00, 0x40, 0x00};
    for (int i = 0; i < 0x150; i++) {
        auto* fn = static_cast<uint8_t*>(vtable[i]);
        if (fn < text.begin || fn >= text.end) break;
        if (fn[0] == 0xE9) fn = fn + 5 + *reinterpret_cast<int32_t*>(fn + 1); // follow jump thunks
        if (fn < text.begin || fn + 0xF00 > text.end) continue;
        if (!FindInRange(fn, 0x400, native, sizeof(native), 1) || !FindInRange(fn, 0xF00, outParms, sizeof(outParms), 1))
            continue;
        MH_STATUS s = MH_CreateHook(fn, reinterpret_cast<void*>(&hkProcessEvent), reinterpret_cast<void**>(&oProcessEvent));
        if (s == MH_OK) s = MH_EnableHook(fn);
        km::Log("ProcessEvent: vtable index 0x%X, exe+0x%llX, hook %s", i,
                static_cast<unsigned long long>(fn - g.base), MH_StatusToString(s));
        g.processEvent = s == MH_OK ? oProcessEvent : reinterpret_cast<ProcessEventFn>(fn);
        return s == MH_OK;
    }
    return false;
}

bool SelfTest(bool quiet)
{
    UClass* actor = FindClass("Actor");
    Prop root = actor ? FindProp(actor, "RootComponent") : Prop{};
    Prop canBeDamaged = actor ? FindProp(actor, "bCanBeDamaged") : Prop{};
    bool ok = root && root.offset >= 0x28 && root.offset <= 0x1000 && canBeDamaged.boolMask;    if (ok || !quiet)
        km::Log("Self-test: Actor %s, RootComponent @0x%X, bCanBeDamaged @0x%X mask 0x%X", actor ? "found" : "MISSING",
                root.offset, canBeDamaged.offset, canBeDamaged.boolMask);
    if (!ok) return false;

    for (const char* name : {"PlayerCharacter", "CoreController", "BP_InfernoCheatManager_C", "ItemManager",
                             "PlayerAttributeSet", "EnemyCharacter", "GameplayStatics", "Item"})
        km::Log("  class %-26s %s", name, FindClass(name) ? "found" : "missing");

    // End-to-end check of ProcessEvent + parameters + game thread: ask the engine for its version.
    RunOnGameThread([] {
        Call c(FindObject("Default__KismetSystemLibrary"), "GetEngineVersion");
        if (c && c.Run()) // shortcut: the returned FString is leaked (one-off, a few bytes)
            km::Log("Game thread ProcessEvent OK (thread %lu): engine %s", GetCurrentThreadId(),
                    c.Out<FString>("ReturnValue").ToUtf8().c_str());
        else
            km::Log("Game thread ProcessEvent: GetEngineVersion not found");
    });
    return true;
}

} // namespace

// ------------------------------------------------------------------ public API

bool Init(unsigned gameThreadId)
{
    g.base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    g.gameThread = gameThreadId;
    MH_STATUS mh = MH_Initialize();
    if (mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED) return false;

    // The engine may still be filling GObjects right after the window appears: retry for a while.
    for (int attempt = 0; attempt < 60 && !FindGObjects(); attempt++) Sleep(1000);
    if (!g.chunks) {
        km::Log("GObjects not found: game features disabled");
        return false;
    }
    if (!FindGNames()) {
        km::Log("GNames not found: game features disabled");
        return false;
    }
    if (!FindAndHookProcessEvent()) {
        km::Log("ProcessEvent not found: game features disabled");
        return false;
    }
    // On a slow PC the window can appear before the engine has built every class's properties (a user's self-test
    // failed once on a game that had passed it before): retry for up to a minute, forgetting the lookups that missed.
    for (int attempt = 1;; attempt++) {
        g.ready = SelfTest(attempt < 60); // quiet until the last try
        if (g.ready || attempt == 60) break;
        if (attempt == 1) km::Log("Self-test: the engine is still starting, retrying for up to a minute");
        {
            std::lock_guard lock(g_cacheMutex);
            g_classes.clear();
            g_props.clear();
            g_funcs.clear();
        }
        Sleep(1000);
    }
    km::Log("Reflection %s", g.ready ? "ready" : "FAILED self-test: game features disabled");
    return g.ready;
}

bool Ready() { return g.ready; }

int32_t ObjectCount() { return g.numElements ? *g.numElements : 0; }

UObject* ObjectAt(int32_t index)
{
    if (index < 0 || index >= ObjectCount()) return nullptr;
    uint8_t* chunk = g.chunks[index / g.perChunk];
    if (!chunk) return nullptr;
    auto* obj = *reinterpret_cast<UObject**>(chunk + size_t(index % g.perChunk) * g.itemSize);
    if (!obj || (Read<uint32_t>(obj, off::ObjFlags) & (RF_BeginDestroyed | RF_FinishDestroyed | RF_MirroredGarbage)))
        return nullptr;
    return obj;
}

std::string Name(const UObject* obj) { return obj ? NameString(reinterpret_cast<const uint8_t*>(obj) + off::ObjName) : ""; }
UClass* ClassOf(const UObject* obj) { return obj ? Read<UClass*>(obj, off::ObjClass) : nullptr; }
UObject* OuterOf(const UObject* obj) { return obj ? Read<UObject*>(obj, off::ObjOuter) : nullptr; }

bool IsChildOf(const UClass* cls, const UClass* parent)
{
    for (const void* c = cls; c && parent; c = SuperOf(c))
        if (c == parent) return true;
    return false;
}

bool IsA(const UObject* obj, const UClass* cls) { return IsChildOf(ClassOf(obj), cls); }

bool IsDefaultObject(const UObject* obj)
{
    return Read<uint32_t>(obj, off::ObjFlags) & (RF_ClassDefaultObject | RF_ArchetypeObject);
}

UClass* FindClass(const char* name)
{
    {
        std::lock_guard lock(g_cacheMutex);
        if (auto it = g_classes.find(name); it != g_classes.end()) {
            if (auto* alive = it->second.Get()) return reinterpret_cast<UClass*>(alive);
            g_classes.erase(it);
        }
    }
    static UClass* classClass; // UClass's own class: the object named "Class" whose class is itself
    UClass* found = nullptr;
    ForEachObject([&](UObject* o) {
        if (!NameEquals(reinterpret_cast<uint8_t*>(o) + off::ObjName, name)) return true;
        if (!classClass) {
            for (UObject* c = reinterpret_cast<UObject*>(ClassOf(o)); c; c = reinterpret_cast<UObject*>(ClassOf(c)))
                if (reinterpret_cast<UObject*>(ClassOf(c)) == c) {
                    classClass = reinterpret_cast<UClass*>(c);
                    break;
                }
        }
        if (!IsA(o, classClass)) return true;
        found = reinterpret_cast<UClass*>(o);
        return false;
    });
    if (found) { // only cache hits: a missing class may be loaded later (e.g. with the next level)
        std::lock_guard lock(g_cacheMutex);
        g_classes[name] = Weak(reinterpret_cast<UObject*>(found));
    }
    return found;
}

UObject* FindObject(const char* name, const UClass* cls)
{
    bool wantDefault = strncmp(name, "Default__", 9) == 0;
    UObject* found = nullptr;
    ForEachObject([&](UObject* o) {
        if (!NameEquals(reinterpret_cast<uint8_t*>(o) + off::ObjName, name)) return true;
        if (IsDefaultObject(o) != wantDefault || (cls && !IsA(o, cls))) return true;
        found = o;
        return false;
    });
    return found;
}

Prop FindProp(const UClass* structOrClass, const char* name)
{
    if (!structOrClass) return {};
    auto key = std::make_pair(static_cast<const void*>(structOrClass), std::string(name));
    {
        std::lock_guard lock(g_cacheMutex);
        if (auto it = g_props.find(key); it != g_props.end()) {
            if (it->second.owner.Get()) return it->second.value;
            g_props.erase(it);
        }
    }
    Prop prop;
    for (const void* s = structOrClass; s && !prop; s = SuperOf(s)) {
        for (auto* f = Read<const uint8_t*>(s, off::StructChildProps); f; f = Read<const uint8_t*>(f, off::FFieldNext)) {
            if (!NameEquals(f + off::FFieldName, name)) continue;
            prop.offset = Read<int32_t>(f, off::PropOffset);
            const void* fieldClass = Read<const void*>(f, off::FFieldClass);
            if (NameEquals(fieldClass, "BoolProperty")) { // FieldSize, ByteOffset, ByteMask, FieldMask
                prop.offset += Read<uint8_t>(f, off::PropExtra + 1);
                prop.boolMask = Read<uint8_t>(f, off::PropExtra + 2);
            } else {
                prop.inner = Read<const void*>(f, off::PropExtra);
            }
            break;
        }
    }
    std::lock_guard lock(g_cacheMutex);
    if (prop) g_props[key] = {Weak(reinterpret_cast<UObject*>(const_cast<UClass*>(structOrClass))), prop};
    return prop;
}

UFunction* FindFunction(const UClass* cls, const char* name)
{
    if (!cls) return nullptr;
    auto key = std::make_pair(static_cast<const void*>(cls), std::string(name));
    {
        std::lock_guard lock(g_cacheMutex);
        if (auto it = g_funcs.find(key); it != g_funcs.end()) {
            if (it->second.owner.Get())
                if (auto* alive = it->second.function.Get()) return reinterpret_cast<UFunction*>(alive);
            g_funcs.erase(it);
        }
    }
    UFunction* found = nullptr;
    for (const void* s = cls; s && !found; s = SuperOf(s))
        for (auto* f = Read<const uint8_t*>(s, off::StructChildren); f && !found; f = Read<const uint8_t*>(f, off::UFieldNext))
            if (NameEquals(f + off::ObjName, name)) found = reinterpret_cast<UFunction*>(const_cast<uint8_t*>(f));
    std::lock_guard lock(g_cacheMutex);
    if (found) g_funcs[key] = {Weak(reinterpret_cast<UObject*>(const_cast<UClass*>(cls))), Weak(reinterpret_cast<UObject*>(found))};
    return found;
}

bool GetBool(UObject* obj, const char* name, bool* out)
{
    Prop p = obj ? FindProp(ClassOf(obj), name) : Prop{};
    if (!p.boolMask) return false;
    *out = Read<uint8_t>(obj, p.offset) & p.boolMask;
    return true;
}

bool SetBool(UObject* obj, const char* name, bool value)
{
    Prop p = obj ? FindProp(ClassOf(obj), name) : Prop{};
    if (!p.boolMask) return false;
    uint8_t& byte = *(reinterpret_cast<uint8_t*>(obj) + p.offset);
    byte = value ? (byte | p.boolMask) : (byte & ~p.boolMask);
    return true;
}

Call::Call(UObject* obj, const char* function) : obj_(obj)
{
    fn_ = obj ? FindFunction(ClassOf(obj), function) : nullptr;
    if (fn_) params_.assign(size_t(Read<int32_t>(fn_, off::StructSize)), '\0');
}

Call::Call(UObject* obj, const UClass* owner, const char* function) : obj_(obj)
{
    fn_ = obj && owner ? FindFunction(owner, function) : nullptr;
    if (fn_) params_.assign(size_t(Read<int32_t>(fn_, off::StructSize)), '\0');
}

Call& Call::ArgRaw(int index, const void* data, size_t size)
{
    auto* p = static_cast<char*>(SlotAt(index));
    if (p && data && p + size <= params_.data() + params_.size()) std::memcpy(p, data, size);
    return *this;
}

int32_t StructSize(const UClass* s) { return s ? Read<int32_t>(s, off::StructSize) : 0; }

void* Call::Slot(const char* name)
{
    Prop p = fn_ ? FindProp(reinterpret_cast<UClass*>(fn_), name) : Prop{};
    return p ? params_.data() + p.offset : nullptr;
}

void* Call::SlotAt(int index)
{
    constexpr uint64_t CPF_Parm = 0x80;
    for (auto* f = fn_ ? Read<const uint8_t*>(fn_, off::StructChildProps) : nullptr; f; f = Read<const uint8_t*>(f, off::FFieldNext))
        if ((Read<uint64_t>(f, off::PropFlags) & CPF_Parm) && index-- == 0)
            return params_.data() + Read<int32_t>(f, off::PropOffset);
    return nullptr;
}

std::string NameToString(const void* fname) { return fname ? NameString(fname) : ""; }

bool Call::Run()
{
    if (!fn_ || !g.processEvent) return false;
    g.processEvent(obj_, fn_, params_.data());
    return true;
}

Weak::Weak(UObject* o) : obj(o), index(o ? Read<int32_t>(o, off::ObjIndex) : -1) {}

UObject* Weak::Get() const { return obj && ObjectAt(index) == obj ? obj : nullptr; }

std::string FString::ToUtf8() const
{
    if (!data || num <= 1) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, data, num - 1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, data, num - 1, s.data(), n, nullptr, nullptr);
    return s;
}

void RunOnGameThread(std::function<void()> fn)
{
    std::lock_guard lock(g_taskMutex);
    g_tasks.push_back(std::move(fn));
    g_tasksPending = true;
}

void SetTick(std::function<void()> fn) { g_tick = std::move(fn); }
void SetFrameTick(std::function<void()> fn) { g_frameTick = std::move(fn); }

bool Guard(std::function<void()>& fn) { return Guarded(fn); }

bool HookNative(UFunction* fn, NativeThunk detour, NativeThunk* original)
{
    // UFunction::Func sits a few fields after FunctionFlags (heap pointers and small counts in between): take the first
    // slot that points into the exe's code in two unrelated native functions.
    static const int funcOffset = [] {
        Range text = Section(".text");
        auto code = [&](const void* f, int o) {
            auto* p = Read<uint8_t*>(f, o);
            return p >= text.begin && p < text.end;
        };
        UClass* ksl = FindClass("KismetSystemLibrary");
        UFunction *a = FindFunction(ksl, "GetFrameCount"), *b = FindFunction(ksl, "GetEngineVersion");
        for (int o = off::FuncFlags + 8; a && b && o <= 0x120; o += 8)
            if (code(a, o) && code(b, o)) return o;
        return -1;
    }();
    auto* obj = reinterpret_cast<UObject*>(fn);
    if (!fn || funcOffset < 0) {
        km::Log("Native hook %s: %s", fn ? Name(obj).c_str() : "(function missing)",
                funcOffset < 0 ? "UFunction::Func not found" : "not found");
        return false;
    }
    auto* slot = reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(fn) + funcOffset);
    *original = reinterpret_cast<NativeThunk>(*slot);
    InterlockedExchangePointer(slot, reinterpret_cast<void*>(detour));
    km::Log("Native hook %s: OK (UFunction+0x%X, was exe+0x%llX)", Name(obj).c_str(), funcOffset,
            static_cast<unsigned long long>(reinterpret_cast<uint8_t*>(*original) - g.base));
    return true;
}

} // namespace ue
