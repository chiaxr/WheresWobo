#include <cmath>

#include "gameplay/run_director.h"
#include "simulation/odometry.h"
#include "test.h"

using namespace wobo;

TEST("odometry: noiseless odometry equals the true motion") {
    OdometryConfig c;
    c.forward_var_per_m = c.lateral_var_per_m = c.turn_var_per_rad = c.turn_var_per_m = 0.0f;
    c.drift_walk = 0.0f;
    OdometrySimulator odo(c);
    Rng rng(1);
    const OdometryDelta d = odo.measure({0.1f, 0.0f, 0.05f}, 0.1f, rng, {});
    CHECK_NEAR(d.forward_m, 0.1f, 1e-6);
    CHECK_NEAR(d.lateral_m, 0.0f, 1e-6);
    CHECK_NEAR(d.delta_heading_rad, 0.05f, 1e-6);
}

TEST("odometry: noise grows with distance, independent of step size") {
    OdometryConfig c;
    c.drift_walk = 0.0f;
    // Drive 1 m in 100 steps vs 10 steps: the spread should match sqrt(var).
    for (int steps : {10, 100}) {
        OdometrySimulator odo(c);
        Rng rng(steps);
        double sum = 0.0, sum2 = 0.0;
        const int trials = 3000;
        for (int t = 0; t < trials; ++t) {
            double total = 0.0;
            for (int s = 0; s < steps; ++s) {
                total += odo.measure({1.0f / steps, 0.0f, 0.0f}, 0.03f, rng, {}).forward_m;
            }
            sum += total - 1.0;
            sum2 += (total - 1.0) * (total - 1.0);
        }
        CHECK_NEAR(sum / trials, 0.0, 0.004);
        CHECK_NEAR(std::sqrt(sum2 / trials), std::sqrt(c.forward_var_per_m), 0.006);
    }
}

TEST("odometry: drift modifiers bias the measurement, not the truth") {
    OdometryConfig c;
    c.forward_var_per_m = c.lateral_var_per_m = c.turn_var_per_rad = c.turn_var_per_m = 0.0f;
    c.drift_walk = 0.0f;
    OdometrySimulator odo(c);
    Rng rng(1);
    SensorModifiers mods;
    mods.odom_scale_bias = 0.25f;
    mods.odom_turn_bias = 0.5f;
    const RelativeMotion truth{0.2f, 0.0f, 0.0f};
    const OdometryDelta d = odo.measure(truth, 0.1f, rng, mods);
    CHECK_NEAR(d.forward_m, 0.25f, 1e-6);
    CHECK_NEAR(d.delta_heading_rad, 0.1f, 1e-6);
    CHECK_NEAR(truth.forward_m, 0.2f, 1e-9);
}

TEST("teleport: moves the true pose but not the robot's software") {
    Config cfg = load_game_config();
    cfg.set("energy.start", "100");
    cfg.set("action.kidnap.owned", "1");
    RunDirector run(cfg);
    std::string err;
    REQUIRE(run.start("workshop", 5, &err));
    for (int i = 0; i < 90; ++i) run.step();

    const Pose2D from = run.sim().true_pose();
    Rng rng(3);
    Pose2D target;
    do {
        target = run.map().sample_valid_pose(rng, 0.2f);
    } while (distance(target.position(), from.position()) < 3.0f);

    const PoseEstimate before = run.agent().estimate();
    REQUIRE(run.activate(ActionId::Kidnap, target) == ActivateResult::Ok);
    CHECK(run.sim().true_pose().x == target.x);
    CHECK(run.sim().true_pose().z == target.z);

    // The localizer was not reset or told: its estimate is untouched.
    const PoseEstimate after = run.agent().estimate();
    CHECK(after.pose.x == before.pose.x);
    CHECK(after.pose.z == before.pose.z);
    CHECK(after.confidence == before.confidence);

    // The next step moves the robot only by ordinary motion (the jump is
    // not reported as odometry, and the robot does not continue from the
    // old position).
    const Pose2D prev = run.sim().true_pose();
    run.step();
    CHECK(distance(run.sim().true_pose().position(), prev.position()) < 0.05f);
}
