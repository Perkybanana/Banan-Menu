#pragma once

// UI translations. Strings are looked up by their English text: the code keeps readable English literals and anything
// missing from the table (or from a language) shows in English.
namespace lang {

enum Id { En, Zh, Ja, Ko, Ru, De, Fr, Es, Pt, Count };

struct Info {
    const char* code;        // ini value ([Menu] Language)
    const char* name;        // native name, shown in Settings
    const wchar_t* fonts[2]; // Windows font for this script (regular, bold); null when Inter covers it
};
extern const Info kLangs[Count];

void Set(int id);
int Get();
int FromCode(const char* code); // -1 when unknown
int FromSystem();               // Windows display language, else English
const char* T(const char* en);  // `en` in the current language

} // namespace lang
