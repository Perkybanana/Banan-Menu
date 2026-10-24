// Exercises the production reconciliation algorithm without a game process.
#include "../src/resources_core.h"
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <vector>
using namespace resources;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); std::exit(1); } } while (0)
struct Inventory final : Backend {
    std::vector<int> stacks{3, 4, 5};
    int mutations = 0, reads = 0, failRead = -1, failMutation = -1;
    int capacity = INT_MAX, addLimit = INT_MAX, removeLimit = INT_MAX, movement = 0;
    int expectedMaterial = 0;
    bool stackOk = true, noProgress = false, negativeRead = false;
    bool Read(int material, int& total) override {
        if (material != expectedMaterial) return false;
        if (++reads == failRead) return false;
        total = 0;
        for (int n : stacks) total += n;
        if (negativeRead) total = -1;
        return true;
    }
    bool Add(int material, int n) override {
        if (material != expectedMaterial) return false;
        if (++mutations == failMutation) return false;
        int total = 0;
        for (int v : stacks) total += v;
        if (!noProgress) stacks.push_back(std::min({n, capacity - total, addLimit}) + movement);
        return true;
    }
    bool CurrentStack(int material, int& count) override {
        if (material != expectedMaterial) return false;
        count = stacks.empty() ? 0 : stacks.front();
        return stackOk;
    }
    bool Remove(int material, int n) override {
        if (material != expectedMaterial) return false;
        if (++mutations == failMutation) return false;
        if (!noProgress) {
            stacks.front() -= std::min(n, removeLimit) + movement;
            if (stacks.front() == 0) stacks.erase(stacks.begin());
        }
        return true;
    }
};
void ExactTotals() {
    Inventory b;
    auto r = Reconcile(b, 0, 25);
    CHECK(r.status == Status::Success && r.observed == 25);
    r = Reconcile(b, 0, 2);
    CHECK(r.status == Status::Success && r.observed == 2);
    int writes = b.mutations;
    r = Reconcile(b, 0, 2);
    CHECK(r.status == Status::Success && r.observed == 2 && b.mutations == writes);
    r = Reconcile(b, 0, 0);
    CHECK(r.status == Status::Success && r.observed == 0 && b.stacks.empty());
}
void AllMaterialsAndUpperBoundary() {
    for (int i = 0; i < 6; ++i) {
        Inventory b;
        b.expectedMaterial = i;
        auto r = Reconcile(b, i, 9999);
        CHECK(r.status == Status::Success && r.observed == 9999);
        r = Reconcile(b, i, 0);
        CHECK(r.status == Status::Success && r.observed == 0);
    }
}
void InvalidRequestsNeverReadOrWrite() {
    for (auto p : {std::pair{-1, 1}, {6, 1}, {INT_MAX, 1}, {0, -1}, {0, 10000}, {0, INT_MAX}}) {
        Inventory b;
        CHECK(Reconcile(b, p.first, p.second).status == Status::InvalidInput);
        CHECK(b.reads == 0 && b.mutations == 0);
    }
}
void ReadFailures() {
    Inventory b; b.failRead = 1;
    CHECK(Reconcile(b, 0, 30).status == Status::ReadFailed && b.mutations == 0);
    b = Inventory{}; b.failRead = 2;
    auto r = Reconcile(b, 0, 30);
    CHECK(r.status == Status::ReadFailed && r.observed == -1);
    b = Inventory{}; b.negativeRead = true;
    CHECK(Reconcile(b, 0, 30).status == Status::ReadFailed && b.mutations == 0);
}
void NoProgressAndCapacity() {
    Inventory b; b.noProgress = true;
    CHECK(Reconcile(b, 0, 30).status == Status::NoProgress && b.mutations == 1);
    b = Inventory{}; b.capacity = 18;
    auto r = Reconcile(b, 0, 30);
    CHECK(r.status == Status::NoProgress && r.observed == 18 && b.mutations == 2);
    b = Inventory{}; b.noProgress = true;
    CHECK(Reconcile(b, 0, 0).status == Status::NoProgress && b.mutations == 1);
}
void PartialProgressAndFailures() {
    Inventory b; b.addLimit = 2;
    CHECK(Reconcile(b, 0, 19).status == Status::Success && b.mutations == 4);
    b = Inventory{}; b.failMutation = 2;
    auto r = Reconcile(b, 0, 0);
    CHECK(r.status == Status::MutationFailed && r.observed == 9 && b.mutations == 2);
    b = Inventory{}; b.addLimit = 2; b.failMutation = 2;
    r = Reconcile(b, 0, 20);
    CHECK(r.status == Status::MutationFailed && r.observed == 14 && b.mutations == 2);
    b = Inventory{}; b.stackOk = false;
    CHECK(Reconcile(b, 0, 0).status == Status::StackUnavailable && b.mutations == 0);
    b = Inventory{}; b.stacks = {0, 12};
    CHECK(Reconcile(b, 0, 0).status == Status::StackUnavailable && b.mutations == 0);
}
void UnexpectedMovementStops() {
    Inventory b; b.movement = 1;
    CHECK(Reconcile(b, 0, 20).status == Status::UnexpectedMovement && b.mutations == 1);
    b = Inventory{}; b.movement = 1;
    CHECK(Reconcile(b, 0, 0).status == Status::UnexpectedMovement && b.mutations == 1);
    b = Inventory{}; b.removeLimit = 1;
    CHECK(Reconcile(b, 0, 0).status == Status::UnexpectedMovement && b.mutations == 1);
}
void BoundedAttempts() {
    Inventory b; b.addLimit = 1;
    auto r = Reconcile(b, 0, 9999);
    CHECK(r.status == Status::AttemptsExceeded && r.observed > 12 && r.observed < 9999);
    CHECK(b.mutations < 1000);
}
void DecimalInputDoesNotWrapOrClamp() {
    int n = 42;
    for (const char* text : {"", "-1", "+2", "10000", "999999999999999999999999999", "2x", "2.0", " 2", "2 "}) {
        CHECK(!ParseTarget(text, n) && n == 42);
    }
    CHECK(ParseTarget("0", n) && n == 0);
    CHECK(ParseTarget("9999", n) && n == 9999);
    CHECK(ParseTarget("0008", n) && n == 8);
}
struct CarriedWriter final : StackWriter {
    int stack = 3, otherStack = 5, stash = 100;
    int creates = 0, writes = 0, refreshes = 0, defaultAmount = 0;
    bool found = true, readOk = true, selectOk = true, createOk = true, writeOk = true, refreshOk = true;
    bool changeDuringSelect = false, changeOtherDuringCreate = false;
    bool ReadTotal(int& total) override { total = stack + otherStack; return readOk; }
    bool SelectStack(bool& selected) override {
        selected = found;
        if (changeDuringSelect) stack += 1;
        return selectOk;
    }
    bool CreateZeroStack() override {
        ++creates;
        stack = defaultAmount;
        if (changeOtherDuringCreate) otherStack = 2;
        return createOk;
    }
    bool ReadStack(int& count) override { count = stack; return readOk; }
    bool WriteStack(int count) override { ++writes; if (writeOk) stack = count; return writeOk; }
    bool RefreshStack() override { ++refreshes; return refreshOk; }
};
void CarriedStackAdditions() {
    CarriedWriter b;
    CHECK(AddToStack(b, 9991));
    CHECK(b.stack == 9994 && b.otherStack == 5 && b.stash == 100 && b.creates == 0 && b.refreshes == 1);
    b = CarriedWriter{}; b.found = false; b.stack = b.otherStack = 0;
    CHECK(AddToStack(b, 9999));
    CHECK(b.stack == 9999 && b.creates == 1 && b.refreshes == 1 && b.stash == 100);
    b = CarriedWriter{}; b.found = false;
    CHECK(!AddToStack(b, 1) && b.creates == 0 && b.writes == 0);
    b = CarriedWriter{}; b.found = false; b.stack = b.otherStack = 0; b.defaultAmount = 1;
    CHECK(!AddToStack(b, 2) && b.writes == 0);
    b = CarriedWriter{}; b.found = false; b.stack = b.otherStack = 0; b.changeOtherDuringCreate = true;
    CHECK(!AddToStack(b, 2) && b.writes == 0);
    b = CarriedWriter{}; b.changeDuringSelect = true;
    CHECK(!AddToStack(b, 2) && b.writes == 0);
    b = CarriedWriter{}; b.refreshOk = false;
    CHECK(!AddToStack(b, 2) && b.stack == 5 && b.refreshes == 1);
    b = CarriedWriter{}; b.readOk = false;
    CHECK(!AddToStack(b, 2) && b.writes == 0 && b.creates == 0);
    b = CarriedWriter{}; b.selectOk = false;
    CHECK(!AddToStack(b, 2) && b.writes == 0 && b.creates == 0);
    b = CarriedWriter{}; b.writeOk = false;
    CHECK(!AddToStack(b, 2) && b.stack == 3 && b.refreshes == 0);
    b = CarriedWriter{}; b.found = false; b.stack = b.otherStack = 0; b.createOk = false;
    CHECK(!AddToStack(b, 2) && b.creates == 1 && b.writes == 0);
    for (int n : {-1, 0, 10000, INT_MAX}) {
        b = CarriedWriter{};
        CHECK(!AddToStack(b, n) && b.writes == 0 && b.creates == 0);
    }
    b = CarriedWriter{};
    CHECK(!AddToStack(b, 9992) && b.writes == 0);
}
int main() {
    ExactTotals(); AllMaterialsAndUpperBoundary(); InvalidRequestsNeverReadOrWrite(); ReadFailures();
    NoProgressAndCapacity(); PartialProgressAndFailures(); UnexpectedMovementStops(); BoundedAttempts();
    DecimalInputDoesNotWrapOrClamp();
    CarriedStackAdditions();
    std::puts("resources_test: 10 test groups passed");
}
