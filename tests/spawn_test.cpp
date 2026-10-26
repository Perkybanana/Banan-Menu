#include "../src/spawn_core.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

// Only native dispatch is substituted; the production phase protocol and deadline
// reducer run unchanged. Mutations between calls emulate native callbacks.
struct PhaseNative {
    spawn::Request request;
    uint64_t now = 101;
    bool context = true, owned = true, generation = true, initialized = true, final = false;
    std::string phase = "Phase_1", afterPhase = "Phase_2";
    std::string phaseOnFinalQuery;
    bool afterFinal = true;
    int failCall = 0, invalidateCall = 0, invalidateGuard = 0, invalidation = 0, guards = 0, setters = 0;
    std::vector<std::string> calls;
    PhaseNative() { CHECK(request.Begin(true, 1, 100) && request.Bind(0, {41})); }
    void Invalidate() {
        if (invalidation == 0) context = false;
        if (invalidation == 1) owned = false;
        if (invalidation == 2) generation = false;
        if (invalidation == 3) now = 15100;
    }
    bool Valid() {
        if (++guards == invalidateGuard) Invalidate();
        if (!request.CheckDeadline(context, now)) return false;
        if (!owned || !generation) { request.Observe(0, {41}, false, false, false); return false; }
        return true;
    }
    bool Call(const char* name) {
        calls.emplace_back(name);
        if (int(calls.size()) == invalidateCall) Invalidate();
        return int(calls.size()) != failCall;
    }
    bool Initialized(bool& out) { out = initialized; return Call("initialized"); }
    bool Current(std::string& out) { out = phase; return Call("current"); }
    bool Final(bool& out) {
        if (!phaseOnFinalQuery.empty()) phase = phaseOnFinalQuery;
        out = final; return Call("final");
    }
    bool SetFinal() {
        ++setters;
        phase = afterPhase; final = afterFinal;
        return Call("set");
    }
};

void PhaseTests()
{
    using State = spawn::PhaseState;
    using Result = spawn::PhaseResult;
    for (int type : {0, 1, 3, 4}) {
        PhaseNative native;
        State state{};
        CHECK(spawn::PrepareCombatPhase(type, state, native) == Result::Ready);
        CHECK(native.calls.empty() && native.guards == 0 && state == State::Fresh);
    }
    for (int type : {-1, 5, INT32_MAX}) {
        PhaseNative native;
        State state{};
        CHECK(spawn::PrepareCombatPhase(type, state, native) == Result::Failed && native.calls.empty());
    }
    PhaseNative pending;
    State state{};
    pending.initialized = false;
    CHECK(spawn::PrepareCombatPhase(2, state, pending) == Result::Pending);
    CHECK(pending.calls == std::vector<std::string>{"initialized"} && pending.setters == 0);
    pending.calls.clear(); pending.initialized = true;
    CHECK(spawn::PrepareCombatPhase(2, state, pending) == Result::Ready);
    CHECK(pending.calls == (std::vector<std::string>{"initialized", "final", "current", "set", "final", "current"}));
    CHECK(pending.phase == "Phase_2" && pending.final && pending.setters == 1 && state == State::Verified);
    CHECK(spawn::PrepareCombatPhase(2, state, pending) == Result::Ready && pending.setters == 1);
    // A regression to the initial phase must never re-run the setter in this slot.
    pending.phase = "Phase_1"; pending.final = false;
    CHECK(spawn::PrepareCombatPhase(2, state, pending) == Result::Failed && pending.setters == 1);
    auto calls = pending.calls;
    CHECK(spawn::PrepareCombatPhase(2, state, pending) == Result::Failed && pending.calls == calls);
    PhaseNative already;
    state = State::Fresh; already.phase = "Phase_2"; already.final = true;
    CHECK(spawn::PrepareCombatPhase(2, state, already) == Result::Ready && already.setters == 0);
    CHECK(spawn::PrepareCombatPhase(2, state, already) == Result::Ready && already.setters == 0);
    for (const char* phase : {"", "None", "Unknown", "CliffSequence", "CageSequence", "Phase_2"}) {
        PhaseNative native;
        state = State::Fresh; native.phase = phase;
        CHECK(spawn::PrepareCombatPhase(2, state, native) == Result::Failed && native.setters == 0);
    }
    PhaseNative inconsistent;
    state = State::Fresh; inconsistent.final = true;
    CHECK(spawn::PrepareCombatPhase(2, state, inconsistent) == Result::Failed && inconsistent.setters == 0);
    PhaseNative changedPhase;
    state = State::Fresh; changedPhase.phaseOnFinalQuery = "CliffSequence";
    CHECK(spawn::PrepareCombatPhase(2, state, changedPhase) == Result::Failed && changedPhase.setters == 0);
    for (int failure = 1; failure <= 6; ++failure) {
        PhaseNative native;
        state = State::Fresh; native.failCall = failure;
        CHECK(spawn::PrepareCombatPhase(2, state, native) == Result::Failed);
        CHECK(int(native.calls.size()) == failure && state == State::Failed);
        calls = native.calls;
        CHECK(spawn::PrepareCombatPhase(2, state, native) == Result::Failed && native.calls == calls);
    }
    for (const char* phase : {"Phase_1", "CliffSequence", "CageSequence", "Unknown"}) {
        PhaseNative native;
        state = State::Fresh; native.afterPhase = phase;
        CHECK(spawn::PrepareCombatPhase(2, state, native) == Result::Failed && native.setters == 1);
    }
    PhaseNative notFinal;
    state = State::Fresh; notFinal.afterFinal = false;
    CHECK(spawn::PrepareCombatPhase(2, state, notFinal) == Result::Failed && notFinal.setters == 1);
    // Every call, especially the setter and postcondition getters, must be fenced
    // on both sides. Invalidation wins even when native dispatch itself fails.
    for (int why = 0; why < 4; ++why) {
        for (int guard = 1; guard <= 12; ++guard) {
            PhaseNative native;
            state = State::Fresh; native.invalidation = why; native.invalidateGuard = guard;
            CHECK(spawn::PrepareCombatPhase(2, state, native) == Result::Invalidated);
            CHECK(int(native.calls.size()) == guard / 2);
        }
        for (int step = 1; step <= 6; ++step) {
            PhaseNative native;
            state = State::Fresh; native.invalidation = why; native.invalidateCall = step; native.failCall = step;
            CHECK(spawn::PrepareCombatPhase(2, state, native) == Result::Invalidated);
            CHECK(int(native.calls.size()) == step);
            CHECK(native.request.Result() == (why == 0 ? spawn::Result::Transition : why == 3 ? spawn::Result::Timeout : spawn::Result::Stale));
        }
    }
}
int main()
{
    PhaseTests();
    // Actor existence must never count as completion; this is the production request reducer.
    spawn::Request request;
    CHECK(!request.Begin(false, 1, 100));
    CHECK(!request.Pending() && request.Ready() == 0);
    CHECK(request.Begin(true, 2, 100));
    CHECK(!request.Begin(true, 1, 101));
    CHECK(!request.Bind(0, {0})); // missing correlation is not actor identity
    CHECK(request.Bind(0, {41}));
    CHECK(!request.Bind(1, {41})); // another slot cannot alias a marker
    CHECK(request.Bind(1, {42}));
    request.Observe(0, {41}, true, false, true);
    request.Observe(1, {42}, true, true, false);
    CHECK(request.Ready() == 0 && request.Pending());
    request.Observe(0, {41}, true, true, true);
    CHECK(request.Ready() == 1 && request.Pending());
    request.Observe(1, {43}, true, true, true); // different request cannot complete this slot
    CHECK(request.Ready() == 1 && !request.Pending());
    CHECK(request.Result() == spawn::Result::Stale);
    CHECK(request.Begin(true, 1, 100));
    request.Poll(true, 100 + spawn::TimeoutMs - 1);
    CHECK(request.Pending() && request.Ready() == 0); // no manager completion is not success
    request.Poll(true, 100 + spawn::TimeoutMs);
    CHECK(request.Result() == spawn::Result::Timeout && request.Ready() == 0);
    CHECK(request.Begin(true, 1, 100));
    request.Poll(false, 101);
    CHECK(request.Result() == spawn::Result::Transition);
    CHECK(request.Begin(true, 1, 100) && request.Bind(0, {41}));
    request.Observe(0, {41}, true, true, true);
    request.Poll(true, 101);
    CHECK(request.Ready() == 1 && request.Result() == spawn::Result::Success && !request.Pending());
    CHECK(request.Begin(true, 1, 100) && request.Bind(0, {41}));
    request.Observe(0, {41}, false, true, true);
    CHECK(request.Result() == spawn::Result::Stale && request.Ready() == 0);
    CHECK(request.Begin(true, 2, 100) && request.Bind(0, {51}) && request.Bind(1, {52}));
    request.Observe(0, {51}, true, true, true);
    request.Observe(0, {51}, true, false, true); // readiness can regress before the batch completes
    CHECK(request.Ready() == 0 && request.Pending());
    request.Fail();
    CHECK(!request.Pending() && request.Result() == spawn::Result::Dispatch);
    for (uint64_t observed : {15099ull, 15100ull, 15101ull}) {
        CHECK(request.Begin(true, 1, 100) && request.Bind(0, {61}));
        request.Observe(0, {61}, true, true, true);
        request.Poll(true, observed);
        CHECK(request.Result() == (observed < 15100 ? spawn::Result::Success : spawn::Result::Timeout));
        request.Poll(false, 20000); // terminal success/timeout must not be rewritten
        CHECK(request.Result() == (observed < 15100 ? spawn::Result::Success : spawn::Result::Timeout));
    }
    CHECK(request.Begin(true, 2, 100) && request.Bind(0, {71}) && request.Bind(1, {72}));
    request.Observe(0, {71}, true, true, true);
    request.Poll(true, 15099);
    CHECK(request.Pending() && request.Ready() == 1);
    request.Poll(true, 15100);
    CHECK(request.Result() == spawn::Result::Timeout && request.Ready() == 1);
    CHECK(request.Begin(true, 1, 100) && request.Bind(0, {81}));
    request.Observe(0, {81}, true, true, true);
    request.Poll(false, 15100);
    CHECK(request.Result() == spawn::Result::Transition); // transition takes precedence over expiry/readiness
    for (uint64_t afterWork : {15099ull, 15100ull, 15101ull}) {
        CHECK(request.Begin(true, 2, 100) && request.Bind(0, {91}) && request.Bind(1, {92}));
        request.Observe(0, {91}, true, true, true);
        CHECK(request.CheckDeadline(true, 15098)); // time before the native readiness work
        // Same production integration seam: check FRESH time before delivering an observation.
        if (request.CheckDeadline(true, afterWork)) request.Observe(1, {92}, true, true, true);
        request.Poll(true, afterWork);
        CHECK(request.Result() == (afterWork < 15100 ? spawn::Result::Success : spawn::Result::Timeout));
        CHECK(request.Ready() == (afterWork < 15100 ? 2 : 1)); // expiry preserves only already-observed partial readiness
    }
    CHECK(spawn::ValidRequest(0, 1));
    CHECK(spawn::ValidRequest(4, 5));
    for (int type : {-1, 5, 999}) CHECK(!spawn::ValidRequest(type, 1));
    for (int count : {-1, 0, 6, 999}) CHECK(!spawn::ValidRequest(0, count));
    std::array<spawn::Point, spawn::MaxCount> points{};
    CHECK(spawn::Positions({10, 20, 30}, {1, 0, 0}, 1, points));
    CHECK(points[0].x == 360 && points[0].y == 20 && points[0].z == 30);
    CHECK(spawn::Positions({10, 20, 30}, {4, 0, 3}, 5, points));
    for (int i = 0; i < 5; ++i) {
        CHECK(points[i].x == 360 && points[i].z == 30);
        CHECK(points[i].y == 20 + (i - 2) * 160);
        if (i) CHECK(points[i].y - points[i - 1].y == 160);
    }
    CHECK(spawn::Positions({0, 0, 0}, {0, 2, 0}, 2, points));
    CHECK(points[0].y == 350 && points[1].y == 350 && points[0].x == 80 && points[1].x == -80);
    for (int count : {0, 6}) CHECK(!spawn::Positions({}, {1, 0, 0}, count, points));
    for (auto bad : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        CHECK(!spawn::Positions({bad, 0, 0}, {1, 0, 0}, 1, points));
        CHECK(!spawn::Positions({}, {1, bad, 0}, 1, points));
        CHECK(!spawn::Positions({}, {1, 0, bad}, 1, points));
    }
    CHECK(!spawn::Positions({}, {}, 1, points));
    CHECK(!spawn::Positions({}, {0, 0, 1}, 1, points));
    CHECK(!spawn::Positions({}, {1e-12, 0, 1}, 1, points));
    CHECK(!spawn::Positions({1e100, 0, 0}, {1, 0, 0}, 5, points));
    puts("Spawn request and placement tests passed");
}
