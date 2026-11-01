#pragma once

// True planar robot motion: unicycle kinematics with acceleration limits and
// circle-vs-grid collision (sliding along walls where possible).

#include "core/config.h"
#include "core/messages.h"
#include "world/map2d.h"

namespace wobo {

struct DynamicsConfig {
    float radius_m = 0.2f;
    float max_speed_mps = 0.55f;
    float max_turn_rps = 1.6f;
    float accel_mps2 = 1.5f;
    float turn_accel_rps2 = 6.0f;

    static DynamicsConfig from(const Config& cfg);
};

struct DynamicsStep {
    Pose2D pose;
    RelativeMotion true_motion;  // in the frame of the starting pose
    bool collided = false;
};

class RobotDynamics {
public:
    explicit RobotDynamics(const DynamicsConfig& config) : config_(config) {}

    const DynamicsConfig& config() const { return config_; }

    DynamicsStep step(const Map2D& map, const Pose2D& pose, const MotionCommand& cmd, float dt_s);

    float linear_velocity() const { return v_; }
    float angular_velocity() const { return w_; }
    void stop() { v_ = 0.0f; w_ = 0.0f; }

private:
    DynamicsConfig config_;
    float v_ = 0.0f;
    float w_ = 0.0f;
};

}  // namespace wobo
