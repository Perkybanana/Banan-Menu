// Test fixture only. Never distribute with the menu or copy into a game.
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#define API extern "C" __declspec(dllexport)
class CallbackAbi {
public:
    virtual void Run(void*) = 0;
    virtual void Run(void*, bool, uint64_t) = 0;
    virtual int GetCallbackSizeBytes() = 0;
    uint8_t flags = 0;
    int callbackId = 0;
};
static CallbackAbi* callbacks[3]{};
static uint32_t app = 42;
static bool online = false, a = false, b = false;
static int writes = 0, stores = 0;
API void TestContext(uint32_t id, bool loggedOn) { app = id; online = loggedOn; }
API int TestWrites() { return writes; }
API int TestStores() { return stores; }
API void* SteamAPI_SteamUserStats_v012() { return reinterpret_cast<void*>(1); }
API void* SteamAPI_SteamUtils_v010() { return reinterpret_cast<void*>(2); }
API void* SteamAPI_SteamUser_v021() { return reinterpret_cast<void*>(3); }
API bool SteamAPI_ISteamUserStats_RequestCurrentStats(void*) { return true; }
API uint32_t SteamAPI_ISteamUserStats_GetNumAchievements(void*) { return 3; }
API const char* SteamAPI_ISteamUserStats_GetAchievementName(void*, uint32_t index) {
    static const char* ids[] = {"OLD", "A", "B"}; return index < 3 ? ids[index] : nullptr;
}
API bool SteamAPI_ISteamUserStats_GetAchievement(void*, const char* id, bool* unlocked) {
    if (!strcmp(id,"OLD")) *unlocked = true;
    else if (!strcmp(id,"A")) *unlocked = a;
    else if (!strcmp(id,"B")) *unlocked = b;
    else return false;
    return true;
}
API bool SteamAPI_ISteamUserStats_SetAchievement(void*, const char* id) {
    if (!strcmp(id,"A")) a = true;
    else if (!strcmp(id,"B")) b = true;
    else return false;
    ++writes; return true;
}
API bool SteamAPI_ISteamUserStats_StoreStats(void*) { ++stores; return true; }
API uint32_t SteamAPI_ISteamUtils_GetAppID(void*) { return app; }
API bool SteamAPI_ISteamUser_BLoggedOn(void*) { return online; }
API uint64_t SteamAPI_ISteamUser_GetSteamID(void*) { return 12345; }
API void SteamAPI_RegisterCallback(CallbackAbi* cb, int id) {
    if (id < 1101 || id > 1103) std::abort();
    cb->flags |= 1; cb->callbackId = id; callbacks[id - 1101] = cb;
}
API void TestReceived(uint64_t game, uint64_t user) {
    // SDK 1.57 steamclientpublic.h packs CSteamID to alignment 1. It starts at byte 12,
    // NOT byte 16. The enclosing callback still has size 24 because gameID has alignment 8.
    // Keep a raw SDK-layout fixture so it cannot repeat the production declaration's error.
    alignas(8) unsigned char buffer[24]{};
    int result = 1;
    std::memcpy(buffer, &game, 8); std::memcpy(buffer + 8, &result, 4); std::memcpy(buffer + 12, &user, 8);
    std::memset(buffer + 20, 0xCD, 4); // padding must never become part of the user ID
    if (!callbacks[0] || callbacks[0]->GetCallbackSizeBytes() != 24) std::abort();
    callbacks[0]->Run(buffer);
}
API void TestStored() {
    alignas(8) unsigned char buffer[16]{};
    uint64_t game = 1551980; int result = 1;
    std::memcpy(buffer, &game, 8); std::memcpy(buffer + 8, &result, 4);
    if (!callbacks[1] || callbacks[1]->GetCallbackSizeBytes() != 16) std::abort();
    callbacks[1]->Run(buffer, false, 0);
}
API void TestAchievement(const char* id, uint32_t progress) {
    alignas(8) unsigned char buffer[152]{};
    uint64_t game = 1551980;
    std::memcpy(buffer, &game, 8); strcpy_s(reinterpret_cast<char*>(buffer + 9), 128, id);
    std::memcpy(buffer + 140, &progress, 4);
    if (!callbacks[2] || callbacks[2]->GetCallbackSizeBytes() != 152) std::abort();
    callbacks[2]->Run(buffer);
}

