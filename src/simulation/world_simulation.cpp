#include "simulation/world_simulation.h"

#include <algorithm>

namespace wobo {

Vec2 slide_until_blocked(const Map2D& map, Vec2 from, Vec2 to, float radius) {
    const float len = distance(from, to);
    if (len < 1e-6f || !map.circle_free(from.x, from.z, radius)) return from;
    const auto at = [&](float t) { return Vec2{from.x + (to.x - from.x) * t, from.z + (to.z - from.z) * t}; };
    const auto free = [&](float t) {
        const Vec2 p = at(t);
        return map.circle_free(p.x, p.z, radius);
    };
    // March in short steps, then narrow down the first one that hits.
    const float step = std::min(1.0f, 0.04f / len);
    float good = 0.0f;
    for (float t = step;; t += step) {
        t = std::min(t, 1.0f);
        if (!free(t)) {
            float bad = t;
            for (int i = 0; i < 10; ++i) {
                const float mid = 0.5f * (good + bad);
                (free(mid) ? good : bad) = mid;
            }
            return at(good);
        }
        good = t;
        if (t >= 1.0f) return to;
    }
}

SimConfig SimConfig::from(const Config& cfg) {
    SimConfig c;
    const float hz = cfg.get_float("sim.step_hz", 30.0f);
    c.step_s = 1.0 / (hz > 1.0f ? hz : 30.0f);
    c.dynamics = DynamicsConfig::from(cfg);
    c.lidar = LidarConfig::from(cfg);
    c.odometry = OdometryConfig::from(cfg);
    return c;
}

WorldSimulation::WorldSimulation(const Map2D& map, const SimConfig& config, std::uint64_t seed,
                                 Pose2D start)
    : map_(&map),
      config_(config),
      dynamics_(config.dynamics),
      lidar_(config.lidar),
      odometry_(config.odometry),
      sensor_rng_(derive_seed(seed, "sensor")),
      odometry_rng_(derive_seed(seed, "odometry")),
      pose_(start),
      prev_pose_(start),
      last_scan_pose_(start) {}

StepResult WorldSimulation::step(const MotionCommand& cmd, const SensorModifiers& mods, const ExternalPull* pull) {
    const float dt = static_cast<float>(config_.step_s);
    prev_pose_ = pose_;
    teleported_ = false;

    const DynamicsStep motion = dynamics_.step(*map_, pose_, cmd, dt);
    pose_ = motion.pose;
    if (pull && pull->distance_m > 0.0f) {
        const Vec2 here = pose_.position();
        const float d = distance(here, pull->to);
        if (d > 1e-4f) {
            const float k = std::min(1.0f, pull->distance_m / d);
            const Vec2 goal{here.x + (pull->to.x - here.x) * k, here.z + (pull->to.z - here.z) * k};
            const Vec2 p = slide_until_blocked(*map_, here, goal, config_.dynamics.radius_m);
            pose_.x = p.x;
            pose_.z = p.z;
        }
    }
    time_s_ += config_.step_s;
    ++steps_;

    StepResult out;
    out.bumped = motion.collided;
    out.odometry = odometry_.measure(motion.true_motion, dt, odometry_rng_, mods);

    if (time_s_ + 1e-9 >= next_scan_s_) {
        next_scan_s_ += 1.0 / static_cast<double>(config_.lidar.scan_hz);
        if (next_scan_s_ < time_s_) next_scan_s_ = time_s_;
        last_scan_ = lidar_.scan(*map_, pose_, time_s_, sensor_rng_, mods);
        last_scan_pose_ = pose_;
        out.scan = last_scan_;
    }
    return out;
}

bool WorldSimulation::teleport(const Pose2D& pose) {
    if (!map_->valid_pose(pose, config_.dynamics.radius_m)) return false;
    pose_ = pose;
    prev_pose_ = pose;  // no interpolation streak across the map
    dynamics_.stop();
    teleported_ = true;
    return true;
}

}  // namespace wobo
