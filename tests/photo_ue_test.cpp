// Exercise production reflection/generation checks without loading a game.
#include "../src/ue.cpp"
#include <array>
#include <cstdio>
#include <cstdlib>
namespace {
alignas(16) std::array<std::array<uint8_t, 0x160>, 8> objects{};
std::array<uint8_t, 24 * 8> items{};
std::array<uint8_t*, 1> chunks{items.data()};
std::array<uint8_t, 4096> names{};
std::array<uint8_t*, 8192> blocks{};
int32_t count = 8;
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
ue::UObject* Object(int index) {
    auto* ptr = reinterpret_cast<ue::UObject*>(objects[index].data());
    Put(ptr, 0xC, index);
    Put(items.data(), index * 24, ptr);
    Put(items.data(), index * 24 + 16, int32_t(17 + index));
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
    auto* object = Object(1);
    ue::GenerationRef ref(object);
    Check(ref.Get() == object, "capture exact live generation");
    Put(items.data(), 24 + 16, int32_t(99));
    Check(!ref.Get(), "reject same pointer and index reused with new serial");
    ref = ue::GenerationRef(object);
    Check(ref.Get() == object, "capture replacement generation");
    Put(object, 8, uint32_t(0x8000));
    Check(!ref.Get(), "reject destroyed object");
    Put(object, 8, uint32_t(0));
    Put(items.data(), 24 + 16, int32_t(0));
    Check(!ue::GenerationRef(object).Get(), "unassigned serial fails closed");
    ue::g.itemSize = 16;
    Check(!ue::GenerationRef(object).Get(), "unverified layout fails closed");
    ue::g.itemSize = 24;

    auto* owner = Object(2); auto* field = Object(3); auto* typeName = Object(4);
    Put(owner, 0x50, field);
    Put(field, 8, typeName);
    Put(field, 0x20, NameId("FieldOfView"));
    Put(typeName, 0, NameId("FloatProperty"));
    Put(field, 0x44, int32_t(0x90)); Put(field, 0x34, int32_t(4)); Put(field, 0x30, int32_t(1));
    auto* cls = reinterpret_cast<ue::UClass*>(owner);
    Check(!ue::FindProp(cls, "Missing"), "missing property rejected");
    Check(ue::FindProp(cls, "FieldOfView").Is(ue::Prop::Kind::Float, 4), "typed scalar metadata from reflection");
    ue::g_props.clear(); Put(typeName, 0, NameId("DoubleProperty")); Put(field, 0x34, int32_t(8));
    Check(!ue::FindProp(cls, "FieldOfView").Is(ue::Prop::Kind::Float, 4), "wrong property type rejected");
    ue::g_props.clear(); Put(typeName, 0, NameId("FloatProperty")); Put(field, 0x34, int32_t(8));
    Check(!ue::FindProp(cls, "FieldOfView").Is(ue::Prop::Kind::Float, 4), "wrong scalar size rejected");
    ue::g_props.clear(); Put(field, 0x34, int32_t(4)); Put(field, 0x30, int32_t(2));
    Check(!ue::FindProp(cls, "FieldOfView").Is(ue::Prop::Kind::Float, 4), "scalar array rejected");
    Put(field, 0x30, int32_t(1)); ue::g_props.clear();
    Check(ue::FindProp(cls, "FieldOfView").offset == 0x90, "cache initial camera property");
    Put(items.data(), 2 * 24 + 16, int32_t(101)); Put(field, 0x44, int32_t(0x98));
    Check(ue::FindProp(cls, "FieldOfView", true).offset == 0x98, "fresh camera property ignores same-address owner cache reuse");
    auto* function = Object(5); Put(function, 0x18, NameId("SetFieldOfView")); Put(owner, 0x48, function);
    Check(ue::FindFunction(cls, "SetFieldOfView") == reinterpret_cast<ue::UFunction*>(function), "cache camera API");
    Put(items.data(), 5 * 24 + 16, int32_t(102)); Put(owner, 0x48, static_cast<ue::UObject*>(nullptr));
    Check(!ue::FindFunction(cls, "SetFieldOfView", true), "fresh camera API ignores same-address function cache reuse");
    Put(owner, 0x48, function); Put(function, 0x50, field); Put(function, 0x58, int32_t(16));
    Put(field, 0x20, NameId("ReturnValue")); Put(field, 0x44, int32_t(0));
    auto* fnClass = reinterpret_cast<ue::UClass*>(function);
    Check(ue::FindProp(fnClass, "ReturnValue").offset == 0, "cache initial output parameter");
    Put(field, 0x44, int32_t(8));
    ue::g.processEvent = +[](ue::UObject*, ue::UFunction*, void* data) {
        const float oldSlot = 11, currentSlot = 42;
        memcpy(data, &oldSlot, sizeof(float));
        memcpy(static_cast<uint8_t*>(data) + 8, &currentSlot, sizeof(float));
    };
    ue::Call fresh(object, cls, "SetFieldOfView", true);
    Check(fresh.Run() && fresh.Out<float>("ReturnValue") == 42, "fresh camera call bypasses cached output offset");
    puts("Photo reflection and object-generation tests passed");
}
