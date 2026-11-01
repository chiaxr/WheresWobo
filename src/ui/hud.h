#pragma once

// The in-run heads-up display: day and survival time, Wobo's status (mood,
// intent, its own confidence, and the referee's lock-on danger meter), the
// energy meter, the action bar, active effects, toasts, the pause / play /
// fast-forward buttons under the day clock, and the view controls.

#include <string>
#include <utility>
#include <vector>

#include "raylib.h"

#include "gameplay/run_director.h"

namespace wobo::ui {

struct HudLayout {
    float s = 1.0f;
    Rectangle day_panel{};
    Rectangle pause{};          // these three sit in the day panel, under the clock
    Rectangle play{};           // 1x
    Rectangle fast{};           // 2x
    Rectangle status_panel{};
    Rectangle help{};
    Rectangle menu{};
    Rectangle energy{};
    std::vector<std::pair<ActionId, Rectangle>> actions;
    Rectangle effects_origin{};
    float top_inset = 0.0f;     // fraction of screen height the top HUD covers
    float bottom_inset = 0.0f;
};

HudLayout layout_hud(int w, int h, float s, const RunDirector& run);

struct HudState {
    bool paused = false;
    int speed_index = 0;           // 0: 1x, 1: 2x
    bool targeting = false;        // picking a kidnap destination
    Vector2 mouse{};
    std::string toast;
    float toast_alpha = 0.0f;
    Color toast_colour = WHITE;
    float time_s = 0.0f;           // render time, for pulses
    float lock_shown = 0.0f;       // lock-on progress eased for display, 0..1
    float alarm = 0.0f;            // 0..1: fades in once a lock has held a while
};

void draw_hud(const HudLayout& l, const RunDirector& run, const HudState& st);
// Whether a screen point is on any HUD element (the map ignores it then).
bool hud_contains(const HudLayout& l, Vector2 p);

constexpr int kSpeedCount = 2;
double speed_value(int index);

}  // namespace wobo::ui
