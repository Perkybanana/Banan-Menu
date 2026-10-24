#pragma once
// Minimal Unreal Engine 5 reflection: objects, names, properties and functions resolved by name at runtime.
// Only GObjects, GNames and ProcessEvent are located by signature; game classes/members are never hard-coded.
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

namespace ue {

struct UObject; // opaque: always accessed through the helpers below
struct UClass;  // a UStruct
struct UFunction;

// Finds GObjects / GNames / ProcessEvent, hooks ProcessEvent for game-thread work and self-tests. Logs everything.
bool Init(unsigned gameThreadId);
bool Ready();

// memcpy that returns false instead of crashing on unreadable memory.
bool SafeRead(const void* src, void* dst, size_t n);

int32_t ObjectCount();
UObject* ObjectAt(int32_t index); // nullptr for empty slots and objects being destroyed
std::string Name(const UObject* obj);
UClass* ClassOf(const UObject* obj);
UObject* OuterOf(const UObject* obj);
bool IsA(const UObject* obj, const UClass* cls);
bool IsChildOf(const UClass* cls, const UClass* parent);
bool IsDefaultObject(const UObject* obj); // class default objects / archetypes: never gameplay instances

UClass* FindClass(const char* name); // cached; nullptr if not loaded
UObject* FindObject(const char* name, const UClass* cls = nullptr); // first live, non-default match
std::string NameToString(const void* fname); // an FName value (8 bytes) stored anywhere

template <class F> void ForEachObject(F&& f) // f(UObject*) -> bool (false stops)
{
    for (int32_t i = 0, n = ObjectCount(); i < n; i++)
        if (UObject* o = ObjectAt(i); o && !f(o)) return;
}

template <class F> void ForEachInstance(const UClass* cls, F&& f) // live gameplay instances only
{
    if (!cls) return;
    ForEachObject([&](UObject* o) { return !IsA(o, cls) || IsDefaultObject(o) || f(o); });
}

struct Prop {
    int32_t offset = -1;
    uint8_t boolMask = 0;       // non-zero for bool properties (bitfields included)
    const void* inner = nullptr; // struct properties: the UScriptStruct; object properties: the class
    explicit operator bool() const { return offset >= 0; }
};
Prop FindProp(const UClass* structOrClass, const char* name); // cached, walks the Super chain
int32_t StructSize(const UClass* structOrClass);               // bytes of one instance
UFunction* FindFunction(const UClass* cls, const char* name);  // cached, walks the Super chain

// Typed access to a property by name. Returns nullptr when the property doesn't exist (feature greys out).
template <class T> T* Member(UObject* obj, const char* name)
{
    Prop p = obj ? FindProp(ClassOf(obj), name) : Prop{};
    return p ? reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(obj) + p.offset) : nullptr;
}
bool GetBool(UObject* obj, const char* name, bool* out);
bool SetBool(UObject* obj, const char* name, bool value);

// ProcessEvent with a parameter buffer built by name. Must run on the game thread (see RunOnGameThread).
class Call {
public:
    Call(UObject* obj, const char* function);
    Call(UObject* obj, const UClass* owner, const char* function); // owner's function (e.g. an interface obj implements)
    explicit operator bool() const { return fn_ != nullptr; }
    Call& ArgRaw(int index, const void* data, size_t size); // a struct copied as is (its size from StructSize)
    template <class T> Call& Arg(const char* name, const T& v)
    {
        if (void* p = Slot(name)) std::memcpy(p, &v, sizeof(T));
        return *this;
    }
    // By position (declaration order): for Blueprint events whose parameter names carry generated suffixes.
    template <class T> Call& Arg(int index, const T& v)
    {
        if (void* p = SlotAt(index)) std::memcpy(p, &v, sizeof(T));
        return *this;
    }
    bool Run();
    template <class T> T Out(const char* name)
    {
        T v{};
        if (void* p = Slot(name)) std::memcpy(&v, p, sizeof(T));
        return v;
    }

private:
    void* Slot(const char* name);
    void* SlotAt(int index);
    UObject* obj_;
    UFunction* fn_ = nullptr;
    std::string params_; // zeroed buffer of the function's parameter size
};

// Weak reference: Get() returns nullptr once the object has been destroyed or its slot reused.
struct Weak {
    UObject* obj = nullptr;
    int32_t index = -1;
    Weak() = default;
    explicit Weak(UObject* o);
    UObject* Get() const;
};

struct FString { // TArray<wchar_t>
    wchar_t* data;
    int32_t num, max;
    std::string ToUtf8() const;
};

// Queue work for the game thread (executed from our ProcessEvent hook). Tick runs ~60 times per second; the frame tick
// once per engine frame (at most 250 times per second), for what must follow the picture (ESP).
void RunOnGameThread(std::function<void()> fn);
void SetTick(std::function<void()> fn);
void SetFrameTick(std::function<void()> fn);

// Swaps the code a native UFunction runs (what both Blueprint and ProcessEvent call) for `detour`, which must call
// `*original`. The parameters are already consumed when the original returns; `result` points at the return value.
using NativeThunk = void (*)(UObject* context, void* stack, void* result);
bool HookNative(UFunction* fn, NativeThunk detour, NativeThunk* original);

// Runs fn behind an SEH guard: false when it faulted (code running inside game functions must never crash the game).
bool Guard(std::function<void()>& fn);

} // namespace ue
