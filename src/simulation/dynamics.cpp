#include "simulation/dynamics.h"

#include <cmath>

namespace wobo {

DynamicsConfig DynamicsConfig::from(const Config& cfg) {
    DynamicsConfig c;
    c.radius_m = cfg.get_float("robot.radius", c.radius_m);
    c.max_speed_mps = cfg.get_float("robot.max_speed", c.max_speed_mps);
    c.max_turn_rps = cfg.get_float("robot.max_turn_rate", c.max_turn_rps);
    c.accel_mps2 = cfg.get_float("robot.accel", c.accel_mps2);
    c.turn_accel_rps2 = cfg.get_float("robot.turn_accel", c.turn_accel_rps2);
    return c;
}

namespace {
float approach(float current, float target, float max_delta) {
    if (target > current) return std::fmin(target, current + max_delta);
    return std::fmax(target, current - max_delta);
}
}  // namespace

DynamicsStep RobotDynamics::step(const Map2D& map, const Pose2D& pose, const MotionCommand& cmd,
                                 float dt_s) {
    const float v_target = clampf(cmd.linear_mps, -config_.max_speed_mps, config_.max_speed_mps);
    const float w_target = clampf(cmd.angular_rps, -config_.max_turn_rps, config_.max_turn_rps);
    v_ = approach(v_, v_target, config_.accel_mps2 * dt_s);
    w_ = approach(w_, w_target, config_.turn_accel_rps2 * dt_s);

    // Midpoint integration of the unicycle model.
    const float mid = pose.heading_rad + 0.5f * w_ * dt_s;
    Pose2D next{pose.x + v_ * std::cos(mid) * dt_s, pose.z + v_ * std::sin(mid) * dt_s,
                wrap_angle(pose.heading_rad + w_ * dt_s)};

    DynamicsStep out;
    const float r = config_.radius_m;
    if (!map.circle_free(next.x, next.z, r)) {
        out.collided = true;
        // Slide along whichever axis is still free; otherwise stay put.
        if (map.circle_free(next.x, pose.z, r)) {
            next.z = pose.z;
        } else if (map.circle_free(pose.x, next.z, r)) {
            next.x = pose.x;
        } else {
            next.x = pose.x;
            next.z = pose.z;
        }
        v_ *= 0.3f;
    }
    out.pose = next;
    out.true_motion = between(pose, next);
    return out;
}

}  // namespace wobo
