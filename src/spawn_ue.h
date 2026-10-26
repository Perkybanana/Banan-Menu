#pragma once
#include "ue.h"
#include <array>

namespace spawn {
using SpawnInfoValue = std::array<uint8_t, 336>;

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
