#include "achievements.h"
#include "log.h"
#include <windows.h>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <deque>
#include <mutex>

namespace ach {
namespace {
constexpr uint32_t kAppId = 1551980; // Clive Barker's Hellraiser: Revival

// Minimal Windows x64 Steam callback ABI; no SDK binaries are distributed.
// Contract: ValveSoftware/Proton, steamworks_sdk_157/steam_api_common.h and isteamuserstats.h.
// The first three virtual slots and the flags/id fields are part of the ABI.
class Callback {
public:
    virtual void Run(void* data) = 0;
    virtual void Run(void* data, bool failure, uint64_t) { if (!failure) Run(data); }
    virtual int GetCallbackSizeBytes() = 0;
protected:
    uint8_t flags_ = 0;
    int id_ = 0;
};
static_assert(sizeof(Callback) == 16);
#pragma pack(push, 8)
// CSteamID is packed to alignment 1 in the SDK. Model its wire bytes rather than
// replacing it with a naturally aligned uint64_t (which incorrectly moves it to byte 16).
struct Received { uint64_t game; int result; uint8_t user[8]; };
struct Stored { uint64_t game; int result; };
struct AchievementStored { uint64_t game; bool group; char name[128]; uint32_t current, maximum; };
#pragma pack(pop)
static_assert(sizeof(Received) == 24 && offsetof(Received, user) == 12);
static_assert(sizeof(Stored) == 16);
static_assert(sizeof(AchievementStored) == 152 && offsetof(AchievementStored, current) == 140);

struct Event { int kind, result; uint64_t user; std::string name; };
std::mutex g_eventsMutex;
std::deque<Event> g_events;
void Enqueue(Event e) {
    std::lock_guard lock(g_eventsMutex);
    if (g_events.size() < 512) g_events.push_back(std::move(e));
}
class ReceivedCallback final : public Callback {
    void Run(void* data) override {
        const auto& p = *static_cast<Received*>(data);
        uint64_t user = 0;
        std::memcpy(&user, p.user, sizeof(user));
        if (p.game == kAppId) Enqueue({1101, p.result, user, {}});
    }
    int GetCallbackSizeBytes() override { return sizeof(Received); }
};
class StoredCallback final : public Callback {
    void Run(void* data) override {
        const auto& p = *static_cast<Stored*>(data);
        if (p.game == kAppId) Enqueue({1102, p.result, 0, {}});
    }
    int GetCallbackSizeBytes() override { return sizeof(Stored); }
};
class AchievementCallback final : public Callback {
    void Run(void* data) override {
        const auto& p = *static_cast<AchievementStored*>(data);
        if (p.game == kAppId && !p.group && p.current == 0 && p.maximum == 0) {
            const size_t n = strnlen_s(p.name, sizeof(p.name));
            if (n > 0 && n < sizeof(p.name)) Enqueue({1103, 1, 0, std::string(p.name, n)});
        }
    }
    int GetCallbackSizeBytes() override { return sizeof(AchievementStored); }
};

class SteamBackend final : public Backend {
    using Accessor = void* (__cdecl*)();
    Accessor getStats_ = nullptr, getUtils_ = nullptr, getUser_ = nullptr;
    bool (__cdecl* request_)(void*) = nullptr;
    uint32_t (__cdecl* count_)(void*) = nullptr;
    const char* (__cdecl* name_)(void*, uint32_t) = nullptr;
    bool (__cdecl* get_)(void*, const char*, bool*) = nullptr;
    bool (__cdecl* set_)(void*, const char*) = nullptr;
    bool (__cdecl* store_)(void*) = nullptr;
    uint32_t (__cdecl* appId_)(void*) = nullptr;
    bool (__cdecl* loggedOn_)(void*) = nullptr;
    uint64_t (__cdecl* userId_)(void*) = nullptr;
    void (__cdecl* register_)(Callback*, int) = nullptr;
    void* stats_ = nullptr;
    bool registered_ = false;
    ReceivedCallback receivedCallback_;
    StoredCallback storedCallback_;
    AchievementCallback achievementCallback_;
    template<class T> static bool Resolve(HMODULE module, T& out, const char* name) {
        out = reinterpret_cast<T>(GetProcAddress(module, name));
        return out != nullptr;
    }
public:
    uint64_t user = 0;
    bool Valid() override {
        // Use ONLY the Steam library already loaded by the game. No injection into other processes,
        // no Steam init/shutdown, callback pumping, manual dispatch, account tokens or network clients.
        HMODULE module = GetModuleHandleW(L"steam_api64.dll");
        if (!module) return false;
        if (!getStats_) {
            const bool ok =
                Resolve(module, getStats_, "SteamAPI_SteamUserStats_v012") &&
                Resolve(module, getUtils_, "SteamAPI_SteamUtils_v010") &&
                Resolve(module, getUser_, "SteamAPI_SteamUser_v021") &&
                Resolve(module, request_, "SteamAPI_ISteamUserStats_RequestCurrentStats") &&
                Resolve(module, count_, "SteamAPI_ISteamUserStats_GetNumAchievements") &&
                Resolve(module, name_, "SteamAPI_ISteamUserStats_GetAchievementName") &&
                Resolve(module, get_, "SteamAPI_ISteamUserStats_GetAchievement") &&
                Resolve(module, set_, "SteamAPI_ISteamUserStats_SetAchievement") &&
                Resolve(module, store_, "SteamAPI_ISteamUserStats_StoreStats") &&
                Resolve(module, appId_, "SteamAPI_ISteamUtils_GetAppID") &&
                Resolve(module, loggedOn_, "SteamAPI_ISteamUser_BLoggedOn") &&
                Resolve(module, userId_, "SteamAPI_ISteamUser_GetSteamID") &&
                Resolve(module, register_, "SteamAPI_RegisterCallback");
            if (!ok) { getStats_ = nullptr; return false; }
        }
        void* utils = getUtils_();
        void* currentUser = getUser_();
        stats_ = getStats_();
        if (!utils || !currentUser || !stats_ || appId_(utils) != kAppId || !loggedOn_(currentUser)) return false;
        uint64_t currentId = userId_(currentUser);
        if (!currentId || (user && user != currentId)) return false;
        user = currentId;
        if (!registered_) {
            register_(&receivedCallback_, 1101);
            register_(&storedCallback_, 1102);
            register_(&achievementCallback_, 1103);
            registered_ = true;
        }
        return true;
    }
    bool RequestStats() override { return request_(stats_); }
    bool Read(std::vector<Item>& items) override {
        const uint32_t n = count_(stats_);
        if (!n || n > 4096) return false;
        items.reserve(n);
        for (uint32_t i = 0; i < n; ++i) {
            const char* id = name_(stats_, i);
            const size_t len = id ? strnlen_s(id, 128) : 0;
            bool unlocked = false;
            if (!len || len >= 128 || !get_(stats_, id, &unlocked)) return false;
            items.push_back({std::string(id, len), unlocked});
        }
        return true;
    }
    bool Set(const char* id) override { return set_(stats_, id); }
    bool Store() override { return store_(stats_); }
};

struct Runtime {
    SteamBackend backend;
    Controller controller{backend};
    std::mutex mutex;
};
Runtime& GetRuntime() {
    // The proxy stays loaded for the game's lifetime. Keep callback objects alive through process exit,
    // rather than calling the Steam DLL from CRT teardown under the loader lock.
    static Runtime* runtime = new Runtime;
    return *runtime;
}
std::atomic<int> g_command{0};
void Queue(int command) { int expected = 0; g_command.compare_exchange_strong(expected, command); }
} // namespace

void RequestRefresh() { Queue(1); }
void RequestUnlock() { Queue(2); }
Snapshot GetSnapshot() {
    auto& r = GetRuntime();
    std::lock_guard lock(r.mutex);
    return r.controller.Get();
}
void Tick() {
    auto& r = GetRuntime();
    std::lock_guard lock(r.mutex);
    const auto previous = r.controller.Get().phase;
    std::deque<Event> events;
    {
        std::lock_guard eventsLock(g_eventsMutex);
        events.swap(g_events);
    }
    for (const auto& e : events) {
        if (e.kind == 1101)
            km::Log("Achievements: UserStatsReceived result=%d current-user=%s",
                    e.result, e.user == r.backend.user ? "yes" : "no");
        if (e.kind == 1101 && e.user == r.backend.user) r.controller.Received(e.result);
        else if (e.kind == 1102) r.controller.Stored(e.result);
        else if (e.kind == 1103) r.controller.Achievement(e.name);
    }
    const uint64_t now = GetTickCount64();
    const int command = g_command.exchange(0);
    if (command == 1) r.controller.Refresh(now);
    else if (command == 2) r.controller.Unlock(now);
    r.controller.Tick(now);
    const auto& state = r.controller.Get();
    if (state.phase != previous)
        km::Log("Achievements: %s total=%u unlocked=%u requested=%u confirmed=%u rejected=%u",
                state.message, state.total, state.unlocked, state.accepted, state.confirmed, state.rejected);
}
} // namespace ach
