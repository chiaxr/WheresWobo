#pragma once

// Title screen, pause menu, help sheet and the end-of-run summary.

#include <string>
#include <vector>

#include "raylib.h"

#include "gameplay/run_director.h"
#include "world/map_loader.h"

namespace wobo::ui {

// The wordmark sits top left; along the bottom, every map as a card, with
// Play and How to play at the end of the row. A screen too narrow for the
// row shows only the chosen map's card, between previous / next arrows.
// The "where's WOBO?" wordmark with its top-left at (x, y); `big` is the
// size of WOBO. Returns the y just below it.
float wordmark(float x, float y, float big, float s, float time_s);

struct TitleLayout {
    Rectangle title{};              // the wordmark block
    Rectangle panel{};              // the whole bottom block (description, cards, buttons)
    std::vector<Rectangle> cards;   // one per map, in order; empty when narrow
    Rectangle card{};               // narrow only: the chosen map
    Rectangle prev{};               // narrow only
    Rectangle next{};               // narrow only
    Rectangle play{};
    Rectangle help{};
};
TitleLayout layout_title(int w, int h, float s, int map_count);
// `bests` holds a short best-run line per map, parallel to `maps`.
void draw_title(const TitleLayout& l, float s, int w, int h, const std::vector<MapInfo>& maps, int selected,
                const std::vector<std::string>& bests, Vector2 mouse, float time_s);

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
