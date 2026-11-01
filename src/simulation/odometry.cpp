#include "simulation/odometry.h"

#include <cmath>

namespace wobo {

OdometryConfig OdometryConfig::from(const Config& cfg) {
    OdometryConfig c;
    c.forward_var_per_m = cfg.get_float("odometry.forward_var_per_m", c.forward_var_per_m);
    c.lateral_var_per_m = cfg.get_float("odometry.lateral_var_per_m", c.lateral_var_per_m);
    c.turn_var_per_rad = cfg.get_float("odometry.turn_var_per_rad", c.turn_var_per_rad);
    c.turn_var_per_m = cfg.get_float("odometry.turn_var_per_m", c.turn_var_per_m);
    c.drift_walk = cfg.get_float("odometry.drift_walk", c.drift_walk);
    c.drift_limit = cfg.get_float("odometry.drift_limit", c.drift_limit);
    return c;
}

OdometryDelta OdometrySimulator::measure(const RelativeMotion& truth, float dt_s, Rng& rng,
                                         const SensorModifiers& mods) {
    const float dist = std::fabs(truth.forward_m) + std::fabs(truth.lateral_m);
    const float rot = std::fabs(truth.delta_heading_rad);
    const float k = mods.odom_noise_scale * mods.odom_noise_scale;  // scale stddev
    const float kd = k * mods.odom_distance_noise_scale * mods.odom_distance_noise_scale;
    const float kt = k * mods.odom_turn_noise_scale * mods.odom_turn_noise_scale;
    const bool moving = dist > 1e-5f || rot > 1e-5f;

    // The drift bias wanders with distance travelled.
    if (dist > 0.0f && config_.drift_walk > 0.0f) {
        drift_bias_ += rng.normal(0.0f, config_.drift_walk * std::sqrt(dist));
        drift_bias_ = clampf(drift_bias_, -config_.drift_limit, config_.drift_limit);
    }

    OdometryDelta out;
    out.dt_s = dt_s;
    out.forward_m = truth.forward_m * (1.0f + mods.odom_scale_bias) +
                    rng.normal(0.0f, std::sqrt(kd * config_.forward_var_per_m * dist));
    out.lateral_m = truth.lateral_m +
                    rng.normal(0.0f, std::sqrt(kd * config_.lateral_var_per_m * dist));
    out.delta_heading_rad =
        truth.delta_heading_rad * (1.0f + mods.odom_turn_scale_bias) +
        rng.normal(0.0f, std::sqrt(kt * (config_.turn_var_per_rad * rot + config_.turn_var_per_m * dist))) +
        (drift_bias_ + mods.odom_turn_bias) * dist +
        (moving ? mods.odom_heading_bias_rps * dt_s : 0.0f);
    return out;
}

}  // namespace wobo
