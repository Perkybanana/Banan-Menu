#pragma once
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace km {

// Folder containing the DLL, with trailing backslash. Set once at startup, before the first Log().
inline std::wstring g_dir;

inline std::string Utf8(const std::wstring& w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// Overwritten each launch, flushed per line so the log survives a crash.
inline void Log(const char* fmt, ...)
{
    static std::mutex m;
    std::lock_guard lock(m);
    static FILE* f = _wfsopen((g_dir + L"BananMenu.log").c_str(), L"w", _SH_DENYNO);
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a;
    va_start(a, fmt);
    vfprintf(f, fmt, a);
    va_end(a);
    fputc('\n', f);
    fflush(f);
}

} // namespace km
