#pragma once
#include <cctype>
#include <cstdint>
#include <string>

// Readable names: "Item_HollowpointRounds" -> "Hollowpoint Rounds", "Objective.EP5.FindTheKey" -> "Find The Key".
inline std::string Pretty(std::string s)
{
    if (s.rfind("Item_", 0) == 0) s.erase(0, 5);
    if (auto dot = s.rfind('.'); dot != std::string::npos) s.erase(0, dot + 1); // gameplay tags: last segment
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i] == '_' ? ' ' : s[i];
        if (i && std::isupper(uint8_t(c)) && std::islower(uint8_t(s[i - 1]))) out += ' ';
        out += c;
    }
    return out;
}
