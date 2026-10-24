// Loads the built proxy and calls winmm through it: each export must reach the real System32 winmm.dll
// (same results), by name and by ordinal.
#include <windows.h>
#include <cstdio>
#include <string>

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) return 2;
    HMODULE proxy = LoadLibraryW(argv[1]);
    if (!proxy) {
        printf("FAIL load error %lu\n", GetLastError());
        return 1;
    }
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    HMODULE real = LoadLibraryW((std::wstring(sys) + L"\\winmm.dll").c_str());
    if (!real || real == proxy) {
        printf("FAIL system winmm.dll\n");
        return 1;
    }

    int fails = 0;
    auto check = [&](const char* what, bool ok) {
        fails += !ok;
        printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    };
    using GetTime = DWORD(WINAPI*)();
    using Period = UINT(WINAPI*)(UINT);
    using NumDevs = UINT(WINAPI*)();
    auto pTime = reinterpret_cast<GetTime>(GetProcAddress(proxy, "timeGetTime"));
    auto rTime = reinterpret_cast<GetTime>(GetProcAddress(real, "timeGetTime"));
    check("timeGetTime is the proxy's own stub", pTime && pTime != rTime);
    check("timeGetTime returns the real time", pTime && rTime && pTime() - rTime() + 50u <= 100u);

    auto pBegin = reinterpret_cast<Period>(GetProcAddress(proxy, "timeBeginPeriod"));
    auto pEnd = reinterpret_cast<Period>(GetProcAddress(proxy, "timeEndPeriod"));
    check("timeBeginPeriod / timeEndPeriod succeed", pBegin && pEnd && pBegin(1) == 0 && pEnd(1) == 0);

    auto pDevs = reinterpret_cast<NumDevs>(GetProcAddress(proxy, "waveOutGetNumDevs"));
    auto rDevs = reinterpret_cast<NumDevs>(GetProcAddress(real, "waveOutGetNumDevs"));
    check("waveOutGetNumDevs matches", pDevs && rDevs && pDevs() == rDevs());

    check("ordinal #2 is exported", GetProcAddress(proxy, MAKEINTRESOURCEA(2)) && GetProcAddress(real, MAKEINTRESOURCEA(2)));

    Sleep(500); // let the trainer thread finish writing its log
    return fails ? 1 : 0;
}
