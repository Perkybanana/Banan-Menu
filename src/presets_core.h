#pragma once
// Bounded, engine-independent data and explicit action validation.
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace presets {
constexpr size_t MaxFile = 256 * 1024, MaxNames = 16, MaxEntries = 32, MaxFavorites = 256;
enum class Error { Ok, InvalidData, Schema, Limit, InvalidSettings, InvalidKit, Unavailable, Io, Protected };
// allowZero adds the game-default sentinel without admitting values between zero and min.
struct ValueSpec { std::string key; float min, max; bool allowZero = false; };
struct Schema { std::vector<std::string> toggles; std::vector<ValueSpec> values; };
struct Settings { std::map<std::string, bool> toggles; std::map<std::string, float> values; };
struct Profile { std::string name; Settings settings; };
struct Entry { std::string id; int quantity; };
struct Kit { std::string name; std::vector<Entry> entries; };
struct Store { std::vector<Profile> profiles; std::set<std::string> favorites; std::vector<Kit> kits; };

bool ValidName(std::string_view name);
bool ValidId(std::string_view id);
Error ValidateSettings(const Settings&, const Schema&);
Error ValidateKit(const Kit&);
Error AddEntry(Kit&, const std::string& id, int quantity);
Error PutProfile(Store&, Profile, const Schema&);
Error PutKit(Store&, Kit);
Error Parse(std::string_view bytes, const Schema&, Store& out);
Error Serialize(const Store&, const Schema&, std::string& out);

// Validation completes before the first callback; these APIs contain no implicit startup or editing actions.
template<class SetToggle, class SetValue>
Error ApplyProfile(const Profile& profile, const Schema& schema, SetToggle toggle, SetValue value)
{
    if (!ValidName(profile.name)) return Error::InvalidData;
    if (auto e = ValidateSettings(profile.settings, schema); e != Error::Ok) return e;
    for (const auto& [key, on] : profile.settings.toggles) toggle(key, on);
    for (const auto& [key, number] : profile.settings.values) value(key, number);
    return Error::Ok;
}
template<class Exists, class Give>
Error SendKit(const Kit& kit, Exists exists, Give give)
{
    if (auto e = ValidateKit(kit); e != Error::Ok) return e;
    for (const auto& entry : kit.entries) if (!exists(entry.id)) return Error::Unavailable;
    for (const auto& entry : kit.entries) give(entry.id, entry.quantity);
    return Error::Ok;
}
} // namespace presets
