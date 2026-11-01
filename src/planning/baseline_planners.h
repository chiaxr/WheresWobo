#pragma once

// Two simpler planners to compare the information-gain planner against.
//
// LandmarkTourPlanner only ever travels: every plan is a leg towards the
// GoalNavigator's current goal (the place that best tells the estimate from
// its look-alikes, or somewhere distinctive and unvisited). No short-range
// search, so it is cheap, but it never lingers to settle a close call.
//
// RandomWalkPlanner is the naive baseline: turn to a random direction that
// the last scan says is open, drive up to 1.7 m, look, repeat.

#include "core/config.h"
#include "core/rng.h"
#include "planning/goal_navigator.h"
#include "planning/planner.h"

namespace wobo {

class LandmarkTourPlanner final : public IActivePlanner {
public:
    explicit LandmarkTourPlanner(const Config& cfg)
        : navigator_(GoalNavigatorConfig::from(cfg)), scan_pause_s_(cfg.get_float("planner.scan_pause", 0.5f)) {}

    std::string_view name() const override { return "landmark_tour"; }
    void reset(std::uint64_t seed) override;
    MotionPlan choose_action(const Map2D& known_map, const PoseEstimate& estimate,
                             const LocalizationDiagnostics* diagnostics,
                             const PlannerContext& context) override;
    const std::vector<PlanCandidate>& last_candidates() const override { return none_; }

private:
    GoalNavigator navigator_;
    float scan_pause_s_;
    std::vector<PlanCandidate> none_;
};

class RandomWalkPlanner final : public IActivePlanner {
public:
    explicit RandomWalkPlanner(const Config& cfg)
        : safety_margin_m_(cfg.get_float("planner.safety_margin", 0.15f)),
          scan_pause_s_(cfg.get_float("planner.scan_pause", 0.5f)) {}

    std::string_view name() const override { return "random_walk"; }
    void reset(std::uint64_t seed) override { rng_.reseed(seed); }
    MotionPlan choose_action(const Map2D& known_map, const PoseEstimate& estimate,
                             const LocalizationDiagnostics* diagnostics,
                             const PlannerContext& context) override;
    const std::vector<PlanCandidate>& last_candidates() const override { return none_; }

private:
    Rng rng_;
    float safety_margin_m_;
    float scan_pause_s_;
    std::vector<PlanCandidate> none_;
};

}  // namespace wobo
