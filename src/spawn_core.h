#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace spawn {
inline constexpr int TypeCount = 5, MaxCount = 5;
inline constexpr uint64_t TimeoutMs = 15000;
// Engine-created FName request marker, not a UObject pointer/index/serial.
struct Identity { uint64_t token = 0; bool operator==(const Identity&) const = default; };
enum class Result { Idle, Pending, Success, Preflight, Timeout, Transition, Stale, Dispatch };
class Request {
public:
    bool Begin(bool preflight, int count, uint64_t now) {
        if (Pending()) return false;
        *this = {};
        if (!preflight || count < 1 || count > MaxCount) { result_ = spawn::Result::Preflight; return false; }
        count_ = count; started_ = now; result_ = spawn::Result::Pending; return true;
    }
    bool Bind(int slot, Identity identity) {
        if (!Pending() || slot < 0 || slot >= count_ || !identity.token || ids_[slot].token) return false;
        for (const auto& id : ids_) if (id == identity) return false;
        ids_[slot] = identity; return true;
    }
    void Observe(int slot, Identity identity, bool owned, bool initialized, bool mesh) {
        if (!Pending() || slot < 0 || slot >= count_ || !ids_[slot].token) return;
        if (!owned || identity != ids_[slot]) { ready_[slot] = false; result_ = spawn::Result::Stale; return; }
        ready_[slot] = initialized && mesh;
    }
    void Poll(bool context, uint64_t now) {
        if (!Pending()) return;
        if (!context) result_ = spawn::Result::Transition;
        else if (Ready() == count_) result_ = spawn::Result::Success;
        else if (now - started_ >= TimeoutMs) result_ = spawn::Result::Timeout;
    }
    void Fail() { if (Pending()) result_ = spawn::Result::Dispatch; }
    bool Pending() const { return result_ == spawn::Result::Pending; }
    int Ready() const { int n = 0; for (bool ready : ready_) n += ready; return n; }
    spawn::Result Result() const { return result_; }
private:
    std::array<Identity, MaxCount> ids_{};
    std::array<bool, MaxCount> ready_{};
    int count_ = 0;
    uint64_t started_ = 0;
    spawn::Result result_ = spawn::Result::Idle;
};
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
