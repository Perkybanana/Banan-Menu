// Isolated state-machine tests: no Steam SDK or game process is loaded.
#include "../src/achievements_core.h"
#include <cstdio>
#include <cstdlib>
#include <unordered_set>
using namespace ach;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); std::exit(1); } } while (0)
struct Fake final : Backend {
    bool valid = true, requestOk = true, readOk = true, storeOk = true;
    int requests = 0, stores = 0;
    std::vector<Item> items{{"OLD", true}, {"A", false}, {"B", false}};
    std::vector<std::string> writes;
    std::unordered_set<std::string> rejected;
    bool Valid() override { return valid; }
    bool RequestStats() override { ++requests; return requestOk; }
    bool Read(std::vector<Item>& out) override { out = items; return readOk; }
    bool Set(const char* id) override {
        writes.emplace_back(id);
        if (rejected.contains(id)) return false;
        for (auto& i : items) if (i.id == id) i.unlocked = true;
        return true;
    }
    bool Store() override { ++stores; return storeOk; }
};
void Ready(Controller& c) { c.Refresh(100); c.Received(1); CHECK(c.Get().canUnlock); }
void ExplicitActionOnly() {
    Fake b; Controller c(b); c.Tick(100); c.Unlock(100);
    CHECK(b.writes.empty()); CHECK(b.requests == 0);
    c.Refresh(100); CHECK(c.Get().phase == Phase::Loading);
    c.Unlock(100); CHECK(b.writes.empty());
    c.Received(1); CHECK(c.Get().total == 3); CHECK(c.Get().unlocked == 1);
    CHECK(c.Get().canUnlock); CHECK(b.writes.empty());
}
void WaitForActualConfirmations() {
    Fake b; Controller c(b); Ready(c); c.Unlock(200);
    CHECK(b.writes == std::vector<std::string>({"A", "B"}));
    CHECK(b.stores == 1); CHECK(c.Get().phase == Phase::Saving);
    c.Stored(1); CHECK(c.Get().phase == Phase::Saving);
    c.Achievement("UNRELATED"); c.Achievement("A"); c.Achievement("A");
    CHECK(c.Get().confirmed == 1); CHECK(c.Get().phase == Phase::Saving);
    c.Achievement("B"); CHECK(c.Get().phase == Phase::Saved); CHECK(c.Get().unlocked == 3);
    c.Unlock(300); c.Refresh(300); CHECK(b.stores == 1); CHECK(b.writes.size() == 2);
}
void CallbackOrder() {
    Fake b; Controller c(b); Ready(c); c.Unlock(200);
    c.Achievement("B"); c.Achievement("A"); CHECK(c.Get().phase == Phase::Saving);
    c.Stored(1); CHECK(c.Get().phase == Phase::Saved);
}
void SessionLostBeforeWrite() {
    Fake b; Controller c(b); Ready(c); b.valid = false; c.Unlock(200);
    CHECK(b.writes.empty()); CHECK(c.Get().phase == Phase::Error);
}
void Unavailable() {
    Fake b; b.valid = false; Controller c(b); c.Refresh(100);
    CHECK(b.requests == 0); CHECK(c.Get().phase == Phase::Error);
}
void ReadFailureDoesNotWrite() {
    Fake b; Controller c(b); Ready(c); b.readOk = false; c.Unlock(200);
    CHECK(b.writes.empty()); CHECK(b.stores == 0);
}
void MalformedListDoesNotWrite() {
    for (auto list : {std::vector<Item>{{"A", false}, {"A", false}}, std::vector<Item>{{"", false}}}) {
        Fake b; b.items = list; Controller c(b); c.Refresh(100); c.Received(1); c.Unlock(200);
        CHECK(b.writes.empty()); CHECK(c.Get().phase == Phase::Error);
    }
}
void PartialFailure() {
    Fake b; b.rejected.insert("B"); Controller c(b); Ready(c); c.Unlock(200);
    CHECK(c.Get().accepted == 1); CHECK(c.Get().rejected == 1); CHECK(b.stores == 1);
    c.Achievement("A"); c.Stored(1);
    CHECK(c.Get().phase == Phase::Partial); CHECK(c.Get().unlocked == 2);
}
void NoFalseSuccessOnSaveFailure() {
    Fake b; b.storeOk = false; Controller c(b); Ready(c); c.Unlock(200);
    CHECK(c.Get().phase == Phase::Unconfirmed); CHECK(!c.Get().canUnlock);
    c.Unlock(300); CHECK(b.stores == 1);
}
void NoFalseSuccessOnServerRejection() {
    Fake b; Controller c(b); Ready(c); c.Unlock(200); c.Stored(8);
    CHECK(c.Get().phase == Phase::Unconfirmed);
}
void Timeouts() {
    Fake b; Controller c(b); c.Refresh(100); c.Tick(30101);
    CHECK(c.Get().phase == Phase::Error); CHECK(b.writes.empty());
    c.Refresh(40000); c.Received(1); c.Unlock(40100); c.Tick(70101);
    CHECK(c.Get().phase == Phase::Unconfirmed); CHECK(b.stores == 1);
    c.Achievement("A"); c.Achievement("B"); c.Stored(1);
    CHECK(c.Get().phase == Phase::Saved);
}
void AlreadyUnlocked() {
    Fake b; for (auto& i : b.items) i.unlocked = true;
    Controller c(b); c.Refresh(100); c.Received(1); c.Unlock(200);
    CHECK(c.Get().phase == Phase::AlreadyUnlocked); CHECK(b.writes.empty()); CHECK(b.stores == 0);
}
void ReadRequestErrors() {
    Fake b; b.requestOk = false; Controller c(b); c.Refresh(100);
    CHECK(c.Get().phase == Phase::Error); CHECK(b.writes.empty());
    b.requestOk = true; c.Refresh(200); c.Received(2);
    CHECK(c.Get().phase == Phase::Error); CHECK(!c.Get().canUnlock);
}
void AllRejected() {
    Fake b; b.rejected = {"A", "B"}; Controller c(b); Ready(c); c.Unlock(200);
    CHECK(c.Get().phase == Phase::Error); CHECK(b.stores == 0); CHECK(!c.Get().canUnlock);
}
int main() {
    ExplicitActionOnly(); WaitForActualConfirmations(); CallbackOrder(); SessionLostBeforeWrite();
    Unavailable(); ReadFailureDoesNotWrite(); MalformedListDoesNotWrite(); PartialFailure();
    NoFalseSuccessOnSaveFailure(); NoFalseSuccessOnServerRejection(); Timeouts();
    AlreadyUnlocked(); ReadRequestErrors(); AllRejected();
    std::puts("14 achievement scenarios passed; no real Steam calls.");
}

