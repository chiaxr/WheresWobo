#include "app/game_app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sstream>

#include "raylib.h"
#include "rlgl.h"

#include "app/click_capture.h"
#include "app/save.h"
#include "rendering/debug_renderer.h"
#include "rendering/primitives.h"
#include "ui/card_screen.h"
#include "ui/menus.h"
#include "rendering/mover_models.h"
#include "ui/poster.h"
#include "ui/ui_text.h"
#include "ui/widgets.h"

namespace wobo {

namespace {

constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 720;

Color event_colour(ActionId id) {
    switch (id) {
        case ActionId::Range: return ui::kCyan;
        case ActionId::Drift: return Color{255, 150, 50, 255};
        case ActionId::Kidnap: return ui::kDanger;
        case ActionId::Gummy: return ui::kGummy;
        case ActionId::Block: return ui::kRoadBlock;
        default: return ui::kInk;
    }
}

// The capture ring is drawn a little wider than the referee's real position
// tolerance, which is smaller than Wobo's body and would hide under it.
// How long a lock must hold before the alarm (screen-edge pulse, countdown,
// "LOCKING ON") sounds. Wobo's guess often flickers in and out of tolerance;
// the alarm should mean it has actually settled on the right answer.
constexpr float kAlarmDelayS = 1.0f;

float capture_ring_radius(const LocalizationReferee& ref) {
    return std::max(ref.config().position_tolerance_m + 0.22f, 0.5f);
}

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void GameApp::init(const LaunchOptions& options) {
    options_ = options;
    profile_.seconds = options.profile_s;
    const bool profiling = profile_.seconds > 0.0f;
    const bool poster = !options.poster_path.empty() || !options.shots_dir.empty();
    // Profiling runs uncapped, so frame times show the real cost. A poster is
    // drawn offscreen, so its window stays hidden.
    SetConfigFlags(FLAG_MSAA_4X_HINT | (poster ? FLAG_WINDOW_HIDDEN : FLAG_WINDOW_RESIZABLE) |
                   (profiling || poster ? 0u : FLAG_VSYNC_HINT));
    // A poster's size is its render target's; its window need not fit the screen.
    if (poster) InitWindow(320, 180, "Where's Wobo?");
    else InitWindow(options.width > 0 ? options.width : kDefaultWidth, options.height > 0 ? options.height : kDefaultHeight,
                    "Where's Wobo?");
    SetTargetFPS(profiling ? 0 : 60);
    SetExitKey(KEY_NULL);

    click_capture::install();
    ui::load_fonts();
    toon_.load();
    if (!toon_.valid()) TraceLog(LOG_WARNING, "WOBO: toon shader failed to compile; drawing unlit");

    config_ = load_game_config();
    maps_ = list_maps();
    seed_counter_ = static_cast<std::uint64_t>(std::time(nullptr));
    load_progress();
    go_title();
}

void GameApp::shutdown() {
    if (options_.poster_path.empty() && options_.shots_dir.empty()) save_progress();
    iso_.unload();
    toon_.unload();
    ui::unload_fonts();
    CloseWindow();
}

float GameApp::ui_scale() const {
    // Lay out for a 1280x720 CSS-pixel screen and scale to fit, in device pixels.
    const float css_w = GetScreenWidth() / pixel_scale_;
    const float css_h = GetScreenHeight() / pixel_scale_;
    return pixel_scale_ * std::clamp(std::min(css_w / 1280.0f, css_h / 720.0f), 0.5f, 1.6f);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void GameApp::load_progress() {
    std::istringstream in(load_save_text());
    std::string line;
    while (std::getline(in, line)) {
        // best.<map>=<seconds>,<hour>
        const auto eq = line.find('=');
        if (line.rfind("best.", 0) != 0 || eq == std::string::npos) continue;
        Best b;
        if (std::sscanf(line.c_str() + eq + 1, "%lf,%d", &b.seconds, &b.hour) == 2) {
            best_[line.substr(5, eq - 5)] = b;
        }
    }
}

void GameApp::save_progress() {
    std::ostringstream out;
    for (const auto& [map, b] : best_) out << "best." << map << "=" << b.seconds << "," << b.hour << "\n";
    store_save_text(out.str());
}

std::string GameApp::best_text(const std::string& map_id) const {
    const auto it = best_.find(map_id);
    if (it == best_.end()) return "No best yet - how long can you last?";
    return "Best: lost until " + ui::survival_text(it->second.seconds, config_.get_float("run.hour_length", 120.0f));
}

std::string GameApp::best_short(const std::string& map_id) const {
    const auto it = best_.find(map_id);
    if (it == best_.end()) return "Not played yet";
    return "Best: " + ui::survival_text(it->second.seconds, config_.get_float("run.hour_length", 120.0f));
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

void GameApp::go_title() {
    screen_ = Screen::Title;
    run_.reset();
    menu_open_ = sim_paused_ = targeting_ = aiming_ = false;
    fx_.clear();
    restart_demo();
}

void GameApp::restart_demo() {
    if (maps_.empty()) return;
    // The attract-mode robot plays with a more forgiving referee off, so it
    // keeps wandering: it never "wins" on the title screen.
    Config cfg = config_;
    cfg.set("referee.stability", "100000");
    demo_ = std::make_unique<RunDirector>(cfg);
    std::string err;
    if (!demo_->start(maps_[map_index_].id, ++seed_counter_, &err)) {
        TraceLog(LOG_ERROR, "WOBO: %s", err.c_str());
        demo_.reset();
        return;
    }
    demo_clock_ = FixedStepClock(demo_->step_s());
    bind_map(*demo_);
}

void GameApp::start_run() {
    if (maps_.empty()) return;
    run_ = std::make_unique<RunDirector>(config_);
    const std::uint64_t seed = ++seed_counter_;
    std::string err;
    if (!run_->start(maps_[map_index_].id, seed, &err)) {
        TraceLog(LOG_ERROR, "WOBO: %s", err.c_str());
        run_.reset();
        return;
    }
    TraceLog(LOG_INFO, "WOBO: run on %s, seed %llu", maps_[map_index_].id.c_str(),
             static_cast<unsigned long long>(seed));
    demo_.reset();
    clock_ = FixedStepClock(run_->step_s());
    screen_ = Screen::Playing;
    menu_open_ = sim_paused_ = show_help_ = false;
    targeting_ = aiming_ = false;
    new_best_ = false;
    last_lock_ = lock_shown_ = alarm_ = 0.0f;
    crane_ = CraneAnimation{};
    spin_start_ = dizzy_until_ = -100.0f;
    fx_.clear();
    bind_map(*run_);
    toast("Hour 1. Keep Wobo lost!", ui::kEnergy);
}

void GameApp::bind_map(const RunDirector& run) {
    if (bound_map_ != run.map_id() || bound_run_ != &run) {
        iso_.build(run.map(), toon_);
        bound_map_ = run.map_id();
        bound_run_ = &run;
    }
    refit_camera(run);
}

void GameApp::refit_camera(const RunDirector& run) {
    const int w = GetScreenWidth();
    const int h = GetScreenHeight();
    if (screen_ == Screen::Title) {
        const ui::TitleLayout tl = ui::layout_title(w, h, ui_scale(), static_cast<int>(maps_.size()));
        // The wordmark only covers the top-left, so the board may tuck in
        // beside it.
        camera_.set_insets((tl.title.y + tl.title.height * 0.6f) / h, (h - tl.panel.y) / static_cast<float>(h));
    } else {
        const ui::HudLayout hud = ui::layout_hud(w, h, ui_scale(), run);
        camera_.set_insets(hud.top_inset, hud.bottom_inset);
    }
    camera_.set_top_down(top_down_);
    camera_.fit(run.map().world_width(), run.map().world_depth(), w, h);
    last_w_ = w;
    last_h_ = h;
}

void GameApp::toast(std::string text, Color colour) {
    toast_ = std::move(text);
    toast_t_ = 2.4f;
    toast_colour_ = colour;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void GameApp::frame() {
    const float dt = std::min(GetFrameTime(), 0.1f);
    time_ += dt;
    const PointerFrame in = input_.poll(10.0f * pixel_scale_);
    if (profile_.seconds > 0.0f) profile_frame();
    if (!options_.poster_path.empty()) {
        render_poster();
        finished_ = true;
        return;
    }
    if (!options_.shots_dir.empty()) {
        render_shots();
        finished_ = true;
        return;
    }
    if (GetScreenWidth() != last_w_ || GetScreenHeight() != last_h_) {
        if (run_) refit_camera(*run_);
        else if (demo_) refit_camera(*demo_);
    }
    update(dt, in);
    draw();
}

void GameApp::update(float dt, const PointerFrame& in) {
    toast_t_ = std::max(0.0f, toast_t_ - dt);
    fx_.update(dt);
    camera_.update(dt);
    switch (screen_) {
        case Screen::Title: update_title(dt, in); break;
        case Screen::Playing: update_playing(dt, in); break;
        case Screen::Over: update_over(dt, in); break;
    }
}

void GameApp::step_run(RunDirector& run, FixedStepClock& clock, float dt, bool is_player_run) {
    const int steps = clock.advance(dt);
    for (int i = 0; i < steps; ++i) {
        run.step();
        handle_events(run, is_player_run);
        if (run.phase() != RunPhase::Playing) {
            clock.reset();
            break;
        }
    }
    const auto& sim = run.sim();
    const float speed = run.phase() == RunPhase::Playing && !clock.paused()
                            ? static_cast<float>(clock.speed()) : 0.0f;
    wobo_.update(dt * std::max(speed, 0.3f), sim.dynamics().linear_velocity() * (speed > 0 ? 1.0f : 0.0f),
                 sim.dynamics().angular_velocity(), run.agent().moving());
}

void GameApp::handle_events(RunDirector& run, bool is_player_run) {
    for (const GameEvent& e : run.drain_events()) {
        const Vec2 at = run.sim().true_pose().position();
        switch (e.type) {
            case GameEvent::Type::Teleported:
                // The crane plays it out; the landing effects follow on touchdown.
                crane_.start(e.from, e.to);
                fx_.float_text(e.from.position(), "Kidnapped!", ui::kDanger);
                break;
            case GameEvent::Type::ActionUsed:
                if (e.action == ActionId::Drift) spin_start_ = time_;
                if (e.action == ActionId::Block) {
                    fx_.poof(e.to.position(), ui::kRoadBlock);
                    fx_.float_text(e.to.position(), e.text, event_colour(e.action));
                } else if (e.action != ActionId::Kidnap) {
                    fx_.float_text(at, e.text, event_colour(e.action));
                }
                break;
            case GameEvent::Type::BlockCleared:
                fx_.poof(e.from.position(), ui::kRoadBlock);
                break;
            case GameEvent::Type::Repainted:
                // A splash where Wobo is, and at the place its walls now pass for.
                fx_.poof(at, ui::kBelief);
                fx_.poof(e.to.position(), ui::kBelief);
                fx_.float_text(e.to.position(), e.text, ui::kBelief);
                break;
            case GameEvent::Type::HourEnded:
                if (is_player_run) toast(e.text + " - pick a card!", ui::kGood);
                break;
            case GameEvent::Type::CardChosen:
                toast("Drafted: " + e.text, ui::kEnergy);
                break;
            case GameEvent::Type::CloseCall:
                if (is_player_run) {
                    fx_.float_text(at, "Close call!", ui::kGood);
                    toast("Close call! Its lock-on slipped.", ui::kGood);
                }
                break;
            case GameEvent::Type::Localized:
                fx_.float_text(at, "Found it!", ui::kEnergy);
                fx_.poof(at, ui::kEnergy);
                break;
            case GameEvent::Type::Spin:
                fx_.float_text(at, "?!", ui::kEnergy);
                break;
        }
    }
}

void GameApp::update_title(float dt, const PointerFrame& in) {
    const int w = GetScreenWidth();
    const int h = GetScreenHeight();
    const float s = ui_scale();

    if (demo_) {
        demo_clock_.set_paused(external_pause_);
        step_run(*demo_, demo_clock_, dt, false);
        if (demo_->phase() == RunPhase::Drafting) demo_->skip_draft();
        if (demo_->sim().time_s() > 150.0) restart_demo();
    }

    if (show_help_) {
        const ui::HelpLayout hl = ui::layout_help(w, h, s);
        if ((in.tap && ui::contains(hl.close, in.pos)) || in.key(KEY_ESCAPE) || in.key(KEY_ENTER)) {
            show_help_ = false;
        }
        return;
    }

    const ui::TitleLayout tl = ui::layout_title(w, h, s, static_cast<int>(maps_.size()));
    const int n = static_cast<int>(maps_.size());
    int pick = -1;
    if (in.tap) {
        for (int i = 0; i < static_cast<int>(tl.cards.size()); ++i) {
            if (ui::contains(tl.cards[i], in.pos)) pick = i;
        }
    }
    int change = 0;
    if ((in.tap && tl.prev.width > 0.0f && ui::contains(tl.prev, in.pos)) || in.key(KEY_LEFT)) change = -1;
    if ((in.tap && tl.next.width > 0.0f && ui::contains(tl.next, in.pos)) || in.key(KEY_RIGHT)) change = 1;
    if (change != 0 && n > 0) pick = (map_index_ + change + n) % n;
    if (pick >= 0 && pick < n && pick != map_index_) {
        map_index_ = pick;
        restart_demo();
    }
    if ((in.tap && ui::contains(tl.play, in.pos)) || in.key(KEY_ENTER) || in.key(KEY_SPACE)) {
        start_run();
        return;
    }
    if ((in.tap && ui::contains(tl.help, in.pos)) || in.key(KEY_H)) show_help_ = true;

    // Let the player look around the map they are choosing.
    if (in.dragging && !ui::contains(tl.panel, in.press_pos)) camera_.pan_pixels(in.drag_delta, h);
    if (in.pan_delta.x != 0.0f || in.pan_delta.y != 0.0f) camera_.pan_pixels(in.pan_delta, h);
    if (in.zoom != 1.0f) camera_.zoom_at(in.zoom, in.zoom_centre, w, h);
}

void GameApp::use_action(ActionId id) {
    if (!run_) return;
    if (action_info(id).targeted) {
        if (targeting_) {
            targeting_ = aiming_ = false;
            return;
        }
        const ActivateResult r = run_->can_activate(id);
        if (r != ActivateResult::Ok) {
            toast(activate_result_text(r), ui::kDanger);
            return;
        }
        targeting_ = true;
        target_id_ = id;
        aiming_ = false;
        return;
    }
    targeting_ = aiming_ = false;
    const ActivateResult r = run_->activate(id);
    if (r != ActivateResult::Ok) toast(activate_result_text(r), ui::kDanger);
}

void GameApp::handle_gameplay_input(const PointerFrame& in, const ui::HudLayout& hud) {
    const int w = GetScreenWidth();
    const int h = GetScreenHeight();

    // Is the pointer over any HUD element? Then the map ignores it.
    const auto over_ui = [&](Vector2 p) { return ui::hud_contains(hud, p); };

    // HUD buttons. Play and fast-forward also resume from a pause.
    if (in.tap) {
        if (ui::contains(hud.pause, in.pos)) sim_paused_ = true;
        if (ui::contains(hud.play, in.pos) || ui::contains(hud.fast, in.pos)) {
            sim_paused_ = false;
            speed_index_ = ui::contains(hud.fast, in.pos) ? 1 : 0;
        }
        if (ui::contains(hud.help, in.pos)) show_help_ = true;
        if (ui::contains(hud.menu, in.pos)) menu_open_ = true;
        for (const auto& [id, r] : hud.actions) {
            if (ui::contains(r, in.pos)) use_action(id);
        }
    }

    // Keyboard.
    if (in.key(KEY_SPACE) || in.key(KEY_P)) sim_paused_ = !sim_paused_;
    if (in.key(KEY_LEFT_BRACKET) || in.key(KEY_MINUS)) speed_index_ = std::max(0, speed_index_ - 1);
    if (in.key(KEY_RIGHT_BRACKET) || in.key(KEY_EQUAL)) speed_index_ = std::min(ui::kSpeedCount - 1, speed_index_ + 1);
    if (in.key(KEY_B)) show_belief_ = !show_belief_;
    if (in.key(KEY_V)) show_scan_ = !show_scan_;
    if (in.key(KEY_C)) show_debug_ = !show_debug_;
    if (in.key(KEY_H)) show_help_ = true;
    if (in.key(KEY_F)) camera_.focus(run_->sim().true_pose().position());
    if (in.key(KEY_TAB)) {
        top_down_ = !top_down_;
        refit_camera(*run_);
    }
    for (int i = 0; i < static_cast<int>(hud.actions.size()) && i < 8; ++i) {
        if (in.key(KEY_ONE + i)) use_action(hud.actions[i].first);
    }
    if (in.key(KEY_ESCAPE)) {
        if (targeting_) targeting_ = aiming_ = false;
        else menu_open_ = true;
    }

    // Kidnap and Road Block targeting: press on the map to place, drag to aim
    // (a Road Block snaps to the grid axes), release to go.
    has_hover_ = false;
    if (targeting_) {
        if (in.cancel) {
            targeting_ = aiming_ = false;
            return;
        }
        Vec2 g;
        if (aiming_) {
            if (in.down && prim::ground_point(in.pos, camera_.camera(), &g)) {
                const float dx = g.x - aim_.x, dz = g.z - aim_.z;
                if (dx * dx + dz * dz > 0.04f) aim_.heading_rad = std::atan2(dz, dx);
            }
            if (in.released) {
                const ActivateResult r = run_->activate(target_id_, aim_);
                if (r != ActivateResult::Ok) toast(activate_result_text(r), ui::kDanger);
                targeting_ = aiming_ = false;
            }
        } else if (prim::ground_point(in.pos, camera_.camera(), &g) && !over_ui(in.pos)) {
            aim_.x = g.x;
            aim_.z = g.z;
            has_hover_ = true;
            if (target_id_ == ActionId::Block) aim_.heading_rad = run_->block_heading_at(aim_.position());
            if (in.pressed) {
                aiming_ = true;
                if (target_id_ == ActionId::Kidnap) aim_.heading_rad = run_->sim().true_pose().heading_rad;
                if (in.released) {
                    const ActivateResult r = run_->activate(target_id_, aim_);
                    if (r != ActivateResult::Ok) toast(activate_result_text(r), ui::kDanger);
                    targeting_ = aiming_ = false;
                    return;
                }
            }
        }
        hover_valid_ = run_->check_target(target_id_, aim_) == ActivateResult::Ok;
        return;
    }

    // Camera.
    if (in.dragging && !over_ui(in.press_pos)) camera_.pan_pixels(in.drag_delta, h);
    if (in.pan_delta.x != 0.0f || in.pan_delta.y != 0.0f) camera_.pan_pixels(in.pan_delta, h);
    if (in.zoom != 1.0f) camera_.zoom_at(in.zoom, in.zoom_centre, w, h);
}

void GameApp::update_playing(float dt, const PointerFrame& in) {
    if (!run_) {
        go_title();
        return;
    }
    const int w = GetScreenWidth();
    const int h = GetScreenHeight();
    const float s = ui_scale();

    if (show_help_) {
        const ui::HelpLayout hl = ui::layout_help(w, h, s);
        if ((in.tap && ui::contains(hl.close, in.pos)) || in.key(KEY_ESCAPE) || in.key(KEY_ENTER)) {
            show_help_ = false;
        }
    } else if (menu_open_) {
        const ui::PauseLayout pl = ui::layout_pause(w, h, s);
        if ((in.tap && ui::contains(pl.resume, in.pos)) || in.key(KEY_ESCAPE)) menu_open_ = false;
        else if (in.tap && ui::contains(pl.beliefs, in.pos)) show_belief_ = !show_belief_;
        else if (in.tap && ui::contains(pl.restart, in.pos)) {
            start_run();
            return;
        } else if (in.tap && ui::contains(pl.quit, in.pos)) {
            go_title();
            return;
        }
    } else if (run_->phase() == RunPhase::Drafting) {
        const ui::CardLayout cl = ui::layout_cards(w, h, s, static_cast<int>(run_->offer().size()));
        int pick = -1;
        for (int i = 0; i < static_cast<int>(cl.cards.size()); ++i) {
            if ((in.tap && ui::contains(cl.cards[i], in.pos)) || in.key(KEY_ONE + i)) pick = i;
        }
        if (pick >= 0) {
            run_->choose_card(pick);
            handle_events(*run_, true);
        }
    } else {
        const ui::HudLayout hud = ui::layout_hud(w, h, s, *run_);
        handle_gameplay_input(in, hud);
    }

    // The crane runs on real time; the simulation waits until Wobo is down.
    if (crane_.update(dt)) {
        const Vec2 at = crane_.frame().wobo.position();
        fx_.poof(at, Color{214, 206, 190, 255});
        fx_.float_text(at, "Whoosh!", ui::kInk);
        wobo_.kick();
        dizzy_until_ = time_ + 2.5f;
    }
    const bool frozen = external_pause_ || sim_paused_ || menu_open_ || show_help_ || crane_.holding();
    clock_.set_paused(frozen);
    clock_.set_speed(ui::speed_value(speed_index_));
    step_run(*run_, clock_, dt, true);

    const float lock = run_->referee().lock_progress();
    // Only a lock that had sounded the alarm shatters; flickers pass quietly.
    if (run_->phase() == RunPhase::Playing && last_lock_ > 0.0f && lock <= 0.0f && alarm_ > 0.5f) {
        fx_.shatter(ring_pos_, capture_ring_radius(run_->referee()));
    }
    last_lock_ = lock;

    // Presentation only: ease the shown progress towards the real one, and
    // fade the alarm in once the lock has held for kAlarmDelayS (out faster).
    const float held = lock * run_->referee().config().stability_s;
    lock_shown_ += (lock - lock_shown_) * (1.0f - std::exp(-dt * 7.0f));
    if (held >= kAlarmDelayS) alarm_ = std::min(1.0f, alarm_ + dt / 0.4f);
    else alarm_ = std::max(0.0f, alarm_ - dt / 0.25f);

    if (run_->phase() == RunPhase::Over) {
        screen_ = Screen::Over;
        over_t_ = 0.0f;
        targeting_ = aiming_ = false;
        const RunScore sc = run_->score();
        Best& b = best_[run_->map_id()];
        new_best_ = sc.survival_s > b.seconds && profile_.seconds <= 0.0f;  // profiling plays no real runs
        if (new_best_) {
            b.seconds = sc.survival_s;
            b.hour = sc.hour;
            save_progress();
        }
    }
}

void GameApp::update_over(float dt, const PointerFrame& in) {
    over_t_ += dt;
    wobo_.update(dt, 0.0f, 0.0f, false);
    if (over_t_ < 1.2f) return;
    const ui::GameOverLayout gl = ui::layout_game_over(GetScreenWidth(), GetScreenHeight(), ui_scale());
    if ((in.tap && ui::contains(gl.again, in.pos)) || in.key(KEY_ENTER) || in.key(KEY_R)) {
        start_run();
    } else if ((in.tap && ui::contains(gl.menu, in.pos)) || in.key(KEY_ESCAPE)) {
        go_title();
    }
}

// ---------------------------------------------------------------------------
// --profile
// ---------------------------------------------------------------------------

void GameApp::profile_frame() {
    Profile& p = profile_;
    const float dt = GetFrameTime();
    if (p.scene >= 0) {
        p.t += dt;
        if (p.t > 1.0f) p.samples.push_back(dt);
    }
    const int scenes = 1 + static_cast<int>(maps_.size());
    if (p.scene < 0 || p.t >= p.seconds + 1.0f) {
        if (p.scene < 0) {
            std::printf("Frame times at %dx%d, uncapped, 2x speed, %.0f s per scene:\n", GetScreenWidth(),
                        GetScreenHeight(), p.seconds);
            std::printf("%-12s %7s %8s %8s %8s %8s %8s\n", "scene", "frames", "avg fps", "avg ms", "p95 ms",
                        "p99 ms", "max ms");
        } else if (!p.samples.empty()) {
            std::vector<float> v = p.samples;
            std::sort(v.begin(), v.end());
            double sum = 0.0;
            for (float x : v) sum += x;
            const double avg = sum / v.size();
            const auto pct = [&](double q) { return 1000.0 * v[std::min(v.size() - 1, std::size_t(q * v.size()))]; };
            const std::string name = p.scene == 0 ? "title" : maps_[p.scene - 1].id;
            std::printf("%-12s %7zu %8.0f %8.2f %8.2f %8.2f %8.2f\n", name.c_str(), v.size(), 1.0 / avg,
                        1000.0 * avg, pct(0.95), pct(0.99), 1000.0 * v.back());
            std::fflush(stdout);
        }
        ++p.scene;
        p.t = 0.0f;
        p.samples.clear();
        if (p.scene == 0) {
            go_title();
        } else if (p.scene < scenes) {
            map_index_ = p.scene - 1;
            start_run();
            speed_index_ = 1;
        } else {
            finished_ = true;
        }
    }
    // Keep the run going: take the first card, and start over if Wobo wins.
    if (screen_ == Screen::Playing && run_ && run_->phase() == RunPhase::Drafting) run_->choose_card(0);
    if (screen_ == Screen::Over && p.scene > 0) {
        start_run();
        speed_index_ = 1;
    }
}

// ---------------------------------------------------------------------------
// --poster
// ---------------------------------------------------------------------------

void GameApp::render_poster() {
    const LaunchOptions& o = options_;
    constexpr int kSuper = 2;  // drawn at twice the size, then filtered down
    const int pw = o.width * kSuper;
    const int ph = o.height * kSuper;
    const float s = static_cast<float>(ph) / 720.0f;

    // The right half: Wobo, kidnapped -- tied up, blindfolded, and dangling
    // from a rope, turned three-quarters to the viewer.
    const int art_w = pw / 2;
    const float lift = 0.12f;
    Camera3D cam{};
    constexpr float kYaw = 45.0f * kDegToRad;
    constexpr float kPitch = 14.0f * kDegToRad;
    cam.target = {0.0f, lift + 0.27f, 0.0f};
    cam.position = {cam.target.x + 10.0f * std::cos(kPitch) * std::sin(kYaw), cam.target.y + 10.0f * std::sin(kPitch),
                    cam.target.z + 10.0f * std::cos(kPitch) * std::cos(kYaw)};
    cam.up = {0.0f, 1.0f, 0.0f};
    cam.fovy = 1.05f;  // world metres across the height (orthographic)
    cam.projection = CAMERA_ORTHOGRAPHIC;
    const float heading = 0.25f * kPi + 0.45f;  // facing the camera, then turned a little away

    RenderTexture2D art = LoadRenderTexture(art_w, ph);
    BeginTextureMode(art);
    ClearBackground(ui::kBackground);
    // A soft pool of light behind it.
    DrawCircleGradient({0.5f * static_cast<float>(art_w), 0.5f * static_cast<float>(ph)}, 0.42f * static_cast<float>(ph), ui::kSpot,
                       ui::kBackground);
    BeginMode3D(cam);
    toon_.begin();
    // The rope it hangs from runs up behind its head, out of the picture.
    const Vec2 back = Vec2{std::cos(heading), std::sin(heading)} * -0.14f;
    const Vector3 knot{back.x, lift + 0.24f, back.z};
    const Vector3 top{back.x * 0.6f, lift + 3.0f, back.z * 0.6f};
    rlDrawRenderBatchActive();
    rlSetCullFace(RL_CULL_FACE_FRONT);
    toon_.set_flat(true);
    prim::cylinder(knot, top, 0.026f, ui::kOutline, 10);
    rlDrawRenderBatchActive();
    rlSetCullFace(RL_CULL_FACE_BACK);
    toon_.set_flat(false);
    prim::cylinder(knot, top, 0.014f, Color{196, 142, 88, 255}, 10);
    WoboStyle look;
    look.lift = lift;
    look.blindfold = true;
    look.tied_up = true;
    wobo_.draw(toon_, {0.0f, 0.0f}, heading, Face::Surprised, look);
    toon_.end();
    EndMode3D();
    EndTextureMode();

    RenderTexture2D poster = LoadRenderTexture(pw, ph);
    BeginTextureMode(poster);
    ClearBackground(ui::kBackground);
    const float aw = static_cast<float>(art_w);
    const float ah = static_cast<float>(ph);
    DrawTexturePro(art.texture, {0.0f, 0.0f, aw, -ah}, {static_cast<float>(pw) - aw, 0.0f, aw, ah}, {0.0f, 0.0f}, 0.0f,
                   WHITE);
    const float left = 80.0f * s;
    ui::draw_poster_text({left, 0.0f, static_cast<float>(pw) - aw - left, ah}, s);
    EndTextureMode();

    Image img = LoadImageFromTexture(poster.texture);
    ImageFlipVertical(&img);
    // Blending can leave alpha below 1; a poster is opaque.
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
    ImageResize(&img, o.width, o.height);
    if (ExportImage(img, o.poster_path.c_str())) {
        std::printf("Poster: %s (%dx%d)\n", o.poster_path.c_str(), o.width, o.height);
    }
    UnloadImage(img);
    UnloadRenderTexture(poster);
    UnloadRenderTexture(art);
}

// --shots
void GameApp::render_shots() {
    const int w = options_.width, h = options_.height;
    RenderTexture2D target = LoadRenderTexture(w, h);
    screen_ = Screen::Playing;
    for (const MapInfo& info : maps_) {
        run_ = std::make_unique<RunDirector>(config_);
        std::string err;
        if (!run_->start(info.id, 3, &err)) continue;
        // A little way in, so Wobo and the movers are about their business.
        for (int i = 0; i < static_cast<int>(20.0 / run_->step_s()) && run_->phase() == RunPhase::Playing; ++i) {
            run_->step();
        }
        iso_.build(run_->map(), toon_);
        bound_map_.clear();
        for (int pass = 0; pass < (info.has_focus ? 2 : 1); ++pass) {
            camera_.set_insets(0.0f, 0.0f);
            camera_.fit(run_->map().world_width(), run_->map().world_depth(), w, h);
            if (pass == 1) camera_.frame_on(info.focus, 4.0f);
            BeginTextureMode(target);
            ClearBackground(ui::kBackground);
            draw_world(*run_, 1.0f, false);
            EndTextureMode();
            Image img = LoadImageFromTexture(target.texture);
            ImageFlipVertical(&img);
            ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
            const std::string path = options_.shots_dir + "/" + info.id + (pass ? "_close" : "") + ".png";
            if (ExportImage(img, path.c_str())) std::printf("Shot: %s\n", path.c_str());
            UnloadImage(img);
        }
    }
    UnloadRenderTexture(target);
    run_.reset();
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

Pose2D GameApp::render_pose(const RunDirector& run, float alpha) const {
    const Pose2D& a = run.sim().previous_pose();
    const Pose2D& b = run.sim().true_pose();
    return {lerpf(a.x, b.x, alpha), lerpf(a.z, b.z, alpha),
            wrap_angle(a.heading_rad + angle_diff(b.heading_rad, a.heading_rad) * alpha)};
}

Face GameApp::face_for(const RunDirector& run) const {
    if (run.phase() == RunPhase::Over) return Face::Confident;
    if (crane_.active() && crane_.frame().carrying) return Face::Surprised;
    if (time_ < dizzy_until_ || run.actions().active(ActionId::Drift)) return Face::Dizzy;
    switch (run.agent().mood()) {
        case RobotMood::Curious: return Face::Curious;
        case RobotMood::Thinking: return Face::Thinking;
        case RobotMood::Hopeful: return Face::Hopeful;
        case RobotMood::Confident: return Face::Confident;
        case RobotMood::Confused: return Face::Confused;
        case RobotMood::ShortSighted: return Face::Squint;
        case RobotMood::Blind: return Face::Blind;
    }
    return Face::Curious;
}

void GameApp::draw_world(const RunDirector& run, float alpha, bool overlays) {
    Pose2D wobo = render_pose(run, alpha);
    const RobotAgent& agent = run.agent();

    // How Wobo looks right now: carried by the crane, spun by Gyro Jam,
    // wearing Colour Blind's shades, gum on its wheels.
    WoboStyle look;
    const bool craning = crane_.active() && &run == run_.get();
    if (craning) {
        const CraneAnimation::Frame f = crane_.frame();
        wobo = f.wobo;
        look.lift = f.wobo_lift;
    }
    // Overlays tied to the sensors make no sense mid-air.
    const bool sensing = overlays && !crane_.holding();
    const float spin_k = clampf((time_ - spin_start_) / 1.4f, 0.0f, 1.0f);
    const float spin = 3.0f * kTwoPi * (1.0f - std::pow(1.0f - spin_k, 3.0f));
    look.sunglasses = run.actions().active(ActionId::Colour);
    look.gummy = run.actions().active(ActionId::Gummy);
    look.dizzy_stars = run.actions().active(ActionId::Drift) || time_ < dizzy_until_;

    BeginMode3D(camera_.camera());
    toon_.begin();
    iso_.draw(toon_);
    toon_.end();
    EndMode3D();

    // On the title, the room is dark but for a pool of light on Wobo (in
    // play the whole board stays lit). It goes between the board and
    // everything on it, so the overlays and Wobo stay bright; 2D drawing
    // leaves the depth buffer alone, so the board still hides what it should
    // once 3D resumes.
    if (screen_ == Screen::Title) draw_spotlight(wobo.position());

    BeginMode3D(camera_.camera());
    toon_.begin();
    if (overlays) {
        toon_.set_flat(true);
        const auto* diag = agent.diagnostics();
        if (show_belief_ && diag) {
            overlay::particles(*diag);
            overlay::hypotheses(*diag, time_);
        }
        if (sensing) {
            const RgbLidarScan& scan = run.sim().last_scan();
            overlay::range_ring(wobo.position(), scan.max_range_m, run.modifiers().range_scale < 0.99f, time_);
            if (show_scan_) overlay::scan(scan, run.sim().last_scan_pose(), 1.0f);
            overlay::intent(wobo, agent.remaining_motion(), time_);
            const LocalizationReferee& ref = run.referee();
            const PoseEstimate guess = agent.estimate();
            const float tol = ref.config().position_tolerance_m;
            if (guess.valid && distance(guess.pose.position(), wobo.position()) > tol) {
                overlay::tether(wobo, guess.pose, time_);
            }
            overlay::capture_ring(wobo.position(), capture_ring_radius(ref), lock_shown_, alarm_, time_);
            ring_pos_ = wobo.position();
        }
        if (show_debug_) overlay::candidates(wobo, agent.planner().last_candidates());
        if (targeting_ && target_id_ == ActionId::Kidnap) {
            overlay::min_distance_ring(wobo.position(), run.stats().get("action.kidnap.min_distance", 2.0f), time_);
            if (aiming_ || has_hover_) overlay::target_marker(aim_, hover_valid_, time_);
        }
        toon_.set_flat(false);
    }

    // The movers, where they are between simulation steps.
    for (const Mover& m : run.movers()) {
        const Pose2D p{lerpf(m.prev_pose.x, m.pose.x, alpha), lerpf(m.prev_pose.z, m.pose.z, alpha),
                       wrap_angle(m.prev_pose.heading_rad + angle_diff(m.pose.heading_rad, m.prev_pose.heading_rad) * alpha)};
        draw_mover(toon_, m, p, time_);
    }

    wobo_.draw_xray(toon_, wobo.position(), wobo.heading_rad + spin, Color{255, 255, 255, 90}, look.lift);
    wobo_.draw(toon_, wobo.position(), wobo.heading_rad + spin, face_for(run), look);
    if (craning) crane_.draw();
    if (const auto& block = run.road_block()) {
        const ActionState& st = run.actions().state(ActionId::Block);
        draw_road_block(*block, run.map(), 1.0f, std::min(1.0f, (st.active_total - st.active_left) / 0.3f), false);
    }
    fx_.draw_bursts();
    fx_.draw_active(run, wobo, time_);

    // Translucent copies last: where Wobo thinks it is, and where it would
    // land if kidnapped.
    rlDisableDepthMask();
    if (overlays && show_belief_) {
        const PoseEstimate est = agent.estimate();
        if (est.valid) {
            WoboStyle ghost;
            ghost.alpha = 0.32f + 0.4f * est.confidence;
            ghost.tint = ui::kBelief;
            ghost.tint_amount = 0.65f;
            ghost.outline = false;
            ghost.face = false;
            wobo_.draw(toon_, est.pose.position(), est.pose.heading_rad, Face::Curious, ghost);
        }
    }
    if (overlays && targeting_ && target_id_ == ActionId::Block && (aiming_ || has_hover_)) {
        draw_road_block(run.block_at(aim_), run.map(), 0.5f, 1.0f, true, hover_valid_ ? ui::kGood : ui::kDanger);
    }
    if (overlays && targeting_ && target_id_ == ActionId::Kidnap && (aiming_ || has_hover_)) {
        WoboStyle ghost;
        ghost.alpha = 0.45f;
        ghost.tint = hover_valid_ ? ui::kGood : ui::kDanger;
        ghost.tint_amount = 0.7f;
        ghost.outline = false;
        wobo_.draw(toon_, aim_.position(), aim_.heading_rad, Face::Surprised, ghost);
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();

    toon_.end();
    EndMode3D();

    if (sensing) {
        draw_lock_on(run, wobo);
        if (alarm_ < 0.5f) draw_bubble(run, wobo.position());
    }
    fx_.draw_texts(camera_.camera(), ui_scale());
}

void GameApp::draw_spotlight(Vec2 at) const {
    const Camera3D& cam = camera_.camera();
    const Vector2 c = GetWorldToScreen({at.x, 0.0f, at.z}, cam);
    // Measure a metre along both floor diagonals: the longer is the screen's
    // horizontal scale, and their ratio is how squashed a floor circle looks.
    constexpr float k = 0.70710678f;
    const Vector2 a = GetWorldToScreen({at.x + k, 0.0f, at.z + k}, cam);
    const Vector2 b = GetWorldToScreen({at.x + k, 0.0f, at.z - k}, cam);
    const float la = std::hypot(a.x - c.x, a.y - c.y);
    const float lb = std::hypot(b.x - c.x, b.y - c.y);
    const float ppm = std::max(la, lb);
    if (ppm <= 0.0f) return;
    ui::spotlight(c, 1.8f * ppm, 6.0f * ppm, std::min(la, lb) / ppm, Color{12, 15, 29, 175}, GetScreenWidth(),
                  GetScreenHeight());
}

void GameApp::draw_lock_on(const RunDirector& run, const Pose2D& wobo) {
    const float s = ui_scale();
    const LocalizationReferee& ref = run.referee();
    const PoseEstimate guess = run.agent().estimate();
    const Camera3D& cam = camera_.camera();
    const auto outlined = [&](const char* t, Vector2 at, float size, Color fill) {
        const float w = ui::measure(t, size, ui::Weight::Bold);
        for (const Vector2 o : {Vector2{-2, 0}, Vector2{2, 0}, Vector2{0, -2}, Vector2{0, 2}, Vector2{2, 2}}) {
            ui::text(t, at.x - w * 0.5f + o.x * s, at.y + o.y * s, size, ui::kOutline, ui::Weight::Bold);
        }
        ui::text(t, at.x - w * 0.5f, at.y, size, fill, ui::Weight::Bold);
    };

    const float lock = ref.lock_progress();
    if (alarm_ > 0.0f) {
        // Red pulse at the screen edges once a lock has settled, fading in.
        const int w = GetScreenWidth();
        const int h = GetScreenHeight();
        const float beat = 0.5f + 0.5f * std::sin(time_ * 12.0f);
        const int band = static_cast<int>(70.0f * s);
        const Color edge{255, 60, 70,
                         static_cast<unsigned char>(alarm_ * (40.0f + 60.0f * lock) * (0.6f + 0.4f * beat))};
        const Color clear{255, 60, 70, 0};
        DrawRectangleGradientV(0, 0, w, band, edge, clear);
        DrawRectangleGradientV(0, h - band, w, band, clear, edge);
        DrawRectangleGradientH(0, 0, band, h, edge, clear);
        DrawRectangleGradientH(w - band, 0, band, h, clear, edge);

        // Countdown over Wobo's head.
        if (alarm_ >= 0.5f && lock > 0.0f) {
            const float left = (1.0f - lock) * ref.config().stability_s;
            char num[8];
            std::snprintf(num, sizeof(num), "%d", static_cast<int>(std::ceil(left)));
            const Vector2 p = GetWorldToScreen({wobo.x, 1.0f, wobo.z}, cam);
            const float size = (34.0f + 8.0f * beat) * s;
            outlined(num, {p.x, p.y - size * 0.5f}, size, ui::kDanger);
            return;
        }
    }

    // A lock that is still building quietly is told by the ring alone.
    if (lock > 0.0f || !guess.valid) return;
    // How far off the guess is, at the middle of the tether -- or, once it is
    // close enough, which way it is facing wrongly.
    const float dist = distance(guess.pose.position(), wobo.position());
    char label[32];
    Vector2 at;
    if (dist > ref.config().position_tolerance_m) {
        std::snprintf(label, sizeof(label), "%.1f m", dist);
        at = GetWorldToScreen({(wobo.x + guess.pose.x) * 0.5f, 0.05f, (wobo.z + guess.pose.z) * 0.5f}, cam);
    } else {
        const float off = std::fabs(angle_diff(guess.pose.heading_rad, wobo.heading_rad)) * kRadToDeg;
        std::snprintf(label, sizeof(label), "facing %.0f\u00b0 off", off);
        at = GetWorldToScreen({wobo.x, 0.05f, wobo.z}, cam);
        at.y += 14.0f * s;
    }
    outlined(label, {at.x, at.y - 9.0f * s}, 16.0f * s, Color{255, 200, 200, 255});
}

void GameApp::draw_bubble(const RunDirector& run, Vec2 pos) {
    // A little thought bubble over Wobo's head.
    const char* glyph = nullptr;
    Color col = ui::kOutline;
    switch (run.agent().mood()) {
        case RobotMood::Confused: glyph = "?"; col = Color{230, 160, 0, 255}; break;
        case RobotMood::Confident: glyph = "!"; col = Color{0, 160, 110, 255}; break;
        case RobotMood::Thinking: glyph = "..."; break;
        case RobotMood::Blind: glyph = "X"; col = ui::kDanger; break;
        default: break;
    }
    if (!glyph || run.phase() == RunPhase::Over) return;
    const float s = ui_scale();
    const Vector2 p = GetWorldToScreen({pos.x, 0.95f, pos.z}, camera_.camera());
    const float bob = std::sin(time_ * 4.0f) * 3.0f * s;
    const float r = 15.0f * s;
    DrawCircleV({p.x + 3.0f * s, p.y + bob + 3.0f * s}, r, ui::kShadow);
    DrawCircleV({p.x, p.y + bob}, r + 2.0f * s, ui::kOutline);
    DrawCircleV({p.x, p.y + bob}, r, ui::kInk);
    DrawCircleV({p.x - 10.0f * s, p.y + bob + 17.0f * s}, 4.0f * s, ui::kInk);
    ui::text_centered(glyph, p.x, p.y + bob - 11.0f * s, 20.0f * s, col, ui::Weight::Bold);
}

void GameApp::draw() {
    const int w = GetScreenWidth();
    const int h = GetScreenHeight();
    const float s = ui_scale();
    const Vector2 mouse = GetMousePosition();

    BeginDrawing();
    ClearBackground(ui::kBackground);

    switch (screen_) {
        case Screen::Title: {
            if (demo_) draw_world(*demo_, demo_clock_.alpha(), false);
            if (show_help_) {
                ui::draw_help(ui::layout_help(w, h, s), s, w, h, mouse);
            } else if (!maps_.empty()) {
                std::vector<std::string> bests;
                for (const MapInfo& m : maps_) bests.push_back(best_short(m.id));
                ui::draw_title(ui::layout_title(w, h, s, static_cast<int>(maps_.size())), s, w, h, maps_, map_index_,
                               bests, mouse, time_);
            }
            break;
        }
        case Screen::Playing:
        case Screen::Over: {
            if (!run_) break;
            draw_world(*run_, screen_ == Screen::Over ? 1.0f : clock_.alpha(), screen_ == Screen::Playing);
            if (screen_ == Screen::Playing) {
                const ui::HudLayout hud = ui::layout_hud(w, h, s, *run_);
                ui::HudState st;
                st.paused = sim_paused_ || external_pause_;
                st.speed_index = speed_index_;
                st.targeting = targeting_;
                st.target = target_id_;
                st.mouse = mouse;
                st.toast = toast_;
                st.toast_alpha = std::min(1.0f, toast_t_ / 0.4f);
                st.toast_colour = toast_colour_;
                st.time_s = time_;
                st.lock_shown = lock_shown_;
                st.alarm = alarm_;
                ui::draw_hud(hud, *run_, st);
                if (sim_paused_ && !menu_open_ && !show_help_) {
                    ui::text_centered("PAUSED", w * 0.5f, hud.bar.y + hud.bar.height + 64.0f * s,
                                      40.0f * s, ui::kInk, ui::Weight::Bold);
                }
                if (run_->phase() == RunPhase::Drafting) {
                    const ui::CardLayout cl = ui::layout_cards(w, h, s, static_cast<int>(run_->offer().size()));
                    ui::draw_cards(cl, s, w, h, run_->score().hour - 1, run_->offer(), run_->cards(), mouse, time_);
                }
                if (menu_open_) ui::draw_pause(ui::layout_pause(w, h, s), s, w, h, show_belief_, mouse);
                if (show_help_) ui::draw_help(ui::layout_help(w, h, s), s, w, h, mouse);
            } else if (over_t_ >= 1.2f) {
                const std::string name = maps_[map_index_].name + "  -  seed " + std::to_string(run_->seed());
                ui::draw_game_over(ui::layout_game_over(w, h, s), s, w, h, run_->score(), run_->hour_length_s(), name,
                                   best_text(run_->map_id()), new_best_, mouse, time_);
            }
            break;
        }
    }
    EndDrawing();
}

}  // namespace wobo
