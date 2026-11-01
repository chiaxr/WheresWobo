#pragma once

// Player interference, expressed as changes to the *measurement* paths. The
// simulator applies these when it generates sensor data; they never touch the
// robot's true motion (teleport is separate and explicit). Defaults mean "no
// interference".

#include "core/types.h"

namespace wobo {

struct SensorModifiers {
    // --- LiDAR ---
    float range_scale = 1.0f;        // multiplies the scanner's max range
    float range_noise_add = 0.0f;    // extra range noise stddev (m)
    float colour_noise_add = 0.0f;   // extra per-channel colour noise (0..255)
    float colour_desaturate = 0.0f;  // 0 = true colour, 1 = fully grey
    float colour_hue_shift = 0.0f;   // 0..1, rotates RGB channels towards (G, B, R)
    float dropout_add = 0.0f;        // extra probability a beam returns nothing
    bool blackout = false;           // scanner produces no usable data

    // Ghost echoes: this fraction of beams report what the scanner would see
    // from a decoy pose instead of the real one.
    float ghost_fraction = 0.0f;
    RelativeMotion ghost_offset;     // decoy pose relative to the true pose

    // --- Odometry ---
    float odom_noise_scale = 1.0f;   // multiplies the configured noise
    float odom_distance_noise_scale = 1.0f;  // further multiplies the wheel (distance) noise
    float odom_turn_noise_scale = 1.0f;      // further multiplies the heading noise
    float odom_scale_bias = 0.0f;    // fractional error on reported distance
    float odom_turn_scale_bias = 0.0f;  // fractional error on reported rotation
    float odom_turn_bias = 0.0f;     // rad of heading error per metre travelled
    float odom_heading_bias_rps = 0.0f;  // rad/s of heading error while moving
};

}  // namespace wobo
