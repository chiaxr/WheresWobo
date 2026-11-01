#pragma once

// What a localizer produces. Every localizer must give a point estimate;
// everything else (covariance, particles, candidate hypotheses) is optional,
// so planners and the UI must cope with it being missing.

#include <vector>

#include "core/types.h"

namespace wobo {

// The robot's prior over its starting pose. Describes what the robot knows,
// not what the player knows.
struct InitialBelief {
    enum class Kind { Uniform, Gaussian };
    Kind kind = Kind::Uniform;
    Pose2D mean;                 // Gaussian only
    float sigma_position_m = 1.0f;
    float sigma_heading_rad = 0.5f;
};

struct PoseEstimate {
    Pose2D pose;
    bool valid = false;
    // The localizer's own belief in its estimate, 0..1. Not a guarantee of
    // correctness: a localizer can be confidently wrong.
    float confidence = 0.0f;
    // How well recent observations fit the belief, 0..1 (1 = perfectly).
    // A sudden drop is the localizer's hint that something odd happened.
    float consistency = 1.0f;

    bool has_covariance = false;
    float var_x = 0.0f;
    float var_z = 0.0f;
    float cov_xz = 0.0f;
    float var_heading = 0.0f;
};

struct WeightedPose {
    Pose2D pose;
    float weight = 0.0f;
};

// Optional diagnostics. A localizer fills only what it naturally has.
struct LocalizationDiagnostics {
    std::vector<WeightedPose> particles;   // empty for non-sampling methods
    std::vector<WeightedPose> hypotheses;  // distinct candidate poses, weight = mass
    float effective_sample_size = 0.0f;    // fraction 0..1 (particle methods)
    float injected_fraction = 0.0f;        // fraction of random particles injected last update
};

}  // namespace wobo
