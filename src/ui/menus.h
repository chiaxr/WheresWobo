#pragma once

// Title screen, pause menu, help sheet and the end-of-run summary.

#include <string>

#include "raylib.h"

#include "gameplay/run_director.h"
#include "world/map_loader.h"

namespace wobo::ui {

struct TitleLayout {
    Rectangle panel{};
    Rectangle prev{};
    Rectangle next{};
    Rectangle play{};
    Rectangle help{};
};
TitleLayout layout_title(int w, int h, float s);
void draw_title(const TitleLayout& l, float s, int w, int h, const MapInfo& map, const std::string& best,
                Vector2 mouse, float time_s);

struct PauseLayout {
    Rectangle panel{};
    Rectangle resume{};
    Rectangle restart{};
    Rectangle quit{};
    Rectangle beliefs{};  // "Show beliefs" checkbox row
};
PauseLayout layout_pause(int w, int h, float s);
void draw_pause(const PauseLayout& l, float s, int w, int h, bool show_beliefs, Vector2 mouse);

struct HelpLayout {
    Rectangle panel{};
    Rectangle close{};
};
HelpLayout layout_help(int w, int h, float s);
void draw_help(const HelpLayout& l, float s, int w, int h, Vector2 mouse);

struct GameOverLayout {
    Rectangle panel{};
    Rectangle again{};
    Rectangle menu{};
};
GameOverLayout layout_game_over(int w, int h, float s);
void draw_game_over(const GameOverLayout& l, float s, int w, int h, const RunScore& score, double day_length_s,
                    const std::string& map_name, const std::string& best, bool new_best, Vector2 mouse,
                    float time_s);

std::string format_time(double seconds);
// The in-game clock: each day runs midnight to midnight. `day_progress` is 0..1.
std::string time_of_day(float day_progress);
// "Day 3, 14:30" for a total survival time.
std::string survival_text(double seconds, double day_length_s);

}  // namespace wobo::ui
