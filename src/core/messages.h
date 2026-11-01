#pragma once

// Value-type messages that flow from the simulated sensors to the robot's
// software, and from the robot's software to its motors. These are the only
// things that cross the boundary between ground truth and the robot: nothing
// here carries a true pose.

#include <vector>

#include "core/types.h"

namespace wobo {

struct LidarReturn {
    float angle_rad = 0.0f;  // relative to robot heading
    float range_m = 0.0f;    // measured range (max range when !hit)
    Rgb rgb;                 // quantised / noisy surface colour
    bool hit = false;        // false for no return within max range
};

struct RgbLidarScan {
    double timestamp_s = 0.0;
    float max_range_m = 0.0f;  // the range the scanner was set to for this scan
    bool valid = true;         // false while the scanner is blacked out
    std::vector<LidarReturn> returns;
};

// Incremental motion reported by the wheel odometry, in the robot frame at
// the start of the interval.
struct OdometryDelta {
    float forward_m = 0.0f;
    float lateral_m = 0.0f;
    float delta_heading_rad = 0.0f;
    float dt_s = 0.0f;

    RelativeMotion motion() const { return {forward_m, lateral_m, delta_heading_rad}; }
};

inline OdometryDelta accumulate(const OdometryDelta& a, const OdometryDelta& b) {
    const RelativeMotion m = chain(a.motion(), b.motion());
    return {m.forward_m, m.lateral_m, m.delta_heading_rad, a.dt_s + b.dt_s};
}

struct MotionCommand {
    float linear_mps = 0.0f;
    float angular_rps = 0.0f;
};

}  // namespace wobo
