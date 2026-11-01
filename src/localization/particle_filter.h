#pragma once

// Baseline localizer: Monte Carlo localization (particle filter) with a
// range + colour beam likelihood, and augmented-MCL random particle
// injection (short- vs long-term likelihood averages) so it can recover
// from being kidnapped.

#include <vector>

#include "core/config.h"
#include "core/rng.h"
#include "localization/localizer.h"

namespace wobo {

struct ParticleFilterConfig {
    int particles = 2000;
    float robot_radius = 0.2f;   // part of the known robot model: no particle inside a wall

    // Observation model.
    int beam_stride = 2;          // use every n-th beam (rotating offset per scan)
    float sigma_range = 0.15f;    // m
    float sigma_colour = 0.15f;   // in colour_distance units (0..1)
    float colour_floor = 0.15f;   // likelihood multiplier for a total colour mismatch
    float p_random = 0.05f;       // per-beam outlier floor
    float p_unexpected_miss = 0.2f;  // no return where the map predicts one
    float temperature = 0.4f;     // scales the log-likelihood (beams are not independent)

    // Motion model: variance per metre / radian. Deliberately looser than
    // the real odometry, as a filter must be.
    float forward_var_per_m = 0.008f;
    float lateral_var_per_m = 0.002f;
    float turn_var_per_rad = 0.006f;
    float turn_var_per_m = 0.004f;
    float jitter_m = 0.015f;      // roughening after resampling
    float jitter_rad = 0.01f;

    // Recovery (augmented MCL).
    float alpha_slow = 0.02f;
    float alpha_fast = 0.3f;
    float max_injection = 0.08f;
    float resample_ess = 0.5f;

    // Clustering for the estimate and hypotheses.
    float cluster_radius_m = 0.6f;
    float cluster_heading_rad = 0.6f;

    static ParticleFilterConfig from(const Config& cfg);
};

class ParticleFilter final : public ILocalizer {
public:
    explicit ParticleFilter(const ParticleFilterConfig& config) : config_(config) {}

    std::string_view name() const override { return "particle_filter"; }
    void reset(const Map2D& known_map, const InitialBelief& prior, std::uint64_t seed) override;
    void predict(const OdometryDelta& odometry) override;
    void observe(const RgbLidarScan& scan) override;
    PoseEstimate estimate() const override { return estimate_; }
    const LocalizationDiagnostics* diagnostics() const override { return &diag_; }

    const ParticleFilterConfig& config() const { return config_; }

private:
    void resample(float inject_fraction);
    void update_estimate();
    Pose2D sample_prior();

    ParticleFilterConfig config_;
    const Map2D* map_ = nullptr;
    InitialBelief prior_;
    Rng rng_;

    LocalizationDiagnostics diag_;  // diag_.particles is the particle set itself
    PoseEstimate estimate_;
    std::vector<float> log_lik_;
    std::vector<WeightedPose> scratch_;
    std::vector<float> histogram_;
    std::vector<unsigned char> claimed_;

    float w_slow_ = 0.0f;
    float w_fast_ = 0.0f;
    bool have_averages_ = false;
    int beam_offset_ = 0;
};

}  // namespace wobo
