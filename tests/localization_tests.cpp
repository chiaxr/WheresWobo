#include <cmath>

#include "gameplay/run_director.h"
#include "localization/localizer_registry.h"
#include "planning/goal_navigator.h"
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

TEST("localizer: registry builds every alternative") {
    const Config cfg = load_game_config();
    for (const std::string& name : localizer_names()) {
        auto l = make_localizer(name, cfg);
        REQUIRE(l != nullptr);
        CHECK(l->name() == name);
    }
    CHECK(localizer_names().size() >= 2);
    for (const std::string& name : planner_names()) {
        auto p = make_planner(name, cfg);
        REQUIRE(p != nullptr);
        CHECK(p->name() == name);
    }
    CHECK(planner_names().size() >= 3);
}

TEST("localizer: histogram filter localizes globally on the distinctive map") {
    Config cfg = load_game_config();
    cfg.set("localizer.type", "histogram_filter");
    int ok = 0;
    for (std::uint64_t seed = 1; seed <= 3; ++seed) ok += time_to_localize("workshop", seed, 120.0, cfg) > 0.0 ? 1 : 0;
    CHECK(ok >= 2);
}

TEST("localizer: histogram filter starts uniform and offers samples") {
    Config cfg = load_game_config();
    cfg.set("localizer.type", "histogram_filter");
    RunDirector run(cfg);
    REQUIRE(run.start("gallery", 5));
    CHECK(run.agent().estimate().confidence < 0.2f);
    const auto* diag = run.agent().diagnostics();
    REQUIRE(diag != nullptr);
    CHECK(diag->particles.size() > 100);
    float sum = 0.0f;
    for (const WeightedPose& p : diag->particles) sum += p.weight;
    CHECK(std::fabs(sum - 1.0f) < 0.02f);
}

TEST("planner: the baselines move Wobo") {
    Config cfg = load_game_config();
    for (const char* name : {"landmark_tour", "random_walk"}) {
        cfg.set("planner.type", name);
        const double t = time_to_localize("greyscale", 2, 200.0, cfg);
        std::printf("    %s: %.1f s\n", name, t);
        CHECK(t > 0.0);
    }
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

TEST("navigator: finds the mirror twin on the Hall of Mirrors") {
    // The map is symmetric under a half turn about its centre, except for a
    // blue corner far from here, so the rotated pose must be a look-alike.
    auto map = load_map("mirror");
    REQUIRE(map.has_value());
    GoalNavigator nav;
    const Pose2D est{10.25f, 4.75f, 0.3f};
    const auto twins = nav.look_alikes(*map, est, 0.2f, 2.5f);
    REQUIRE(!twins.empty());
    const Pose2D mirrored{map->world_width() - est.x, map->world_depth() - est.z, wrap_angle(est.heading_rad + kPi)};
    bool found = false;
    for (const Pose2D& t : twins) {
        found = found || (distance(t.position(), mirrored.position()) < 0.3f &&
                          std::fabs(angle_diff(t.heading_rad, mirrored.heading_rad)) < 0.2f);
    }
    CHECK(found);
    for (const Pose2D& t : twins) CHECK(distance(t.position(), est.position()) > 1.0f);
}

TEST("planner: a collapsed belief keeps moving") {
    // A single, certain hypothesis gives the greedy search nothing to gain;
    // the planner must still travel (the stall that let a confidently wrong
    // Wobo stand still forever).
    const Config cfg = load_game_config();
    auto map = load_map("offices");
    REQUIRE(map.has_value());
    auto planner = make_planner("info_gain", cfg);
    planner->reset(1);
    PoseEstimate est;
    est.valid = true;
    est.confidence = 1.0f;
    est.pose = {10.25f, 4.25f, 0.0f};  // the middle of the long hallway
    LocalizationDiagnostics diag;
    diag.particles.assign(200, {est.pose, 1.0f / 200.0f});
    diag.hypotheses.push_back({est.pose, 1.0f});
    PlannerContext ctx;
    int moving = 0;
    for (int i = 0; i < 4; ++i) {
        ctx.time_s = i;
        const MotionPlan plan = planner->choose_action(*map, est, &diag, ctx);
        moving += plan.forward_m > 0.0f ? 1 : 0;
    }
    CHECK(moving >= 3);
}

TEST("localizer: recovers from wrong convergence on the symmetric Office Block") {
    int ok = 0;
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        const double t = time_to_localize("offices", seed, 300.0);
        std::printf("    seed %llu: %.1f s\n", static_cast<unsigned long long>(seed), t);
        ok += t > 0.0 ? 1 : 0;
    }
    CHECK(ok >= 3);
}
