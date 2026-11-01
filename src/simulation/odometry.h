#pragma once

// Noisy wheel odometry. The simulator knows the robot's true incremental
// motion; this turns it into what the wheel encoders and gyro report.
// Noise variance grows with distance and rotation (not with the number of
// steps), so results do not depend on the simulation rate. A slowly
// wandering bias models drift.

#include "core/config.h"
#include "core/messages.h"
#include "core/rng.h"
#include "simulation/sensor_modifiers.h"

namespace wobo {

struct OdometryConfig {
    // Variance per metre / per radian of motion.
    float forward_var_per_m = 0.0025f;      // ~5 cm stddev over 1 m
    float lateral_var_per_m = 0.0006f;
    float turn_var_per_rad = 0.0016f;       // ~2.3 deg stddev over 1 rad
    float turn_var_per_m = 0.0008f;
    // Bias random walk (rad per metre), and how far it may wander.
    float drift_walk = 0.002f;
    float drift_limit = 0.03f;

    static OdometryConfig from(const Config& cfg);
};

class OdometrySimulator {
public:
    explicit OdometrySimulator(const OdometryConfig& config) : config_(config) {}

    const OdometryConfig& config() const { return config_; }

    // `truth` is the true robot-frame motion over dt_s.
    OdometryDelta measure(const RelativeMotion& truth, float dt_s, Rng& rng,
                          const SensorModifiers& mods);

    float drift_bias() const { return drift_bias_; }

private:
    OdometryConfig config_;
    float drift_bias_ = 0.0f;  // rad per metre, slowly varying
};

}  // namespace wobo
