#pragma once
#include <array>
#include <cmath>

namespace spawn {
inline constexpr int TypeCount = 5, MaxCount = 5;
struct Type { const char* label; const char* path; };
// Exact generated classes observed in the game's helper defaults. Never accept a caller-supplied path.
inline constexpr Type Types[TypeCount] = {
    {"Hellghoul", "/Game/Blueprints/Characters/Enemies/Hellghoul/CH_Hellghoul.CH_Hellghoul_C"},
    {"Exploding Hellghoul", "/Game/Blueprints/Characters/Enemies/Hellghoul/Hellghoul_Exploding/CH_Hellghoul_Exploding.CH_Hellghoul_Exploding_C"},
    {"Cambion", "/Game/Blueprints/Characters/Enemies/Cambion/CH_Cambion.CH_Cambion_C"},
    {"Cultist", "/Game/Blueprints/Characters/Enemies/Cultist/CH_Cultist.CH_Cultist_C"},
    {"Runner", "/Game/Blueprints/Characters/Enemies/Runner/CH_Runner.CH_Runner_C"},
};
struct Point { double x = 0, y = 0, z = 0; };
inline bool ValidRequest(int type, int count) { return type >= 0 && type < TypeCount && count >= 1 && count <= MaxCount; }
inline bool Finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
inline bool Positions(Point origin, Point forward, int count, std::array<Point, MaxCount>& out)
{
    if (count < 1 || count > MaxCount || !Finite(origin) || !Finite(forward) ||
        std::abs(origin.x) > 1e8 || std::abs(origin.y) > 1e8 || std::abs(origin.z) > 1e8) return false;
    const double length = std::hypot(forward.x, forward.y);
    if (!std::isfinite(length) || length < 1e-6) return false;
    const double x = forward.x / length, y = forward.y / length;
    std::array<Point, MaxCount> positions{};
    for (int i = 0; i < count; ++i) {
        const double lateral = (i - (count - 1) * .5) * 160;
        positions[i] = {origin.x + x * 350 - y * lateral, origin.y + y * 350 + x * lateral, origin.z};
        if (!Finite(positions[i])) return false;
    }
    out = positions;
    return true;
}
} // namespace spawn
