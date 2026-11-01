#include "gameplay/run_director.h"
#include "test.h"

using namespace wobo;

namespace {

RunStats base_stats() {
    RunStats s;
    s.load(load_game_config());
    return s;
}

}  // namespace

TEST("energy: regenerates with simulation time up to capacity") {
    RunStats stats = base_stats();
    stats.set("energy.capacity", 100.0f);
    stats.set("energy.start", 10.0f);
    stats.set("energy.regen", 5.0f);
    EnergySystem e;
    e.reset(stats);
    CHECK_NEAR(e.current(), 10.0f, 1e-5);
    e.step(2.0f, stats, 1.0f);
    CHECK_NEAR(e.current(), 20.0f, 1e-4);
    e.step(100.0f, stats, 1.0f);
    CHECK_NEAR(e.current(), 100.0f, 1e-4);
    CHECK(e.spend(30.0f));
    CHECK_NEAR(e.current(), 70.0f, 1e-4);
    CHECK(!e.spend(80.0f));
    CHECK_NEAR(e.current(), 70.0f, 1e-4);
}

TEST("actions: cost, cooldown, duration and ownership") {
    RunStats stats = base_stats();
    stats.set("energy.start", 100.0f);
    EnergySystem energy;
    energy.reset(stats);
    PlayerActionSystem actions;
    actions.reset(1);

    CHECK(actions.owned(ActionId::Range, stats));
    CHECK(actions.owned(ActionId::Drift, stats));
    CHECK(!actions.owned(ActionId::Kidnap, stats));  // only ever from a card
    CHECK(!actions.owned(ActionId::Colour, stats));
    CHECK(actions.activate(ActionId::Colour, stats, energy, 0.0f) == ActivateResult::NotOwned);

    const float cost = actions.cost(ActionId::Range, stats, 0.0f);
    CHECK(actions.activate(ActionId::Range, stats, energy, 0.0f) == ActivateResult::Ok);
    CHECK_NEAR(energy.current(), 100.0f - cost, 1e-4);
    CHECK(actions.active(ActionId::Range));
    CHECK(actions.activate(ActionId::Range, stats, energy, 0.0f) == ActivateResult::Cooldown);

    const float duration = stats.get("action.range.duration");
    actions.step(duration + 0.01f);
    CHECK(!actions.active(ActionId::Range));
    actions.step(stats.get("action.range.cooldown"));
    CHECK(actions.check(ActionId::Range, stats, energy, 0.0f) == ActivateResult::Ok);

    energy.spend(energy.current());
    CHECK(actions.activate(ActionId::Range, stats, energy, 0.0f) == ActivateResult::NoEnergy);
}

TEST("actions: each action changes only its own data path") {
    RunStats stats = base_stats();
    stats.set("action.colour.owned", 1.0f);
    stats.set("energy.capacity", 10000.0f);
    stats.set("energy.start", 10000.0f);
    EnergySystem energy;
    energy.reset(stats);

    const SensorModifiers none;
    const auto run_one = [&](ActionId id) {
        PlayerActionSystem a;
        a.reset(1);
        REQUIRE(a.activate(id, stats, energy, 0.0f) == ActivateResult::Ok);
        return a.modifiers(stats);
    };
    const auto odom_untouched = [&](const SensorModifiers& m) {
        return m.odom_noise_scale == none.odom_noise_scale && m.odom_scale_bias == 0.0f &&
               m.odom_turn_bias == 0.0f && m.odom_heading_bias_rps == 0.0f;
    };
    const auto lidar_untouched = [&](const SensorModifiers& m) {
        return m.range_scale == 1.0f && m.range_noise_add == 0.0f && m.colour_noise_add == 0.0f &&
               m.colour_desaturate == 0.0f && m.colour_hue_shift == 0.0f && m.dropout_add == 0.0f &&
               !m.blackout && m.ghost_fraction == 0.0f;
    };

    const SensorModifiers range = run_one(ActionId::Range);
    CHECK(range.range_scale < 1.0f);
    CHECK(odom_untouched(range));
    CHECK(range.colour_desaturate == 0.0f);

    const SensorModifiers drift = run_one(ActionId::Drift);
    CHECK(lidar_untouched(drift));
    CHECK(drift.odom_noise_scale > 1.0f);

    const SensorModifiers colour = run_one(ActionId::Colour);
    CHECK(odom_untouched(colour));
    CHECK(colour.range_scale == 1.0f);
    CHECK(colour.colour_desaturate > 0.0f);
}

TEST("referee: thresholds and stability window") {
    RefereeConfig c;
    c.position_tolerance_m = 0.4f;
    c.heading_tolerance_rad = 15.0f * kDegToRad;
    c.stability_s = 3.0f;
    c.close_call = 0.5f;
    LocalizationReferee ref(c);
    const Pose2D truth{2.0f, 2.0f, 0.5f};
    PoseEstimate good;
    good.valid = true;
    good.pose = {2.2f, 2.1f, 0.6f};
    PoseEstimate far = good;
    far.pose.x = 3.0f;
    PoseEstimate turned = good;
    turned.pose.heading_rad = 0.5f + 20.0f * kDegToRad;

    CHECK(!ref.step(0.1f, truth, far));
    CHECK(!ref.within_tolerance());
    CHECK(!ref.step(0.1f, truth, turned));
    CHECK(!ref.within_tolerance());

    // 2.9 s correct, then wrong: not found, but a close call.
    for (int i = 0; i < 29; ++i) CHECK(!ref.step(0.1f, truth, good));
    CHECK(!ref.step(0.1f, truth, far));
    CHECK(!ref.localized());
    CHECK(ref.close_calls() == 1);
    CHECK_NEAR(ref.lock_progress(), 0.0f, 1e-6);

    bool found = false;
    for (int i = 0; i < 31 && !found; ++i) found = ref.step(0.1f, truth, good);
    CHECK(found);
    CHECK(ref.localized());
}

TEST("cards: same seed and picks reproduce the same drafts") {
    const auto drafts = [](std::uint64_t seed) {
        Config cfg = load_game_config();
        cfg.set("run.day_length", "5");
        RunDirector run(cfg);
        run.start("offices", seed);
        std::vector<std::string> seen;
        for (int i = 0; i < 30 * 60 && run.phase() != RunPhase::Over; ++i) {
            run.step();
            if (run.phase() == RunPhase::Drafting) {
                for (const CardDef* c : run.offer()) seen.push_back(c->id);
                run.choose_card(0);
            }
        }
        return seen;
    };
    const auto a = drafts(77);
    const auto b = drafts(77);
    CHECK(!a.empty());
    CHECK(a == b);
}

TEST("cards: drafting pauses the simulation and energy") {
    Config cfg = load_game_config();
    cfg.set("run.day_length", "5");
    RunDirector run(cfg);
    REQUIRE(run.start("offices", 3));
    for (int i = 0; i < 30 * 10 && run.phase() == RunPhase::Playing; ++i) run.step();
    REQUIRE(run.phase() == RunPhase::Drafting);
    REQUIRE(run.offer().size() == 3);
    const double t = run.sim().time_s();
    const float e = run.energy().current();
    for (int i = 0; i < 100; ++i) run.step();
    CHECK(run.sim().time_s() == t);
    CHECK(run.energy().current() == e);
    CHECK(run.activate(ActionId::Range) == ActivateResult::Unavailable);
    CHECK(run.choose_card(1));
    CHECK(run.phase() == RunPhase::Playing);
}

TEST("cards: offers are distinct and basic actions stay owned") {
    Config cfg = load_game_config();
    cfg.set("run.day_length", "5");
    RunDirector run(cfg);
    REQUIRE(run.start("offices", 12));
    int drafts = 0;
    for (int i = 0; i < 30 * 200 && run.phase() != RunPhase::Over; ++i) {
        run.step();
        if (run.phase() != RunPhase::Drafting) continue;
        const auto& offer = run.offer();
        for (std::size_t a = 0; a < offer.size(); ++a) {
            for (std::size_t b = a + 1; b < offer.size(); ++b) CHECK(offer[a] != offer[b]);
        }
        run.choose_card(static_cast<int>(drafts % offer.size()));
        ++drafts;
    }
    CHECK(drafts > 5);
    CHECK(run.owns(ActionId::Range));
    CHECK(run.owns(ActionId::Drift));
}

TEST("kidnap: not owned until its card is drafted") {
    RunDirector run(load_game_config());
    REQUIRE(run.start("workshop", 4));
    CHECK(!run.owns(ActionId::Kidnap));
    CHECK(run.activate(ActionId::Kidnap, run.sim().true_pose()) == ActivateResult::NotOwned);
    CardDraftSystem cards;
    cards.reset(load_cards(), 1);
    const CardDef* card = cards.find("kidnap");
    REQUIRE(card != nullptr);
    CHECK(card->category == CardCategory::Ability);
}

TEST("cards: effects apply to stats") {
    Config cfg = load_game_config();
    RunDirector run(cfg);
    REQUIRE(run.start("workshop", 1));
    CardDraftSystem cards;
    cards.reset(load_cards(), 1);
    CHECK(cards.find("colour") != nullptr);
    CHECK(cards.find("battery") != nullptr);
    RunStats stats = base_stats();
    const float cap = stats.get("energy.capacity");
    CHECK(stats.apply("energy.capacity", "+25"));
    CHECK_NEAR(stats.get("energy.capacity"), cap + 25.0f, 1e-4);
    CHECK(stats.apply("action.range.cost", "*0.5"));
    CHECK(stats.apply("action.colour.desaturate", "=1"));
    CHECK_NEAR(stats.get("action.colour.desaturate"), 1.0f, 1e-6);
    CHECK(!stats.apply("x", "?3"));
}

TEST("kidnap: rejects invalid and too-close destinations") {
    Config cfg = load_game_config();
    cfg.set("energy.start", "100");
    cfg.set("action.kidnap.owned", "1");
    RunDirector run(cfg);
    REQUIRE(run.start("workshop", 2));
    const Pose2D here = run.sim().true_pose();
    CHECK(run.activate(ActionId::Kidnap, here) == ActivateResult::TooClose);
    CHECK(run.activate(ActionId::Kidnap, Pose2D{0.1f, 0.1f, 0.0f}) == ActivateResult::InvalidTarget);
    CHECK(run.activate(ActionId::Kidnap) == ActivateResult::InvalidTarget);
    CHECK_NEAR(run.energy().current(), 100.0f, 1e-4);
}
