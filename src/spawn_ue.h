#pragma once
#include "ue.h"
#include "spawn_core.h"
#include <array>

namespace spawn {
using SpawnInfoValue = std::array<uint8_t, 336>;

// Prepare only a request's local copy, never the retained native defaults. A nonempty
// authored subtype triggers the game's configured async ability initialization.
inline bool PrepareSubtype(SpawnInfoValue& value, const ue::UClass* info, int type, uint64_t name, const char*& fieldName)
{
    fieldName = "EnemySubtype conversion";
    if (type < 0 || type >= TypeCount || !name || ue::NameToString(&name) != Types[type].subtype) return false;
    fieldName = "ReturnValue.SpawnInfo";
    if (!info || ue::StructSize(info) != int(value.size()) ||
        ue::Name(reinterpret_cast<const ue::UObject*>(info)) != "SpawnInfo") return false;
    fieldName = "EnemySubtype";
    const auto subtype = ue::FindProp(info, fieldName, true);
    if (!subtype.Is(ue::Prop::Kind::Struct, 8) || subtype.offset > int(value.size()) - 8 || !subtype.inner ||
        ue::Name(static_cast<const ue::UObject*>(subtype.inner)) != "GameplayTag") return false;
    const auto* tag = static_cast<const ue::UClass*>(subtype.inner);
    if (ue::StructSize(tag) != 8) return false;
    fieldName = "EnemySubtype.TagName";
    const auto member = ue::FindProp(tag, "TagName", true);
    if (!member.Is(ue::Prop::Kind::Name, 8) || member.offset > subtype.elementSize - 8) return false;
    std::memcpy(value.data() + subtype.offset + member.offset, &name, sizeof(name));
    return true;
}

// Only the owned native return value is eligible for preparation.
inline bool PrepareSeed(SpawnInfoValue& value, const ue::UClass* info, const char*& fieldName)
{
    fieldName = "ReturnValue.SpawnInfo";
    if (!info || ue::StructSize(info) != int(value.size()) ||
        ue::Name(reinterpret_cast<const ue::UObject*>(info)) != "SpawnInfo") return false;
    fieldName = "HearingDistanceCurveOverride";
    const auto field = ue::FindProp(info, fieldName, true);
    if (!field.Is(ue::Prop::Kind::Object, 8) || field.offset > int(value.size()) - 8 ||
        !field.inner || ue::Name(static_cast<const ue::UObject*>(field.inner)) != "CurveFloat") return false;
    // The null-input native builder can leave this optional word indeterminate.
    // Free spawning explicitly chooses no override. Never inspect/dereference/free
    // the incoming word; the native consumer preserves its settings for nullptr.
    ue::UObject* none = nullptr;
    std::memcpy(value.data() + field.offset, &none, sizeof(none));
    return true;
}
} // namespace spawn
