#pragma once

// Simulated colourized 2D LiDAR: a planar range scanner whose returns carry
// the RGB colour of the surface each beam hit. This is a game abstraction for
// a range scan paired with aligned camera colour, not a claim that a
// single-channel 2D LiDAR measures colour.

#include "core/config.h"
#include "core/messages.h"
#include "core/rng.h"
#include "simulation/sensor_modifiers.h"
#include "world/map2d.h"

namespace wobo {

struct LidarConfig {
    int beams = 36;            // evenly spaced over 360 degrees
    float max_range_m = 4.0f;
    float scan_hz = 5.0f;
    float range_noise_m = 0.02f;
    float colour_noise = 8.0f;  // per-channel stddev, 0..255
    int colour_levels = 8;      // quantisation levels per channel
    float dropout = 0.02f;

    static LidarConfig from(const Config& cfg);
};

class RgbLidarSimulator {
public:
    explicit RgbLidarSimulator(const LidarConfig& config) : config_(config) {}

    const LidarConfig& config() const { return config_; }

    // Scan from the robot's *true* pose.
    RgbLidarScan scan(const Map2D& map, const Pose2D& true_pose, double time_s, Rng& rng,
                      const SensorModifiers& mods) const;

    // Colour processing alone (exposed for tests).
    Rgb measure_colour(Rgb surface, Rng& rng, const SensorModifiers& mods) const;

private:
    Rgb measure_colour(Rgb surface, Rng& rng, const SensorModifiers& mods, bool repainted) const;

    LidarConfig config_;
};

// Beam angle (robot frame) for beam i of n.
inline float beam_angle(int i, int n) { return wrap_angle(kTwoPi * static_cast<float>(i) / n); }

}  // namespace wobo
