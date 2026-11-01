#include <cmath>

#include "gameplay/run_director.h"
#include "localization/localizer_registry.h"
#include "planning/planner_registry.h"
#include "test.h"
#include "world/map_loader.h"

using namespace wobo;

namespace {

// Run with no player interference until the referee declares Wobo found or
// the limit passes. Returns seconds taken, or a negative number on timeout.
double time_to_localize(const std::string& map, std::uint64_t seed, double limit_s,
                        Config cfg = load_game_config()) {
    RunDirector run(cfg);
    std::string err;
    if (!run.start(map, seed, &err)) return -2.0;
    const int steps = static_cast<int>(limit_s / run.step_s());
    for (int i = 0; i < steps; ++i) {
        run.step();
        if (run.phase() == RunPhase::Drafting) run.skip_draft();
        if (run.phase() == RunPhase::Over) return run.sim().time_s();
    }
    return -1.0;
}

}  // namespace

TEST("localizer: registry builds the baseline") {
    const Config cfg = load_game_config();
    auto pf = make_localizer("particle_filter", cfg);
    REQUIRE(pf != nullptr);
    CHECK(pf->name() == "particle_filter");
    CHECK(make_localizer("nonsense", cfg) == nullptr);
}

TEST("localizer: tight gaussian prior converges near the truth") {
    Config cfg = load_game_config();
    cfg.set("run.prior", "gaussian");
    cfg.set("run.prior_sigma", "0.3");
    const double t = time_to_localize("workshop", 11, 60.0, cfg);
    CHECK(t > 0.0);
    CHECK(t < 30.0);
}

TEST("localizer: global localization succeeds on the distinctive map") {
    int ok = 0;
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        const double t = time_to_localize("workshop", seed, 240.0);
        std::printf("    seed %llu: %.1f s\n", static_cast<unsigned long long>(seed), t);
        ok += t > 0.0 ? 1 : 0;
    }
    CHECK(ok >= 3);
}

TEST("localizer: uniform prior is not localized at the start") {
    RunDirector run(load_game_config());
    REQUIRE(run.start("workshop", 3));
    const PoseEstimate e = run.agent().estimate();
    CHECK(e.confidence < 0.2f);
    const auto* diag = run.agent().diagnostics();
    REQUIRE(diag != nullptr);
    CHECK(diag->particles.size() > 100);
}

TEST("planner: works from a point estimate without diagnostics") {
    const Config cfg = load_game_config();
    auto map = load_map("workshop");
    REQUIRE(map.has_value());
    auto planner = make_planner("info_gain", cfg);
    REQUIRE(planner != nullptr);
    planner->reset(1);

    PoseEstimate est;
    est.valid = true;
    Rng rng(2);
    est.pose = map->sample_valid_pose(rng, 0.2f);
    est.has_covariance = true;
    est.var_x = est.var_z = 0.1f;
    est.var_heading = 0.1f;
    PlannerContext ctx;
    const MotionPlan plan = planner->choose_action(*map, est, nullptr, ctx);
    CHECK(!plan.reason.empty());
    CHECK(!planner->last_candidates().empty());

    est.has_covariance = false;
    const MotionPlan plan2 = planner->choose_action(*map, est, nullptr, ctx);
    CHECK(!plan2.reason.empty());
}
