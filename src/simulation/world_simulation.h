#pragma once

// Fixed-step orchestration of the physical world: the robot's true pose, its
// motion, and the sensor data it produces. Owns ground truth. The robot's
// software only ever sees the StepResult's odometry and scans.

#include <optional>

#include "core/config.h"
#include "core/messages.h"
#include "core/rng.h"
#include "simulation/dynamics.h"
#include "simulation/lidar.h"
#include "simulation/odometry.h"
#include "simulation/sensor_modifiers.h"
#include "world/map2d.h"

namespace wobo {

struct SimConfig {
    double step_s = 1.0 / 30.0;
    DynamicsConfig dynamics;
    LidarConfig lidar;
    OdometryConfig odometry;

    static SimConfig from(const Config& cfg);
};

struct StepResult {
    OdometryDelta odometry;              // this step's noisy motion measurement
    std::optional<RgbLidarScan> scan;    // present on steps where the scanner fires
    bool bumped = false;                 // contact sensor
};

class WorldSimulation {
public:
    WorldSimulation(const Map2D& map, const SimConfig& config, std::uint64_t seed, Pose2D start);

    StepResult step(const MotionCommand& cmd, const SensorModifiers& mods);

    // Kidnap: move the robot's true pose. Nothing is reported to the robot:
    // no odometry, no event. Returns false (and does nothing) if the pose is
    // not collision-free.
    bool teleport(const Pose2D& pose);

    const Map2D& map() const { return *map_; }
    const SimConfig& config() const { return config_; }
    const Pose2D& true_pose() const { return pose_; }
    const Pose2D& previous_pose() const { return prev_pose_; }
    double time_s() const { return time_s_; }
    std::uint64_t step_count() const { return steps_; }
    const RobotDynamics& dynamics() const { return dynamics_; }
    const OdometrySimulator& odometry() const { return odometry_; }
    // The most recent scan, as the robot received it (for visualisation).
    const RgbLidarScan& last_scan() const { return last_scan_; }
    // The true pose the most recent scan was taken from.
    const Pose2D& last_scan_pose() const { return last_scan_pose_; }
    bool teleported_this_step() const { return teleported_; }

private:
    const Map2D* map_;
    SimConfig config_;
    RobotDynamics dynamics_;
    RgbLidarSimulator lidar_;
    OdometrySimulator odometry_;
    Rng sensor_rng_;
    Rng odometry_rng_;

    Pose2D pose_;
    Pose2D prev_pose_;
    double time_s_ = 0.0;
    double next_scan_s_ = 0.0;
    std::uint64_t steps_ = 0;
    RgbLidarScan last_scan_;
    Pose2D last_scan_pose_;
    bool teleported_ = false;
};

}  // namespace wobo
