#include "planning/baseline_planners.h"

#include <algorithm>

namespace wobo {

void LandmarkTourPlanner::reset(std::uint64_t /*seed*/) { navigator_.reset(); }

MotionPlan LandmarkTourPlanner::choose_action(const Map2D& map, const PoseEstimate& estimate,
                                              const LocalizationDiagnostics* diagnostics,
                                              const PlannerContext& ctx) {
    navigator_.record(ctx);
    MotionPlan plan;
    plan.pause_s = scan_pause_s_;
    if (estimate.valid && navigator_.step(map, estimate.pose, diagnostics, ctx, &plan)) {
        if (estimate.confidence < 0.5f) plan.kind = MotionPlan::Kind::Explore;
        return plan;
    }
    plan.kind = MotionPlan::Kind::Scan;
    plan.turn_rad = 0.5f * kPi;
    plan.reason = "Looking around";
    return plan;
}

MotionPlan RandomWalkPlanner::choose_action(const Map2D& /*map*/, const PoseEstimate& /*estimate*/,
                                            const LocalizationDiagnostics* /*diagnostics*/,
                                            const PlannerContext& ctx) {
    MotionPlan plan;
    plan.kind = MotionPlan::Kind::Explore;
    plan.pause_s = scan_pause_s_;
    plan.reason = "Wandering";
    for (int attempt = 0; attempt < 8; ++attempt) {
        const float turn = rng_.uniform(-kPi, kPi);
        const float clear =
            scan_clearance(ctx.last_scan, turn, ctx.robot_radius_m) - ctx.robot_radius_m - safety_margin_m_;
        if (clear >= 0.25f) {
            plan.turn_rad = turn;
            plan.forward_m = std::min(1.7f, clear);
            return plan;
        }
    }
    plan.kind = MotionPlan::Kind::Scan;
    plan.turn_rad = 0.5f * kPi;
    plan.reason = "Boxed in - turning round";
    return plan;
}

}  // namespace wobo
