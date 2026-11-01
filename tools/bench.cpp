// Headless localization benchmark: runs Wobo with no player interference
// across maps and seeds, and reports how quickly (and whether) it localizes,
// how long it takes to recover from a kidnapping (--kidnap: teleported as soon
// as it is halfway to locking on), and the CPU cost per simulated second.
// --player [weak|medium|strong] adds a scripted saboteur (see bot_step) so
// balance changes can be judged by how many hours players of each skill last.
// Used both to validate the robotics and to balance the game.
//
//   wobo_bench [--map ID|all] [--seeds N] [--seed FIRST] [--limit S] [--kidnap] [--player [LEVEL]]
//              [--study [--only KEY,...]] [--localizer NAME] [--planner NAME] [--set key=value]...
//              [--data DIR] [--csv] [--trace]
//
// --trace prints, every two simulated seconds of each run, the true pose,
// Wobo's estimate and what it is doing (for debugging the agent).
//
// --study measures what each player action does to the localizer at full
// strength (final compute, fully calibrated), with energy and ownership free. "Lock": the moment Wobo is halfway to locking
// on, the action is used once; how often the lock breaks, how far the
// estimate strays in the next 20 s, and how long until Wobo is found again.
// "Spam": the action is used whenever it is off cooldown from the start;
// how many runs Wobo still localizes in, and how long it takes.
// --only limits it to some actions, by key (none, range, drift, kidnap,
// colour, gummy, block, overload, magnet, ...).

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <initializer_list>
#include <optional>
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
    int hours = 1;                  // hour reached
    double ms_per_sim_s = 0.0;
    float final_pos_err = 0.0f;
    float final_head_err = 0.0f;
};

// A Road Block where it hurts: across Wobo's way ahead if it fits, else the
// nearest spot it fits within scanner range, so it shows up in the scans.
std::optional<Pose2D> block_spot(const RunDirector& run) {
    const Pose2D p = run.sim().true_pose();
    for (float d : {1.0f, 1.3f, 0.8f, 1.6f}) {
        const Vec2 at{p.x + std::cos(p.heading_rad) * d, p.z + std::sin(p.heading_rad) * d};
        const float across = std::fabs(std::cos(p.heading_rad)) > std::fabs(std::sin(p.heading_rad)) ? kPi * 0.5f : 0.0f;
        for (float h : {across, across + kPi * 0.5f}) {
            if (run.check_target(ActionId::Block, {at.x, at.z, h}) == ActivateResult::Ok) return Pose2D{at.x, at.z, h};
        }
    }
    std::optional<Pose2D> best;
    float best_d = 2.5f;
    for (int cz = 0; cz < run.map().height(); ++cz) {
        for (int cx = 0; cx < run.map().width(); ++cx) {
            const Vec2 c = run.map().cell_center(cx, cz);
            const float d = distance(c, p.position());
            if (d >= best_d || d < 0.5f) continue;
            for (float h : {0.0f, kPi * 0.5f}) {
                if (run.check_target(ActionId::Block, {c.x, c.z, h}) == ActivateResult::Ok) {
                    best = Pose2D{c.x, c.z, h};
                    best_d = d;
                    break;
                }
            }
        }
    }
    return best;
}

// Scripted saboteurs of three skill levels (--player weak|medium|strong), so
// balance changes can be judged by the hour each one reaches.
enum class Profile { None, Weak, Medium, Strong };

bool lock_alarm(const RunDirector& run) {
    const LocalizationReferee& ref = run.referee();
    return ref.lock_progress() * ref.config().stability_s >= 1.0f;
}

bool kidnap_anywhere(RunDirector& run, Rng& rng) {
    for (int i = 0; i < 50; ++i) {
        const Pose2D target = run.map().sample_valid_pose(rng, 0.2f);
        if (run.activate(ActionId::Kidnap, target) == ActivateResult::Ok) return true;
    }
    return false;
}

// A Magnet where it drags Wobo farthest before anything stops it.
std::optional<Pose2D> magnet_spot(const RunDirector& run) {
    const Pose2D p = run.sim().true_pose();
    const float range = run.stats().get("action.magnet.range", 5.0f);
    std::optional<Pose2D> best;
    float best_d = 0.5f;
    for (int i = 0; i < 32; ++i) {
        const float a = kTwoPi * i / 32.0f;
        for (float k : {0.95f, 0.75f, 0.5f}) {
            const Pose2D t{p.x + std::cos(a) * range * k, p.z + std::sin(a) * range * k, 0.0f};
            if (run.check_target(ActionId::Magnet, t) != ActivateResult::Ok) continue;
            const float d = distance(run.magnet_stop(t.position()), p.position());
            if (d > best_d) {
                best = t;
                best_d = d;
            }
        }
    }
    return best;
}

bool magnet_far(RunDirector& run) {
    if (run.can_activate(ActionId::Magnet) != ActivateResult::Ok) return false;
    const auto spot = magnet_spot(run);
    return spot && run.activate(ActionId::Magnet, *spot) == ActivateResult::Ok;
}

bool magnet_anywhere(RunDirector& run, Rng& rng) {
    const Pose2D p = run.sim().true_pose();
    const float range = run.stats().get("action.magnet.range", 5.0f);
    for (int i = 0; i < 50; ++i) {
        const float a = rng.uniform() * kTwoPi;
        const float d = range * std::sqrt(rng.uniform());
        const Pose2D t{p.x + std::cos(a) * d, p.z + std::sin(a) * d, 0.0f};
        if (run.activate(ActionId::Magnet, t) == ActivateResult::Ok) return true;
    }
    return false;
}

bool block_ahead(RunDirector& run) {
    if (run.can_activate(ActionId::Block) != ActivateResult::Ok) return false;
    const auto spot = block_spot(run);
    return spot && run.activate(ActionId::Block, *spot) == ActivateResult::Ok;
}

// An ordinary player, who keeps something running all the time: any action
// that is ready and affordable, in a fixed order (Gyro Jam, Smoke Bomb,
// Gummy Wheels, Colour Blind, Overload, a Road Block across Wobo's way, a
// Magnet that drags Wobo as far as it can), whether or not
// Wobo is anywhere near the truth. Saves Kidnap, to a random spot, for the
// lock-on alarm (a lock held for a second). Drafts the first card offered.
void medium_step(RunDirector& run, Rng& rng) {
    if (run.phase() == RunPhase::Drafting) {
        run.choose_card(0);
        return;
    }
    if (lock_alarm(run) && run.can_activate(ActionId::Kidnap) == ActivateResult::Ok && kidnap_anywhere(run, rng)) return;
    for (ActionId id : {ActionId::Drift, ActionId::Range, ActionId::Gummy, ActionId::Colour, ActionId::Overload}) {
        if (!run.actions().active(id) && run.activate(id) == ActivateResult::Ok) return;
    }
    if (block_ahead(run)) return;
    magnet_far(run);
}

// A beginner: drafts at random and, about every other second, presses any
// action that is ready and affordable, Kidnap (to a random spot) included,
// whatever Wobo is doing; so it is often short of energy when it matters.
void weak_step(RunDirector& run, Rng& rng, double& next_look_s) {
    if (run.phase() == RunPhase::Drafting) {
        run.choose_card(static_cast<int>(rng.below(static_cast<std::uint32_t>(run.offer().size()))));
        return;
    }
    if (run.sim().time_s() < next_look_s) return;
    next_look_s = run.sim().time_s() + 1.0;
    if (!rng.chance(0.6f)) return;
    std::vector<ActionId> usable;
    for (int i = 0; i < static_cast<int>(ActionId::Count); ++i) {
        const auto id = static_cast<ActionId>(i);
        if (!run.actions().active(id) && run.can_activate(id) == ActivateResult::Ok) usable.push_back(id);
    }
    if (usable.empty()) return;
    const ActionId id = usable[rng.below(static_cast<std::uint32_t>(usable.size()))];
    if (id == ActionId::Kidnap) kidnap_anywhere(run, rng);
    else if (id == ActionId::Block) block_ahead(run);
    else if (id == ActionId::Magnet) magnet_anywhere(run, rng);
    else run.activate(id);
}

// An expert: plays like the ordinary player (holding energy back measured
// worse than spending it), but drafts by a fixed priority (Kidnap and what
// feeds it, Magnet, then Gyro Jam, Overload, energy, and the rest, as
// --study ranks them), favours the stronger effects, Kidnaps to the spot
// farthest from where Wobo thinks it is, and saves Magnet, then Overload, for
// a lock-on alarm that Kidnap can't answer.
void strong_step(RunDirector& run, Rng& rng) {
    static const char* const kPriority[] = {
        "kidnap", "spare_kidnap", "kidnap_cool", "kidnap_cheap", "drift_long", "aftershock", "overload",
        "overload_cool", "overload_long", "battery", "capacitor", "solar", "magnet", "magnet_long",
        "magnet_strong", "overclock", "vertigo", "drift_strong", "gummy", "gummy_sticky", "block",
        "block_long", "brinkmanship", "chain_reaction", "power_surge", "cold_snap", "colour", "repaint",
        "colour_long", "range_cheap", "range_long", "range_deep", "static_cling"};
    if (run.phase() == RunPhase::Drafting) {
        int best = 0;
        std::size_t best_rank = std::size(kPriority);
        for (std::size_t i = 0; i < run.offer().size(); ++i) {
            for (std::size_t r = 0; r < best_rank; ++r) {
                if (run.offer()[i]->id == kPriority[r]) {
                    best = static_cast<int>(i);
                    best_rank = r;
                }
            }
        }
        run.choose_card(best);
        return;
    }
    if (lock_alarm(run) && run.can_activate(ActionId::Kidnap) == ActivateResult::Ok) {
        const Vec2 guess = run.agent().estimate().pose.position();
        std::optional<Pose2D> far;
        float far_d = 0.0f;
        for (int i = 0; i < 40; ++i) {
            const Pose2D t = run.map().sample_valid_pose(rng, 0.2f);
            const float d = distance(t.position(), guess);
            if (d > far_d && run.check_target(ActionId::Kidnap, t) == ActivateResult::Ok) {
                far = t;
                far_d = d;
            }
        }
        if (far && run.activate(ActionId::Kidnap, *far) == ActivateResult::Ok) return;
    }
    if (lock_alarm(run) && (run.activate(ActionId::Overload) == ActivateResult::Ok || magnet_far(run))) return;
    for (ActionId id : {ActionId::Drift, ActionId::Gummy, ActionId::Colour, ActionId::Range}) {
        if (!run.actions().active(id) && run.activate(id) == ActivateResult::Ok) return;
    }
    block_ahead(run);
}

void bot_step(Profile profile, RunDirector& run, Rng& rng, double& next_look_s) {
    switch (profile) {
        case Profile::Weak: weak_step(run, rng, next_look_s); break;
        case Profile::Medium: medium_step(run, rng); break;
        case Profile::Strong: strong_step(run, rng); break;
        case Profile::None: break;
    }
}

void trace_line(const RunDirector& run) {
    const Pose2D t = run.sim().true_pose();
    const PoseEstimate e = run.agent().estimate();
    std::string effects;
    for (int i = 0; i < static_cast<int>(ActionId::Count); ++i) {
        if (run.actions().active(static_cast<ActionId>(i))) effects += std::string(" +") + action_info(static_cast<ActionId>(i)).key;
    }
    std::printf("  %6.1f true(%5.1f,%5.1f,%4.0f) est(%5.1f,%5.1f,%4.0f) conf %.2f cons %.2f err %5.2f E%3.0f %s%s%s\n",
                run.sim().time_s(), t.x, t.z, t.heading_rad * kRadToDeg, e.pose.x, e.pose.z,
                e.pose.heading_rad * kRadToDeg, e.confidence, e.consistency, run.referee().position_error(),
                run.energy().current(), run.agent().plan().reason.c_str(), run.agent().moving() ? "" : " (still)",
                effects.c_str());
}

Result run_one(const Config& cfg, const std::string& map, std::uint64_t seed, double limit_s,
               bool kidnap, Profile player, bool trace) {
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
    double next_look_s = 0.0;

    const auto t0 = std::chrono::steady_clock::now();
    const int steps = static_cast<int>(limit_s / run.step_s());
    for (int i = 0; i < steps && run.phase() != RunPhase::Over; ++i) {
        if (kidnap && !kidnapped && run.referee().lock_progress() >= 0.5f) {
            Pose2D target;
            do {
                target = run.map().sample_valid_pose(rng, 0.2f);
            } while (run.check_target(ActionId::Kidnap, target) != ActivateResult::Ok ||
                     distance(target.position(), run.sim().true_pose().position()) < 4.0f);
            run.force_teleport(target);
            kidnapped = true;
            kidnap_time = run.sim().time_s();
            r.time_s = kidnap_time;
        }
        bot_step(player, run, rng, next_look_s);
        if (trace && i % static_cast<int>(2.0 / run.step_s()) == 0) trace_line(run);
        run.step();
        if (run.phase() == RunPhase::Drafting) {
            if (player != Profile::None) bot_step(player, run, rng, next_look_s);
            else run.skip_draft();
        }
    }
    r.hours = run.score().hour;
    if (run.phase() == RunPhase::Over && !run.score().survived) {
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

// --- --study ------------------------------------------------------------

constexpr ActionId kNoAction = ActionId::Count;

const char* study_name(ActionId id) { return id == kNoAction ? "(none)" : action_info(id).name; }

bool use_action(RunDirector& run, ActionId id, Rng& rng) {
    if (id == kNoAction) return true;
    if (id == ActionId::Kidnap) {
        for (int i = 0; i < 200; ++i) {
            const Pose2D t = run.map().sample_valid_pose(rng, 0.2f);
            if (distance(t.position(), run.sim().true_pose().position()) < 4.0f) continue;
            if (run.activate(id, t) == ActivateResult::Ok) return true;
        }
        return false;
    }
    if (id == ActionId::Block) {
        const auto spot = block_spot(run);
        return spot && run.activate(id, *spot) == ActivateResult::Ok;
    }
    if (id == ActionId::Magnet) {
        const auto spot = magnet_spot(run);
        return spot && run.activate(id, *spot) == ActivateResult::Ok;
    }
    return run.activate(id) == ActivateResult::Ok;
}

struct StudyResult {
    bool used = false;
    bool broke = false;          // the lock was lost before Wobo was found
    double refind_s = -1.0;      // action to localization
    float peak_err = 0.0f;       // worst estimate error in the 20 s after the action
    double spam_found_s = -1.0;  // with --spam: time to localization
};

Config study_config(Config cfg) {
    for (int i = 0; i < kActionCount; ++i) {
        cfg.set(std::string("action.") + action_info(static_cast<ActionId>(i)).key + ".owned", "1");
    }
    cfg.set("energy.capacity", "1000000");
    cfg.set("energy.start", "1000000");
    cfg.set("energy.regen", "1000");
    cfg.set("run.hour_length", "1000000");
    // One endless hour would leave Wobo as it is in hour 1, on little
    // compute and uncalibrated: measure against it at full strength instead,
    // with the compute it ends up with.
    cfg.set("compute.share", std::to_string(cfg.get_float("compute.final_share", 1.0f)));
    cfg.set("compute.update_hz", "0");
    cfg.set("calibration.range_noise", "0");
    cfg.set("calibration.colour_noise", "0");
    cfg.set("calibration.odometry_noise", "0");
    return cfg;
}

StudyResult study_lock(const Config& cfg, const std::string& map, std::uint64_t seed, ActionId id, double limit_s) {
    StudyResult r;
    RunDirector run(cfg);
    if (!run.start(map, seed)) return r;
    Rng rng(derive_seed(seed, "study"));
    double used_at = -1.0;
    const int steps = static_cast<int>(limit_s / run.step_s());
    for (int i = 0; i < steps && run.phase() != RunPhase::Over; ++i) {
        if (used_at < 0.0 && run.referee().lock_progress() >= 0.5f) {
            if (!use_action(run, id, rng)) return r;
            r.used = true;
            used_at = run.sim().time_s();
        }
        run.step();
        if (used_at >= 0.0) {
            if (run.referee().lock_progress() <= 0.0f && run.phase() != RunPhase::Over) r.broke = true;
            if (run.sim().time_s() - used_at <= 20.0) r.peak_err = std::max(r.peak_err, run.referee().position_error());
        }
    }
    if (r.used && run.phase() == RunPhase::Over) r.refind_s = run.sim().time_s() - used_at;
    return r;
}

StudyResult study_spam(const Config& cfg, const std::string& map, std::uint64_t seed, ActionId id, double limit_s) {
    StudyResult r;
    RunDirector run(cfg);
    if (!run.start(map, seed)) return r;
    Rng rng(derive_seed(seed, "study"));
    const int steps = static_cast<int>(limit_s / run.step_s());
    for (int i = 0; i < steps && run.phase() != RunPhase::Over; ++i) {
        if (id != kNoAction && run.sim().time_s() > 3.0 && run.can_activate(id) == ActivateResult::Ok) {
            use_action(run, id, rng);
        }
        run.step();
    }
    if (run.phase() == RunPhase::Over) r.spam_found_s = run.sim().time_s();
    return r;
}

double median(std::vector<double> v);

void run_study(const Config& base, const std::vector<std::string>& maps, int first_seed, int seeds, double limit_s,
               const std::string& only) {
    const Config cfg = study_config(base);
    const ActionId ids[] = {kNoAction, ActionId::Range, ActionId::Drift, ActionId::Gummy,
                            ActionId::Colour, ActionId::Block, ActionId::Overload, ActionId::Magnet,
                            ActionId::Kidnap};
    std::printf("%-12s %-13s | %6s %7s %8s %9s | %7s %8s\n", "map", "action", "used", "broke", "refind", "peak err",
                "spam ok", "median");
    for (const std::string& map : maps) {
        for (ActionId id : ids) {
            const std::string key = id == kNoAction ? "none" : action_info(id).key;
            if (!only.empty() && ("," + only + ",").find("," + key + ",") == std::string::npos) continue;
            int used = 0, broke = 0, spam_ok = 0;
            std::vector<double> refind, peak, spam;
            for (int s = first_seed; s < first_seed + seeds; ++s) {
                const StudyResult a = study_lock(cfg, map, static_cast<std::uint64_t>(s), id, limit_s);
                if (a.used) {
                    ++used;
                    broke += a.broke ? 1 : 0;
                    // Not found again within the limit counts as the limit.
                    refind.push_back(a.refind_s >= 0.0 ? a.refind_s : limit_s);
                    peak.push_back(a.peak_err);
                }
                const StudyResult b = study_spam(cfg, map, static_cast<std::uint64_t>(s), id, limit_s);
                if (b.spam_found_s >= 0.0) {
                    ++spam_ok;
                    spam.push_back(b.spam_found_s);
                }
            }
            std::printf("%-12s %-13s | %3d/%-2d %4d/%-2d %7.1fs %8.2fm | %4d/%-2d %7.1fs\n", map.c_str(), study_name(id),
                        used, seeds, broke, used, median(refind), median(peak), spam_ok, seeds, median(spam));
            std::fflush(stdout);
        }
    }
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
    Profile player = Profile::None;
    bool csv = false;
    bool trace = false;
    bool study = false;
    std::string only;
    Config cfg = load_game_config();

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--map") map_arg = next();
        else if (a == "--seeds") seeds = std::atoi(next().c_str());
        else if (a == "--seed") first_seed = std::atoi(next().c_str());
        else if (a == "--limit") limit_s = std::atof(next().c_str());
        else if (a == "--kidnap") kidnap = true;
        else if (a == "--player") {
            player = Profile::Medium;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                const std::string level = next();
                if (level == "weak") player = Profile::Weak;
                else if (level == "strong") player = Profile::Strong;
                else if (level != "medium") {
                    std::fprintf(stderr, "unknown player %s (weak, medium or strong)\n", level.c_str());
                    return 2;
                }
            }
        }
        else if (a == "--localizer") cfg.set("localizer.type", next());
        else if (a == "--planner") cfg.set("planner.type", next());
        else if (a == "--csv") csv = true;
        else if (a == "--trace") trace = true;
        else if (a == "--study") study = true;
        else if (a == "--only") only = next();
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

    if (study) {
        run_study(cfg, maps, first_seed, seeds, limit_s, only);
        return 0;
    }

    if (csv) std::printf("map,seed,time_s,recovery_s,ms_per_sim_s,pos_err,head_err\n");
    else std::printf("%-12s %8s %8s %8s %9s %6s %10s\n", "map", "success", "median", "mean", "recovery", "hour", "ms/sim-s");

    for (const std::string& map : maps) {
        std::vector<double> times;
        std::vector<double> recoveries;
        std::vector<double> hours;
        double ms = 0.0;
        int ok = 0;
        for (int s = first_seed; s < first_seed + seeds; ++s) {
            const Result r = run_one(cfg, map, static_cast<std::uint64_t>(s), limit_s, kidnap, player, trace);
            ms += r.ms_per_sim_s;
            if (r.time_s >= 0.0) times.push_back(r.time_s);
            // "success": localized, or with --kidnap, recovered from the kidnap.
            if (kidnap ? r.recovery_s >= 0.0 : r.time_s >= 0.0) ++ok;
            if (r.recovery_s >= 0.0) recoveries.push_back(r.recovery_s);
            hours.push_back(r.hours);
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
                        median(recoveries), median(hours), ms / seeds);
        }
    }
    return 0;
}
