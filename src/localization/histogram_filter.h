#pragma once

// Alternative localizer: Markov localization on a grid (a histogram filter).
//
// The belief is a probability for every (cell, heading) state, 0.25 m cells by
// 32 headings, so unlike a particle filter it never runs out of samples: every
// look-alike place keeps its share until the scans rule it out. The expected
// view from every cell in every direction is ray-cast once per map, and each
// scan's beam likelihoods are tabulated, so an update is a table lookup per
// state and beam.
//
// Motion shifts each heading layer of the grid by whole cells, carrying the
// sub-cell remainder (one per layer, and one shared heading remainder), and
// then blurs it a little for odometry noise. Recovery mixes in the current
// scan's likelihood when recent scans fit worse than older ones (the grid
// version of augmented MCL with sensor resetting).

#include <vector>

#include "core/config.h"
#include "localization/localizer.h"

namespace wobo {

struct HistogramFilterConfig {
    float cell_m = 0.25f;
    int headings = 32;
    float robot_radius = 0.2f;
    float max_range = 2.5f;       // the scanner's rated range, for the precomputed views

    // Observation model (the particle filter's keys and meaning).
    int beam_stride = 2;
    float sigma_range = 0.2f;
    float sigma_colour = 0.2f;
    float colour_floor = 0.15f;
    float p_random = 0.05f;
    float p_unexpected_miss = 0.2f;
    float temperature = 0.08f;

    // Motion noise, and a floor of blur per update for the discretisation.
    float forward_var_per_m = 0.008f;
    float turn_var_per_rad = 0.006f;
    float turn_var_per_m = 0.004f;
    float blur = 0.03f;           // kernel weight per side per update (0..0.25)

    // Recovery.
    float alpha_slow = 0.02f;
    float alpha_fast = 0.3f;
    float max_mix = 0.08f;

    // Estimate and diagnostics.
    float cluster_radius_m = 0.6f;
    float cluster_heading_rad = 0.6f;
    int export_samples = 800;     // poses drawn from the grid for the UI and planner

    static HistogramFilterConfig from(const Config& cfg);
};

class HistogramFilter final : public ILocalizer {
public:
    explicit HistogramFilter(const HistogramFilterConfig& config) : config_(config) {}

    std::string_view name() const override { return "histogram_filter"; }
    void reset(const Map2D& known_map, const InitialBelief& prior, std::uint64_t seed) override;
    void predict(const OdometryDelta& odometry) override;
    void observe(const RgbLidarScan& scan) override;
    PoseEstimate estimate() const override { return estimate_; }
    const LocalizationDiagnostics* diagnostics() const override { return &diag_; }

private:
    int state(int cell, int h) const { return cell * config_.headings + h; }
    float heading_of(int h) const { return wrap_angle(kTwoPi * h / config_.headings + heading_rem_); }
    Vec2 position_of(int cell, int h) const;
    void build(const Map2D& map);
    void shift_layer(int h, int dx, int dz);
    void rotate_layers(int dh);
    void blur(float a, float b);
    void normalise();
    void update_estimate();

    HistogramFilterConfig config_;
    const Map2D* map_ = nullptr;
    const Map2D* built_for_ = nullptr;
    int gw_ = 0;
    int gh_ = 0;
    std::vector<unsigned char> valid_;     // per grid cell
    std::vector<float> exp_range_;         // [cell][direction], at the rated range
    std::vector<unsigned char> exp_mat_;   // [cell][direction], 255 = nothing in range

    std::vector<float> belief_;            // [cell][heading]
    std::vector<float> scratch_;
    std::vector<float> ll_;
    std::vector<Vec2> layer_rem_;          // per heading layer, world-frame metres
    float heading_rem_ = 0.0f;
    float pending_dist_ = 0.0f;
    float pending_rot_ = 0.0f;
    int beam_offset_ = 0;

    float w_slow_ = 0.0f;
    float w_fast_ = 0.0f;
    bool have_averages_ = false;

    PoseEstimate estimate_;
    LocalizationDiagnostics diag_;
    std::vector<unsigned char> claimed_;
};

}  // namespace wobo
