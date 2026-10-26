#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace spawn {
inline constexpr int TypeCount = 5, MaxCount = 5;
inline constexpr uint64_t TimeoutMs = 15000;
enum class PhaseState { Fresh, Attempted, Verified, Failed };
enum class PhaseResult { Pending, Ready, Failed, Invalidated };
// Used only by pending, freshly correlated spawn slots. Native::Valid rechecks
// current context/deadline/identity/unique alive ownership around every dispatch.
template<class Native> PhaseResult PrepareCombatPhase(int type, PhaseState& state, Native& native)
{
    if (type < 0 || type >= TypeCount) return PhaseResult::Failed;
    if (type != 2) return PhaseResult::Ready;
    if (state == PhaseState::Failed) return PhaseResult::Failed;
    auto fail = [&] { state = PhaseState::Failed; return PhaseResult::Failed; };
    auto invoke = [&](auto&& call) {
        if (!native.Valid()) return PhaseResult::Invalidated;
        const bool dispatched = call();
        if (!native.Valid()) return PhaseResult::Invalidated;
        return dispatched ? PhaseResult::Ready : fail();
    };
    bool initialized = false, final = false;
    std::string phase;
    auto result = invoke([&] { return native.Initialized(initialized); });
    if (result != PhaseResult::Ready) return result;
    if (!initialized) return PhaseResult::Pending;
    result = invoke([&] { return native.Final(final); });
    if (result != PhaseResult::Ready) return result;
    // Read the phase last, immediately before the guarded mutation decision.
    result = invoke([&] { return native.Current(phase); });
    if (result != PhaseResult::Ready) return result;
    if (phase == "Phase_2" && final) { state = PhaseState::Verified; return PhaseResult::Ready; }
    if (phase != "Phase_1" || final || state != PhaseState::Fresh) return fail();
    state = PhaseState::Attempted; // never repeat a setter, even if its dispatch/postcondition fails
    result = invoke([&] { return native.SetFinal(); });
    if (result != PhaseResult::Ready) return result;
    result = invoke([&] { return native.Final(final); });
    if (result != PhaseResult::Ready) return result;
    result = invoke([&] { return native.Current(phase); });
    if (result != PhaseResult::Ready) return result;
    if (phase != "Phase_2" || !final) return fail();
    state = PhaseState::Verified;
    return PhaseResult::Ready;
}
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
    bool CheckDeadline(bool context, uint64_t now) {
        if (!Pending()) return false;
        if (!context) result_ = spawn::Result::Transition;
        else if (now - started_ >= TimeoutMs) result_ = spawn::Result::Timeout;
        return Pending();
    }
    void Poll(bool context, uint64_t now) {
        if (CheckDeadline(context, now) && Ready() == count_) result_ = spawn::Result::Success;
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
struct Type { const char* label; const char* path; const char* subtype; };
// Exact generated classes observed in the game's helper defaults. Never accept a caller-supplied path.
inline constexpr Type Types[TypeCount] = {
    {"Hellghoul", "/Game/Blueprints/Characters/Enemies/Hellghoul/CH_Hellghoul.CH_Hellghoul_C", "Enemies.Hellghoul.Types.Default"},
    {"Exploding Hellghoul", "/Game/Blueprints/Characters/Enemies/Hellghoul/Hellghoul_Exploding/CH_Hellghoul_Exploding.CH_Hellghoul_Exploding_C", "Enemies.HellghoulExploding.Types.Default"},
    {"Cambion", "/Game/Blueprints/Characters/Enemies/Cambion/CH_Cambion.CH_Cambion_C", "Enemies.Cambion.Types.ArenaTwoCombat"},
    {"Cultist", "/Game/Blueprints/Characters/Enemies/Cultist/CH_Cultist.CH_Cultist_C", "Enemies.Cultist.Types.Ranged"},
    {"Runner", "/Game/Blueprints/Characters/Enemies/Runner/CH_Runner.CH_Runner_C", "Enemies.Runner.Types.Default"},
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
