#pragma once

// Long-range goal seeking, for when no short move is informative: the belief
// has collapsed onto one pose (perhaps the wrong one) or every view within
// reach looks alike. A greedy one-step planner stalls there, because all its
// hypotheses predict the same scans. The navigator instead picks a distinctive
// spot of the map that, according to the current estimate, Wobo has not been
// near, and follows a grid path to it. If the estimate is right Wobo sees the
// landmark it expects; if it is wrong, the scans on the way stop matching and
// the localizer notices.
//
// Each traversable cell's 360-degree view is precomputed per map, with a
// rotation-invariant summary (histograms of ranges and wall materials). The
// goal is the place that best tells the estimate from its rivals: the
// localizer's other hypotheses, plus "twins" -- poses elsewhere whose view
// matches the estimate's, found by matching the precomputed views under every
// rotation. A goal scores well where the estimate and every rival expect
// different views (in a mirror-symmetric map, the one wall that is not
// mirrored). With no rivals, a cell whose view is unique on the map will do.
// The visited trail is kept in the odometry frame and carried into the
// map frame through the current estimate, so no ground truth is needed.

#include <deque>
#include <utility>
#include <vector>

#include "core/config.h"
#include "planning/planner.h"

namespace wobo {

// Free distance along a robot-frame direction according to a scan (the swept
// corridor of a robot of `radius`). A short default when there is no scan.
float scan_clearance(const RgbLidarScan* scan, float direction, float radius);

struct GoalNavigatorConfig {
    float landmark_weight = 1.5f;  // bonus for a goal whose view is unique on the map
    float path_cost = 0.06f;       // per metre of path to the goal
    float novelty_radius_m = 1.5f; // a trail point this close means "been there"
    float reach_m = 0.6f;          // goal reached
    float timeout_s = 30.0f;       // give up on a goal after this long
    float step_m = 1.7f;           // longest straight leg per plan
    float safety_margin_m = 0.15f;

    static GoalNavigatorConfig from(const Config& cfg);
};

class GoalNavigator {
public:
    explicit GoalNavigator(const GoalNavigatorConfig& config = {}) : config_(config) {}

    void reset();
    // Record the dead-reckoned pose; call on every planning step.
    void record(const PlannerContext& ctx);
    // Plan the next leg towards a goal, choosing one if needed. False when
    // nothing is reachable from the estimate.
    bool step(const Map2D& map, const Pose2D& estimate, const LocalizationDiagnostics* diagnostics,
              const PlannerContext& ctx, MotionPlan* out);
    int twins() const { return twins_; }  // rivals considered for the current goal
    void drop_goal() { has_goal_ = false; }
    bool has_goal() const { return has_goal_; }
    int goals_chosen() const { return goals_chosen_; }

    // Poses elsewhere on the map whose view matches the view from `estimate`
    // (best first, at least a metre apart): where Wobo could equally be.
    std::vector<Pose2D> look_alikes(const Map2D& map, const Pose2D& estimate, float radius, float max_range);

    // Per-cell distinctiveness, 0..1 (0 for cells Wobo cannot stand in).
    // Built on first use for a map.
    const std::vector<float>& landmarks(const Map2D& map, float radius, float max_range);

private:
    void build(const Map2D& map, float radius, float max_range);
    void distance_field(const Map2D& map, int from);
    int nearest_traversable(const Map2D& map, Vec2 p) const;
    // 0..1: how differently the views at `cell` and at the point `other` read.
    float view_difference(const Map2D& map, int cell, Vec2 other) const;
    void find_twins(const Map2D& map, const Pose2D& estimate, int cell, float max_range,
                    std::vector<Pose2D>* twins) const;
    bool segment_free(const Map2D& map, Vec2 a, Vec2 b, float radius) const;

    GoalNavigatorConfig config_;
    const Map2D* built_for_ = nullptr;
    std::vector<unsigned char> traversable_;
    std::vector<float> landmark_;
    std::vector<int> slot_;            // cell -> index into the per-view arrays, -1 if not traversable
    std::vector<float> ranges_;        // [slot][direction]
    std::vector<int> hit_material_;    // [slot][direction], -1 for nothing in range
    std::vector<float> desc_;          // [slot][dims_]
    int dims_ = 0;
    float desc_scale_ = 1.0f;
    std::vector<float> dist_;  // path metres from the distance-field source
    std::vector<int> queue_;

    std::deque<Vec2> trail_;  // odometry frame
    bool has_goal_ = false;
    int goal_cell_ = -1;
    int goals_chosen_ = 0;
    float goal_telling_ = 0.0f;
    int twins_ = 0;
    int blocked_ = 0;                             // consecutive legs with no way forward
    std::vector<std::pair<int, double>> failed_;  // goal cells given up on, until when
    double goal_since_s_ = 0.0;
    Pose2D last_estimate_;
    Pose2D last_odom_;
};

}  // namespace wobo
