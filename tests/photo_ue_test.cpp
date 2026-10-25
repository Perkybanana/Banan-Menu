// Exercise production reflection/generation checks without loading a game.
#include "../src/ue.cpp"
#include <array>
#include <cstdio>
#include <cstdlib>
namespace {
alignas(16) std::array<std::array<uint8_t, 0x160>, 16> objects{};
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

    // The live failure: retained actor has a positive serial, its strongly referenced
    // camera component/manager has no assigned serial. Exercise the production resolver.
    auto* actor = Object(6); auto* actorClass = Object(7); auto* component = Object(8);
    auto* componentClass = Object(9); auto* componentField = Object(10); auto* fieldType = Object(11);
    Put(actor, 0x10, actorClass); Put(actorClass, 0x50, componentField);
    Put(component, 0x10, componentClass);
    Put(actor, 0x90, component); Put(componentField, 8, fieldType);
    Put(componentField, 0x20, NameId("CameraComponent")); Put(fieldType, 0, NameId("ObjectProperty"));
    Put(componentField, 0x44, int32_t(0x90)); Put(componentField, 0x34, int32_t(8));
    Put(componentField, 0x30, int32_t(1)); Put(componentField, 0x70, componentClass);
    Put(items.data(), 8 * 24 + 16, int32_t(0));
    const ue::GenerationRef actorRef(actor);
    const auto* expectedClass = reinterpret_cast<ue::UClass*>(componentClass);
    Check(!ue::GenerationRef(component).Get(), "zero-serial component cannot become a retained generation reference");
    const ue::FreshObjectProperty strong(actorRef, "CameraComponent", expectedClass);
    Check(strong.Get() == component, "positive-owner zero-serial strong property target resolves fresh");
    Check(!ue::GenerationRef(component).Get(), "fresh resolution does not assign a target serial");
    Put(items.data(), 8 * 24 + 16, int32_t(31));
    Check(strong.Get() == component, "strong property also accepts an assigned target serial");
    const ue::FreshObjectProperty assignedTarget(actorRef, "CameraComponent", expectedClass);
    Put(items.data(), 8 * 24 + 16, int32_t(32));
    Check(!assignedTarget.Get(), "already assigned target generation reuse invalidates operation");
    Put(items.data(), 8 * 24 + 16, int32_t(-1));
    Check(!strong.Get(), "negative target serial is not an unassigned serial");
    Put(items.data(), 8 * 24 + 16, int32_t(0));
    Put(items.data(), 6 * 24 + 16, int32_t(333));
    Check(!strong.Get(), "same owner pointer and index with reused serial invalidates operation");
    Put(items.data(), 6 * 24 + 16, actorRef.serial);
    Put(items.data(), 6 * 24 + 16, int32_t(0));
    Check(!ue::FreshObjectProperty(ue::GenerationRef(actor), "CameraComponent", expectedClass).Get(), "zero-serial owner cannot anchor a strong target");
    Put(items.data(), 6 * 24 + 16, actorRef.serial);
    Put(actor, 0xC, int32_t(7));
    Check(!strong.Get(), "retained owner's current index must still match its slot");
    Check(!ue::FreshObjectProperty(ue::GenerationRef(actor), "CameraComponent", expectedClass).Get(), "wrong owner index cannot establish an anchor");
    Put(actor, 0xC, int32_t(6));
    Put(actor, 8, uint32_t(0x8000)); Check(!strong.Get(), "destroyed owner rejected");
    Put(actor, 8, uint32_t(0x10)); Check(!strong.Get(), "CDO owner rejected"); Put(actor, 8, uint32_t(0));
    Put(component, 0xC, int32_t(9)); Check(!strong.Get(), "target index must address its actual slot");
    Put(component, 0xC, int32_t(-1)); Check(!strong.Get(), "negative target index rejected");
    Put(component, 0xC, count); Check(!strong.Get(), "out-of-range target index rejected");
    Put(component, 0xC, int32_t(15)); Put(items.data(), 15 * 24, component);
    Check(!strong.Get(), "same target pointer moved to a different valid slot invalidates operation");
    Put(items.data(), 15 * 24, static_cast<ue::UObject*>(nullptr));
    Put(component, 0xC, int32_t(8));
    Put(items.data(), 8 * 24, actor); Check(!strong.Get(), "target object-array slot must match pointer");
    Put(items.data(), 8 * 24, component);
    for (uint32_t flags : {0x8000u, 0x10000u, 0x40000000u, 0x10u, 0x20u}) {
        Put(component, 8, flags); Check(!strong.Get(), "destroyed, garbage, CDO and archetype targets rejected");
    }
    Put(component, 8, uint32_t(0));
    Put(component, 0x10, actorClass); Check(!strong.Get(), "incompatible target class rejected");
    Put(component, 0x10, componentClass);
    auto* derivedClass = Object(14); Put(derivedClass, 0x40, componentClass); Put(component, 0x10, derivedClass);
    Check(ue::FreshObjectProperty(actorRef, "CameraComponent", reinterpret_cast<ue::UClass*>(derivedClass)).Get() == component,
        "declared base property accepts a compatible expected subclass");
    Put(component, 0x10, componentClass);
    Check(!ue::FreshObjectProperty(actorRef, "CameraComponent", reinterpret_cast<ue::UClass*>(actorClass)).Get(), "incompatible expected class rejected");
    Check(!ue::FreshObjectProperty(actorRef, "CameraComponent", nullptr).Get(), "missing expected class rejected");
    Check(!ue::FreshObjectProperty(actorRef, "Missing", expectedClass).Get(), "missing strong property rejected");
    Put(componentField, 0x70, actorClass); Check(!strong.Get(), "changed declared property class rejected");
    Check(!ue::FreshObjectProperty(actorRef, "CameraComponent", expectedClass).Get(), "target must match declared property class too");
    Put(componentField, 0x70, static_cast<ue::UObject*>(nullptr));
    Check(!ue::FreshObjectProperty(actorRef, "CameraComponent", expectedClass).Get(), "missing declared property class rejected");
    Put(componentField, 0x70, componentClass);
    Put(fieldType, 0, NameId("WeakObjectProperty"));
    Check(!strong.Get() && !ue::FreshObjectProperty(actorRef, "CameraComponent", expectedClass).Get(), "weak property is not a strong ObjectProperty");
    Put(fieldType, 0, NameId("ObjectProperty"));
    Put(componentField, 0x34, int32_t(4));
    Check(!strong.Get() && !ue::FreshObjectProperty(actorRef, "CameraComponent", expectedClass).Get(), "wrong object pointer size rejected");
    Put(componentField, 0x34, int32_t(8)); Put(componentField, 0x30, int32_t(2));
    Check(!strong.Get() && !ue::FreshObjectProperty(actorRef, "CameraComponent", expectedClass).Get(), "object pointer array rejected");
    Put(componentField, 0x30, int32_t(1));
    Put(actor, 0x98, component); Put(componentField, 0x44, int32_t(0x98));
    Check(!strong.Get(), "changed field offset invalidates operation even if target matches");
    Put(componentField, 0x44, int32_t(0x90));
    auto* replacementField = Object(12);
    memcpy(replacementField, componentField, 0x100); Put(actorClass, 0x50, replacementField);
    Check(!strong.Get(), "replaced reflected field invalidates operation even with identical metadata");
    Put(actorClass, 0x50, componentField);
    Put(actor, 0x90, static_cast<ue::UObject*>(nullptr));
    Check(!strong.Get() && !ue::FreshObjectProperty(actorRef, "CameraComponent", expectedClass).Get(), "null current owning reference rejected");
    auto* replacement = Object(13); Put(replacement, 0x10, componentClass); Put(items.data(), 13 * 24 + 16, int32_t(0));
    Put(actor, 0x90, replacement); Check(!strong.Get(), "replaced current owning reference invalidates operation");
    Check(ue::FreshObjectProperty(actorRef, "CameraComponent", expectedClass).Get() == replacement, "new operation can resolve new strong target");
    Put(actor, 0x90, component);
    Put(component, 0x20, actorClass);
    Check(strong.Get() == component, "strong reference does not require actor target outer to equal owner");
    // Synthetic native mutation between capture and use: Get must not hand back the old target.
    ue::g.processEvent = +[](ue::UObject* targetOwner, ue::UFunction*, void*) {
        Put(targetOwner, 0x90, static_cast<ue::UObject*>(nullptr));
    };
    ue::Call mutate(actor, cls, "SetFieldOfView", true);
    Check(mutate.Run() && !strong.Get(), "re-resolve after native call rejects changed owner property");
    Put(actor, 0x90, component);
    Check(strong.Get() == component, "unchanged live strong property still resolves");
    puts("Photo reflection and object-generation tests passed");
}
