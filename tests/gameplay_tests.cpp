#include <cstdio>

#include "gameplay/look_alikes.h"
#include "gameplay/map_traits.h"
#include "gameplay/run_director.h"
#include "test.h"
#include "world/map_loader.h"

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
    stats.set("action.gummy.owned", 1.0f);
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
    const auto distance_untouched = [&](const SensorModifiers& m) {
        return m.odom_noise_scale == none.odom_noise_scale && m.odom_distance_noise_scale == 1.0f &&
               m.odom_scale_bias == 0.0f;
    };
    const auto heading_untouched = [&](const SensorModifiers& m) {
        return m.odom_noise_scale == none.odom_noise_scale && m.odom_turn_noise_scale == 1.0f &&
               m.odom_turn_scale_bias == 0.0f && m.odom_turn_bias == 0.0f && m.odom_heading_bias_rps == 0.0f;
    };
    const auto odom_untouched = [&](const SensorModifiers& m) {
        return distance_untouched(m) && heading_untouched(m);
    };
    const auto lidar_untouched = [&](const SensorModifiers& m) {
        return m.range_scale == 1.0f && m.range_noise_add == 0.0f && m.colour_noise_add == 0.0f &&
               m.colour_desaturate == 0.0f && m.colour_hue_shift == 0.0f && m.dropout_add == 0.0f &&
               m.blind_cone_rad == 0.0f && m.repaint.empty() && !m.blackout && m.ghost_fraction == 0.0f;
    };

    const SensorModifiers range = run_one(ActionId::Range);
    CHECK(range.range_scale < 1.0f);
    CHECK(odom_untouched(range));
    CHECK(range.colour_desaturate == 0.0f);

    // Gyro Jam is the heading, Gummy Wheels the distance.
    const SensorModifiers drift = run_one(ActionId::Drift);
    CHECK(lidar_untouched(drift));
    CHECK(distance_untouched(drift));
    CHECK(drift.odom_turn_noise_scale > 1.0f);
    CHECK(drift.odom_heading_bias_rps != 0.0f);

    const SensorModifiers gummy = run_one(ActionId::Gummy);
    CHECK(lidar_untouched(gummy));
    CHECK(heading_untouched(gummy));
    CHECK(gummy.odom_distance_noise_scale > 1.0f);
    CHECK(gummy.odom_scale_bias > 0.0f);

    const SensorModifiers colour = run_one(ActionId::Colour);
    CHECK(odom_untouched(colour));
    CHECK(colour.range_scale == 1.0f);
    CHECK(colour.colour_hue_shift > 0.0f);
    CHECK(colour.blind_cone_rad > 0.0f);  // and blind straight ahead
    CHECK(colour.repaint.empty());        // only with the Repainting Works upgrade
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
        cfg.set("run.hour_length", "5");
        RunDirector run(cfg);
        run.start("office", seed);
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
    cfg.set("run.hour_length", "5");
    RunDirector run(cfg);
    REQUIRE(run.start("office", 3));
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
    cfg.set("run.hour_length", "5");
    RunDirector run(cfg);
    REQUIRE(run.start("office", 12));
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
    REQUIRE(run.start("barn", 4));
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
    REQUIRE(run.start("barn", 1));
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
    REQUIRE(run.start("barn", 2));
    const Pose2D here = run.sim().true_pose();
    CHECK(run.activate(ActionId::Kidnap, here) == ActivateResult::TooClose);
    CHECK(run.activate(ActionId::Kidnap, Pose2D{0.1f, 0.1f, 0.0f}) == ActivateResult::InvalidTarget);
    CHECK(run.activate(ActionId::Kidnap) == ActivateResult::InvalidTarget);
    CHECK_NEAR(run.energy().current(), 100.0f, 1e-4);
}

TEST("gummy wheels: slows Wobo for real while it lasts") {
    RunStats stats = base_stats();
    stats.set("action.gummy.owned", 1.0f);
    stats.set("energy.start", 100.0f);
    EnergySystem energy;
    energy.reset(stats);
    PlayerActionSystem a;
    a.reset(1);
    CHECK_NEAR(a.speed_scale(stats), 1.0f, 1e-6);
    REQUIRE(a.activate(ActionId::Gummy, stats, energy, 0.0f) == ActivateResult::Ok);
    CHECK(a.speed_scale(stats) < 1.0f);
    a.step(100.0f);
    CHECK_NEAR(a.speed_scale(stats), 1.0f, 1e-6);
}

TEST("map traits: reachability and colour on the shipped maps") {
    const float radius = load_game_config().get_float("robot.radius", 0.2f);
    for (const MapInfo& m : list_maps()) {
        const auto map = load_map(m.id);
        REQUIRE(map.has_value());
        // One floor: wherever Wobo fits, it can drive to anywhere else it fits.
        const auto labels = label_floor(*map, {}, radius);
        int components = 0;
        for (int l : labels) components = std::max(components, l + 1);
        CHECK(components == 1);
        RunDirector run(load_game_config());
        REQUIRE(run.start(m.id, 1));
        const LookAlikes alikes(*map, 2.5f, radius);
        std::printf("    %-10s colourful wall share %.2f, colour card %s, road block fits %s, colour twins %.2f, "
                    "repaint card %s\n",
                    m.id.c_str(), colourful_wall_share(*map), run.suits_map("colour") ? "yes" : "no",
                    run.suits_map("block") ? "yes" : "no", alikes.colour_twin_share(3.0f, 4, 200),
                    run.suits_map("repaint") ? "yes" : "no");
    }
    // The gallery is all about colour.
    CHECK(colourful_wall_share(*load_map("gallery")) > 0.3f);
}

TEST("movers: walk their rounds, and the walker never finishes") {
    Config cfg = load_game_config();
    cfg.set("referee.stability", "100000");  // keep Wobo lost: the run never ends
    for (const char* id : {"gallery", "barn", "konigsberg"}) {
        RunDirector run(cfg);
        REQUIRE(run.start(id, 2));
        REQUIRE(!run.movers().empty());
        const int steps = static_cast<int>(180.0 / run.step_s());
        for (int i = 0; i < steps && run.phase() != RunPhase::Over; ++i) {
            run.step();
            if (run.phase() == RunPhase::Drafting) run.skip_draft();
            // Never inside Wobo.
            const Mover& m = run.movers()[0];
            REQUIRE(distance(m.pose.position(), run.sim().true_pose().position()) >
                    m.radius + run.sim().config().dynamics.radius_m - 0.02f);
        }
        const Mover& m = run.movers()[0];
        std::printf("    %-10s %s walked %.0f m%s\n", id, m.kind.c_str(), m.stride_m,
                    m.kind == "walker" ? (", best " + std::to_string(m.best_crossings) + " bridges").c_str() : "");
        CHECK(m.stride_m > 15.0f);
        if (m.kind == "walker") {
            CHECK(m.best_crossings >= 2);
            CHECK(m.best_crossings < 7);
        }
    }
}

TEST("road block: only in the world, never cutting the map, and it clears") {
    Config cfg = load_game_config();
    cfg.set("energy.start", "100");
    cfg.set("action.block.owned", "1");
    RunDirector run(cfg);
    REQUIRE(run.start("office", 3));
    const Map2D& map = run.map();

    // Across an office doorway (row 6, the gap at x = 11) the barrier would
    // shut the office off; the hallway's 3-cell width cannot be closed either.
    const Vec2 door = map.cell_center(11, 6);
    CHECK(map.solid(10, 6) && map.solid(12, 6) && !map.solid(11, 6));
    CHECK(run.check_target(ActionId::Block, {door.x, door.z, kPi * 0.5f}) == ActivateResult::CutsOff);
    const Vec2 hall = map.cell_center(20, 8);
    CHECK(run.check_target(ActionId::Block, {hall.x, hall.z, kPi * 0.5f}) == ActivateResult::CutsOff);
    CHECK(run.check_target(ActionId::Block, {0.1f, 0.1f, 0.0f}) == ActivateResult::InvalidTarget);
    const Pose2D wobo = run.sim().true_pose();
    for (float heading : {0.0f, kPi * 0.5f}) {
        CHECK(run.check_target(ActionId::Block, {wobo.x, wobo.z, heading}) != ActivateResult::Ok);
    }

    // Along the hallway is fine.
    REQUIRE(run.check_target(ActionId::Block, {hall.x, hall.z, 0.0f}) == ActivateResult::Ok);
    REQUIRE(run.activate(ActionId::Block, Pose2D{hall.x, hall.z, 0.0f}) == ActivateResult::Ok);
    REQUIRE(run.road_block().has_value());
    CHECK(run.world().solid(20, 8) && run.world().solid(19, 8) && run.world().solid(21, 8));
    CHECK(!run.map().solid(20, 8));  // Wobo's map never learns about it

    const int steps = static_cast<int>((run.stats().get("action.block.duration") + 1.0f) / run.step_s());
    for (int i = 0; i < steps && run.phase() != RunPhase::Over; ++i) {
        run.step();
        if (run.phase() == RunPhase::Drafting) run.skip_draft();
    }
    CHECK(!run.road_block().has_value());
    CHECK(!run.world().solid(20, 8));
}

TEST("repainting works: Colour Blind repaints Wobo's surroundings in a look-alike's colours") {
    Config cfg = load_game_config();
    cfg.set("action.colour.owned", "1");
    cfg.set("action.colour.repaint_chance", "1");
    cfg.set("referee.stability", "100000");  // keep Wobo lost: the run never ends
    cfg.set("run.hour_length", "100000");
    cfg.set("energy.start", "1000");
    cfg.set("energy.capacity", "1000");
    RunDirector run(cfg);
    REQUIRE(run.start("gallery", 4));
    int repainted = 0;
    for (int i = 0; i < 40 * 30; ++i) {
        if (run.can_activate(ActionId::Colour) == ActivateResult::Ok) {
            REQUIRE(run.activate(ActionId::Colour) == ActivateResult::Ok);
            if (const auto twin = run.repaint_twin()) {
                ++repainted;
                CHECK(distance(*twin, run.sim().true_pose().position()) >= 2.9f);
                run.step();
                // Something Wobo can see now reads in another material's colour.
                const auto& table = run.modifiers().repaint;
                int changed = 0;
                for (std::size_t m = 0; m < table.size(); ++m) changed += table[m] != m ? 1 : 0;
                CHECK(changed > 0);
            }
        }
        run.step();
    }
    std::printf("    repainted on %d uses\n", repainted);
    CHECK(repainted > 0);
    // Off again once Colour Blind wears off.
    for (int i = 0; i < 30 * 30 && run.actions().active(ActionId::Colour); ++i) run.step();
    run.step();
    CHECK(run.modifiers().repaint.empty());
}

TEST("cards: map-specific abilities are only offered where they work") {
    // Every shipped map has some colour, so raise the bar until the office
    // (blackboards and desks on grey plaster) falls short of it.
    Config cfg = load_game_config();
    cfg.set("run.hour_length", "5");
    cfg.set("action.colour.min_colour_share", "0.3");
    RunDirector grey(cfg);
    REQUIRE(grey.start("office", 1));
    CHECK(!grey.suits_map("colour"));
    RunDirector gallery(cfg);
    REQUIRE(gallery.start("gallery", 1));
    CHECK(gallery.suits_map("colour"));
    CHECK(gallery.suits_map("block"));

    // Over many drafts on the office, no Colour Blind card (or its upgrade);
    // on the gallery it turns up.
    const auto offered = [](const RunDirector& run, bool own_colour) {
        CardDraftSystem cards;
        cards.reset(load_cards(), 7);
        int n = 0;
        for (int i = 0; i < 300; ++i) {
            const auto offer = cards.draw(
                3, 5, [&](const std::string& key) { return own_colour && key == "colour"; },
                [&](const std::string& key) { return run.suits_map(key); });
            for (const CardDef* c : offer) n += c->id == "colour" || c->id == "colour_long";
        }
        return n;
    };
    CHECK(offered(grey, false) == 0);
    CHECK(offered(grey, true) == 0);
    CHECK(offered(gallery, false) > 0);
    CHECK(offered(gallery, true) > 0);
}

TEST("map traits: a barrier needs three floor cells in a row that nothing depends on") {
    const std::vector<Material> mats = {Material{"wall", "Wall", Rgb{160, 160, 170}, true, 1.0f, Pattern::None, ""},
                                        Material{"floor", "Floor", Rgb{230, 230, 220}, false, 1.0f, Pattern::None, ""}};
    // A staircase of floor: never three cells in a row, so nothing fits.
    const int n = 8;
    std::vector<std::uint8_t> stairs(n * n, 0);
    for (int i = 0; i < n - 1; ++i) {
        stairs[i * n + i] = 1;
        stairs[i * n + i + 1] = 1;
    }
    const Map2D a("stairs", n, n, 0.5f, mats, stairs);
    CHECK(!barrier_fits_somewhere(a, label_floor(a), 3));
    // A 1-wide corridor joining two 3x3 rooms: any barrier in the corridor
    // cuts the rooms apart, but one along a room's edge row leaves it whole.
    std::vector<std::uint8_t> rooms(12 * 5, 0);
    for (int z = 1; z < 4; ++z) {
        for (int x = 1; x < 4; ++x) rooms[z * 12 + x] = rooms[z * 12 + x + 7] = 1;
    }
    for (int x = 4; x < 8; ++x) rooms[2 * 12 + x] = 1;
    const Map2D b("rooms", 12, 5, 0.5f, mats, rooms);
    const auto labels = label_floor(b);
    CHECK(barrier_fits_somewhere(b, labels, 3));
    CHECK(!stays_connected(b, labels, {b.index(5, 2)}));  // a single cell in the corridor cuts it
}
