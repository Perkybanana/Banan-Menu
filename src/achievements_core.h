#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_set>

namespace ach {
enum class Phase { Idle, Loading, Ready, Saving, Saved, Partial, AlreadyUnlocked, Error, Unconfirmed };
struct Item { std::string id; bool unlocked = false; };
struct Snapshot {
    Phase phase = Phase::Idle;
    const char* message = "Check Steam achievements to begin.";
    unsigned total = 0, unlocked = 0, accepted = 0, confirmed = 0, rejected = 0;
    bool canUnlock = false;
};
struct Backend {
    virtual ~Backend() = default;
    virtual bool Valid() = 0;
    virtual bool RequestStats() = 0;
    virtual bool Read(std::vector<Item>& items) = 0;
    virtual bool Set(const char* id) = 0;
    virtual bool Store() = 0;
};
class Controller {
public:
    explicit Controller(Backend& backend) : api_(backend) {}
    const Snapshot& Get() const { return state_; }
    void Refresh(uint64_t now) {
        if (attempted_ || Busy()) return;
        state_ = {};
        if (!api_.Valid()) return Fail("Steam is unavailable. Launch this game through Steam and sign in.");
        state_.phase = Phase::Loading;
        state_.message = "Waiting for Steam achievements...";
        deadline_ = now + 30000;
        if (!api_.RequestStats()) Fail("Steam could not request achievements. Try again later.");
    }
    void Received(int result) {
        if (state_.phase != Phase::Loading) return;
        if (result != 1) return Fail("Steam could not load achievements. Try again later.");
        std::vector<Item> items;
        if (!Read(items)) return;
        Count(items);
        state_.phase = state_.unlocked == state_.total ? Phase::AlreadyUnlocked : Phase::Ready;
        state_.canUnlock = state_.phase == Phase::Ready;
        state_.message = state_.canUnlock ? "Ready. Nothing has been changed." : "All achievements are already unlocked.";
    }
    void Unlock(uint64_t now) {
        if (!state_.canUnlock || attempted_ || state_.phase != Phase::Ready) return;
        std::vector<Item> items;
        if (!Read(items)) return; // validate the entire list and session before the first write
        Count(items);
        state_.canUnlock = false;
        if (state_.unlocked == state_.total) {
            state_.phase = Phase::AlreadyUnlocked;
            state_.message = "All achievements are already unlocked.";
            return;
        }
        attempted_ = true; // never repeat writes automatically, including after an uncertain result
        initialUnlocked_ = state_.unlocked;
        state_.phase = Phase::Saving;
        state_.message = "Waiting for Steam to confirm the unlock...";
        deadline_ = now + 30000;
        for (const auto& item : items) {
            if (item.unlocked) continue;
            if (api_.Set(item.id.c_str())) pending_.insert(item.id);
            else ++state_.rejected;
        }
        state_.accepted = static_cast<unsigned>(pending_.size());
        if (pending_.empty()) return Fail("Steam rejected the unlock request.");
        if (!api_.Store()) Unconfirmed("Save not confirmed. Changes may sync later; check your Steam profile.");
    }
    void Stored(int result) {
        if (!Awaiting()) return;
        if (result != 1) {
            storeAcknowledged_ = false;
            Unconfirmed("Steam reported a save error. Changes may sync later; check your Steam profile.");
            return;
        }
        storeAcknowledged_ = true;
        FinishIfConfirmed();
    }
    void Achievement(const std::string& id) {
        if (!Awaiting() || !pending_.contains(id)) return;
        confirmed_.insert(id);
        state_.confirmed = static_cast<unsigned>(confirmed_.size());
        state_.unlocked = initialUnlocked_ + state_.confirmed;
        FinishIfConfirmed();
    }
    void Tick(uint64_t now) {
        if (now < deadline_) return;
        if (state_.phase == Phase::Loading) Fail("Steam did not respond. Try checking again.");
        else if (state_.phase == Phase::Saving)
            Unconfirmed("Save not confirmed. Changes may sync later; check your Steam profile.");
    }
private:
    bool Busy() const { return state_.phase == Phase::Loading || state_.phase == Phase::Saving; }
    bool Awaiting() const {
        return !pending_.empty() && (state_.phase == Phase::Saving || state_.phase == Phase::Unconfirmed);
    }
    void Fail(const char* message) { state_.phase = Phase::Error; state_.message = message; state_.canUnlock = false; }
    void Unconfirmed(const char* message) {
        state_.phase = Phase::Unconfirmed; state_.message = message; state_.canUnlock = false;
    }
    bool Read(std::vector<Item>& items) {
        if (!api_.Valid()) {
            Fail("Steam is unavailable. Launch this game through Steam and sign in.");
            return false;
        }
        if (!api_.Read(items) || items.empty() || items.size() > 4096) {
            Fail("Could not read the complete achievement list. Nothing was changed.");
            return false;
        }
        std::unordered_set<std::string> ids;
        for (const auto& item : items) {
            if (item.id.empty() || item.id.size() >= 128 || !ids.insert(item.id).second) {
                Fail("Could not read the complete achievement list. Nothing was changed.");
                return false;
            }
        }
        return true;
    }
    void Count(const std::vector<Item>& items) {
        state_.total = static_cast<unsigned>(items.size());
        state_.unlocked = 0;
        for (const auto& i : items) if (i.unlocked) ++state_.unlocked;
    }
    void FinishIfConfirmed() {
        if (!storeAcknowledged_ || confirmed_.size() != pending_.size()) return;
        state_.phase = state_.rejected ? Phase::Partial : Phase::Saved;
        state_.message = state_.rejected ? "Steam confirmed some unlocks; other achievements were rejected."
                                        : "Steam confirmed all requested unlocks.";
    }
    Backend& api_;
    Snapshot state_;
    uint64_t deadline_ = 0;
    unsigned initialUnlocked_ = 0;
    bool attempted_ = false, storeAcknowledged_ = false;
    std::unordered_set<std::string> pending_, confirmed_;
};
}

