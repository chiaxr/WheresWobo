#pragma once

// The replaceable active-localization policy. Given the robot's belief (a
// point estimate plus whatever optional diagnostics the localizer offers), it
// chooses the next motion: a turn, a straight drive, and a pause to scan.
// Like the localizer, it never sees ground truth.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/messages.h"
#include "localization/localization_types.h"
#include "world/map2d.h"

namespace wobo {

// What the robot itself knows that a planner may use.
struct PlannerContext {
    const RgbLidarScan* last_scan = nullptr;  // most recent valid scan, if any
    float robot_radius_m = 0.2f;
    float nominal_max_range_m = 4.0f;         // the scanner's rated range
    Pose2D odom_pose;                         // dead-reckoned pose in the odometry frame
    double time_s = 0.0;
};

struct MotionPlan {
    enum class Kind { Explore, Disambiguate, Verify, Scan, Spin, BackOff };
    Kind kind = Kind::Scan;
    float turn_rad = 0.0f;
    float forward_m = 0.0f;
    float pause_s = 0.5f;
    float expected_gain = 0.0f;  // nats of expected uncertainty reduction
    std::string reason;          // short human-readable intent, for the UI
};

// One evaluated option, for the debug overlay.
struct PlanCandidate {
    float turn_rad = 0.0f;
    float forward_m = 0.0f;
    float gain = 0.0f;
    float score = 0.0f;
    bool chosen = false;
};

class IActivePlanner {
public:
    virtual ~IActivePlanner() = default;

    virtual std::string_view name() const = 0;
    virtual void reset(std::uint64_t seed) = 0;

    virtual MotionPlan choose_action(const Map2D& known_map, const PoseEstimate& estimate,
                                     const LocalizationDiagnostics* diagnostics,
                                     const PlannerContext& context) = 0;

    virtual const std::vector<PlanCandidate>& last_candidates() const = 0;
};

}  // namespace wobo
