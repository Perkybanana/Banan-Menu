// These tests catch partial apply/delivery, permissive parsing, UTF-8 corruption and destructive failed reads.
#include "../src/presets.h"
#include "../src/fov_core.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
using namespace presets;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); std::exit(1); } } while (0)
const Schema schema{{"GodMode", "Esp"}, {{"SpeedMult", .5f, 5.f}, {"Fov", fov::MinAngle, fov::MaxAngle, true}}};
Profile Example() { return {"Русский профиль", {{{"GodMode", false}, {"Esp", true}}, {{"SpeedMult", .5f}, {"Fov", 130.f}}}}; }
std::string Fixture() {
    // Hand-authored v1 file: one UTF-8 profile, two toggles and two values, favorite and named kit.
    return "BANAN_PRESETS 1\n1\n8:Тест\n2\n3:Esp\n1\n7:GodMode\n0\n2\n3:Fov\n130\n9:SpeedMult\n0.5\n"
           "1\n9:Item_Ammo\n1\n6:Наб\n1\n9:Item_Ammo\n5\n";
}
void Parsing() {
    Store s;
    CHECK(Parse(Fixture(), schema, s) == Error::Ok);
    CHECK(s.profiles.size() == 1 && s.profiles[0].name == "Тест");
    CHECK(s.profiles[0].settings.values.at("SpeedMult") == .5f);
    CHECK(s.kits[0].name == "Наб" && s.kits[0].entries[0].quantity == 5);
    CHECK(s.favorites.count("Item_Ammo") == 1);
    std::string bytes;
    CHECK(Serialize(s, schema, bytes) == Error::Ok);
    Store back; CHECK(Parse(bytes, schema, back) == Error::Ok);
    CHECK(back.profiles[0].name == "Тест" && back.kits[0].name == "Наб");
    CHECK(Serialize(back, schema, bytes) == Error::Ok);
    auto bad = [&](std::string input) {
        Store original = s;
        CHECK(Parse(input, schema, original) != Error::Ok);
        CHECK(original.profiles[0].name == "Тест"); // failed parse is transactional
    };
    bad("BANAN_PRESETS 2\n0\n0\n0\n");
    bad(std::string(MaxFile + 1, 'x'));
    for (size_t i = 0; i < Fixture().size(); ++i) bad(Fixture().substr(0, i));
    bad(Fixture() + "trailing");
    auto replace = [&](std::string from, std::string to) { auto text = Fixture(); text.replace(text.find(from), from.size(), to); bad(text); };
    replace("3:Esp", "7:Unknown");
    replace("7:GodMode", "3:Esp"); // duplicate key
    replace("3:Fov", "5:Alien");
    replace("9:SpeedMult", "3:Fov"); // duplicate numeric key
    replace("130\n", "nan\n"); replace("130\n", "inf\n"); replace("130\n", "130garbage\n");
    replace("130\n", "130.001\n"); replace("0.5\n", "0.4999\n");
    replace("130\n", "130.0000001\n");
    replace("130\n", "hello\n"); replace("130\n", "1e999\n");
    replace("3:Esp\n1", "3:Esp\n2");
    replace("9:Item_Ammo\n5", "9:Item_Ammo\n0");
    replace("9:Item_Ammo\n5", "9:Item_Ammo\n10000");
    replace("9:Item_Ammo\n5", "9:Item_Ammo\n9999999999999999999999");
    replace("1\n9:Item_Ammo\n1\n", "2\n9:Item_Ammo\n9:Item_Ammo\n1\n"); // duplicate favorite
    CHECK(!ValidName(std::string(65, 'a')) && ValidName(std::string(64, 'a')));
    std::string cyr; for (int i = 0; i < 64; ++i) cyr += "Я";
    CHECK(ValidName(cyr)); CHECK(!ValidName(cyr + "Я"));
    CHECK(!ValidName("\xC0\xAF") && !ValidName("\xED\xA0\x80") && !ValidName("\xF4\x90\x80\x80"));
    CHECK(!ValidName("") && !ValidName(std::string("a\0b", 3)));
    // Decimal spelling of a declared .1f boundary must work without accepting values outside it.
    auto decimalSchema = schema; decimalSchema.values[0].min = .1f;
    auto atMinimum = Fixture(); atMinimum.replace(atMinimum.find("0.5\n"), 4, "0.1\n");
    Store decimal;
    CHECK(Parse(atMinimum, decimalSchema, decimal) == Error::Ok);
    CHECK(decimal.profiles[0].settings.values.at("SpeedMult") == .1f);
    CHECK(Serialize(decimal, decimalSchema, bytes) == Error::Ok);
    CHECK(Parse(bytes, decimalSchema, back) == Error::Ok);
    atMinimum.replace(atMinimum.find("0.1\n"), 4, "0.09999999999\n");
    CHECK(Parse(atMinimum, decimalSchema, decimal) != Error::Ok);
    Store duplicates;
    CHECK(Parse("BANAN_PRESETS 1\n2\n1:A\n0\n0\n1:A\n0\n0\n0\n0\n", {}, duplicates) != Error::Ok);
    CHECK(Parse("BANAN_PRESETS 1\n0\n0\n2\n1:A\n1\n1:X\n1\n1:A\n1\n1:Y\n1\n", {}, duplicates) != Error::Ok);
    CHECK(Parse("BANAN_PRESETS 1\n0\n0\n1\n1:A\n2\n1:X\n1\n1:X\n1\n", {}, duplicates) != Error::Ok);
    CHECK(Parse("BANAN_PRESETS 1\n17\n", {}, duplicates) != Error::Ok);
    CHECK(Parse("BANAN_PRESETS 1\n999999999999999999999999\n", {}, duplicates) != Error::Ok);
}
void ActionsAndLimits() {
    auto p = Example(); int settings = 0, deliveries = 0;
    Settings applied;
    auto apply = [&] { return ApplyProfile(p, schema,
        [&](const auto& key, bool on) { applied.toggles[key] = on; ++settings; },
        [&](const auto& key, float n) { applied.values[key] = n; ++settings; }); };
    CHECK(apply() == Error::Ok && settings == 4 && deliveries == 0);
    CHECK(!applied.toggles.at("GodMode") && applied.toggles.at("Esp"));
    CHECK(applied.values.at("SpeedMult") == .5f && applied.values.at("Fov") == 130.f);
    settings = 0; p.settings.values["Fov"] = std::numeric_limits<float>::infinity();
    CHECK(apply() != Error::Ok && settings == 0);
    p = Example(); p.settings.toggles["Unknown"] = true; CHECK(apply() != Error::Ok && settings == 0);
    p = Example(); p.settings.values.erase("Fov"); CHECK(apply() != Error::Ok && settings == 0);
    Store s;
    for (int i = 0; i < 16; ++i) { p = Example(); p.name = std::to_string(i); CHECK(PutProfile(s, p, schema) == Error::Ok); }
    p.name = "overflow"; CHECK(PutProfile(s, p, schema) != Error::Ok);
    p.name = "0"; CHECK(PutProfile(s, p, schema) == Error::Ok);
    Kit k{"Набор", {}};
    CHECK(AddEntry(k, "Item_Ammo", 9000) == Error::Ok);
    CHECK(AddEntry(k, "Item_Ammo", 999) == Error::Ok && k.entries[0].quantity == 9999);
    CHECK(AddEntry(k, "Item_Ammo", 1) != Error::Ok && k.entries[0].quantity == 9999);
    CHECK(AddEntry(k, "Item_Ammo", std::numeric_limits<int>::max()) != Error::Ok);
    CHECK(AddEntry(k, "Item_Ammo", -1) != Error::Ok);
    for (int i = 1; i < 32; ++i) CHECK(AddEntry(k, "Item_" + std::to_string(i), 1) == Error::Ok);
    CHECK(AddEntry(k, "Item_33", 1) != Error::Ok);
    for (int i = 0; i < 16; ++i) { k.name = std::to_string(i); CHECK(PutKit(s, k) == Error::Ok); }
    k.name = "overflow"; CHECK(PutKit(s, k) != Error::Ok);
    k = s.kits[0]; CHECK(deliveries == 0); // saving / loading for edit does not send
    CHECK(SendKit(k, [](const auto& id) { return id != "Item_31"; }, [&](const auto&, int) { ++deliveries; }) == Error::Unavailable);
    CHECK(deliveries == 0); // late unknown ID cannot partially deliver
    CHECK(SendKit(k, [](const auto&) { return true; }, [&](const auto&, int n) { CHECK(n >= 1 && n <= 9999); ++deliveries; }) == Error::Ok);
    CHECK(deliveries == 32);
    k.entries.push_back(k.entries[0]); deliveries = 0;
    CHECK(SendKit(k, [](const auto&) { return true; }, [&](const auto&, int) { ++deliveries; }) != Error::Ok && deliveries == 0);
    k = {"Missing", {{"Item_Ammo_old", 1}}};
    CHECK(SendKit(k, [](const auto& id) { return id == "Item_Ammo"; }, [&](const auto&, int) { ++deliveries; }) == Error::Unavailable);
    std::vector<Entry> sent;
    k = {"Exact", {{"Item_Ammo", 7}, {"Item_Ammo_2", 9}}};
    CHECK(SendKit(k, [](const auto&) { return true; }, [&](const auto& id, int n) { sent.push_back({id, n}); }) == Error::Ok);
    CHECK(sent.size() == 2 && sent[0].id == "Item_Ammo" && sent[0].quantity == 7 && sent[1].id == "Item_Ammo_2" && sent[1].quantity == 9);
    // A saturated store must roundtrip at the declared limits and preserve unknown IDs as inert data.
    for (int i = 0; i < 256; ++i) s.favorites.insert("Missing_" + std::to_string(i));
    std::string bytes; CHECK(Serialize(s, schema, bytes) == Error::Ok);
    Store loaded; CHECK(Parse(bytes, schema, loaded) == Error::Ok);
    CHECK(loaded.profiles.size() == 16 && loaded.kits.size() == 16 && loaded.kits[0].entries.size() == 32 && loaded.favorites.size() == 256);
    s.favorites.insert("overflow"); CHECK(Serialize(s, schema, bytes) == Error::Limit);
    s.favorites.erase("overflow");
    s.kits.push_back(s.kits[0]); CHECK(Serialize(s, schema, bytes) != Error::Ok); s.kits.pop_back();
    s.profiles.push_back(s.profiles[0]); CHECK(Serialize(s, schema, bytes) != Error::Ok); s.profiles.pop_back();
}
void FovProfilesRejectBeforeAnyCallback() {
    // Each fixture is otherwise complete and valid: the FOV alone decides acceptance.
    int failures = 0;
    for (const auto& [spelling, accepted] : std::vector<std::pair<std::string, bool>>{
        {"0", true}, {"30", true}, {"49", true}, {"130", true},
        {"1", false}, {"29", false}, {"29.999999999", false},
        {"130.0000001", false}, {"nan", false}, {"inf", false}, {"-inf", false}}) {
        auto bytes = Fixture(); bytes.replace(bytes.find("130\n"), 4, spelling + "\n");
        Store parsed;
        const bool parsedOk = Parse(bytes, schema, parsed) == Error::Ok;
        if (parsedOk != accepted) {
            std::fprintf(stderr, "FOV %s: parsing acceptance was %d, expected %d\n", spelling.c_str(), parsedOk, accepted);
            ++failures;
        }
        if (parsedOk && accepted) {
            std::string saved; Store restored;
            CHECK(Serialize(parsed, schema, saved) == Error::Ok);
            CHECK(Parse(saved, schema, restored) == Error::Ok);
            CHECK(restored.profiles[0].settings.values.at("Fov") == std::stof(spelling));
        }
    }
    for (float angle : {0.f, 30.f, 49.f, 130.f, 1.f, 29.f, 130.1f,
                       std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        auto profile = Example(); profile.settings.values["Fov"] = angle;
        const bool accepted = angle == 0 || (std::isfinite(angle) && angle >= 30 && angle <= 130);
        int callbacks = 0;
        float appliedFov = -1;
        const auto result = ApplyProfile(profile, schema,
            [&](const auto&, bool) { ++callbacks; },
            [&](const auto& key, float value) { ++callbacks; if (key == "Fov") appliedFov = value; });
        if ((result == Error::Ok) != accepted || callbacks != (accepted ? 4 : 0)) {
            std::fprintf(stderr, "FOV %g: apply result %d, callbacks %d; expected accepted %d\n",
                         angle, int(result), callbacks, accepted);
            ++failures;
        }
        CHECK(!accepted || appliedFov == angle);
    }
    CHECK(failures == 0);
}
void Persistence() {
    const auto folder = std::filesystem::current_path() / ("banan-presets-test-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    auto path = folder / "settings.presets";
    std::filesystem::remove(path); std::filesystem::remove(path.wstring() + L".tmp");
    Repository r(path, schema); CHECK(r.Load() == Error::Ok);
    Store s; CHECK(PutProfile(s, Example(), schema) == Error::Ok); CHECK(r.Save(s) == Error::Ok);
    Repository read(path, schema); CHECK(read.Load() == Error::Ok && read.Data().profiles[0].name == "Русский профиль");
    std::ofstream(path, std::ios::binary | std::ios::trunc) << "malformed original";
    Repository bad(path, schema); CHECK(bad.Load() != Error::Ok); CHECK(bad.Save(s) == Error::Protected);
    std::ifstream in(path, std::ios::binary); std::string bytes((std::istreambuf_iterator<char>(in)), {});
    CHECK(bytes == "malformed original"); in.close();
    // Failed replacement keeps both the previously loaded state and original bytes.
    std::filesystem::remove(path); Repository fresh(path, schema); CHECK(fresh.Load() == Error::Ok);
    CHECK(fresh.Save(s) == Error::Ok);
    std::filesystem::create_directory(path.wstring() + L".tmp");
    auto changed = s; changed.profiles[0].name = "Changed";
    CHECK(fresh.Save(changed) == Error::Io && fresh.Data().profiles[0].name == "Русский профиль");
    Repository preserved(path, schema); CHECK(preserved.Load() == Error::Ok && preserved.Data().profiles[0].name == "Русский профиль");
    std::filesystem::remove(path.wstring() + L".tmp");
    HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(locked != INVALID_HANDLE_VALUE);
    CHECK(fresh.Save(changed) == Error::Io && fresh.Data().profiles[0].name == "Русский профиль");
    CHECK(!std::filesystem::exists(path.wstring() + L".tmp"));
    CHECK(CloseHandle(locked));
    Repository afterFailedReplace(path, schema);
    CHECK(afterFailedReplace.Load() == Error::Ok && afterFailedReplace.Data().profiles[0].name == "Русский профиль");
    CHECK(fresh.Save(changed) == Error::Ok);
    Repository afterSuccess(path, schema); CHECK(afterSuccess.Load() == Error::Ok && afterSuccess.Data().profiles[0].name == "Changed");
    std::filesystem::remove(path); std::filesystem::remove(folder);
}
int main() { Parsing(); ActionsAndLimits(); FovProfilesRejectBeforeAnyCallback(); Persistence(); std::puts("presets_test: strict parsing, UTF-8, bounds, FOV domain, explicit actions, persistence passed"); }
