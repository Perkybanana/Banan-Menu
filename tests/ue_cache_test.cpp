// Exercise the production reflection caches across Blueprint teardown and reload.
#include "../src/ue.cpp"
#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
alignas(16) std::array<std::array<uint8_t, 0x140>, 16> objects{};
std::array<uint8_t, 24 * 16> items{};
std::array<uint8_t*, 1> chunks{items.data()};
std::array<uint8_t, 4096> names{};
std::array<uint8_t*, 8192> blocks{};
int32_t count = 16;
uint32_t cursor = 2;
template<class T> void Put(void* p, size_t offset, T value) { memcpy(static_cast<uint8_t*>(p) + offset, &value, sizeof value); }
uint32_t NameId(const char* name) {
    auto id = cursor / 2;
    auto size = strlen(name);
    Put(names.data(), cursor, uint16_t(size << 6));
    memcpy(names.data() + cursor + 2, name, size);
    cursor += uint32_t((size + 3) & ~size_t(1));
    return id;
}
ue::UObject* Object(int index, uint32_t name, ue::UObject* cls) {
    auto* ptr = reinterpret_cast<ue::UObject*>(objects[index].data());
    Put(ptr, 0xC, index); Put(ptr, 0x10, cls); Put(ptr, 0x18, name);
    Put(items.data(), index * 24, ptr);
    return ptr;
}
void Check(bool value, const char* message) {
    if (!value) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
}
int main() {
    blocks[0] = names.data();
    ue::g.chunks = chunks.data(); ue::g.numElements = &count; ue::g.perChunk = 65536;
    ue::g.itemSize = 24; ue::g.nameBlocks = blocks.data(); ue::g.lenShift = 6;
    auto* meta = Object(0, NameId("Class"), nullptr); Put(meta, 0x10, meta);
    auto fooName = NameId("W_MainMenu_C");
    auto* oldClass = Object(1, fooName, meta);
    Check(ue::FindClass("W_MainMenu_C") == reinterpret_cast<ue::UClass*>(oldClass), "initial class lookup");
    Put(oldClass, 8, uint32_t(0x8000));
    auto* newClass = Object(2, fooName, meta);
    Check(ue::FindClass("W_MainMenu_C") == reinterpret_cast<ue::UClass*>(newClass), "reacquire reloaded menu class");

    auto fnName = NameId("SetupButton");
    auto* oldFn = Object(3, fnName, meta); Put(newClass, 0x48, oldFn);
    auto* cls = reinterpret_cast<ue::UClass*>(newClass);
    Check(ue::FindFunction(cls, "SetupButton") == reinterpret_cast<ue::UFunction*>(oldFn), "initial function lookup");
    Put(oldFn, 8, uint32_t(0x8000));
    auto* newFn = Object(4, fnName, meta); Put(newClass, 0x48, newFn);
    Check(ue::FindFunction(cls, "SetupButton") == reinterpret_cast<ue::UFunction*>(newFn), "discard destroyed cached function");
    Check(!ue::FindFunction(cls, "LateLoaded"), "missing function");
    auto* lateFn = Object(5, NameId("LateLoaded"), meta); Put(newFn, 0x28, lateFn);
    Check(ue::FindFunction(cls, "LateLoaded") == reinterpret_cast<ue::UFunction*>(lateFn), "retry function loaded later");
    puts("Reflection reload tests passed");
}
