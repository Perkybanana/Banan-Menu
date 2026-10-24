// Catch wrong-map/disabled dispatch, lost orientation/FOV, and corrupt pose acceptance.
#include "../src/photo_core.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace photo;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); std::exit(1); } } while (0)
struct Camera final : Backend {
    bool active = true, readOk = true, applyOk = true;
    std::string map = "/Game/Maps/One.One";
    Pose pose{{10, 20, 30}, {15, -45, 0}, 75};
    int writes = 0;
    bool Active() const override { return active; }
    std::string Map() const override { return map; }
    bool Read(Pose& out) override { if (!readOk) return false; out = pose; return true; }
    bool Apply(const Pose& value) override { ++writes; if (!applyOk) return false; pose = value; return true; }
};
void SaveRestoreAndDeleteAllSlots() {
    Bookmarks book; Camera c;
    for (int slot = 0; slot < 3; ++slot) {
        c.pose = {{10, 20, 30}, {15, -45, 3}, 75, slot == 1};
        CHECK(book.Save(slot, c) == Result::Saved);
        c.pose = {{100, 200, 300}, {-20, 60, 0}, 100};
        CHECK(book.Restore(slot, c) == Result::RestoreRequested);
        CHECK((c.pose.position == std::array<double, 3>{10, 20, 30}));
        CHECK((c.pose.rotation == std::array<double, 3>{15, -45, 3}));
        CHECK(c.pose.fov == 75);
        CHECK(c.pose.fovLocked == (slot == 1));
        CHECK(book.Delete(slot) == Result::Deleted);
        CHECK(book.Restore(slot, c) == Result::Empty);
    }
    CHECK(c.writes == 3);
}
void RejectUnavailableAndWrongMapWithoutWrites() {
    Bookmarks book; Camera c;
    CHECK(book.Save(0, c) == Result::Saved);
    c.active = false;
    CHECK(book.Save(1, c) == Result::CameraUnavailable);
    CHECK(book.Restore(0, c) == Result::CameraUnavailable);
    c.active = true; c.map = "/Game/Other/One.One"; // same short name is not the same map
    CHECK(book.Restore(0, c) == Result::WrongMap);
    c.map.clear();
    CHECK(book.Save(1, c) == Result::MapUnavailable);
    CHECK(book.Restore(0, c) == Result::MapUnavailable);
    CHECK(c.writes == 0 && book.Slots()[1].map.empty());
}
void InvalidSlotsAndFailuresDoNotReplaceSavedPose() {
    Bookmarks book; Camera c;
    CHECK(book.Save(0, c) == Result::Saved);
    for (int slot : {-1, 3, 999}) {
        CHECK(book.Save(slot, c) == Result::InvalidSlot);
        CHECK(book.Restore(slot, c) == Result::InvalidSlot);
        CHECK(book.Delete(slot) == Result::InvalidSlot);
    }
    c.readOk = false;
    CHECK(book.Save(0, c) == Result::ReadFailed);
    c.readOk = true; c.pose = {};
    CHECK(book.Restore(0, c) == Result::RestoreRequested && c.pose.fov == 75);
    c.applyOk = false;
    CHECK(book.Restore(0, c) == Result::ApplyFailed);
    c.active = false;
    CHECK(book.Delete(0) == Result::Deleted); // deleting never needs a camera
}
void CorruptValuesAreRejected() {
    Bookmarks book; Camera c;
    CHECK(book.Save(0, c) == Result::Saved);
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto inf = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        for (double value : {nan, inf, -inf, 100000001.0, -100000001.0}) {
            c.pose = {}; c.pose.position[axis] = value;
            CHECK(book.Save(0, c) == Result::InvalidPose);
        }
        for (double value : {nan, inf, -inf, 360001.0, -360001.0}) {
            c.pose = {}; c.pose.rotation[axis] = value;
            CHECK(book.Save(0, c) == Result::InvalidPose);
        }
    }
    for (float value : {float(nan), float(inf), -float(inf), 0.f, 29.9f, 130.1f}) {
        c.pose = {}; c.pose.fov = value;
        CHECK(book.Save(0, c) == Result::InvalidPose);
    }
    c.pose = {};
    CHECK(book.Restore(0, c) == Result::RestoreRequested && c.pose.fov == 75);
    CHECK(c.writes == 1); // no invalid save caused an engine dispatch
    // Simulate a damaged stored pose: restore must validate again before dispatch.
    auto& corrupted = const_cast<Bookmark&>(book.Slots()[0]);
    corrupted.pose.rotation[1] = nan;
    CHECK(book.Restore(0, c) == Result::InvalidPose && c.writes == 1);
    for (float angle : {30.f, 49.f, 130.f}) {
        c.pose = {{100000000, -100000000, 0}, {360000, -360000, 0}, angle, true};
        CHECK(book.Save(2, c) == Result::Saved);
        c.pose = {};
        CHECK(book.Restore(2, c) == Result::RestoreRequested && c.pose.fov == angle && c.pose.fovLocked);
    }
}
void FovDisplayShowsEveryLockedAngle() {
    CHECK(std::string(fov::DisplayFormat(0, "%.0f degrees", "Game default")) == "Game default");
    for (float angle : {30.f, 35.f, 49.f, 130.f}) {
        CHECK(fov::ValidSetting(angle));
        char shown[32];
        std::snprintf(shown, sizeof(shown), fov::DisplayFormat(angle, "%.0f degrees", "Game default"), angle);
        CHECK(std::string(shown) == std::to_string(int(angle)) + " degrees");
    }
    CHECK(fov::ValidSetting(0) && !fov::ValidAngle(0));
    for (double invalid : {1., 29., 130.1, std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity()}) CHECK(!fov::ValidSetting(invalid));
}
int main() {
    SaveRestoreAndDeleteAllSlots();
    RejectUnavailableAndWrongMapWithoutWrites();
    InvalidSlotsAndFailuresDoNotReplaceSavedPose();
    CorruptValuesAreRejected();
    FovDisplayShowsEveryLockedAngle();
    puts("Photo bookmark tests passed");
}
