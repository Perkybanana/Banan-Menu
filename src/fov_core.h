#pragma once
#include <cmath>

namespace fov {
inline constexpr float MinAngle = 30.f, MaxAngle = 130.f;
inline bool ValidAngle(double angle) { return std::isfinite(angle) && angle >= MinAngle && angle <= MaxAngle; }
// Zero releases the explicit camera lock; it is not a measured camera angle.
inline bool ValidSetting(double setting) { return setting == 0 || ValidAngle(setting); }
inline const char* DisplayFormat(float setting, const char* angleFormat, const char* defaultLabel) {
    return setting == 0 ? defaultLabel : angleFormat;
}
}
