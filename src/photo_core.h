#pragma once
// In-session photo bookmarks. No engine objects, player actions or persistence.
#include <array>
#include <cmath>
#include <string>
#include "fov_core.h"

namespace photo {
struct Pose {
    std::array<double, 3> position{};
    std::array<double, 3> rotation{}; // pitch, yaw, roll, degrees
    float fov = 90;
    bool fovLocked = false; // restore the existing explicit FOV setting along with the view
};
struct Bookmark { std::string map; Pose pose; };
enum class Result { Saved, Deleted, RestoreRequested, InvalidSlot, CameraUnavailable,
                    MapUnavailable, Empty, WrongMap, InvalidPose, ReadFailed, ApplyFailed };
struct Backend {
    virtual ~Backend() = default;
    virtual bool Active() const = 0;
    virtual std::string Map() const = 0;
    virtual bool Read(Pose& pose) = 0;
    virtual bool Apply(const Pose& pose) = 0; // dispatch only; not gameplay acceptance
};
inline bool Valid(const Pose& pose) {
    for (double v : pose.position) if (!std::isfinite(v) || std::abs(v) > 100000000) return false;
    for (double v : pose.rotation) if (!std::isfinite(v) || std::abs(v) > 360000) return false;
    return fov::ValidAngle(pose.fov);
}
class Bookmarks {
public:
    const std::array<Bookmark, 3>& Slots() const { return slots_; }
    Result Save(int slot, Backend& camera) {
        if (slot < 0 || slot >= 3) return Result::InvalidSlot;
        if (!camera.Active()) return Result::CameraUnavailable;
        const std::string map = camera.Map();
        if (map.empty()) return Result::MapUnavailable;
        Pose pose;
        if (!camera.Read(pose)) return Result::ReadFailed;
        if (!Valid(pose)) return Result::InvalidPose;
        slots_[slot] = {map, pose};
        return Result::Saved;
    }
    Result Restore(int slot, Backend& camera) const {
        if (slot < 0 || slot >= 3) return Result::InvalidSlot;
        if (!camera.Active()) return Result::CameraUnavailable;
        const auto& mark = slots_[slot];
        if (mark.map.empty()) return Result::Empty;
        const std::string map = camera.Map();
        if (map.empty()) return Result::MapUnavailable;
        if (mark.map != map) return Result::WrongMap;
        if (!Valid(mark.pose)) return Result::InvalidPose;
        return camera.Apply(mark.pose) ? Result::RestoreRequested : Result::ApplyFailed;
    }
    Result Delete(int slot) {
        if (slot < 0 || slot >= 3) return Result::InvalidSlot;
        slots_[slot] = {};
        return Result::Deleted;
    }
private:
    std::array<Bookmark, 3> slots_{};
};
}
