#pragma once

// Wobo's software: a localizer, an active planner, and the controller that
// turns plans into motor commands. It is fed exactly what a real robot gets
// -- per-step odometry, scans when the scanner fires, and a bump sensor --
// and never touches the simulator's ground truth.

#include <memory>
#include <optional>
#include <string>

#include "core/config.h"
#include "core/messages.h"
#include "localization/localizer.h"
#include "planning/planner.h"

namespace wobo {

// How Wobo feels about its belief, for its face and the HUD. Derived only
// from the robot's own state.
enum class RobotMood { Curious, Thinking, Hopeful, Confident, Confused, ShortSighted, Blind };

const char* mood_name(RobotMood mood);

struct AgentConfig {
    float radius_m = 0.2f;
    float max_speed_mps = 0.55f;
    float max_turn_rps = 1.6f;
    float nominal_max_range_m = 4.0f;
    float front_stop_m = 0.12f;          // stop if something is this close ahead (beyond radius)
    float confusion_threshold = 0.45f;   // consistency below this triggers a re-check spin
    float confusion_cooldown_s = 10.0f;
    float cautious_speed = 0.7f;         // speed factor when the scanner is degraded
    float boot_s = 3.0f;                 // seconds spent waking up before sensing or moving

    static AgentConfig from(const Config& cfg);
};

struct AgentInput {
    OdometryDelta odometry;
    const RgbLidarScan* scan = nullptr;  // present when the scanner fired this step
    bool bumped = false;
    double time_s = 0.0;
};

class RobotAgent {
public:
    RobotAgent(std::unique_ptr<ILocalizer> localizer, std::unique_ptr<IActivePlanner> planner,
               const AgentConfig& config);

    void reset(const Map2D& known_map, const InitialBelief& prior, std::uint64_t seed);

    MotionCommand update(const AgentInput& input);
    // How much compute Wobo has: the localizer's share of its full budget,
    // and how often per second it runs (0: on every scan).
    void set_budget(float share, float update_hz);
    // How sharply its localizer re-seeds from the map's views (0: off).
    void set_view_injection(float temperature) { localizer_->set_view_injection(temperature); }

    PoseEstimate estimate() const { return localizer_->estimate(); }
    const LocalizationDiagnostics* diagnostics() const { return localizer_->diagnostics(); }
    const ILocalizer& localizer() const { return *localizer_; }
    const IActivePlanner& planner() const { return *planner_; }

    RobotMood mood() const { return mood_; }
    const MotionPlan& plan() const { return plan_; }
    // What is left of the current plan, in the robot frame (turn then drive).
    RelativeMotion remaining_motion() const;
    bool moving() const { return phase_ == Phase::Turn || phase_ == Phase::Drive; }
    int spins() const { return spins_; }

private:
    enum class Phase { Idle, Turn, Drive, Pause, Wait };

    void start_plan(const MotionPlan& plan);
    void update_mood(double time_s);

    std::unique_ptr<ILocalizer> localizer_;
    std::unique_ptr<IActivePlanner> planner_;
    AgentConfig config_;
    const Map2D* map_ = nullptr;

    OdometryDelta pending_odometry_;     // since the last localizer update
    float update_interval_s_ = 0.0f;     // 0: every scan
    double last_update_s_ = -1e9;
    Pose2D odom_pose_;                   // dead reckoning, odometry frame
    std::optional<RgbLidarScan> last_scan_;
    bool blind_ = false;

    Phase phase_ = Phase::Idle;
    MotionPlan plan_;
    float turned_ = 0.0f;
    float driven_ = 0.0f;
    float pause_left_ = 0.0f;
    double last_spin_s_ = -1e9;
    double confused_until_s_ = -1.0;
    int spins_ = 0;
    RobotMood mood_ = RobotMood::Curious;
};

}  // namespace wobo
