#pragma once

// Baseline localizer: Monte Carlo localization (particle filter) with a
// range + colour beam likelihood, and augmented-MCL random particle
// injection (short- vs long-term likelihood averages) so it can recover
// from being kidnapped. Injected particles are the best fit to the current
// scan among a few random poses (sensor-resetting localization), so a filter
// that converged on the wrong place re-seeds where the scan says it could be.
// With view injection on, the poses tried are drawn from a table instead: the
// 360-degree view from every cell centre, ray-cast once per map, is scored
// against the scan under every turn, and injected particles are drawn in
// proportion to that fit (the "dual" proposal of Mixture MCL), so every place
// that looks like the scan gets particles at once.

#include <cstdint>
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
    int inject_candidates = 8;    // random poses tried per injected particle (1 = plain uniform)
    bool view_injection = false;  // draw them from the view table instead (poses near the draw)
    float view_range = 2.5f;      // the scanner's rated range, for the view table
    float view_temperature = 0.08f;  // how sharply draws favour the best-fitting views
    float resample_ess = 0.5f;

    // Clustering for the estimate and hypotheses.
    float cluster_radius_m = 0.6f;
    float cluster_heading_rad = 0.6f;

    static ParticleFilterConfig from(const Config& cfg);
};

class ParticleFilter final : public ILocalizer {
public:
    explicit ParticleFilter(const ParticleFilterConfig& config)
        : config_(config), view_temperature_(config.view_injection ? config.view_temperature : 0.0f) {}

    std::string_view name() const override { return "particle_filter"; }
    void reset(const Map2D& known_map, const InitialBelief& prior, std::uint64_t seed) override;
    void predict(const OdometryDelta& odometry) override;
    void observe(const RgbLidarScan& scan) override;
    // Scales the particle count (the set grows or shrinks at the next
    // update) and the random poses tried per injected particle.
    void set_budget(float share) override;
    void set_view_injection(float temperature) override;
    PoseEstimate estimate() const override { return estimate_; }
    const LocalizationDiagnostics* diagnostics() const override { return &diag_; }

    const ParticleFilterConfig& config() const { return config_; }

private:
    struct Beam {
        float angle;
        float range;
        bool hit;
        Rgb rgb;
    };

    void resample(float inject_fraction);
    std::size_t target_particles() const;
    int inject_candidates() const;
    int beam_stride() const;
    float log_likelihood(const Pose2D& pose) const;  // against beams_
    void update_estimate();
    Pose2D sample_prior();
    void build_views();
    void score_views(const RgbLidarScan& scan);
    Pose2D draw_from_views(float u);

    ParticleFilterConfig config_;
    const Map2D* map_ = nullptr;
    InitialBelief prior_;
    Rng rng_;

    float budget_ = 1.0f;
    LocalizationDiagnostics diag_;  // diag_.particles is the particle set itself
    PoseEstimate estimate_;
    std::vector<float> log_lik_;
    std::vector<WeightedPose> scratch_;
    std::vector<float> histogram_;
    std::vector<unsigned char> claimed_;
    std::vector<Beam> beams_;  // the beams of the latest scan in use
    float max_range_ = 0.0f;

    float w_slow_ = 0.0f;
    float w_fast_ = 0.0f;
    bool have_averages_ = false;
    int beam_offset_ = 0;

    // View injection: per cell with room for Wobo, the view under each of
    // kViews directions as keys into the latest scan's per-beam likelihood
    // tables (range bin and material, or nothing in range).
    static constexpr int kViews = 32;
    static constexpr int kRangeBins = 50;
    float view_temperature_ = 0.0f;  // 0: view injection off
    const Map2D* views_for_ = nullptr;
    std::vector<int> view_cells_;
    std::vector<std::uint16_t> view_keys_;  // [cell slot][direction]
    int view_materials_ = 0;
    std::vector<float> view_cdf_;           // [cell slot * kViews + heading], latest scan's fit
    bool views_scored_ = false;
};

}  // namespace wobo
