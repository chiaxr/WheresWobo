#pragma once

// Application lifecycle and main loop: owns the window-side systems
// (renderers, camera, input, UI) and the current run, and moves between the
// title screen, a run, and the end-of-run summary.
//   native (src/main.cpp): init(); while (!WindowShouldClose()) frame(); shutdown();
//   web    (src/web/web_main.cpp): init(); emscripten_set_main_loop -> frame();

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/input_router.h"
#include "core/clock.h"
#include "core/config.h"
#include "gameplay/run_director.h"
#include "rendering/camera_rig.h"
#include "rendering/effects_renderer.h"
#include "rendering/iso_renderer.h"
#include "rendering/robot_model.h"
#include "rendering/toon_shader.h"
#include "ui/hud.h"
#include "world/map_loader.h"

namespace wobo {

class GameApp {
public:
    void init();
    void frame();
    void shutdown();

    // Paused from outside (the web build pauses in portrait orientation).
    void set_paused(bool paused) { external_pause_ = paused; }
    // Device pixels per CSS pixel; the web loop tracks it, native is 1.
    void set_pixel_scale(float scale) { pixel_scale_ = scale > 0.0f ? scale : 1.0f; }
    void save_progress();

private:
    enum class Screen { Title, Playing, Over };

    struct Best {
        double seconds = 0.0;
        int day = 0;
    };

    float ui_scale() const;
    void load_progress();
    std::string best_text(const std::string& map_id) const;
    std::string best_short(const std::string& map_id) const;  // for the title's map cards

    void start_run();
    void go_title();
    void restart_demo();
    void bind_map(const RunDirector& run);  // (re)build meshes and frame the camera
    void refit_camera(const RunDirector& run);

    void update(float dt, const PointerFrame& in);
    void update_title(float dt, const PointerFrame& in);
    void update_playing(float dt, const PointerFrame& in);
    void update_over(float dt, const PointerFrame& in);
    void handle_gameplay_input(const PointerFrame& in, const ui::HudLayout& hud);
    void use_action(ActionId id);
    void step_run(RunDirector& run, FixedStepClock& clock, float dt, bool is_player_run);
    void handle_events(RunDirector& run, bool is_player_run);
    void toast(std::string text, Color colour);

    void draw();
    void draw_world(const RunDirector& run, float alpha, bool overlays);
    void draw_bubble(const RunDirector& run, Vec2 pos);
    void draw_spotlight(Vec2 at) const;
    void draw_lock_on(const RunDirector& run, const Pose2D& wobo);
    Pose2D render_pose(const RunDirector& run, float alpha) const;
    Face face_for(const RunDirector& run) const;

    Config config_;
    std::vector<MapInfo> maps_;
    int map_index_ = 0;
    std::uint64_t seed_counter_ = 0;

    std::unique_ptr<RunDirector> run_;   // the player's run
    std::unique_ptr<RunDirector> demo_;  // title-screen attract mode
    FixedStepClock clock_;
    FixedStepClock demo_clock_;
    const RunDirector* bound_run_ = nullptr;
    std::string bound_map_;

    ToonShader toon_;
    IsoRenderer iso_;
    CameraRig camera_;
    RobotModel wobo_;
    EffectsRenderer fx_;
    CraneAnimation crane_;      // Kidnap: holds the simulation while it carries Wobo
    float spin_start_ = -100.0f;  // render time Spin It was last used
    InputRouter input_;

    Screen screen_ = Screen::Title;
    bool external_pause_ = false;
    bool sim_paused_ = false;
    bool menu_open_ = false;
    bool show_help_ = false;
    bool show_belief_ = true;
    bool show_scan_ = true;
    bool show_debug_ = false;
    bool top_down_ = false;
    int speed_index_ = 0;  // 0: 1x, 1: 2x

    bool targeting_ = false;
    bool aiming_ = false;
    bool hover_valid_ = false;
    Pose2D aim_;
    bool has_hover_ = false;

    std::string toast_;
    float toast_t_ = 0.0f;
    Color toast_colour_ = WHITE;

    float time_ = 0.0f;
    float pixel_scale_ = 1.0f;
    int last_w_ = 0;
    int last_h_ = 0;
    float dizzy_until_ = -1.0f;
    float over_t_ = 0.0f;
    float last_lock_ = 0.0f;   // lock progress last frame, to catch a lock slipping
    float lock_shown_ = 0.0f;  // lock progress eased for display
    float alarm_ = 0.0f;       // 0..1, fades in once a lock has held for a while
    Vec2 ring_pos_;            // where the capture ring was last drawn (a kidnap moves Wobo first)
    bool new_best_ = false;

    std::map<std::string, Best> best_;
};

}  // namespace wobo
