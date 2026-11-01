// Headless localization benchmark: runs Wobo with no player interference
// across maps and seeds, and reports how quickly (and whether) it localizes,
// how long it takes to recover from a kidnapping (--kidnap: teleported as soon
// as it is halfway to locking on), and the CPU cost per simulated second.
// --player adds a simple scripted saboteur (see bot_step) so balance changes
// can be judged by how many days a reasonable player survives.
// Used both to validate the robotics and to balance the game.
//
//   wobo_bench [--map ID|all] [--seeds N] [--seed FIRST] [--limit S] [--kidnap] [--player]
//              [--localizer NAME] [--planner NAME] [--set key=value]...
//              [--data DIR] [--csv] [--trace]
//
// --trace prints, every two simulated seconds of each run, the true pose,
// Wobo's estimate and what it is doing (for debugging the agent).

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "core/data.h"
#include "gameplay/run_director.h"
#include "world/map_loader.h"

using namespace wobo;

namespace {

struct Result {
    double time_s = -1.0;           // to localization (or to the kidnap, with --kidnap)
    double recovery_s = -1.0;       // kidnap to re-localization
    int days = 1;                   // day reached
    double ms_per_sim_s = 0.0;
    float final_pos_err = 0.0f;
    float final_head_err = 0.0f;
};

// A deliberately ordinary player. When the lock-on alarm would sound (a lock
// held for a second) it breaks the lock: Kidnap if drafted, else Spin It.
// When Wobo's guess is merely close it pre-empts with Smoke Bomb or Colour
// Blind. Drafts the first card offered.
void bot_step(RunDirector& run, Rng& rng) {
    if (run.phase() == RunPhase::Drafting) {
        run.choose_card(0);
        return;
    }
    const LocalizationReferee& ref = run.referee();
    const bool alarm = ref.lock_progress() * ref.config().stability_s >= 1.0f;
    const bool close = ref.position_error() < 1.0f && run.actions().active_count() == 0;
    if (!alarm && !close) return;
    if (alarm && run.can_activate(ActionId::Kidnap) == ActivateResult::Ok) {
        for (int i = 0; i < 50; ++i) {
            const Pose2D target = run.map().sample_valid_pose(rng, 0.2f);
            if (run.activate(ActionId::Kidnap, target) == ActivateResult::Ok) return;
        }
    }
    const auto order = alarm ? std::initializer_list<ActionId>{ActionId::Drift, ActionId::Range}
                             : std::initializer_list<ActionId>{ActionId::Range, ActionId::Colour};
    for (ActionId id : order) {
        if (!run.actions().active(id) && run.activate(id) == ActivateResult::Ok) return;
    }
}

void trace_line(const RunDirector& run) {
    const Pose2D t = run.sim().true_pose();
    const PoseEstimate e = run.agent().estimate();
    std::printf("  %6.1f true(%5.1f,%5.1f,%4.0f) est(%5.1f,%5.1f,%4.0f) conf %.2f cons %.2f err %5.2f %s%s\n",
                run.sim().time_s(), t.x, t.z, t.heading_rad * kRadToDeg, e.pose.x, e.pose.z,
                e.pose.heading_rad * kRadToDeg, e.confidence, e.consistency, run.referee().position_error(),
                run.agent().plan().reason.c_str(), run.agent().moving() ? "" : " (still)");
}

Result run_one(const Config& cfg, const std::string& map, std::uint64_t seed, double limit_s,
               bool kidnap, bool player, bool trace) {
    Result r;
    RunDirector run(cfg);
    std::string err;
    if (!run.start(map, seed, &err)) {
        std::fprintf(stderr, "start failed: %s\n", err.c_str());
        return r;
    }
    // Kidnap scenario: the moment Wobo is halfway through locking on to the
    // right answer, teleport it somewhere far away, then time the recovery.
    bool kidnapped = false;
    double kidnap_time = 0.0;
    Rng rng(derive_seed(seed, "bench"));

    const auto t0 = std::chrono::steady_clock::now();
    const int steps = static_cast<int>(limit_s / run.step_s());
    for (int i = 0; i < steps && run.phase() != RunPhase::Over; ++i) {
        if (kidnap && !kidnapped && run.referee().lock_progress() >= 0.5f) {
            Pose2D target;
            do {
                target = run.map().sample_valid_pose(rng, 0.2f);
            } while (run.check_target(target) != ActivateResult::Ok ||
                     distance(target.position(), run.sim().true_pose().position()) < 4.0f);
            run.force_teleport(target);
            kidnapped = true;
            kidnap_time = run.sim().time_s();
            r.time_s = kidnap_time;
        }
        if (player) bot_step(run, rng);
        if (trace && i % static_cast<int>(2.0 / run.step_s()) == 0) trace_line(run);
        run.step();
        if (run.phase() == RunPhase::Drafting) {
            if (player) bot_step(run, rng);
            else run.skip_draft();
        }
    }
    r.days = run.score().day;
    if (run.phase() == RunPhase::Over) {
        if (kidnapped) r.recovery_s = run.sim().time_s() - kidnap_time;
        else r.time_s = run.sim().time_s();
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    r.ms_per_sim_s = ms / std::max(1e-6, run.sim().time_s());
    r.final_pos_err = run.referee().position_error();
    r.final_head_err = run.referee().heading_error() * kRadToDeg;
    return r;
}

double median(std::vector<double> v) {
    if (v.empty()) return -1.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

}  // namespace

int main(int argc, char** argv) {
    std::string map_arg = "all";
    int seeds = 8;
    int first_seed = 1;
    double limit_s = 300.0;
    bool kidnap = false;
    bool player = false;
    bool csv = false;
    bool trace = false;
    Config cfg = load_game_config();

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--map") map_arg = next();
        else if (a == "--seeds") seeds = std::atoi(next().c_str());
        else if (a == "--seed") first_seed = std::atoi(next().c_str());
        else if (a == "--limit") limit_s = std::atof(next().c_str());
        else if (a == "--kidnap") kidnap = true;
        else if (a == "--player") player = true;
        else if (a == "--localizer") cfg.set("localizer.type", next());
        else if (a == "--planner") cfg.set("planner.type", next());
        else if (a == "--csv") csv = true;
        else if (a == "--trace") trace = true;
        else if (a == "--data") {
            data::set_override_dir(next());
            cfg = load_game_config();
        } else if (a == "--set") {
            const std::string kv = next();
            const auto eq = kv.find('=');
            if (eq != std::string::npos) cfg.set(kv.substr(0, eq), kv.substr(eq + 1));
        } else {
            std::fprintf(stderr, "unknown argument %s\n", a.c_str());
            return 2;
        }
    }

    std::vector<std::string> maps;
    if (map_arg == "all") {
        for (const MapInfo& m : list_maps()) maps.push_back(m.id);
    } else {
        maps.push_back(map_arg);
    }

    if (csv) std::printf("map,seed,time_s,recovery_s,ms_per_sim_s,pos_err,head_err\n");
    else std::printf("%-12s %8s %8s %8s %9s %6s %10s\n", "map", "success", "median", "mean", "recovery", "day", "ms/sim-s");

    for (const std::string& map : maps) {
        std::vector<double> times;
        std::vector<double> recoveries;
        std::vector<double> days;
        double ms = 0.0;
        int ok = 0;
        for (int s = first_seed; s < first_seed + seeds; ++s) {
            const Result r = run_one(cfg, map, static_cast<std::uint64_t>(s), limit_s, kidnap, player, trace);
            ms += r.ms_per_sim_s;
            if (r.time_s >= 0.0) times.push_back(r.time_s);
            // "success": localized, or with --kidnap, recovered from the kidnap.
            if (kidnap ? r.recovery_s >= 0.0 : r.time_s >= 0.0) ++ok;
            if (r.recovery_s >= 0.0) recoveries.push_back(r.recovery_s);
            days.push_back(r.days);
            if (csv) {
                std::printf("%s,%d,%.2f,%.2f,%.3f,%.3f,%.2f\n", map.c_str(), s, r.time_s, r.recovery_s,
                            r.ms_per_sim_s, r.final_pos_err, r.final_head_err);
            }
        }
        if (!csv) {
            double mean = 0.0;
            for (double t : times) mean += t;
            mean = times.empty() ? -1.0 : mean / static_cast<double>(times.size());
            std::printf("%-12s %5d/%-2d %8.1f %8.1f %9.1f %6.0f %10.2f\n", map.c_str(), ok, seeds, median(times), mean,
                        median(recoveries), median(days), ms / seeds);
        }
    }
    return 0;
}
