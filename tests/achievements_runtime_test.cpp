#include "../src/achievements.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
using namespace ach;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
int wmain(int argc, wchar_t** argv) {
    CHECK(argc == 2); CHECK(!GetModuleHandleW(L"steam_api64.dll"));
    RequestRefresh(); Tick(); CHECK(GetSnapshot().phase == Phase::Error);
    HMODULE fake = LoadLibraryW(argv[1]); CHECK(fake);
    auto context = reinterpret_cast<void(*)(uint32_t, bool)>(GetProcAddress(fake,"TestContext"));
    auto writes = reinterpret_cast<int(*)()>(GetProcAddress(fake,"TestWrites"));
    auto stores = reinterpret_cast<int(*)()>(GetProcAddress(fake,"TestStores"));
    auto received = reinterpret_cast<void(*)(uint64_t, uint64_t)>(GetProcAddress(fake,"TestReceived"));
    auto stored = reinterpret_cast<void(*)()>(GetProcAddress(fake,"TestStored"));
    auto achievement = reinterpret_cast<void(*)(const char*, uint32_t)>(GetProcAddress(fake,"TestAchievement"));
    CHECK(context && writes && stores && received && stored && achievement);
    context(42,true); RequestRefresh(); Tick(); CHECK(GetSnapshot().phase == Phase::Error);
    context(1551980,false); RequestRefresh(); Tick(); CHECK(GetSnapshot().phase == Phase::Error);
    CHECK(writes() == 0);
    context(1551980,true); RequestRefresh(); Tick(); CHECK(GetSnapshot().phase == Phase::Loading);
    received(42,12345); Tick(); CHECK(GetSnapshot().phase == Phase::Loading);
    received(1551980,99999); Tick(); CHECK(GetSnapshot().phase == Phase::Loading);
    received(1551980,12345); Tick(); CHECK(GetSnapshot().canUnlock);
    CHECK(GetSnapshot().total == 3 && GetSnapshot().unlocked == 1 && writes() == 0);
    RequestUnlock(); Tick(); CHECK(GetSnapshot().phase == Phase::Saving);
    CHECK(writes() == 2 && stores() == 1);
    stored(); Tick(); CHECK(GetSnapshot().phase == Phase::Saving);
    achievement("A",1); Tick(); CHECK(GetSnapshot().confirmed == 0);
    achievement("A",0); achievement("B",0); Tick();
    CHECK(GetSnapshot().phase == Phase::Saved && GetSnapshot().unlocked == 3);
    RequestUnlock(); Tick(); CHECK(writes() == 2 && stores() == 1);
    std::puts("Steam adapter fixture passed: ABI, app/user filters, explicit unlock, callbacks.");
    // Keep the fixture loaded while the registered callback objects exist.
}

