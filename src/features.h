#pragma once
// Game features. All game access happens on the game thread (Tick and queued actions);
// the menu reads a Snapshot / EspFrame and writes the atomics below.
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "resources_core.h"
#include "presets_core.h"

namespace feat {

enum Toggle {
    GodMode, InfiniteHealth, NoSprintCooldown, OneHitKill, Invisible,
    InfiniteAmmo, NoSufferingCost, NoPowerCooldown, Unbreakable, SilentAim,
    Fly, Noclip,
    FreezeAI, HideHUD, FreeCam, AutoLockpick, SkipIntros,
    Esp, EspBox, EspHealth, EspName, EspDistance, EspSkeleton, EspHead, EspChams, EspSnaplines, EspOffscreen,
    EspItems, EspContainers, EspSavePoints, EspAimFov,
    ToggleCount
};

struct ToggleInfo {
    const char* key;   // ini key
    const char* label;
    const char* help;
    bool def = false;  // default state (ESP sub-options start on so the master switch shows something)
};
extern const ToggleInfo kToggles[ToggleCount];
extern std::atomic<bool> g_on[ToggleCount];

enum Value { DamageMult, SpeedMult, JumpMult, TimeScale, Fov, KillRadius, EspRange, EspWorldRange, AimFov, ValueCount };
struct ValueInfo {
    const char* key;
    float def, min, max;
};
extern const ValueInfo kValues[ValueCount];
extern std::atomic<float> g_value[ValueCount];

// Genesis Configuration powers the story unlocks one by one.
enum Power { Telekinesis, Pyrokinesis, Hellchains, PowerCount };
extern const char* const kPowerNames[PowerCount];

struct Named {
    std::string name;
    uint64_t fname = 0; // FName (objectives) or object index (checkpoints) for the action
};

struct Snapshot {
    bool reflection = false; // engine hooked
    bool player = false;     // a save is loaded and the player exists
    bool cheatManager = false;
    std::string map;
    float health = 0, maxHealth = 0, suffering = 0, maxSuffering = 0;
    int money = -1, shopLevel = -1;
    std::array<int, resources::Count> resourceTotals{-1, -1, -1, -1, -1, -1};
    std::array<std::string, ToggleCount> why; // non-empty = unavailable, shown as tooltip
    std::array<std::string, ValueCount> valueWhy;
    std::array<bool, PowerCount> powers{}; // unlocked
    std::string powersWhy;                 // non-empty = can't unlock now
    std::vector<std::pair<std::string, std::vector<Named>>> items; // by category
    std::vector<Named> objectivesActive, objectivesDone, checkpoints;
    std::string tracked;
    std::array<std::string, 3> slots; // "" = empty, else map name
    bool photoReady = false;
    std::string photoWhy, photoMessage;
    std::array<std::string, 3> cameraBookmarks; // full map identity; session only
    std::array<bool, 3> cameraBookmarkRestorable{};
};
Snapshot GetSnapshot();

// What the ESP draws, gathered on the game thread once per engine frame together with that frame's camera and the
// engine's own projection, so every point lands where the game draws it.
struct Vec3 {
    double x = 0, y = 0, z = 0;
};
struct EspEnemy {
    std::string name;
    Vec3 feet, top, head;
    bool hasHead = false;
    float health = 0, maxHealth = 0, distance = 0;
    std::vector<std::pair<Vec3, Vec3>> bones; // skeleton segments, world space
};
enum class EspKind : uint8_t { Item, Container, SavePoint };
struct EspThing {
    std::string label;
    Vec3 pos;
    float distance = 0;
    EspKind kind = EspKind::Item;
};
struct EspFrame {
    bool active = false; // ESP data below
    bool camera = false; // the camera and projection below were measured this frame (also used by the aim circle)
    Vec3 camLoc, camFwd, camRight, camUp;
    // Screen = (cx + fx * right/forward, cy - fy * up/forward), in pixels of a width x height viewport.
    double cx = 0, cy = 0, fx = 0, fy = 0, width = 0, height = 0;
    std::vector<EspEnemy> enemies;
    std::vector<EspThing> things;
};
EspFrame GetEspFrame();

void Init();  // loads toggles/values from the ini, registers the game-thread tick
void Save();  // writes toggles/values to the ini
const presets::Schema& ProfileSchema();
presets::Profile CaptureProfile(std::string name); // atomics only; no game objects or actions
void ApplyProfile(presets::Profile profile); // copied, validated and applied on the game thread
void GiveKit(presets::Kit kit); // validates every exact ID before queueing any existing GiveItem action

// One-shot actions, queued to the game thread.
void Heal();
void SetHealth(float value);
void FillSuffering();
void AddMoney(int delta);
void GiveItem(const std::string& name, int count);
void SetResourceTotal(int materialIndex, int total);
void GiveAllSkins();
void SetShopLevel(int level);
void KillNearby();
void SaveSlot(int slot);
void LoadSlot(int slot);
void SaveCameraBookmark(int slot);
void RestoreCameraBookmark(int slot);
void DeleteCameraBookmark(int slot);
void TeleportCheckpoint(uint64_t index);
void CompleteObjective(uint64_t fname);
void UnlockPower(Power power);

} // namespace feat
