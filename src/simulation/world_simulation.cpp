#include "simulation/world_simulation.h"

namespace wobo {

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

StepResult WorldSimulation::step(const MotionCommand& cmd, const SensorModifiers& mods) {
    const float dt = static_cast<float>(config_.step_s);
    prev_pose_ = pose_;
    teleported_ = false;

    const DynamicsStep motion = dynamics_.step(*map_, pose_, cmd, dt);
    pose_ = motion.pose;
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
