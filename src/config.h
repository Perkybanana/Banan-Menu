#pragma once
// BananMenu.ini next to the DLL (plain Win32 profile API: readable and editable by hand).
#include "log.h"

#include <cstdio>
#include <string>

namespace cfg {

inline std::wstring Path() { return km::g_dir + L"BananMenu.ini"; }

inline std::wstring W(const char* s) { return std::wstring(s, s + strlen(s)); } // keys are ASCII

inline std::string GetString(const char* section, const char* key, const char* def = "")
{
    wchar_t buf[512];
    GetPrivateProfileStringW(W(section).c_str(), W(key).c_str(), W(def).c_str(), buf, 512, Path().c_str());
    return km::Utf8(buf);
}

inline void SetString(const char* section, const char* key, const std::string& value)
{
    WritePrivateProfileStringW(W(section).c_str(), W(key).c_str(), W(value.c_str()).c_str(), Path().c_str());
}

inline int GetInt(const char* section, const char* key, int def)
{
    return GetPrivateProfileIntW(W(section).c_str(), W(key).c_str(), def, Path().c_str());
}

inline void SetInt(const char* section, const char* key, int value) { SetString(section, key, std::to_string(value)); }

inline float GetFloat(const char* section, const char* key, float def)
{
    std::string s = GetString(section, key);
    return s.empty() ? def : static_cast<float>(atof(s.c_str()));
}

inline void SetFloat(const char* section, const char* key, float value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", value);
    SetString(section, key, buf);
}

inline void Reset() { DeleteFileW(Path().c_str()); }

} // namespace cfg
