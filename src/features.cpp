#include "features.h"
#include "achievements.h"
#include "missions.h"
#include "photo_core.h"
#include "fov_core.h"
#include "config.h"
#include "lang.h"
#include "log.h"
#include "menu.h"
#include "text.h"
#include "ue.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <map>
#include <mutex>

using namespace ue;

namespace feat {

const ToggleInfo kToggles[ToggleCount] = {
    {"GodMode", "God mode", "Health stays full and damage is ignored."},
    {"InfiniteHealth", "Infinite health", "Health refills instantly. You still take the hits."},
    {"NoSprintCooldown", "No sprint cooldown", "The game has no stamina: this removes the pause between sprints."},
    {"OneHitKill", "One-hit kill", "Any damage you deal kills the enemy. Bosses with scripted phases may resist."},
    {"Invisible", "Invisible to enemies", "Uses the developers' invisibility cheat."},
    {"InfiniteAmmo", "Infinite ammo", "Your magazine never empties, so you never reload."},
    {"NoSufferingCost", "Powers cost nothing", "Genesis Configuration powers use no Suffering, and it stays full."},
    {"NoPowerCooldown", "No power cooldown", "Removes telekinesis and pyrokinesis cooldowns."},
    {"Unbreakable", "Unbreakable melee weapons", "Melee weapons never lose durability."},
    {"SilentAim", "Silent aim",
     "Your shots hit the head of the enemy nearest the crosshair, inside the circle. The camera doesn't move."},
    {"Fly", "Fly", "Flies where you look, with your usual movement keys or stick."},
    {"Noclip", "Noclip", "Fly through walls, where you look, with your usual movement keys."},
    {"FreezeAI", "Freeze enemies", "Enemies stop moving and attacking."},
    {"HideHUD", "Hide HUD", "Hides the game interface (menus you open later stay visible)."},
    {"FreeCam", "Free camera", "Detached camera: movement keys to move, mouse to look. Your character stays in place."},
    {"AutoLockpick", "Auto lockpick", "Locks open by themselves a moment after you start picking them."},
    {"SkipIntros", "Skip intros",
     "Skips the logos and notices when the game starts. The EULA and brightness screens still show the first time."},
    {"Esp", "ESP", "Shows enemies and objects through walls."},
    {"EspBox", "Box", "Corner box around each enemy.", true},
    {"EspHealth", "Health bar", "Enemy health next to the box.", true},
    {"EspName", "Name", "Enemy type.", true},
    {"EspDistance", "Distance", "Distance in metres.", true},
    {"EspSkeleton", "Skeleton", "Bones drawn over the body."},
    {"EspHead", "Head marker", "Glowing dot on the head."},
    {"EspChams", "Chams", "Glowing body silhouette, visible through walls."},
    {"EspSnaplines", "Snaplines", "Lines from the bottom of the screen to each enemy."},
    {"EspOffscreen", "Off-screen arrows", "Arrows at the edge of the screen pointing at enemies you can't see.", true},
    {"EspItems", "Items on the ground", "Ammo, money, healing, weapons and materials you can pick up.", true},
    {"EspContainers", "Loot containers", "Chests, shelves and breakable crates."},
    {"EspSavePoints", "Save points", "Where you can save."},
    {"EspAimFov", "Aim FOV circle", "Shows the silent aim circle around the crosshair while silent aim is on.", true},
};

const ValueInfo kValues[ValueCount] = {
    {"DamageMult", 1, 1, 50}, {"SpeedMult", 1, 0.5f, 5}, {"JumpMult", 1, 1, 10},
    {"TimeScale", 1, 0.1f, 4}, {"Fov", 0, 0, fov::MaxAngle}, {"KillRadius", 20, 5, 100},
    {"EspRange", 80, 10, 300}, {"EspWorldRange", 30, 5, 150}, {"AimFov", 8, 1, 45},
};

const char* const kPowerNames[PowerCount] = {"Telekinesis", "Pyrokinesis", "Hellchains"};

std::atomic<bool> g_on[ToggleCount];
std::atomic<float> g_value[ValueCount];

namespace {

// The game's gameplay tags for each power (native tags, spelled as in the exe).
const char* const kPowerTags[PowerCount] = {"Character.Ability.Genesisconfiguration.Telekinesis",
                                            "Character.Ability.Genesisconfiguration.Pyrokinesis",
                                            "Character.Ability.Genesisconfiguration.Hellchains"};

using Vec = Vec3;
struct Rot {
    double pitch, yaw, roll;
};
template <class T> struct TArray {
    T* data;
    int32_t num, max;
};

constexpr uint8_t MOVE_Falling = 3, MOVE_Flying = 5;
constexpr uint8_t SlateHidden = 2, SlateCollapsed = 1;

std::mutex g_snapMutex;
Snapshot g_snap;

// Copied under the snapshot mutex for a click; dereferenced only on the game thread.
struct SpawnerContext {
    GenerationRef controller, player, world;
    const char* Why() const;
};
SpawnerContext g_spawnerContext;
std::atomic<bool> g_spawnerBusy{false};

// A give that dropped items: copies beyond the asked amount are removed, also those the game spawns a moment later.
struct DropWatch {
    UObject* item;
    std::string name;
    std::vector<UObject*> lying, kept; // pickups there before the give / the ones we leave on the ground
    int keep, removed = 0;
    uint64_t until;
    Vec3 at; // where the player stood: only pickups within 5 m of it are ours
};

// Game-thread state.
struct State {
    Weak pc, player, attrs, cmc, cheat, objectives;
    Weak cheatPawn; // the pawn the tag-based cheats (invisible, infinite ammo) were applied to
    uint64_t lockAt = 0;                 // when the current lockpick close-up opened (UINT64_MAX: already opened)
    uint64_t splashAt = 0, splashSince = 0; // last / first look for the startup splash screens
    bool introsDone = false;
    bool aimHooked = false, aimFailed = false;
    bool viewLogged = false, viewFallbackLogged = false;
    std::vector<DropWatch> dropWatch;
    uint64_t dropAt = 0;
    std::map<std::string, Weak> cdo;
    std::map<std::string, Weak> items;
    std::vector<Weak> enemies;
    uint64_t pcScanAt = 0, enemiesAt = 0, slowAt = 0, cheatClassAt = 0;
    bool cheatTried = false, cheatClass = false;
    bool cheatState[ToggleCount] = {};
    bool applied[ToggleCount] = {};
    float lastValue[ValueCount] = {};
    std::map<std::string, float> originals; // data-asset values we changed, restored when turned off
    std::map<UObject*, int> maxAmmo, maxDurability;
    std::vector<std::pair<Weak, uint8_t>> hiddenWidgets;
    std::map<UObject*, std::pair<int32_t, float>> enemyHealth; // object index + last health seen, to measure damage
    Weak camera;                            // free camera actor
    GenerationRef photoCamera, photoController, photoPlayer, photoWorld;
    photo::Bookmarks cameraBookmarks;
    const char* photoMessage = "";
    const char* enemySpawnerMessage = "";
    int enemySpawnerCreated = 0, enemySpawnerRequested = 0;
    Vec camPos{};
    uint64_t camAt = 0;
    float jumpZ = 0;
    float speedBase = -1;
    std::array<Vec, 3> slotPos{};
    std::array<std::string, 3> slotMap;
} s;

constexpr double kDegToRad = 3.14159265358979 / 180.0;
// "No cooldown" uses a tiny duration: with 0 the game's timer never fires and the action stays blocked.
constexpr float kInstant = 0.05f;
constexpr uint8_t MOVE_Walking = 1;

struct Transform { // FTransform with double precision (UE5): rotation quat, translation, scale
    double rot[4] = {0, 0, 0, 1};
    Vec pos{};
    double pad0 = 0;
    Vec scale{1, 1, 1};
    double pad1 = 0;
};
static_assert(sizeof(Transform) == 0x60);

UObject* Ptr(UObject* o, const char* prop)
{
    auto p = o ? Member<UObject*>(o, prop) : nullptr;
    return p ? *p : nullptr;
}

UObject* Cdo(const char* className)
{
    Weak& w = s.cdo[className];
    if (!w.Get()) w = Weak(FindObject(("Default__" + std::string(className)).c_str()));
    return w.Get();
}

// FGameplayAttributeData: BaseValue / CurrentValue.
struct Attr {
    float* base = nullptr;
    float* cur = nullptr;
    explicit operator bool() const { return cur != nullptr; }
    float Get() const { return cur ? *cur : 0.f; }
    void Set(float v) const
    {
        if (cur) *base = *cur = v;
    }
    // Multipliers only touch the current value: the base (what a save could store) stays pristine.
    void SetCurrent(float v) const
    {
        if (cur) *cur = v;
    }
};

Attr GetAttr(UObject* set, const char* name)
{
    Prop p = set ? FindProp(ClassOf(set), name) : Prop{};
    if (!p || !p.inner) return {};
    auto* type = static_cast<const UClass*>(p.inner);
    Prop b = FindProp(type, "BaseValue"), c = FindProp(type, "CurrentValue");
    if (!b || !c) return {};
    uint8_t* data = reinterpret_cast<uint8_t*>(set) + p.offset;
    return {reinterpret_cast<float*>(data + b.offset), reinterpret_cast<float*>(data + c.offset)};
}

UObject* AttributeSet(UObject* actor, const char* className)
{
    auto* list = Member<TArray<UObject*>>(Ptr(actor, "AbilitySystem"), "SpawnedAttributes");
    UClass* cls = FindClass(className);
    if (!list || !cls) return nullptr;
    for (int i = 0; i < list->num; i++)
        if (list->data[i] && IsA(list->data[i], cls)) return list->data[i];
    return nullptr;
}

Vec Location(UObject* actor)
{
    Call c(actor, "K2_GetActorLocation");
    return c.Run() ? c.Out<Vec>("ReturnValue") : Vec{};
}

bool Teleport(UObject* actor, Vec to)
{
    Call r(actor, "K2_GetActorRotation");
    r.Run();
    Call c(actor, "K2_TeleportTo");
    return c && c.Arg(0, to).Arg(1, r.Out<Rot>("ReturnValue")).Run();
}

double Distance(Vec a, Vec b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

// Camera orientation: where it looks (pitch included) and its flat right vector.
Rot LookAxes(UObject* pc, Vec& forward, Vec& right)
{
    Call c(pc, "GetControlRotation");
    c.Run();
    Rot r = c.Out<Rot>("ReturnValue");
    double p = r.pitch * kDegToRad, y = r.yaw * kDegToRad;
    forward = {std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
    right = {-std::sin(y), std::cos(y), 0};
    return r;
}

// The game's own movement input, so any keyboard layout or a gamepad works: returned as forward / right amounts.
void MoveInput(UObject* player, const Vec& right, double& forwardAmount, double& rightAmount)
{
    Call c(player, "GetLastMovementInputVector");
    c.Run();
    Vec in = c.Out<Vec>("ReturnValue");
    forwardAmount = in.x * right.y - in.y * right.x; // flat forward = (right.y, -right.x)
    rightAmount = in.x * right.x + in.y * right.y;
}

// Velocity that flies where the camera looks.
Vec FlyVelocity(UObject* pc, UObject* player, float speed)
{
    Vec fwd, right;
    LookAxes(pc, fwd, right);
    double f, r;
    MoveInput(player, right, f, r);
    return {(fwd.x * f + right.x * r) * speed, (fwd.y * f + right.y * r) * speed, fwd.z * f * speed};
}

// Restores a float changed in a shared data asset (player settings) when the feature turns off.
void Override(UObject* obj, const char* prop, bool on, float value = 0)
{
    float* p = Member<float>(obj, prop);
    if (!p) return;
    auto it = s.originals.find(prop);
    if (on) {
        if (it == s.originals.end()) s.originals[prop] = *p;
        *p = value;
    } else if (it != s.originals.end()) {
        *p = it->second;
        s.originals.erase(it);
    }
}

// Returns the toggle state; runs `off` once when it goes from on to off.
bool Track(Toggle t, const std::function<void()>& off)
{
    bool on = g_on[t];
    if (s.applied[t] && !on) off();
    s.applied[t] = on;
    return on;
}

UObject* CheatManager()
{
    if (UObject* cm = s.cheat.Get()) return cm;
    UObject* pc = s.pc.Get();
    if (!pc || s.cheatTried) return nullptr;
    s.cheatTried = true; // once per controller

    UClass* base = FindClass("InfernoCheatManager");
    UObject** slot = Member<UObject*>(pc, "CheatManager");
    if (slot && *slot && IsA(*slot, base)) {
        s.cheat = Weak(*slot);
        km::Log("Cheat manager: using the controller's %s", Name(*slot).c_str());
        return *slot;
    }
    auto* cheatClass = Member<UClass*>(pc, "CheatClass");
    UClass* cls = cheatClass && *cheatClass && IsChildOf(*cheatClass, base) ? *cheatClass : FindClass("BP_InfernoCheatManager_C");
    if (!cls) {
        km::Log("Cheat manager: class not loaded, cheat-based options disabled");
        return nullptr;
    }
    Call spawn(Cdo("GameplayStatics"), "SpawnObject");
    spawn.Arg(0, cls).Arg(1, pc).Run();
    UObject* cm = spawn.Out<UObject*>("ReturnValue");
    if (cm && slot) *slot = cm; // owned by the controller: kept alive by the GC, visible to the game's cheat code
    km::Log("Cheat manager: spawned %s -> %s", Name(reinterpret_cast<UObject*>(cls)).c_str(), cm ? "OK" : "FAILED");
    s.cheat = Weak(cm);
    return cm;
}

// The cheat manager is created on first use only (it derives from the game's dev hotkey system and may touch input):
// until then, report whether it could be.
bool CheatAvailable()
{
    if (s.cheat.Get()) return true;
    if (s.cheatTried || !s.pc.Get()) return false;
    uint64_t now = GetTickCount64();
    if (!s.cheatClass && now - s.cheatClassAt > 1000) { // FindClass only caches hits: look for it once a second
        s.cheatClassAt = now;
        s.cheatClass = FindClass("BP_InfernoCheatManager_C") != nullptr;
    }
    return s.cheatClass;
}

bool CheatCall(const char* fn)
{
    Call c(CheatManager(), fn);
    bool ok = c && c.Run();
    km::Log("Cheat %s: %s", fn, ok ? "called" : "unavailable");
    return ok;
}

const char* SpawnerContext::Why() const
{
    if (!Ready()) return "Game engine not hooked: see BananMenu.log.";
    UObject* pc = controller.Get();
    UObject* pawn = player.Get();
    UObject* levelWorld = world.Get();
    if (!pc || !pawn || !levelWorld || pc != s.pc.Get() || pawn != s.player.Get() ||
        IsDefaultObject(pc) || IsDefaultObject(pawn) || IsDefaultObject(levelWorld) ||
        !IsA(pc, FindClass("CoreController")) || !IsA(levelWorld, FindClass("World")) ||
        FreshObjectProperty(controller, "Pawn", FindClass("PlayerCharacter")).Get() != pawn ||
        OuterOf(OuterOf(pawn)) != levelWorld || OuterOf(OuterOf(pc)) != levelWorld)
        return "Enemy spawner unavailable: load a playable level first.";
    return "";
}

using Kind = Prop::Kind;
const std::initializer_list<Parameter> kMakeClassPath = {
    {"PathString", Kind::String, 16}, {"ReturnValue", Kind::Struct, 32, "SoftClassPath", true}};
const std::initializer_list<Parameter> kMakeSoftClass = {
    {"SoftClassPath", Kind::Struct, 32, "SoftClassPath"}, {"ReturnValue", Kind::SoftClass, 40, "Class", true}};
const std::initializer_list<Parameter> kLoadEnemyClass = {
    {"AssetClass", Kind::SoftClass, 40, "Class"}, {"ReturnValue", Kind::Class, 8, "Class", true}};
const std::initializer_list<Parameter> kMakeSpawnTransform = {
    {"Location", Kind::Struct, 24, "Vector"}, {"Rotation", Kind::Struct, 24, "Rotator"},
    {"Scale", Kind::Struct, 24, "Vector"}, {"ReturnValue", Kind::Struct, 96, "Transform", true}};
const std::initializer_list<Parameter> kBeginEnemy = {
    {"WorldContextObject", Kind::Object, 8, "Object"}, {"ActorClass", Kind::Class, 8, "Class"},
    {"SpawnTransform", Kind::Struct, 96, "Transform"},
    {"CollisionHandlingOverride", Kind::Enum, 1, "ESpawnActorCollisionHandlingMethod"},
    {"Owner", Kind::Object, 8, "Actor"}, {"TransformScaleMethod", Kind::Enum, 1, "ESpawnActorScaleMethod"},
    {"ReturnValue", Kind::Object, 8, "Actor", true}};
const std::initializer_list<Parameter> kFinishEnemy = {
    {"Actor", Kind::Object, 8, "Actor"}, {"SpawnTransform", Kind::Struct, 96, "Transform"},
    {"TransformScaleMethod", Kind::Enum, 1, "ESpawnActorScaleMethod"}, {"ReturnValue", Kind::Object, 8, "Actor", true}};

Call SpawnerCall(UObject* object, const char* function, std::initializer_list<Parameter> parameters)
{
    return Call(object && Signature(ClassOf(object), function, parameters) ? object : nullptr, function, true);
}

UObject* SpawnerLibrary(const char* name)
{
    UObject* object = Cdo(name);
    return object && IsA(object, FindClass(name)) && IsDefaultObject(object) ? object : nullptr;
}

bool SpawnerAPIs()
{
    return FindClass("EnemyCharacter") &&
        Signature(FindClass("KismetSystemLibrary"), "MakeSoftClassPath", kMakeClassPath) &&
        Signature(FindClass("KismetSystemLibrary"), "Conv_SoftClassPathToSoftClassRef", kMakeSoftClass) &&
        Signature(FindClass("KismetSystemLibrary"), "LoadClassAsset_Blocking", kLoadEnemyClass) &&
        Signature(FindClass("KismetMathLibrary"), "MakeTransform", kMakeSpawnTransform) &&
        Signature(FindClass("GameplayStatics"), "BeginDeferredActorSpawnFromClass", kBeginEnemy) &&
        Signature(FindClass("GameplayStatics"), "FinishSpawningActor", kFinishEnemy) &&
        Signature(FindClass("Actor"), "K2_GetActorLocation", {{"ReturnValue", Kind::Struct, 24, "Vector", true}}) &&
        Signature(FindClass("Actor"), "GetActorForwardVector", {{"ReturnValue", Kind::Struct, 24, "Vector", true}}) &&
        Signature(FindClass("Actor"), "K2_GetActorRotation", {{"ReturnValue", Kind::Struct, 24, "Rotator", true}}) &&
        Signature(FindClass("Actor"), "K2_DestroyActor", {});
}

bool SpawnerRun(Call& call)
{
    bool ran = false;
    std::function<void()> invoke = [&] { ran = call.Run(); };
    return Guard(invoke) && ran;
}

// Reacquire by exact generated-class identity after native calls; never retain a raw asset pointer.
UClass* SelectedEnemyClass(int type)
{
    const std::string path = spawn::Types[type].path;
    UClass* cls = FindClass(path.substr(path.find_last_of('.') + 1).c_str());
    if (!cls || !IsChildOf(cls, FindClass("EnemyCharacter"))) return nullptr;
    std::string actual;
    std::vector<UObject*> seen;
    for (UObject* object = reinterpret_cast<UObject*>(cls); object; object = OuterOf(object)) {
        if (seen.size() >= 32 || std::find(seen.begin(), seen.end(), object) != seen.end()) return nullptr;
        seen.push_back(object);
        const std::string name = Name(object);
        if (name.empty()) return nullptr;
        actual = actual.empty() ? name : name + "." + actual;
    }
    return actual == path ? cls : nullptr;
}

UObject* ItemManager()
{
    Call c(Cdo("CoreGameplayStatics"), "GetItemManager");
    return c.Arg(0, s.pc.Get()).Run() ? c.Out<UObject*>("ReturnValue") : nullptr;
}

// An FName from text, through the engine (finds the existing name, or adds it).
uint64_t MakeName(const char* text)
{
    std::wstring w(text, text + strlen(text));
    FString str{w.data(), int32_t(w.size() + 1), int32_t(w.size() + 1)};
    Call name(Cdo("KismetStringLibrary"), "Conv_StringToName");
    return name.Arg(0, str).Run() ? name.Out<uint64_t>("ReturnValue") : 0;
}

// ---- Every item of the game, loaded or not. Listing the loaded item data assets only showed what the story had
// met (no shotgun before finding one); the asset registry knows them all and GetAsset loads one when it's given.
// The registry fills an array that is kept (never freed): its entries stay valid for GetAsset.
struct ItemAsset {
    std::string name, cls;
    const uint8_t* data; // FAssetData
};
std::vector<ItemAsset> g_itemAssets;
std::vector<std::string> g_itemsLeftOut; // paid / bonus content, also when it happens to be loaded
int32_t g_assetDataSize = 0;

void ReadItemAssets()
{
    static bool done;
    if (done) return;
    done = true;
    Call reg(Cdo("AssetRegistryHelpers"), "GetAssetRegistry");
    UObject* registry = reg.Run() ? reg.Out<UObject*>("ReturnValue") : nullptr; // TScriptInterface: object first
    auto* info = reinterpret_cast<const UClass*>(FindObject("AssetData", FindClass("ScriptStruct")));
    Prop name = FindProp(info, "AssetName"), cls = FindProp(info, "AssetClassPath"), path = FindProp(info, "PackagePath");
    g_assetDataSize = StructSize(info);
    struct {
        uint64_t package, asset; // FTopLevelAssetPath of the UItem class
    } itemClass{MakeName("/Script/Project"), MakeName("Item")};
    Call get(registry, FindClass("AssetRegistry"), "GetAssetsByClass");
    if (!get || !name || !cls || !path || g_assetDataSize <= 0 || !get.Arg(0, itemClass).Arg(2, true).Run() ||
        !get.Out<bool>("ReturnValue")) {
        km::Log("Item list: asset registry unavailable, only loaded items are listed");
        return;
    }
    auto found = get.Out<TArray<uint8_t>>("OutAssetData");
    std::string skipped;
    for (int i = 0; i < found.num; i++) {
        const uint8_t* e = found.data + size_t(i) * size_t(g_assetDataSize);
        std::string asset = NameToString(e + name.offset), folder = NameToString(e + path.offset), low = folder;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        // Paid / bonus editions' content stays out (giving it would bypass the purchase).
        if (low.find("deluxe") != std::string::npos || low.find("preorder") != std::string::npos ||
            low.find("extracontent") != std::string::npos || low.find("dlc") != std::string::npos ||
            low.find("retail") != std::string::npos || low.find("bonus") != std::string::npos) {
            skipped += " " + asset + " (" + folder + ")";
            g_itemsLeftOut.push_back(asset);
            continue;
        }
        g_itemAssets.push_back({asset, NameToString(e + cls.offset + 8), e}); // AssetClassPath.AssetName
    }
    km::Log("Item list: %zu item assets in the asset registry, left out:%s", g_itemAssets.size(),
            skipped.empty() ? " none" : skipped.c_str());
}

UObject* LoadItemAsset(const std::string& name)
{
    for (const ItemAsset& a : g_itemAssets)
        if (a.name == name) {
            Call load(Cdo("AssetRegistryHelpers"), "GetAsset");
            UObject* item = load.ArgRaw(0, a.data, size_t(g_assetDataSize)).Run() ? load.Out<UObject*>("ReturnValue") : nullptr;
            km::Log("Item %s: %s", name.c_str(), item ? "loaded" : "could not be loaded");
            return item;
        }
    return nullptr;
}

UObject* FindItem(const std::string& name)
{
    Weak& w = s.items[name];
    if (!w.Get()) w = Weak(FindObject(name.c_str(), FindClass("Item")));
    if (!w.Get()) w = Weak(LoadItemAsset(name)); // not met yet in the story (or unloaded since)
    return w.Get();
}

int ItemCount(UObject* item, bool withStash = false)
{
    Call c(ItemManager(), "GetItemCountInInventory");
    return c && c.Arg(0, s.player.Get()).Arg(1, item).Arg(2, false).Arg(3, withStash).Run() ? c.Out<int32_t>("ReturnValue")
                                                                                             : -1;
}

// This adapter only accesses the carried inventory. Native void calls are never treated as proof of success:
// resources::Reconcile rereads the total after every operation, including failed calls.
struct ResourceInventory final : resources::Backend, resources::StackWriter {
    Weak player, manager, item, selected;
    int selectedCount = 0;
    ResourceInventory(UObject* owner, UObject* asset) : player(owner), manager(ItemManager()), item(asset) {}
    bool HasParams(const char* function, std::initializer_list<const char*> names)
    {
        UObject* im = manager.Get();
        if (!im) return false;
        auto* fn = reinterpret_cast<const UClass*>(FindFunction(ClassOf(im), function));
        if (!fn) return false;
        for (const char* name : names) if (!FindProp(fn, name)) return false;
        return true;
    }
    bool Run(Call& call)
    {
        bool ran = false;
        std::function<void()> invoke = [&] { ran = call.Run(); };
        return Guard(invoke) && ran;
    }
    bool CarriedStack(UObject* info)
    {
        bool inStash = true;
        return info && player.Get() && item.Get() && IsA(info, FindClass("ItemInfoStackable")) &&
            Ptr(info, "Item") == item.Get() && Ptr(info, "Owner") == player.Get() &&
            GetBool(info, "bIsInStash", &inStash) && !inStash;
    }
    bool Read(int, int& total) override
    {
        UObject* owner = player.Get();
        UObject* asset = item.Get();
        Call count(manager.Get(), "GetItemCountInInventory");
        if (!owner || !asset || !HasParams("GetItemCountInInventory", {"Owner", "Item", "bInStash", "bIncludeStash", "ReturnValue"}) || !count)
            return false;
        count.Arg("Owner", owner).Arg("Item", asset).Arg("bInStash", false).Arg("bIncludeStash", false);
        if (!Run(count)) return false;
        total = count.Out<int32_t>("ReturnValue");
        return total >= 0;
    }
    bool CurrentStack(int, int& count) override
    {
        bool found = false;
        return SelectStack(found) && found && ReadStack(count) && count > 0;
    }
    bool SelectStack(bool& found) override
    {
        found = false;
        selected = {};
        UObject* owner = player.Get();
        UObject* asset = item.Get();
        Call get(manager.Get(), "GetItemFromInventory");
        if (!owner || !asset || !HasParams("GetItemFromInventory", {"Owner", "Item", "bIncludeStash", "ReturnValue"}) || !get) return false;
        get.Arg("Owner", owner).Arg("Item", asset).Arg("bIncludeStash", false);
        if (!Run(get)) return false;
        UObject* info = get.Out<UObject*>("ReturnValue");
        if (!info) return true;
        if (!CarriedStack(info)) return false;
        selected = Weak(info);
        found = true;
        return true;
    }
    bool ReadTotal(int& total) override { return Read(0, total); }
    bool ReadStack(int& count) override
    {
        UObject* info = selected.Get();
        int32_t* stack = Member<int32_t>(info, "StackableCount");
        if (!CarriedStack(info) || !stack || !SafeRead(stack, &count, sizeof(count)) || count < 0) return false;
        selectedCount = count;
        return true;
    }
    bool Remove(int, int amount) override
    {
        UObject* info = selected.Get();
        int count = 0;
        int32_t* stack = Member<int32_t>(info, "StackableCount");
        if (!CarriedStack(info) || !stack || !SafeRead(stack, &count, sizeof(count)) ||
            count != selectedCount || amount <= 0 || amount > count ||
            !HasParams("RemoveItem", {"ItemInfo", "RemoveCount", "bSkipCallback"})) return false;
        Call remove(manager.Get(), "RemoveItem");
        remove.Arg("ItemInfo", info).Arg("RemoveCount", amount).Arg("bSkipCallback", false);
        return Run(remove);
    }
    bool CanRefresh()
    {
        return HasParams("AddPlayerItemToSaveGame", {"ItemInfo"}) &&
            HasParams("CallbackUpdatedUIWithItem", {"ItemInfo", "Owner", "bBlockUIUpdate"});
    }
    bool CreateZeroStack() override
    {
        UObject* asset = item.Get();
        UObject* owner = player.Get();
        auto* infoClass = FindClass("ItemInfoStackable");
        UObject* defaults = Cdo("ItemInfoStackable");
        int count = -1, total = -1;
        bool defaultInStash = true;
        uint8_t inventoryType = 255, itemType = 255;
        int32_t* defaultCount = Member<int32_t>(defaults, "StackableCount");
        uint8_t* inventory = Member<uint8_t>(asset, "InventoryType");
        uint8_t* type = Member<uint8_t>(asset, "ItemType");
        // Confirm the native creation assumptions at runtime. A different item class/default is unavailable.
        if (!asset || !owner || !infoClass || Ptr(asset, "ItemInfo") != reinterpret_cast<UObject*>(infoClass) ||
            !defaultCount || !SafeRead(defaultCount, &count, sizeof(count)) || count != 0 ||
            !GetBool(defaults, "bIsInStash", &defaultInStash) || defaultInStash ||
            !inventory || !SafeRead(inventory, &inventoryType, sizeof(inventoryType)) || inventoryType != 0 ||
            !type || !SafeRead(type, &itemType, sizeof(itemType)) || itemType != 140 || !CanRefresh() ||
            !HasParams("CreateAndInitializeItem", {"Item", "Owner", "ItemInfoRef"}) || !ReadTotal(total) || total != 0)
            return false;
        // The verified native path registers a zero-count non-grid stack and places it via its callback.
        // It does not call AddItemInInventory/DropItem; zero count avoids native stack-limit removal.
        Call create(manager.Get(), "CreateAndInitializeItem");
        create.Arg("Item", asset).Arg("Owner", owner);
        if (!Run(create)) return false;
        selected = Weak(create.Out<UObject*>("ItemInfoRef"));
        struct Position { double x, y; } pos{-1, -1};
        Position* placed = Member<Position>(selected.Get(), "ItemPos");
        return ReadStack(count) && count == 0 && placed && SafeRead(placed, &pos, sizeof(pos)) &&
            std::isfinite(pos.x) && std::isfinite(pos.y) && pos.x >= 0 && pos.y >= 0;
    }
    bool WriteStack(int count) override
    {
        UObject* info = selected.Get();
        int before = -1;
        int32_t* stack = Member<int32_t>(info, "StackableCount");
        if (count < 0 || count > 9999 || !CanRefresh() || !CarriedStack(info) || !stack ||
            !SafeRead(stack, &before, sizeof(before)) || before != selectedCount) return false;
        // This is an explicit material cheat, like the native GiveItem SetStackableCount branch.
        // Its 0..9999 requested total intentionally is not clamped to the pickup stack size (10).
        *stack = count;
        return true;
    }
    bool RefreshStack() override
    {
        UObject* info = selected.Get();
        if (!CarriedStack(info) || !CanRefresh()) return false;
        Call save(manager.Get(), "AddPlayerItemToSaveGame");
        save.Arg("ItemInfo", info);
        if (!Run(save)) return false;
        Call refresh(manager.Get(), "CallbackUpdatedUIWithItem");
        refresh.Arg("ItemInfo", info).Arg("Owner", player.Get()).Arg("bBlockUIUpdate", false);
        return Run(refresh);
    }
    bool Add(int, int amount) override { return resources::AddToStack(*this, amount); }
};

const char* ResourceResultText(resources::Status status)
{
    switch (status) {
    case resources::Status::Success: return "Material total verified";
    case resources::Status::InvalidInput: return "Invalid material or target total";
    case resources::Status::ReadFailed: return "Could not read the carried material total";
    case resources::Status::StackUnavailable: return "Could not find a carried stack";
    case resources::Status::MutationFailed: return "Material change failed";
    case resources::Status::NoProgress: return "Inventory full or no progress";
    case resources::Status::UnexpectedMovement: return "Unexpected material count change; stopped";
    case resources::Status::AttemptsExceeded: return "Material change limit reached; stopped";
    }
    return "Material change failed";
}

// Pickups of this item lying in the level.
std::vector<UObject*> Pickables(UObject* item)
{
    std::vector<UObject*> out;
    ForEachInstance(FindClass("CorePickable"), [&](UObject* o) {
        if (Ptr(o, "Item") == item) out.push_back(o);
        return true;
    });
    return out;
}

// The watch for this item's drops, extended by 2 s: one per item, so quick repeated gives add up instead of each
// removing the others' drops as extra copies.
DropWatch& WatchDrops(UObject* item, const std::string& name, const std::vector<UObject*>& lying, Vec at)
{
    // 6 s: the first drop of an item can wait for its mesh to load before its copy appears.
    uint64_t until = GetTickCount64() + 6000;
    for (DropWatch& w : s.dropWatch)
        if (w.item == item) {
            w.until = until;
            w.at = at;
            return w;
        }
    s.dropWatch.push_back(DropWatch{item, name, lying, {}, 0, 0, until, at});
    return s.dropWatch.back();
}

// A pickup the player can see: not hidden (hidden copies may be the game's own bookkeeping: left alone).
// Not bItemIsActive: pickups lying in plain sight have it off.
bool OnTheGround(UObject* p)
{
    bool hidden = false;
    GetBool(p, "bHidden", &hidden);
    return !hidden;
}

void TrimDrops(DropWatch& w)
{
    for (UObject* p : Pickables(w.item)) {
        if (std::find(w.lying.begin(), w.lying.end(), p) != w.lying.end() ||
            std::find(w.kept.begin(), w.kept.end(), p) != w.kept.end() || !OnTheGround(p))
            continue;
        double d = Distance(Location(p), w.at);
        if (d > 500) continue; // not from this give (or still flying: looked at again next time)
        bool keep = int(w.kept.size()) < w.keep;
        if (keep) w.kept.push_back(p);
        else if (Call(p, "K2_DestroyActor").Run()) w.removed++;
        km::Log("Give %s: pickup %s %s (%.1f m away)", w.name.c_str(), Name(p).c_str(), keep ? "kept" : "removed", d / 100);
    }
}

void RefreshEnemies(uint64_t now)
{
    if (now - s.enemiesAt < 1000) return;
    s.enemiesAt = now;
    s.enemies.clear();
    ForEachInstance(FindClass("EnemyCharacter"), [](UObject* o) {
        s.enemies.emplace_back(o);
        return true;
    });
}

template <class F> void ForEachEnemy(F&& f)
{
    for (const Weak& w : s.enemies)
        if (UObject* e = w.Get()) f(e);
}

// Gameplay widgets currently on screen (taken when Hide HUD turns on; menus opened later are left alone).
void HideWidgets()
{
    Call hud(Cdo("CoreGameplayStatics"), "GetHUD");
    if (hud.Arg(0, s.pc.Get()).Run()) SetBool(hud.Out<UObject*>("ReturnValue"), "bShowHUD", false);
    ForEachInstance(FindClass("UserWidget"), [](UObject* w) {
        Call in(w, "IsInViewport");
        if (!in.Run() || !in.Out<bool>("ReturnValue")) return true;
        Call vis(w, "GetVisibility");
        vis.Run();
        uint8_t v = vis.Out<uint8_t>("ReturnValue");
        if (v == SlateHidden || v == SlateCollapsed) return true;
        Call(w, "SetVisibility").Arg(0, SlateHidden).Run();
        s.hiddenWidgets.emplace_back(Weak(w), v);
        return true;
    });
}

void ShowWidgets()
{
    Call hud(Cdo("CoreGameplayStatics"), "GetHUD");
    if (hud.Arg(0, s.pc.Get()).Run()) SetBool(hud.Out<UObject*>("ReturnValue"), "bShowHUD", true);
    for (auto& [w, v] : s.hiddenWidgets)
        if (UObject* o = w.Get()) Call(o, "SetVisibility").Arg(0, v).Run();
    s.hiddenWidgets.clear();
}

// Free camera: a camera actor we move ourselves while the player stays frozen in place.
void StartFreeCam(UObject* pc, UObject* player, uint64_t now)
{
    Transform t;
    t.pos = Location(player);
    t.pos.z += 60; // roughly eye height
    Call spawn(Cdo("GameplayStatics"), "BeginDeferredActorSpawnFromClass");
    spawn.Arg(0, pc).Arg(1, FindClass("CameraActor")).Arg(2, t).Arg(3, uint8_t(1)).Arg(4, static_cast<UObject*>(nullptr)).Arg(5, uint8_t(1));
    UObject* cam = spawn.Run() ? spawn.Out<UObject*>("ReturnValue") : nullptr;
    if (cam) {
        Call(Cdo("GameplayStatics"), "FinishSpawningActor").Arg(0, cam).Arg(1, t).Arg(2, uint8_t(1)).Run();
        Call(pc, "SetViewTargetWithBlend").Arg(0, cam).Arg(1, 0.f).Arg(2, uint8_t(0)).Arg(3, 0.f).Arg(4, false).Run();
        Call(s.cmc.Get(), "DisableMovement").Run();
    }
    km::Log("Free camera: %s", cam ? "on" : "camera actor could not be spawned");
    s.camera = Weak(cam);
    s.photoCamera = GenerationRef(cam);
    s.photoController = GenerationRef(pc);
    s.photoPlayer = GenerationRef(player);
    s.photoWorld = GenerationRef(OuterOf(OuterOf(player)));
    s.camPos = t.pos;
    s.camAt = now;
}

void StopFreeCam(UObject* pc, UObject* player)
{
    if (UObject* cam = s.camera.Get()) {
        Call(pc, "SetViewTargetWithBlend").Arg(0, player).Arg(1, 0.f).Arg(2, uint8_t(0)).Arg(3, 0.f).Arg(4, false).Run();
        Call(cam, "K2_DestroyActor").Run();
    }
    Call(s.cmc.Get(), "SetMovementMode").Arg(0, MOVE_Walking).Arg(1, uint8_t(0)).Run();
    s.camera = {};
    s.photoCamera = s.photoController = s.photoPlayer = s.photoWorld = {};
}

void MoveFreeCam(UObject* pc, UObject* player, uint64_t now)
{
    UObject* cam = s.camera.Get();
    if (!cam) return;
    double dt = std::min((now - s.camAt) / 1000.0, 0.1);
    s.camAt = now;
    Vec v = FlyVelocity(pc, player, 1000.f * g_value[SpeedMult]);
    s.camPos = {s.camPos.x + v.x * dt, s.camPos.y + v.y * dt, s.camPos.z + v.z * dt};
    Vec fwd, right;
    Rot rot = LookAxes(pc, fwd, right);
    Call(cam, "K2_SetActorLocationAndRotation").Arg(0, s.camPos).Arg(1, rot).Arg(2, false).Arg(4, true).Run();
}

// Includes the package and every outer: matching a short world name alone is insufficient.
std::string PhotoMapIdentity(UObject* player)
{
    UObject* world = player ? OuterOf(OuterOf(player)) : nullptr;
    if (!world || !IsA(world, FindClass("World"))) return {};
    std::vector<UObject*> seen;
    std::string path;
    for (UObject* o = world; o; o = OuterOf(o)) {
        if (seen.size() >= 32 || std::find(seen.begin(), seen.end(), o) != seen.end()) return {};
        seen.push_back(o);
        const std::string name = Name(o);
        if (name.empty()) return {};
        path = path.empty() ? name : name + "." + path;
    }
    return path;
}

UObject* PhotoObject(const GenerationRef& owner, const char* name, const char* className)
{
    return FreshObjectProperty(owner, name, FindClass(className)).Get();
}

Call PhotoCall(UObject* object, const char* function) { return Call(object, function, true); }

class PhotoCamera final : public photo::Backend {
public:
    const char* Why() const {
        if (!g_on[FreeCam] || !s.applied[FreeCam]) return "Enable free camera first.";
        UObject* pc = s.photoController.Get();
        UObject* player = s.photoPlayer.Get();
        UObject* camera = s.photoCamera.Get();
        if (!pc || !player || !camera || pc != s.pc.Get() || player != s.player.Get() || camera != s.camera.Get() ||
            PhotoObject(s.photoController, "Pawn", "PlayerCharacter") != player || !s.photoWorld.Get() ||
            OuterOf(OuterOf(player)) != s.photoWorld.Get())
            return "Free camera lifetime unavailable. Re-enable free camera.";
        if (!IsA(camera, FindClass("CameraActor")) || IsDefaultObject(camera)) return "Free camera unavailable.";
        auto target = PhotoCall(pc, "GetViewTarget");
        if (!target.Run() || target.Out<UObject*>("ReturnValue") != camera) return "Free camera is not the active view.";
        UObject* component = PhotoObject(s.photoCamera, "CameraComponent", "CameraComponent");
        UObject* manager = PhotoObject(s.photoController, "PlayerCameraManager", "PlayerCameraManager");
        if (!component || !manager || !FindProp(ClassOf(component), "FieldOfView", true).Is(Prop::Kind::Float, sizeof(float)))
            return "Camera field of view unavailable.";
        if (!PhotoCall(camera, "K2_GetActorLocation") || !PhotoCall(camera, "K2_GetActorRotation") ||
            !PhotoCall(camera, "K2_SetActorLocationAndRotation") || !PhotoCall(pc, "SetControlRotation") ||
            !PhotoCall(pc, "FOV") || !PhotoCall(component, "SetFieldOfView") || !PhotoCall(manager, "GetFOVAngle"))
            return "Camera bookmark API unavailable.";
        return "";
    }
    bool Active() const override { return !*Why(); }
    std::string Map() const override { return PhotoMapIdentity(s.photoPlayer.Get()); }
    bool Read(photo::Pose& pose) override {
        if (!Active()) return false;
        const GenerationRef camera = s.photoCamera;
        const FreshObjectProperty manager(s.photoController, "PlayerCameraManager", FindClass("PlayerCameraManager"));
        auto position = PhotoCall(camera.Get(), "K2_GetActorLocation");
        if (!position.Run()) return false;
        auto rotation = PhotoCall(camera.Get(), "K2_GetActorRotation");
        if (!rotation.Run()) return false;
        // Native calls may replace strongly referenced zero-serial objects. Re-resolve now,
        // never carry a manager pointer or a Call targeting it across those calls.
        auto fov = PhotoCall(manager.Get(), "GetFOVAngle");
        if (!fov.Run() || !manager.Get() || !camera.Get()) return false;
        Vec p = position.Out<Vec>("ReturnValue");
        Rot r = rotation.Out<Rot>("ReturnValue");
        const float setting = g_value[Fov];
        if (!fov::ValidSetting(setting)) return false;
        pose = {{p.x, p.y, p.z}, {r.pitch, r.yaw, r.roll}, fov.Out<float>("ReturnValue"), setting != 0};
        return photo::Valid(pose);
    }
    bool Apply(const photo::Pose& pose) override {
        if (!Active() || !photo::Valid(pose)) return false;
        const GenerationRef camera = s.photoCamera, controller = s.photoController;
        const FreshObjectProperty component(camera, "CameraComponent", FindClass("CameraComponent"));
        const Vec p{pose.position[0], pose.position[1], pose.position[2]};
        const Rot r{pose.rotation[0], pose.rotation[1], pose.rotation[2]};
        // MoveFreeCam consumes controller rotation and camPos next tick. Update both paths.
        if (!component.Get() || !PhotoCall(controller.Get(), "SetControlRotation").Arg(0, r).Run()) return false;
        if (!component.Get() || !controller.Get()) return false;
        auto move = PhotoCall(camera.Get(), "K2_SetActorLocationAndRotation");
        if (!move.Arg(0, p).Arg(1, r).Arg(2, false).Arg(4, true).Run() || !move.Out<bool>("ReturnValue")) return false;
        s.camPos = p;
        s.camAt = GetTickCount64();
        if (!controller.Get() || !PhotoCall(component.Get(), "SetFieldOfView").Arg(0, pose.fov).Run()) return false;
        // The controller's explicit FOV lock wins over camera-component FOV. Restore that
        // setting too: locked bookmarks use the measured angle, unlocked ones release it.
        const float setting = pose.fovLocked ? pose.fov : 0.f;
        if (!component.Get() || !PhotoCall(controller.Get(), "FOV").Arg(0, setting).Run() ||
            !component.Get() || !controller.Get()) return false;
        g_value[Fov] = setting;
        s.lastValue[Fov] = setting;
        return true; // dispatch accepted; the UI deliberately says requested
    }
};

const char* PhotoMessage(photo::Result result)
{
    switch (result) {
    case photo::Result::Saved: return "Camera bookmark saved for this session.";
    case photo::Result::Deleted: return "Camera bookmark deleted.";
    case photo::Result::RestoreRequested: return "Camera restore requested; check the view.";
    case photo::Result::InvalidSlot: return "Invalid camera bookmark.";
    case photo::Result::CameraUnavailable: return "Live free camera required; nothing restored.";
    case photo::Result::MapUnavailable: return "Camera map unavailable; nothing restored.";
    case photo::Result::Empty: return "Camera bookmark is empty.";
    case photo::Result::WrongMap: return "Camera bookmark belongs to another map.";
    case photo::Result::InvalidPose: return "Camera bookmark values are invalid.";
    case photo::Result::ReadFailed: return "Could not read camera; bookmark unchanged.";
    case photo::Result::ApplyFailed: return "Camera restore incomplete; check the view.";
    }
    return "";
}

UObject* EnemyAttributes(UObject* enemy) { return AttributeSet(enemy, "CharacterAttributeSet"); }

bool KillEnemy(UObject* enemy, UObject* player)
{
    Call c(enemy, "KillEnemyInstantly");
    return c && c.Arg(0, false).Arg(1, player).Arg(2, false).Run();
}

// One-hit kill and damage multiplier, whatever the weapon or power: watch each enemy's health and,
// when it drops, remove (multiplier - 1) times the damage, or kill it outright through the game's own code.
void TickEnemyDamage(UObject* player, uint64_t now)
{
    bool oneHit = g_on[OneHitKill];
    float mult = g_value[DamageMult];
    if (!oneHit && mult <= 1.01f) {
        s.enemyHealth.clear();
        return;
    }
    bool first = s.enemyHealth.empty();
    RefreshEnemies(now);
    int tracked = 0;
    ForEachEnemy([&](UObject* e) {
        Attr h = GetAttr(EnemyAttributes(e), "Health");
        if (!h) return;
        tracked++;
        float cur = h.Get();
        int32_t index = Weak(e).index; // a new enemy may reuse a dead one's address
        auto it = s.enemyHealth.find(e);
        float last = it != s.enemyHealth.end() && it->second.first == index ? it->second.second : cur;
        if (cur > 0 && cur < last - 0.01f) {
            float left = oneHit ? 0 : cur - (last - cur) * (mult - 1);
            if (left <= 0) {
                bool ok = KillEnemy(e, player);
                km::Log("%s: %s %s", oneHit ? "One-hit kill" : "Damage multiplier", Name(e).c_str(), ok ? "killed" : "kill failed");
                cur = 0;
            } else {
                h.Set(left);
                cur = left;
            }
        }
        s.enemyHealth[e] = {index, cur};
    });
    if (s.enemyHealth.size() > 512) s.enemyHealth.clear(); // dead enemies pile up over a long session
    if (first && tracked) km::Log("Enemy damage tracking: %d of %zu enemies have health", tracked, s.enemies.size());
}

// ---------------------------------------------------------------- ESP (game-thread side)

std::mutex g_espMutex;
EspFrame g_esp;

// Bones worth drawing on any rig (human or not), each linked to its nearest drawn ancestor.
struct Skeleton {
    int32_t meshIndex = -1;
    std::vector<std::pair<uint64_t, int>> bones; // bone FName, parent slot (-1 = none)
    int head = -1;
};
std::map<UObject*, Skeleton> g_skeletons;

bool KeepBone(std::string n)
{
    for (char& c : n) c = char(std::tolower(uint8_t(c)));
    static const char* const keep[] = {"head", "neck", "spine", "pelvis", "hips", "clavicle", "upperarm", "lowerarm",
                                       "forearm", "hand", "thigh", "calf", "shin", "foot", "leg"};
    static const char* const drop[] = {"twist", "ik_", "_ik", "finger", "thumb", "index", "middle", "ring", "pinky",
                                       "toe", "ball", "corrective", "_end", "roll", "weapon", "attach", "socket", "jaw",
                                       "eye", "brow", "lip_", "cheek", "tongue", "hair", "cloth", "breast", "bag",
                                       "strap", "prop", "helper", "nub", "teeth", "nose", "camera", "_ear", "mouth",
                                       "vb_", "dummy", "root"};
    for (const char* d : drop)
        if (n.find(d) != std::string::npos) return false;
    for (const char* k : keep)
        if (n.find(k) != std::string::npos) return true;
    return false;
}

Skeleton& SkeletonOf(UObject* mesh)
{
    if (g_skeletons.size() > 256) g_skeletons.clear(); // meshes of dead enemies
    Skeleton& sk = g_skeletons[mesh];
    int32_t index = Weak(mesh).index;
    if (sk.meshIndex == index) return sk;
    sk = {};
    sk.meshIndex = index;
    Call num(mesh, "GetNumBones");
    int n = num.Run() ? std::min(num.Out<int32_t>("ReturnValue"), 512) : 0;
    std::map<uint64_t, int> slot;
    Call boneName(mesh, "GetBoneName");
    for (int i = 0; i < n; i++) {
        boneName.Arg(0, i).Run();
        uint64_t name = boneName.Out<uint64_t>("ReturnValue");
        if (!KeepBone(NameToString(&name)) || slot.count(name)) continue;
        slot[name] = int(sk.bones.size());
        if (sk.head < 0 && NameToString(&name).find("ead") != std::string::npos) sk.head = int(sk.bones.size());
        sk.bones.push_back({name, -1});
    }
    Call parentOf(mesh, "GetParentBone");
    for (auto& [name, parent] : sk.bones) {
        uint64_t cur = name;
        for (int guard = 0; guard < 64 && cur; guard++) { // FName "None" = 0
            parentOf.Arg(0, cur).Run();
            cur = parentOf.Out<uint64_t>("ReturnValue");
            if (auto it = slot.find(cur); it != slot.end()) {
                parent = it->second;
                break;
            }
        }
    }
    static int logged;
    if (logged++ < 8) km::Log("ESP skeleton: %s keeps %zu of %d bones", Name(reinterpret_cast<UObject*>(ClassOf(OuterOf(mesh)))).c_str(), sk.bones.size(), n);
    return sk;
}

// "BP_Cultist_Melee_C" -> "Cultist Melee"
std::string PrettyClass(UObject* actor)
{
    static std::map<UClass*, std::string> cache;
    UClass* cls = ClassOf(actor);
    if (auto it = cache.find(cls); it != cache.end()) return it->second;
    std::string n = Name(reinterpret_cast<UObject*>(cls));
    for (const char* prefix : {"BP_", "ABP_", "CH_", "B_"})
        if (n.rfind(prefix, 0) == 0) n.erase(0, strlen(prefix));
    if (n.size() > 2 && n.compare(n.size() - 2, 2, "_C") == 0) n.resize(n.size() - 2);
    for (const char* word : {"Character", "Base"})
        if (auto p = n.find(word); p != std::string::npos && n.size() > strlen(word)) n.erase(p, strlen(word));
    std::string out;
    for (size_t i = 0; i < n.size(); i++) {
        char c = n[i] == '_' ? ' ' : n[i];
        if (i && std::isupper(uint8_t(c)) && std::islower(uint8_t(n[i - 1]))) out += ' ';
        if (!(c == ' ' && (out.empty() || out.back() == ' '))) out += c;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return cache[cls] = out.empty() ? "Enemy" : out;
}

std::vector<EspThing> g_world; // items / containers / save points with their (static) positions
uint64_t g_worldAt = 0;

void RefreshWorld(uint64_t now)
{
    if (now - g_worldAt < 1000) return;
    g_worldAt = now;
    g_world.clear();
    UClass* pickable = g_on[EspItems] ? FindClass("CorePickable") : nullptr;
    UClass* shelf = g_on[EspContainers] ? FindClass("ShelfLootContainer") : nullptr;
    UClass* chest = g_on[EspContainers] ? FindClass("BP_LootContainerChest_C") : nullptr;
    UClass* crate = g_on[EspContainers] ? FindClass("BPD_DestructableLoot_C") : nullptr;
    UClass* save = g_on[EspSavePoints] ? FindClass("BP_TASK_SavePoint_C") : nullptr;
    std::vector<std::pair<UObject*, EspThing>> found;
    int pickups = 0, inactive = 0, hiddenCount = 0, empty = 0; // logged below: why pickups are (not) shown
    ForEachObject([&](UObject* o) { // one pass for every kind
        if (IsDefaultObject(o)) return true;
        EspThing t;
        if (pickable && IsA(o, pickable)) {
            bool active = true, hidden = false;
            GetBool(o, "bItemIsActive", &active);
            GetBool(o, "bHidden", &hidden);
            UObject* item = Ptr(o, "Item");
            pickups++;
            inactive += !active;
            hiddenCount += hidden;
            empty += !item;
            // Not bItemIsActive: the user saw pickups lying in plain sight that the ESP skipped because of it.
            if (hidden || !item) return true;
            t.label = Name(item);
            int* stack = Member<int32_t>(o, "StackableCount");
            if (stack && *stack > 1) t.label += "  x" + std::to_string(*stack);
        } else if ((shelf && IsA(o, shelf)) || (chest && IsA(o, chest)) || (crate && IsA(o, crate))) {
            t.kind = EspKind::Container;
            t.label = IsA(o, chest) ? "Chest" : IsA(o, shelf) ? "Shelf" : "Crate";
        } else if (save && IsA(o, save)) {
            t.kind = EspKind::SavePoint;
            t.label = "Save point";
        } else {
            return true;
        }
        found.push_back({o, std::move(t)});
        return true;
    });
    for (auto& [o, t] : found) {
        t.pos = Location(o);
        g_world.push_back(std::move(t));
    }
    static uint64_t loggedAt;
    static std::string logged;
    char summary[160];
    snprintf(summary, sizeof(summary), "%d pickups (%d inactive, %d hidden, %d without item), %zu things kept", pickups,
             inactive, hiddenCount, empty, g_world.size());
    if (summary != logged && now - loggedAt > 10000) {
        loggedAt = now;
        logged = summary;
        km::Log("World ESP: %s", summary);
    }
}

// The player's camera this frame: PlayerCameraManager.CameraCachePrivate.POV.{Location, Rotation}.
bool ReadPov(UObject* pc, Vec& loc, Vec& rot)
{
    UObject* pcm = Ptr(pc, "PlayerCameraManager");
    Prop cache = pcm ? FindProp(ClassOf(pcm), "CameraCachePrivate") : Prop{};
    Prop pov = cache.inner ? FindProp(static_cast<const UClass*>(cache.inner), "POV") : Prop{};
    auto* view = static_cast<const UClass*>(pov.inner);
    Prop l = view ? FindProp(view, "Location") : Prop{}, r = view ? FindProp(view, "Rotation") : Prop{};
    if (!l || !r) return false;
    const uint8_t* base = reinterpret_cast<const uint8_t*>(pcm) + cache.offset + pov.offset;
    std::memcpy(&loc, base + l.offset, sizeof(Vec));
    std::memcpy(&rot, base + r.offset, sizeof(Vec)); // FRotator: pitch, yaw, roll as doubles
    return true;
}

// Unreal's rotation matrix axes: X forward, Y right, Z up.
void Axes(const Vec& rot, Vec& fwd, Vec& right, Vec& up)
{
    double sp = std::sin(rot.x * kDegToRad), cp = std::cos(rot.x * kDegToRad), sy = std::sin(rot.y * kDegToRad),
           cy = std::cos(rot.y * kDegToRad), sr = std::sin(rot.z * kDegToRad), cr = std::cos(rot.z * kDegToRad);
    fwd = {cp * cy, cp * sy, sp};
    right = {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp};
    up = {-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp};
}

// The camera and the engine's own projection, measured this frame. APlayerController::ProjectWorldLocationToScreen
// is what the picture uses (the FOV editor's locked FOV, the aspect-ratio rules, letterboxing, aiming zoom): a
// perspective projection is set by its centre and two focal lengths, so three points in front of the camera give
// it exactly, and every ESP point is then projected with it (no per-bone engine call). Rebuilding it from the camera's
// FOV went wrong: the picture uses the camera manager's FOV angle (locked by the FOV editor), not the cached one.
bool MeasureView(UObject* pc, EspFrame& f)
{
    Vec rot;
    if (!ReadPov(pc, f.camLoc, rot)) return false;
    Axes(rot, f.camFwd, f.camRight, f.camUp);
    struct Vec2 {
        double x, y;
    } o{}, r{}, u{};
    constexpr double kAhead = 1000, kSide = 0.25; // 10 m ahead, a quarter of that to the side / up
    auto screen = [&](double side, double lift, Vec2& out) {
        Vec w{f.camLoc.x + (f.camFwd.x + f.camRight.x * side + f.camUp.x * lift) * kAhead,
              f.camLoc.y + (f.camFwd.y + f.camRight.y * side + f.camUp.y * lift) * kAhead,
              f.camLoc.z + (f.camFwd.z + f.camRight.z * side + f.camUp.z * lift) * kAhead};
        Call c(pc, "ProjectWorldLocationToScreen");
        if (!c.Arg(0, w).Arg(2, false).Run() || !c.Out<bool>("ReturnValue")) return false;
        out = c.Out<Vec2>("ScreenLocation");
        return true;
    };
    Call size(pc, "GetViewportSize");
    if (!size.Run()) return false;
    f.width = size.Out<int32_t>("SizeX");
    f.height = size.Out<int32_t>("SizeY");
    if (f.width <= 0 || f.height <= 0) return false;
    if (screen(0, 0, o) && screen(kSide, 0, r) && screen(0, kSide, u)) {
        f.cx = o.x;
        f.cy = o.y;
        f.fx = (r.x - o.x) / kSide;
        f.fy = (o.y - u.y) / kSide;
        if (f.fx > 1 && f.fy > 1) return true;
    }
    // The engine call failed: the camera manager's FOV angle (what the picture uses) across the width.
    Call angle(Ptr(pc, "PlayerCameraManager"), "GetFOVAngle");
    float fov = angle.Run() ? angle.Out<float>("ReturnValue") : 0.f;
    if (fov < 5 || fov > 170) return false;
    if (!s.viewFallbackLogged) {
        s.viewFallbackLogged = true;
        km::Log("ESP projection: the engine's projection failed, using the FOV angle (%.1f)", fov);
    }
    f.cx = f.width / 2;
    f.cy = f.height / 2;
    f.fx = f.fy = f.cx / std::tan(fov * 0.5 * kDegToRad);
    return true;
}

void TickEsp(UObject* pc, UObject* player, uint64_t now)
{
    EspFrame f;
    f.camera = MeasureView(pc, f);
    if (f.camera && !s.viewLogged) {
        s.viewLogged = true;
        km::Log("ESP projection from the engine: %.0fx%.0f, centre %.1f,%.1f, focal %.1f,%.1f", f.width, f.height, f.cx,
                f.cy, f.fx, f.fy);
    }
    f.active = g_on[Esp] && f.camera;
    if (f.active) {
        Vec me = Location(player);
        double range = g_value[EspRange] * 100.0, worldRange = g_value[EspWorldRange] * 100.0;
        bool bones = g_on[EspSkeleton] || g_on[EspChams] || g_on[EspHead];
        RefreshEnemies(now);
        ForEachEnemy([&](UObject* e) {
            Attr h = GetAttr(EnemyAttributes(e), "Health"), m = GetAttr(EnemyAttributes(e), "MaxHealth");
            if (h && h.Get() <= 0) return; // dead
            Vec p = Location(e);
            double d = Distance(me, p);
            if (d > range) return;
            float* half = Member<float>(Ptr(e, "CapsuleComponent"), "CapsuleHalfHeight");
            float hh = half ? *half : 90.f;
            EspEnemy en;
            en.name = PrettyClass(e);
            en.feet = {p.x, p.y, p.z - hh};
            en.top = {p.x, p.y, p.z + hh};
            en.health = h.Get();
            en.maxHealth = m.Get();
            en.distance = float(d / 100.0);
            UObject* mesh = Ptr(e, "Mesh");
            if (bones && mesh && f.enemies.size() < 24) {
                Skeleton& sk = SkeletonOf(mesh);
                std::vector<Vec> pos(sk.bones.size());
                Call socket(mesh, "GetSocketLocation");
                for (size_t i = 0; i < sk.bones.size(); i++) pos[i] = socket.Arg(0, sk.bones[i].first).Run() ? socket.Out<Vec>("ReturnValue") : Vec{};
                for (size_t i = 0; i < sk.bones.size(); i++)
                    if (sk.bones[i].second >= 0) en.bones.push_back({pos[i], pos[size_t(sk.bones[i].second)]});
                if (sk.head >= 0) {
                    en.head = pos[size_t(sk.head)];
                    en.hasHead = true;
                }
            }
            f.enemies.push_back(std::move(en));
        });

        if (g_on[EspItems] || g_on[EspContainers] || g_on[EspSavePoints]) {
            RefreshWorld(now);
            for (const EspThing& t : g_world) {
                double d = Distance(me, t.pos);
                if (d > worldRange) continue;
                EspThing copy = t;
                copy.distance = float(d / 100.0);
                f.things.push_back(std::move(copy));
            }
        }
    }
    std::lock_guard lock(g_espMutex);
    g_esp = std::move(f);
}

// Once per engine frame: the ESP and the aim circle follow the picture.
void TickFrame()
{
    UObject* player = s.player.Get();
    if (player && (g_on[Esp] || (g_on[SilentAim] && g_on[EspAimFov]))) return TickEsp(s.pc.Get(), player, GetTickCount64());
    std::lock_guard lock(g_espMutex);
    g_esp = {};
}

// ---- Silent aim. The fire ability asks UPlayerAbility_FireBase where each bullet's trace ends; we let the game
// answer, then move the end behind the head of the enemy nearest the crosshair (within the aim FOV and in sight).

Vec HeadOf(UObject* enemy)
{
    if (UObject* mesh = Ptr(enemy, "Mesh")) {
        Skeleton& sk = SkeletonOf(mesh);
        Call socket(mesh, "GetSocketLocation");
        if (sk.head >= 0 && socket.Arg(0, sk.bones[size_t(sk.head)].first).Run()) return socket.Out<Vec>("ReturnValue");
    }
    Vec p = Location(enemy); // no head bone: upper part of the capsule
    float* half = Member<float>(Ptr(enemy, "CapsuleComponent"), "CapsuleHalfHeight");
    p.z += (half ? *half : 90.f) * 0.75;
    return p;
}

bool AimPoint(Vec& out)
{
    UObject* pc = s.pc.Get();
    Vec cam, rot, fwd, right, up;
    if (!pc || !ReadPov(pc, cam, rot)) return false;
    Axes(rot, fwd, right, up);
    double best = std::cos(g_value[AimFov] * kDegToRad); // cosine of the angle to the crosshair: higher is closer
    bool found = false;
    RefreshEnemies(GetTickCount64());
    ForEachEnemy([&](UObject* e) {
        Attr h = GetAttr(EnemyAttributes(e), "Health");
        if (h && h.Get() <= 0) return;
        Vec head = HeadOf(e), d{head.x - cam.x, head.y - cam.y, head.z - cam.z};
        double len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len < 1) return;
        double c = (d.x * fwd.x + d.y * fwd.y + d.z * fwd.z) / len;
        if (c <= best) return;
        Call sight(pc, "LineOfSightTo");
        if (!sight.Arg(0, e).Arg(1, Vec{}).Arg(2, false).Run() || !sight.Out<bool>("ReturnValue")) return;
        best = c;
        found = true;
        out = {head.x + d.x / len * 100, head.y + d.y / len * 100, head.z + d.z / len * 100}; // 1 m past the head
    });
    return found;
}

NativeThunk oTraceEnd, oAllTraceEnds;

void Redirect(void* result, bool many)
{
    std::function<void()> work = [&] {
        Vec p;
        if (!result || !g_on[SilentAim] || !AimPoint(p)) return;
        if (!many) {
            *static_cast<Vec*>(result) = p;
            return;
        }
        auto* ends = static_cast<TArray<Vec>*>(result);
        for (int i = 0; i < ends->num; i++) ends->data[i] = p;
    };
    static int faults;
    if (!Guard(work) && faults++ < 5) km::Log("Silent aim faulted (shot left as is)");
}

void hkTraceEnd(UObject* ctx, void* stack, void* result)
{
    oTraceEnd(ctx, stack, result);
    Redirect(result, false);
}

void hkAllTraceEnds(UObject* ctx, void* stack, void* result) // shotguns: one end per pellet
{
    oAllTraceEnds(ctx, stack, result);
    Redirect(result, true);
}

void TickPlayer(UObject* player, uint64_t now, Snapshot& snap)
{
    UObject* pc = s.pc.Get();
    UObject* attrs = s.attrs.Get();
    UObject* cmc = s.cmc.Get();
    UObject* settings = Ptr(player, "CharacterSettings");
    auto& why = snap.why;

    Attr health = GetAttr(attrs, "Health"), maxHealth = GetAttr(attrs, "MaxHealth");
    Attr suffering = GetAttr(attrs, "Suffering"), maxSuffering = GetAttr(attrs, "MaxSuffering");
    snap.health = health.Get();
    snap.maxHealth = maxHealth.Get();
    snap.suffering = suffering.Get();
    snap.maxSuffering = maxSuffering.Get();

    // Powers given so far (a tag container: its first member is the tag array), and the Genesis Configuration itself.
    auto* given = Member<TArray<uint64_t>>(player, "GivenGenesisConfigurationAbilities");
    bool hasBox = false;
    snap.powersWhy = !given || !GetBool(player, "bHasGenesisConfiguration", &hasBox) ? "Powers not found."
                     : !hasBox ? "Get the Genesis Configuration first: the story gives it."
                               : "";
    for (int p = 0; p < PowerCount; p++) {
        snap.powers[p] = false;
        for (int i = 0; given && i < given->num && i < 32; i++)
            snap.powers[p] = snap.powers[p] || _stricmp(NameToString(&given->data[i]).c_str(), kPowerTags[p]) == 0;
    }

    // Player
    if (!health || !maxHealth) why[GodMode] = why[InfiniteHealth] = "Health attribute not found.";
    if (Track(GodMode, [&] { SetBool(player, "bCanBeDamaged", true); })) SetBool(player, "bCanBeDamaged", false);
    if ((g_on[GodMode] || g_on[InfiniteHealth]) && health.Get() < maxHealth.Get()) health.Set(maxHealth.Get());

    if (!Member<float>(settings, "SprintCooldownAfterSprint")) why[NoSprintCooldown] = "Sprint settings not found.";
    bool sprint = g_on[NoSprintCooldown];
    Override(settings, "SprintCooldownAfterSprint", sprint, kInstant);
    Override(settings, "SprintCooldownAfterShooting", sprint, kInstant);

    if (!CheatAvailable()) why[Invisible] = "The developers' cheat manager isn't available.";
    // These cheats flip a state on each call: mirror it (reset when the cheat manager is recreated).
    // Their state is a tag on the pawn's ability system (Cheats.Character.Invisible / .infiniteAmmo), so a new pawn
    // (death, reload) starts without them; the same pawn coming back after a cutscene keeps them.
    if (s.cheatPawn.Get() != player) {
        s.cheatPawn = Weak(player);
        s.cheatState[Invisible] = s.cheatState[InfiniteAmmo] = false;
    }
    auto cheatToggle = [&](Toggle t, const char* fn) {
        if (g_on[t] != s.cheatState[t] && (g_on[t] || s.cheat.Get()) && CheatAvailable() && CheatCall(fn))
            s.cheatState[t] = g_on[t];
    };
    cheatToggle(Invisible, "K2_InvisiblePlayer");
    cheatToggle(InfiniteAmmo, "K2_ToggleUnlimitedAmmo");

    if (!FindClass("EnemyCharacter")) why[OneHitKill] = snap.valueWhy[DamageMult] = "Enemy class not found.";
    TickEnemyDamage(player, now);

    // Combat
    UObject* item = Ptr(player, "CurrentEquipmentItemInfo");
    if (g_on[InfiniteAmmo]) { // also keep the magazine full ourselves: covers the cheat being missing or removed
        int* ammo = item && IsA(item, FindClass("ItemInfoRangedWeapon")) ? Member<int>(item, "AmmoInClip") : nullptr;
        if (ammo) {
            int& top = s.maxAmmo[item];
            float* clip = Member<float>(item, "ClipSizeInfo");
            top = std::max({top, *ammo, clip ? int(*clip) : 0});
            *ammo = top;
        }
    }
    // Silent aim: the shot functions are hooked the first time it's turned on (shots pass through untouched when off).
    UClass* fire = FindClass("PlayerAbility_FireBase");
    UFunction* traceEnd = FindFunction(fire, "GetTraceEndPosition");
    if (g_on[SilentAim] && traceEnd && !s.aimHooked) {
        s.aimHooked = true;
        s.aimFailed = !HookNative(traceEnd, hkTraceEnd, &oTraceEnd);
        if (UFunction* all = FindFunction(fire, "GetAllTraceEndPositions")) HookNative(all, hkAllTraceEnds, &oAllTraceEnds);
    }
    if (!traceEnd || s.aimFailed) why[SilentAim] = "The game's shot function wasn't found.";

    // Auto lockpick: once a lockpick close-up has been open for a moment, let the game's own puzzle skip open it.
    // The player state points at the puzzle task only while a puzzle is open, so the class lookups stay rare.
    auto flag = [](UObject* o, const char* name) {
        bool v = false;
        return GetBool(o, name, &v) && v;
    };
    UObject* task = g_on[AutoLockpick] ? Ptr(Ptr(pc, "PlayerState"), "PuzzleRef") : nullptr;
    UObject* puzzle = task && IsA(task, FindClass("BP_TASK_FocusEnvPuzzle_C")) ? Ptr(task, "PuzzleActorRef") : nullptr;
    if (!puzzle || !IsA(puzzle, FindClass("BP_EnvPuzzleBaseActor_LockpickPuzzle_C")) || !flag(puzzle, "bPuzzleActive") ||
        flag(puzzle, "PuzzleFinished")) {
        s.lockAt = 0;
    } else if (!s.lockAt) {
        s.lockAt = now;
    } else if (s.lockAt != UINT64_MAX && now - s.lockAt > 600) {
        s.lockAt = UINT64_MAX; // once per close-up
        bool ok = Call(task, "SkipPuzzle").Run() || Call(puzzle, "PuzzleSkipped").Run();
        km::Log("Auto lockpick: %s", ok ? "lock opened" : "the puzzle can't be skipped");
    }

    if (!FindProp(ClassOf(player), "bIgnoreSufferingCost")) why[NoSufferingCost] = "Suffering settings not found.";
    if (Track(NoSufferingCost, [&] {
            SetBool(player, "bIgnoreSufferingCost", false);
            SetBool(player, "bInfiniteSuffering", false);
        })) {
        SetBool(player, "bIgnoreSufferingCost", true);
        SetBool(player, "bInfiniteSuffering", true);
        if (suffering.Get() < maxSuffering.Get()) suffering.Set(maxSuffering.Get());
    }
    bool cooldown = g_on[NoPowerCooldown];
    if (!Member<float>(settings, "TelekinesisReleaseSuccessCooldown")) why[NoPowerCooldown] = "Power settings not found.";
    for (const char* p : {"TelekinesisReleaseSuccessCooldown", "TelekinesisReleaseFailCooldown",
                          "PyrokinesisFlamePillarReignitionCooldown", "PyrokinesisLabyrinthFireReignitionCooldown"})
        Override(settings, p, cooldown, kInstant);
    if (g_on[Unbreakable] && item && IsA(item, FindClass("ItemInfoMeleeWeapon"))) {
        if (int* d = Member<int>(item, "Durability")) {
            int& top = s.maxDurability[item];
            top = std::max(top, *d);
            *d = top;
        }
    }

    // Movement
    if (!cmc) why[Fly] = why[Noclip] = why[FreeCam] = "Movement component not found.";
    if (!FindClass("CameraActor")) why[FreeCam] = "Camera class not found.";
    bool camWasOn = s.applied[FreeCam];
    if (Track(FreeCam, [&] { StopFreeCam(pc, player); })) {
        if (!camWasOn) StartFreeCam(pc, player, now);
        MoveFreeCam(pc, player, now);
    }
    bool fly = (g_on[Fly] || g_on[Noclip]) && !g_on[FreeCam]; // the free camera freezes the player
    static bool flying;
    if (cmc && fly) {
        float speed = 1000.f * g_value[SpeedMult];
        SetBool(cmc, "bCheatFlying", true);
        if (auto* mode = Member<uint8_t>(cmc, "MovementMode"); mode && *mode != MOVE_Flying)
            Call(cmc, "SetMovementMode").Arg(0, MOVE_Flying).Arg(1, uint8_t(0)).Run();
        if (auto* max = Member<float>(cmc, "MaxFlySpeed")) *max = speed;
        if (auto* v = Member<Vec>(cmc, "Velocity")) *v = FlyVelocity(pc, player, speed); // where you look
    } else if (cmc && flying) {
        SetBool(cmc, "bCheatFlying", false);
        Call(cmc, "SetMovementMode").Arg(0, MOVE_Falling).Arg(1, uint8_t(0)).Run();
    }
    flying = fly && cmc;
    if (Track(Noclip, [&] { Call(player, "SetActorEnableCollision").Arg(0, true).Run(); }))
        Call(player, "SetActorEnableCollision").Arg(0, false).Run();

    Attr speedAttr = GetAttr(attrs, "SpeedMultiplier");
    float speed = g_value[SpeedMult];
    if (!speedAttr) snap.valueWhy[SpeedMult] = "Speed attribute not found.";
    else {
        if (s.speedBase < 0) s.speedBase = *speedAttr.base;
        if (speed != 1 || s.lastValue[SpeedMult] != 1) speedAttr.SetCurrent(s.speedBase * speed);
    }
    float jump = g_value[JumpMult];
    if (float* z = Member<float>(cmc, "JumpZVelocity")) {
        if (s.jumpZ <= 0) s.jumpZ = *z;
        if (jump != 1 || s.lastValue[JumpMult] != 1) *z = s.jumpZ * jump;
    } else {
        snap.valueWhy[JumpMult] = "Jump settings not found.";
    }

    // World
    if (g_on[FreezeAI] || s.applied[FreezeAI]) RefreshEnemies(now);
    auto dilate = [](float value) {
        ForEachEnemy([value](UObject* e) {
            if (float* d = Member<float>(e, "CustomTimeDilation")) *d = value;
        });
    };
    if (Track(FreezeAI, [&] { dilate(1); })) dilate(0);

    float time = g_value[TimeScale];
    static uint64_t timeAt; // a level load resets time dilation: re-apply now and then
    if (time != s.lastValue[TimeScale] || (time != 1 && now - timeAt > 2000)) {
        timeAt = now;
        Call(Cdo("GameplayStatics"), "SetGlobalTimeDilation").Arg(0, pc).Arg(1, time).Run();
    }
    float fov = g_value[Fov];
    if (fov != s.lastValue[Fov]) Call(pc, "FOV").Arg(0, fov).Run();

    bool hudWasHidden = s.applied[HideHUD];
    if (Track(HideHUD, ShowWidgets) && !hudWasHidden) HideWidgets();

    for (int v = 0; v < ValueCount; v++) s.lastValue[v] = g_value[v];
}

void SlowRefresh(UObject* player, Snapshot& snap)
{
    UObject* pc = s.pc.Get();
    snap.money = ItemCount(FindItem("Item_Currency"));
    ReadItemAssets();
    for (int i = 0; i < resources::Count; ++i) {
        UObject* item = FindItem(resources::Names[i]);
        ResourceInventory inventory(player, item);
        int total = -1;
        snap.resourceTotals[i] = inventory.Read(i, total) ? total : -1;
    }
    Call shop(Cdo("ShopFunctions"), "GetShopItemAvailability");
    snap.shopLevel = shop.Arg(0, pc).Run() ? shop.Out<int32_t>("ReturnValue") : -1;
    snap.map = Name(OuterOf(OuterOf(player))); // player -> PersistentLevel -> World

    // Items, grouped by the class of their data asset.
    static const std::map<std::string, std::string> kCategory = {
        {"CurrencyItem", "Currency"},       {"AmmoItem", "Ammo"},
        {"WeaponRangedItem", "Firearms"},   {"WeaponMeleeItem", "Melee weapons"},
        {"ItemWeaponUpgrade", "Weapon upgrades"}, {"ItemEquipableToolMultiUserUsable", "Healing"},
        {"ItemEquippableTool", "Tools"},    {"ItemEquippable", "Equipment"},
    };
    std::map<std::string, std::string> classOf; // item -> class name: every item from the registry, plus loaded ones
    ReadItemAssets();
    for (const ItemAsset& a : g_itemAssets) classOf.emplace(a.name, a.cls);
    ForEachInstance(FindClass("Item"), [&](UObject* o) {
        std::string item = Name(o);
        if (std::find(g_itemsLeftOut.begin(), g_itemsLeftOut.end(), item) == g_itemsLeftOut.end())
            classOf.emplace(item, Name(reinterpret_cast<UObject*>(ClassOf(o))));
        return true;
    });
    std::map<std::string, std::vector<Named>> byCat;
    for (const auto& [item, cls] : classOf) {
        auto it = kCategory.find(cls);
        byCat[it == kCategory.end() ? "Misc" : it->second].push_back({item, 0});
    }
    snap.items.clear();
    for (auto& [cat, list] : byCat) {
        std::sort(list.begin(), list.end(), [](const Named& a, const Named& b) { return a.name < b.name; });
        snap.items.emplace_back(cat, std::move(list));
    }

    // Objectives (gameplay tags) from the objective subsystem.
    if (!s.objectives.Get()) ForEachInstance(FindClass("ObjectiveSystem"), [](UObject* o) {
            s.objectives = Weak(o);
            return false;
        });
    snap.objectivesActive.clear();
    snap.objectivesDone.clear();
    snap.tracked.clear();
    if (UObject* sys = s.objectives.Get()) {
        auto tags = [&](const char* prop) {
            std::vector<uint64_t> out;
            Prop p = FindProp(ClassOf(sys), prop);
            Prop arr = p.inner ? FindProp(static_cast<const UClass*>(p.inner), "GameplayTags") : Prop{};
            if (!arr) return out;
            auto* list = reinterpret_cast<TArray<uint64_t>*>(reinterpret_cast<uint8_t*>(sys) + p.offset + arr.offset);
            out.assign(list->data, list->data + list->num);
            return out;
        };
        auto done = tags("CompletedObjectives");
        for (uint64_t t : tags("StartedObjectives"))
            if (std::find(done.begin(), done.end(), t) == done.end()) snap.objectivesActive.push_back({NameToString(&t), t});
        for (uint64_t t : done) snap.objectivesDone.push_back({NameToString(&t), t});
        if (auto* tracked = Member<uint64_t>(sys, "TrackedObjective")) snap.tracked = NameToString(tracked);
    }

    // Level checkpoints, nearest first.
    Vec me = Location(player);
    std::vector<std::pair<double, Named>> cps;
    ForEachInstance(FindClass("LevelCheckpoint"), [&](UObject* o) {
        double d = Distance(me, Location(o)) / 100.0;
        char label[96];
        snprintf(label, sizeof(label), "%s  ·  %.0f m", Name(o).c_str(), d);
        cps.push_back({d, {label, uint64_t(Weak(o).index)}});
        return true;
    });
    std::sort(cps.begin(), cps.end(), [](auto& a, auto& b) { return a.first < b.first; });
    snap.checkpoints.clear();
    for (auto& c : cps) snap.checkpoints.push_back(c.second);
}

// Startup logos and notices: as soon as the splash widget exists, mark each screen skipped and shrink its durations
// so it ends at once through its own flow. The interactive screens (EULA, brightness) are left alone.
void TickIntros(uint64_t now)
{
    if (s.introsDone || !g_on[SkipIntros] || now - s.splashAt < 100) return;
    if (!s.splashAt) s.splashSince = now;
    s.splashAt = now;
    if (now - s.splashSince > 60000) s.introsDone = true; // missed them (menu loaded late): stop scanning
    UObject* splash = nullptr; // the live widget, not a template: the one whose screens exist
    ForEachInstance(FindClass("W_SplashScreens_C"), [&](UObject* o) {
        if (Ptr(o, "W_SplashScreen_Saber")) splash = o;
        return !splash;
    });
    int changed = 0;
    for (const char* name : {"W_SplashScreen_Warning", "W_SplashScreen_UE", "W_SplashScreen_Saber", "W_SplashScreen_BTG",
                             "W_SplashScreen_Photosensitivty", "W_SplashScreen_JoyCon", "W_SplashScreen_AutoSave"}) {
        UObject* screen = Ptr(splash, name);
        if (!screen) continue;
        changed++;
        SetBool(screen, "bSkip", true);
        for (const char* d : {"StayDuration", "FadeDuration"})
            if (double* v = Member<double>(screen, d)) *v = 0.01;
    }
    if (!changed) return; // not created yet (or its screens aren't built yet)
    s.introsDone = true;
    km::Log("Intros: %d splash screens skipped", changed);
}

void Tick()
{
    ach::Tick();
    uint64_t now = GetTickCount64();
    missions::Tick(now);
    Snapshot snap;
    {
        std::lock_guard lock(g_snapMutex);
        snap = g_snap; // keep the slow-refresh lists between refreshes
    }
    snap.reflection = Ready();
    snap.why.fill("");
    snap.valueWhy.fill("");

    if (!s.pc.Get() && now - s.pcScanAt > 1000) {
        s.pcScanAt = now;
        UObject* pc = nullptr;
        ForEachInstance(FindClass("CoreController"), [&](UObject* o) {
            pc = o;
            return false;
        });
        s.pc = Weak(pc);
        s.cheat = {};
        s.cheatTried = s.cheatClass = false;
        std::fill(std::begin(s.cheatState), std::end(s.cheatState), false);
    }
    UObject* pawn = Ptr(s.pc.Get(), "Pawn");
    UObject* player = pawn && IsA(pawn, FindClass("PlayerCharacter")) ? pawn : nullptr;
    if (player != s.player.Get()) { // new level or first load: per-pawn state starts over
        snap.resourceTotals.fill(-1);
        s.player = Weak(player);
        s.attrs = Weak(AttributeSet(player, "PlayerAttributeSet"));
        s.cmc = Weak(Ptr(player, "CharacterMovement"));
        s.jumpZ = 0;
        s.speedBase = -1;
        s.maxAmmo.clear();
        s.maxDurability.clear();
        s.hiddenWidgets.clear();
        s.camera = {};
        s.photoCamera = s.photoController = s.photoPlayer = s.photoWorld = {};
        std::fill(std::begin(s.applied), std::end(s.applied), false);
        for (int v = 0; v < ValueCount; v++) s.lastValue[v] = kValues[v].def; // re-apply values to the new pawn
        if (player) km::Log("Player found: %s", Name(player).c_str());
    }

    if (!s.dropWatch.empty() && now - s.dropAt >= 100) {
        s.dropAt = now;
        for (DropWatch& w : s.dropWatch) TrimDrops(w);
        std::erase_if(s.dropWatch, [&](const DropWatch& w) {
            if (now < w.until) return false;
            km::Log("Give %s: %zu left on the ground, %d extra copies removed", w.name.c_str(), w.kept.size(), w.removed);
            return true;
        });
    }

    snap.player = player != nullptr;
    if (player) s.introsDone = true; // the intros only play before the first save is loaded
    TickIntros(now);
    if (player) {
        TickPlayer(player, now, snap);
        if (now - s.slowAt > 1500) {
            s.slowAt = now;
            SlowRefresh(player, snap);
        }
    } else {
        snap.why.fill("Load a save first.");
        snap.why[SkipIntros].clear(); // works from the title screen
        snap.valueWhy.fill("Load a save first.");
        snap.powersWhy = "Load a save first.";
        snap.powers = {};
        snap.money = snap.shopLevel = -1;
        snap.resourceTotals.fill(-1);
        std::lock_guard lock(g_espMutex);
        g_esp = {};
    }
    if (!snap.reflection) snap.why.fill("Game engine not hooked: see BananMenu.log.");
    snap.cheatManager = CheatAvailable();
    for (int i = 0; i < 3; i++) snap.slots[i] = s.slotMap[i];
    const PhotoCamera photoCamera;
    snap.photoWhy = photoCamera.Why();
    snap.photoReady = snap.reflection && snap.photoWhy.empty();
    snap.photoMessage = s.photoMessage;
    const SpawnerContext spawner{GenerationRef(s.pc.Get()), GenerationRef(s.player.Get()),
        GenerationRef(player ? OuterOf(OuterOf(player)) : nullptr)};
    snap.enemySpawnerWhy = spawner.Why();
    if (snap.enemySpawnerWhy.empty() && !SpawnerAPIs()) snap.enemySpawnerWhy = "Native enemy spawner API unavailable.";
    snap.enemySpawnerBusy = g_spawnerBusy.load();
    snap.enemySpawnerReady = snap.enemySpawnerWhy.empty() && !snap.enemySpawnerBusy;
    snap.enemySpawnerMessage = s.enemySpawnerMessage;
    snap.enemySpawnerCreated = s.enemySpawnerCreated;
    snap.enemySpawnerRequested = s.enemySpawnerRequested;
    const std::string photoMap = snap.photoReady ? photoCamera.Map() : "";
    for (int i = 0; i < 3; ++i) {
        const auto& mark = s.cameraBookmarks.Slots()[i];
        snap.cameraBookmarks[i] = mark.map;
        snap.cameraBookmarkRestorable[i] = snap.photoReady && !mark.map.empty() && mark.map == photoMap;
    }

    std::lock_guard lock(g_snapMutex);
    g_snap = std::move(snap);
    g_spawnerContext = spawner;
}

template <class F> void OnGame(F&& f)
{
    RunOnGameThread([f = std::forward<F>(f)] {
        if (UObject* player = s.player.Get()) f(player);
    });
}

} // namespace

EspFrame GetEspFrame()
{
    std::lock_guard lock(g_espMutex);
    return g_esp;
}

void SaveCameraBookmark(int slot)
{
    RunOnGameThread([slot] {
        PhotoCamera camera;
        s.photoMessage = PhotoMessage(s.cameraBookmarks.Save(slot, camera));
    });
}

void RestoreCameraBookmark(int slot)
{
    RunOnGameThread([slot] {
        PhotoCamera camera;
        s.photoMessage = PhotoMessage(s.cameraBookmarks.Restore(slot, camera));
    });
}

void DeleteCameraBookmark(int slot)
{
    RunOnGameThread([slot] { s.photoMessage = PhotoMessage(s.cameraBookmarks.Delete(slot)); });
}

void SpawnEnemies(int type, int count)
{
    if (!spawn::ValidRequest(type, count)) { menu::Toast(lang::T("Invalid enemy type or count.")); return; }
    if (g_spawnerBusy.exchange(true)) return;
    SpawnerContext context;
    {
        std::lock_guard lock(g_snapMutex);
        context = g_spawnerContext;
    }
    RunOnGameThread([context, type, count] {
        struct BusyReset { ~BusyReset() { g_spawnerBusy = false; } } reset;
        int created = 0;
        const char* message = "Enemy creation failed or was blocked.";
        std::function<void()> work = [&] {
            if (*context.Why()) { message = "Level changed; spawning stopped."; return; }
            if (!SpawnerAPIs()) { message = "Native enemy spawner API unavailable."; return; }
            // Opaque returned structs remain alive through the dependent synchronous calls.
            const std::string path = spawn::Types[type].path;
            std::wstring wide(path.begin(), path.end());
            FString input{wide.data(), int32_t(wide.size() + 1), int32_t(wide.size() + 1)};
            auto makePath = SpawnerCall(SpawnerLibrary("KismetSystemLibrary"), "MakeSoftClassPath", kMakeClassPath);
            makePath.Arg("PathString", input);
            if (!SpawnerRun(makePath)) { message = "Enemy class load failed."; return; }
            if (*context.Why()) { message = "Level changed; spawning stopped."; return; }
            const auto classPath = makePath.Out<std::array<uint8_t, 32>>("ReturnValue");
            auto makeSoft = SpawnerCall(SpawnerLibrary("KismetSystemLibrary"), "Conv_SoftClassPathToSoftClassRef", kMakeSoftClass);
            makeSoft.ArgRaw(0, classPath.data(), classPath.size());
            if (!SpawnerRun(makeSoft)) { message = "Enemy class load failed."; return; }
            if (*context.Why()) { message = "Level changed; spawning stopped."; return; }
            const auto softClass = makeSoft.Out<std::array<uint8_t, 40>>("ReturnValue");
            auto load = SpawnerCall(SpawnerLibrary("KismetSystemLibrary"), "LoadClassAsset_Blocking", kLoadEnemyClass);
            load.ArgRaw(0, softClass.data(), softClass.size());
            if (!SpawnerRun(load)) { message = "Enemy class load failed."; return; }
            if (*context.Why()) { message = "Level changed; spawning stopped."; return; }
            UClass* selected = SelectedEnemyClass(type);
            if (!selected || load.Out<UObject*>("ReturnValue") != reinterpret_cast<UObject*>(selected)) {
                message = "Enemy class load failed."; return;
            }
            auto position = SpawnerCall(context.player.Get(), "K2_GetActorLocation", {{"ReturnValue", Kind::Struct, 24, "Vector", true}});
            if (!SpawnerRun(position)) { message = "Player pose unavailable; nothing spawned."; return; }
            if (*context.Why()) { message = "Level changed; spawning stopped."; return; }
            auto forward = SpawnerCall(context.player.Get(), "GetActorForwardVector", {{"ReturnValue", Kind::Struct, 24, "Vector", true}});
            if (!SpawnerRun(forward)) { message = "Player pose unavailable; nothing spawned."; return; }
            if (*context.Why()) { message = "Level changed; spawning stopped."; return; }
            auto rotation = SpawnerCall(context.player.Get(), "K2_GetActorRotation", {{"ReturnValue", Kind::Struct, 24, "Rotator", true}});
            if (!SpawnerRun(rotation)) { message = "Player pose unavailable; nothing spawned."; return; }
            if (*context.Why()) { message = "Level changed; spawning stopped."; return; }
            std::array<spawn::Point, spawn::MaxCount> positions{};
            const Rot facing = rotation.Out<Rot>("ReturnValue");
            if (!spawn::Finite({facing.pitch, facing.yaw, facing.roll}) ||
                !spawn::Positions(position.Out<spawn::Point>("ReturnValue"), forward.Out<spawn::Point>("ReturnValue"), count, positions)) {
                message = "Player pose unavailable; nothing spawned."; return;
            }
            for (int i = 0; i < count; ++i) {
                if (*context.Why()) { message = "Level changed; spawning stopped."; return; }
                auto transform = SpawnerCall(SpawnerLibrary("KismetMathLibrary"), "MakeTransform", kMakeSpawnTransform);
                transform.Arg("Location", positions[i]).Arg("Rotation", Rot{0, facing.yaw, 0}).Arg("Scale", Vec{1, 1, 1});
                if (!SpawnerRun(transform)) return;
                if (*context.Why()) { message = "Level changed; spawning stopped."; return; }
                selected = SelectedEnemyClass(type);
                if (!selected) { message = "Enemy class load failed."; return; }
                const auto pose = transform.Out<std::array<uint8_t, 96>>("ReturnValue");
                auto begin = SpawnerCall(SpawnerLibrary("GameplayStatics"), "BeginDeferredActorSpawnFromClass", kBeginEnemy);
                begin.Arg("WorldContextObject", context.player.Get()).Arg("ActorClass", selected).Arg("SpawnTransform", pose)
                    .Arg("CollisionHandlingOverride", uint8_t(3)).Arg("Owner", static_cast<UObject*>(nullptr))
                    .Arg("TransformScaleMethod", uint8_t(0));
                const bool began = SpawnerRun(begin);
                const FreshWorldActor actor(context.world, begin.Out<UObject*>("ReturnValue"), selected);
                struct DeferredCleanup {
                    const FreshWorldActor& actor;
                    bool completed = false;
                    ~DeferredCleanup() {
                        if (completed) return;
                        std::function<void()> cleanup = [&] {
                            if (UObject* live = actor.Get()) {
                                auto destroy = SpawnerCall(live, "K2_DestroyActor", {});
                                if (!SpawnerRun(destroy)) km::Log("Enemy spawner: deferred actor cleanup failed");
                            }
                        };
                        if (!Guard(cleanup)) km::Log("Enemy spawner: deferred actor cleanup faulted");
                    }
                } cleanup{actor};
                if (!began) return;
                if (*context.Why()) { message = "Level changed; spawning stopped."; return; }
                if (!actor.Get()) return;
                auto finish = SpawnerCall(SpawnerLibrary("GameplayStatics"), "FinishSpawningActor", kFinishEnemy);
                finish.Arg("Actor", actor.Get()).Arg("SpawnTransform", pose).Arg("TransformScaleMethod", uint8_t(0));
                if (!SpawnerRun(finish)) return;
                if (*context.Why()) { message = "Level changed; spawning stopped."; return; }
                UObject* completed = actor.Get();
                if (!completed || finish.Out<UObject*>("ReturnValue") != completed || !IsA(completed, FindClass("EnemyCharacter"))) {
                    return;
                }
                ++created;
                cleanup.completed = true;
                const Prop controller = FindProp(ClassOf(completed), "Controller", true);
                UObject* ai = nullptr;
                if (controller.Is(Kind::Object, sizeof(UObject*))) SafeRead(reinterpret_cast<uint8_t*>(completed) + controller.offset, &ai, sizeof(ai));
                km::Log("Enemy spawner: completed %s; Controller %s", Name(reinterpret_cast<UObject*>(ClassOf(completed))).c_str(), ai ? "present" : "absent");
            }
            message = "Actors created; check enemy behavior in game.";
        };
        if (!Guard(work)) message = "Enemy creation failed or was blocked.";
        s.enemySpawnerCreated = created;
        s.enemySpawnerRequested = count;
        s.enemySpawnerMessage = message;
        char status[256];
        std::snprintf(status, sizeof(status), lang::T("Created %d of %d enemies. %s"), created, count, lang::T(message));
        menu::Toast(status);
        km::Log("Enemy spawner: created %d of %d; %s", created, count, message);
    });
}

Snapshot GetSnapshot()
{
    Snapshot snap;
    {
        std::lock_guard lock(g_snapMutex);
        snap = g_snap;
    }
    snap.enemySpawnerBusy = g_spawnerBusy.load();
    snap.enemySpawnerReady = snap.enemySpawnerReady && !snap.enemySpawnerBusy;
    if (!Ready() && GetEnvironmentVariableA("KM_PREVIEW", nullptr, 0)) { // test host: sample values for screenshots
        snap.reflection = snap.player = snap.cheatManager = true;
        snap.health = 74, snap.maxHealth = 100, snap.suffering = 45, snap.maxSuffering = 100;
        snap.money = 1040, snap.shopLevel = 2;
        snap.resourceTotals = {24, 18, 12, 6, 8, 3};
        snap.map = snap.slots[0] = "MAP_EP_5";
        snap.powers = {true, false, false};
        snap.powersWhy.clear();
        return snap;
    }
    // The tick never runs without the engine hooks: grey everything out here.
    if (!Ready()) {
        snap.reflection = false;
        snap.photoReady = false;
        snap.enemySpawnerReady = false;
        snap.enemySpawnerWhy = "Game engine not hooked: see BananMenu.log.";
        snap.cameraBookmarkRestorable.fill(false);
        snap.why.fill("Game engine not hooked: see BananMenu.log.");
        snap.valueWhy.fill("Game engine not hooked: see BananMenu.log.");
        snap.powersWhy = "Game engine not hooked: see BananMenu.log.";
        snap.resourceTotals.fill(-1);
    }
    return snap;
}

void Init()
{
    for (int t = 0; t < ToggleCount; t++) g_on[t] = cfg::GetInt("Toggles", kToggles[t].key, kToggles[t].def) != 0;
    for (int v = 0; v < ValueCount; v++) {
        g_value[v] = std::clamp(cfg::GetFloat("Values", kValues[v].key, kValues[v].def), kValues[v].min, kValues[v].max);
        if (v == Fov && !fov::ValidSetting(g_value[v])) g_value[v] = 0;
        s.lastValue[v] = kValues[v].def;
    }
    for (int i = 0; i < 3; i++) {
        std::string slot = cfg::GetString("Teleport", ("Slot" + std::to_string(i + 1)).c_str());
        char map[128] = {};
        Vec p{};
        if (sscanf_s(slot.c_str(), "%127[^,],%lf,%lf,%lf", map, unsigned(sizeof(map)), &p.x, &p.y, &p.z) == 4) {
            s.slotMap[i] = map;
            s.slotPos[i] = p;
        }
    }
    SetTick(Tick);
    SetFrameTick(TickFrame);
}

void Save()
{
    for (int t = 0; t < ToggleCount; t++) cfg::SetInt("Toggles", kToggles[t].key, g_on[t] ? 1 : 0);
    for (int v = 0; v < ValueCount; v++) cfg::SetFloat("Values", kValues[v].key, g_value[v]);
}

const presets::Schema& ProfileSchema()
{
    static const auto schema = [] {
        presets::Schema result;
        for (const auto& toggle : kToggles) result.toggles.emplace_back(toggle.key);
        for (int v = 0; v < ValueCount; ++v)
            result.values.push_back({kValues[v].key, v == Fov ? fov::MinAngle : kValues[v].min, kValues[v].max, v == Fov});
        return result;
    }();
    return schema;
}

presets::Profile CaptureProfile(std::string name)
{
    presets::Profile profile{std::move(name), {}};
    for (int t = 0; t < ToggleCount; ++t) profile.settings.toggles.emplace(kToggles[t].key, g_on[t].load());
    for (int v = 0; v < ValueCount; ++v) profile.settings.values.emplace(kValues[v].key, g_value[v].load());
    return profile;
}

void ApplyProfile(presets::Profile profile)
{
    RunOnGameThread([profile = std::move(profile)] {
        const auto result = presets::ApplyProfile(profile, ProfileSchema(),
            [](const std::string& key, bool on) {
                for (int t = 0; t < ToggleCount; ++t) if (key == kToggles[t].key) { g_on[t] = on; break; }
            },
            [](const std::string& key, float value) {
                for (int v = 0; v < ValueCount; ++v) if (key == kValues[v].key) { g_value[v] = value; break; }
            });
        if (result == presets::Error::Ok) Save();
        menu::Toast(lang::T(result == presets::Error::Ok ? "Profile applied" : "Invalid settings profile"));
    });
}

void GiveKit(presets::Kit kit)
{
    RunOnGameThread([kit = std::move(kit)] {
        if (presets::ValidateKit(kit) != presets::Error::Ok) {
            menu::Toast(lang::T("Invalid item kit")); return;
        }
        if (!s.player.Get()) { menu::Toast(lang::T("Load a save first.")); return; }
        // Rebuild exact availability on this thread; the render snapshot is never an authority for delivery.
        ReadItemAssets();
        std::set<std::string> available;
        for (const auto& asset : g_itemAssets) available.insert(asset.name);
        ForEachInstance(FindClass("Item"), [&](UObject* object) {
            auto id = Name(object);
            if (std::find(g_itemsLeftOut.begin(), g_itemsLeftOut.end(), id) == g_itemsLeftOut.end()) available.insert(std::move(id));
            return true;
        });
        auto result = presets::SendKit(kit,
            [&](const std::string& id) { return available.contains(id); },
            [](const std::string& id, int quantity) { GiveItem(id, quantity); });
        menu::Toast(lang::T(result == presets::Error::Ok ? "Item requests sent" : "Item unavailable; no requests sent"));
    });
}

void SetHealth(float value)
{
    OnGame([value](UObject*) {
        Attr h = GetAttr(s.attrs.Get(), "Health"), m = GetAttr(s.attrs.Get(), "MaxHealth");
        if (h && m) h.Set(std::clamp(value, 1.f, m.Get()));
    });
}

void Heal()
{
    OnGame([](UObject*) {
        Attr h = GetAttr(s.attrs.Get(), "Health"), m = GetAttr(s.attrs.Get(), "MaxHealth");
        if (h && m) h.Set(m.Get());
    });
}

void FillSuffering()
{
    OnGame([](UObject*) {
        Attr h = GetAttr(s.attrs.Get(), "Suffering"), m = GetAttr(s.attrs.Get(), "MaxSuffering");
        if (h && m) h.Set(m.Get());
    });
}

void GiveItem(const std::string& name, int count)
{
    OnGame([name, count](UObject* player) {
        UObject* item = FindItem(name);
        if (!item || count <= 0) return;
        int before = ItemCount(item, true);
        Call cheat(CheatManager(), "K2_GiveItem");
        bool cheatRan = cheat && cheat.Arg(0, item).Arg(1, count).Run();
        if (cheatRan) {
            int after = ItemCount(item, true);
            km::Log("Give %s x%d via cheat manager: %d -> %d", name.c_str(), count, before, after);
            if (after > before) {
                menu::Toast("+" + std::to_string(after - before) + "  " + Pretty(name));
                return;
            }
            // Nothing added: no room (the developers' give then does nothing at all). Drop the items below.
        }
        // One item info per item (one for a stack). Without the cheat: the game's AddItemInInventory. When there's no
        // room: the game's DropItem, or AddItemInInventory if that left nothing on the ground (it drops what doesn't
        // fit, but some firearms twice). Every item that didn't go into the bag counts as one pickup to leave on the
        // ground; any copy beyond that is removed (see DropWatch).
        UObject* im = ItemManager();
        UClass* stackable = FindClass("ItemInfoStackable");
        std::vector<UObject*> lying = Pickables(item);
        DropWatch* watch = nullptr; // made on the first item that doesn't fit
        int added = 0, dropped = 0;
        bool created = false;
        for (int i = 0, left = count; left > 0 && i < 50; i++) {
            Call create(im, "CreateAndInitializeItem");
            UObject* info = create.Arg(0, item).Arg(1, player).Run() ? create.Out<UObject*>("ItemInfoRef") : nullptr;
            if (!info) break;
            created = true;
            int n = 1;
            if (IsA(info, stackable)) {
                n = left;
                *Member<int32_t>(info, "StackableCount") = n;
            }
            left -= n;
            int inBag = ItemCount(item, true);
            size_t onGround = Pickables(item).size();
            if (cheatRan) Call(im, "DropItem").Arg(0, info).Arg(1, player).Arg(2, true).Run();
            if (!cheatRan || Pickables(item).size() == onGround)
                Call(im, "AddItemInInventory").Arg(0, info).Arg(1, player).Arg(2, false).Arg(3, false).Arg(4, false).Run();
            if (ItemCount(item, true) > inBag) {
                added += n;
                continue;
            }
            if (!watch) watch = &WatchDrops(item, name, lying, Location(player));
            watch->keep++;
            dropped += n;
        }
        if (watch) TrimDrops(*watch);
        km::Log("Give %s x%d via item manager: %d added, %d dropped (%d pickups kept, %d extra copies removed so far)",
                name.c_str(), count, added, dropped, watch ? int(watch->kept.size()) : 0, watch ? watch->removed : 0);
        if (added) menu::Toast("+" + std::to_string(added) + "  " + Pretty(name));
        if (dropped) menu::Toast(lang::T("Inventory full: dropped at your feet"));
        if (!created) {
            char failed[160];
            snprintf(failed, sizeof(failed), lang::T("Couldn't give %s"), Pretty(name).c_str());
            menu::Toast(failed);
        }
    });
}

void AddMoney(int delta)
{
    if (delta > 0) return GiveItem("Item_Currency", delta);
    OnGame([delta](UObject* player) {
        UObject* currency = FindItem("Item_Currency");
        UObject* im = ItemManager();
        int left = -delta;
        for (int i = 0; left > 0 && i < 50; i++) { // money may sit in several stacks
            Call get(im, "GetItemFromInventory");
            if (!get.Arg(0, player).Arg(1, currency).Arg(2, false).Run()) break;
            UObject* info = get.Out<UObject*>("ReturnValue");
            int* stack = Member<int32_t>(info, "StackableCount");
            if (!stack || *stack <= 0) break;
            int n = std::min(left, *stack);
            Call(im, "RemoveItem").Arg(0, info).Arg(1, n).Arg(2, false).Run();
            left -= n;
        }
        km::Log("Remove money %d: %d left to remove", -delta, left);
    });
}

void SetResourceTotal(int materialIndex, int total)
{
    RunOnGameThread([materialIndex, total] {
        // Validate again at execution, before indexing names or resolving assets.
        if (materialIndex < 0 || materialIndex >= resources::Count || total < 0 || total > 9999) {
            menu::Toast(lang::T("Invalid material or target total"));
            return;
        }
        UObject* player = s.player.Get();
        if (!player || Ptr(s.pc.Get(), "Pawn") != player) {
            menu::Toast(lang::T("Load a save first."));
            return;
        }
        resources::Result result{resources::Status::ReadFailed};
        std::function<void()> reconcile = [&] {
            ReadItemAssets();
            UObject* item = FindItem(resources::Names[materialIndex]);
            ResourceInventory inventory(player, item);
            result = resources::Reconcile(inventory, materialIndex, total);
        };
        if (!Guard(reconcile)) result = {resources::Status::MutationFailed};
        {
            std::lock_guard lock(g_snapMutex);
            g_snap.resourceTotals[materialIndex] = result.observed;
        }
        char message[512];
        snprintf(message, sizeof(message), lang::T("%s: %s (current %d, target %d)"),
                 lang::T(resources::Labels[materialIndex]), lang::T(ResourceResultText(result.status)), result.observed, total);
        menu::Toast(message);
        km::Log("Set resource %s to %d: status %d, observed %d", resources::Names[materialIndex], total,
                int(result.status), result.observed);
    });
}

void GiveAllSkins()
{
    OnGame([](UObject*) {
        menu::Toast(lang::T(CheatCall("K2_AddAllSkinsAndCharms") ? "Skins & charms unlocked" : "Skins & charms: cheat unavailable"));
    });
}

void SetShopLevel(int level)
{
    OnGame([level](UObject*) {
        Call(Cdo("ShopFunctions"), "SetShopItemAvailability").Arg(0, s.pc.Get()).Arg(1, level).Arg(2, false).Run();
    });
}

void KillNearby()
{
    OnGame([](UObject* player) {
        s.enemiesAt = 0;
        RefreshEnemies(GetTickCount64());
        Vec me = Location(player);
        double radius = g_value[KillRadius] * 100.0;
        int killed = 0;
        ForEachEnemy([&](UObject* e) {
            if (Distance(me, Location(e)) > radius) return;
            Call c(e, "KillEnemyInstantly");
            if (c && c.Arg(0, false).Arg(1, player).Arg(2, false).Run()) killed++;
        });
        km::Log("Kill nearby: %d enemies within %.0f m", killed, radius / 100);
        char text[96];
        snprintf(text, sizeof(text), lang::T(killed == 1 ? "%d enemy killed" : "%d enemies killed"), killed);
        menu::Toast(text);
    });
}

void SaveSlot(int slot)
{
    OnGame([slot](UObject* player) {
        s.slotPos[slot] = Location(player);
        s.slotMap[slot] = Name(OuterOf(OuterOf(player)));
        char buf[192];
        snprintf(buf, sizeof(buf), "%s,%.1f,%.1f,%.1f", s.slotMap[slot].c_str(), s.slotPos[slot].x, s.slotPos[slot].y,
                 s.slotPos[slot].z);
        cfg::SetString("Teleport", ("Slot" + std::to_string(slot + 1)).c_str(), buf);
    });
}

void LoadSlot(int slot)
{
    OnGame([slot](UObject* player) {
        if (s.slotMap[slot] == Name(OuterOf(OuterOf(player)))) Teleport(player, s.slotPos[slot]);
    });
}

void TeleportCheckpoint(uint64_t index)
{
    OnGame([index](UObject* player) {
        UObject* cp = ObjectAt(int32_t(index));
        if (cp && IsA(cp, FindClass("LevelCheckpoint"))) Teleport(player, Location(cp));
    });
}

void CompleteObjective(uint64_t fname)
{
    OnGame([fname](UObject*) {
        Call c(Cdo("ObjectiveFunctions"), "CompleteObjective");
        bool ok = c.Arg(0, s.pc.Get()).Arg(1, fname).Arg(2, true).Arg(3, 3.f).Run() && c.Out<bool>("ReturnValue");
        km::Log("Complete objective %s: %s", NameToString(&fname).c_str(), ok ? "OK" : "refused by the game");
    });
}

void UnlockPower(Power power)
{
    OnGame([power](UObject* player) {
        uint64_t tag = MakeName(kPowerTags[power]); // a gameplay tag is just its FName
        bool ok = tag && Call(player, "AddGenesisConfigurationAbility").Arg(0, tag).Run();
        km::Log("Unlock %s (%s): %s", kPowerNames[power], tag ? NameToString(&tag).c_str() : "no tag", ok ? "called" : "failed");
    });
}

} // namespace feat
