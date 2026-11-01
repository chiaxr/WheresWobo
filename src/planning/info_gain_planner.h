#pragma once

// Greedy information-gain planner.
//
// It draws a handful of pose hypotheses from the belief, and for each
// candidate motion predicts the scan every hypothesis would see after making
// it. Where those predicted scans differ, the scan that actually arrives will
// tell the hypotheses apart; the planner scores that as expected entropy
// reduction, minus costs for distance and for paths that would hit walls,
// plus a small bonus for going somewhere it has not been recently (tracked in
// its own odometry frame, so no ground truth is needed).

#include <deque>

#include "core/config.h"
#include "core/rng.h"
#include "planning/planner.h"

namespace wobo {

struct InfoGainPlannerConfig {
    int hypotheses = 24;         // poses sampled from the belief
    int beams = 16;              // beams in each predicted scan
    float sigma_range = 0.2f;
    float sigma_colour = 0.18f;
    float discrimination = 0.5f; // scales predicted scan differences (like filter temperature)
    float distance_cost = 0.06f; // per metre
    float turn_cost = 0.02f;     // per radian
    float blocked_cost = 2.5f;   // per unit of belief mass whose path is blocked
    float novelty_weight = 0.25f;
    float novelty_radius_m = 1.2f;
    float safety_margin_m = 0.15f;
    float randomness = 0.03f;    // tie-breaking jitter on scores
    float scan_pause_s = 0.5f;

    static InfoGainPlannerConfig from(const Config& cfg);
};

class InfoGainPlanner final : public IActivePlanner {
public:
    explicit InfoGainPlanner(const InfoGainPlannerConfig& config) : config_(config) {}

    std::string_view name() const override { return "info_gain"; }
    void reset(std::uint64_t seed) override;
    MotionPlan choose_action(const Map2D& known_map, const PoseEstimate& estimate,
                             const LocalizationDiagnostics* diagnostics,
                             const PlannerContext& context) override;
    const std::vector<PlanCandidate>& last_candidates() const override { return candidates_; }

private:
    struct Hyp {
        Pose2D pose;
        float weight;
    };
    struct Predicted {
        float range;
        Rgb rgb;
        bool hit;
    };

    void sample_hypotheses(const Map2D& map, const PoseEstimate& estimate,
                           const LocalizationDiagnostics* diagnostics);
    // Free distance along a robot-frame direction according to the last scan.
    float scan_clearance(const RgbLidarScan* scan, float direction, float radius) const;

    InfoGainPlannerConfig config_;
    Rng rng_;
    std::vector<Hyp> hyps_;
    std::vector<PlanCandidate> candidates_;
    std::vector<Predicted> predicted_;  // [hypothesis][beam]
    std::deque<Vec2> visited_;          // odometry-frame trail
    double last_visit_s_ = -1e9;
};

}  // namespace wobo
