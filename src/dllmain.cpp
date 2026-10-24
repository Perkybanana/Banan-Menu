#include "winmm_exports.h"
#include "log.h"
#include "menu.h"
#include "overlay.h"
#include "ue.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static std::wstring ModulePath(HMODULE m)
{
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(m, buf, MAX_PATH);
    return buf;
}

static DWORD g_mainThread; // UE runs its game thread on the process main thread, which loads us

extern "C" void* g_kmWinmm[kWinmmCount] = {};
static wchar_t g_winmmPath[MAX_PATH];
static int g_winmmResolved;

static UINT WINAPI WinmmMissing() { return 0; } // an export the real DLL lacks (some under Wine): harmless no-op

// Points every export stub at the real winmm.dll, opened by its System32 path so it is never this proxy. Done in
// DllMain: the game calls winmm as soon as the loader hands it over (ReShade and ASI loaders do the same).
static void ResolveWinmm(HMODULE self)
{
    UINT n = GetSystemDirectoryW(g_winmmPath, MAX_PATH);
    HMODULE real = n && n < MAX_PATH - 11 && !wcscat_s(g_winmmPath, L"\\winmm.dll") ? LoadLibraryW(g_winmmPath) : nullptr;
    for (int i = 0; i < kWinmmCount; i++) {
        const char* import = kWinmmImports[i];
        FARPROC fn = real && real != self ? GetProcAddress(real, import[0] == '#' ? MAKEINTRESOURCEA(atoi(import + 1)) : import)
                                          : nullptr;
        g_winmmResolved += fn != nullptr;
        g_kmWinmm[i] = fn ? reinterpret_cast<void*>(fn) : reinterpret_cast<void*>(&WinmmMissing);
    }
}

static void Main(HMODULE self)
{
    fs::path dll = ModulePath(self);
    km::g_dir = dll.parent_path().wstring() + L"\\";
    km::Log("Banan Menu v%s", KM_VERSION);
    km::Log("Loaded as: %ls", dll.c_str());

    fs::path exe = ModulePath(nullptr);
    km::Log("Process: %ls", exe.c_str());
    std::ifstream vf(exe.parent_path() / "../../../Version.txt");
    std::string ver;
    std::getline(vf, ver);
    km::Log("Game version: %s", ver.empty() ? "(Version.txt not found)" : ver.c_str());

    km::Log("winmm forwarding: %s, %d of %d functions (%ls)", g_winmmResolved > kWinmmCount / 2 ? "OK" : "FAILED",
            g_winmmResolved, kWinmmCount, g_winmmPath);

    menu::Init(self, ver); // settings + game-thread tick, before any hook can call into them
    km::Log("Overlay: %s", overlay::Install() ? "hooks installed" : "disabled");
    ue::Init(g_mainThread);

#ifdef KM_DUMPER // dev build only (cmake -DKM_DUMPER=ON): re-dump the SDK after a game patch
    if (auto d7 = km::g_dir + L"Dumper-7.dll"; GetFileAttributesW(d7.c_str()) != INVALID_FILE_ATTRIBUTES)
        km::Log("Dumper-7: %s", LoadLibraryW(d7.c_str()) ? "loaded" : "load failed");
#endif
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID)
{
    // Only the winmm stubs and a thread here: anything heavier would run under the loader lock.
    if (reason == DLL_PROCESS_ATTACH) {
        ResolveWinmm(self);
        g_mainThread = GetCurrentThreadId();
        HANDLE t = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD { Main(static_cast<HMODULE>(p)); return 0; },
                                self, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
