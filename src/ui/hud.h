#pragma once

// The in-run heads-up display. One slim bar along the top holds the day clock
// and time, the pause / play / fast-forward buttons, Wobo's status (mood,
// intent, its own confidence, and the referee's lock-on outline) and the
// help and menu buttons; on narrow screens the status drops to a second row.
// Along the bottom: the energy meter and the action bar; plus active
// effects and toasts.

#include <string>
#include <utility>
#include <vector>

#include "raylib.h"

#include "gameplay/run_director.h"

namespace wobo::ui {

struct HudLayout {
    float s = 1.0f;
    Rectangle bar{};            // the top strip; everything up to `menu` sits in it
    bool two_rows = false;      // status on a second row (narrow screens)
    Vector2 clock{};
    float clock_r = 0.0f;
    Rectangle day_panel{};      // clock plus day and time; hovering it tells when the draft is
    Rectangle transport{};      // the well holding the three time buttons
    Rectangle pause{};
    Rectangle play{};           // 1x
    Rectangle fast{};           // 2x
    Rectangle status_panel{};   // Wobo's status; the lock-on outline runs round it
    Rectangle mood{};           // inside status_panel: mood and what Wobo is doing
    Rectangle confidence{};     // inside status_panel: confidence meter and best-guess line
    Rectangle help{};
    Rectangle menu{};
    std::vector<float> dividers;  // x of the hairlines between groups on the top row
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
