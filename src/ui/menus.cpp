#include "ui/menus.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui/ui_text.h"
#include "ui/widgets.h"

namespace wobo::ui {

std::string format_time(double seconds) {
    const int total = static_cast<int>(seconds);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
    return buf;
}

std::string time_of_day(float day_progress) {
    const int minutes = static_cast<int>(std::clamp(day_progress, 0.0f, 0.9999f) * 24.0f * 60.0f);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
    return buf;
}

std::string survival_text(double seconds, double day_length_s) {
    if (day_length_s <= 0.0) return format_time(seconds);
    const int day = static_cast<int>(seconds / day_length_s) + 1;
    const double into = seconds - (day - 1) * day_length_s;
    return "Day " + std::to_string(day) + ", " + time_of_day(static_cast<float>(into / day_length_s));
}

namespace {

Color difficulty_colour(const std::string& d) {
    if (d == "Easy") return kGood;
    if (d == "Medium") return kEnergy;
    if (d == "Hard") return kDanger;
    return kMuted;
}

// Title lettering with a chunky offset shadow.
void title_text(const char* s, float cx, float y, float size, Color fill) {
    const float o = size * 0.06f;
    text_centered(s, cx + o, y + o, size, kOutline, Weight::Bold);
    text_centered(s, cx, y, size, fill, Weight::Bold);
}

}  // namespace

TitleLayout layout_title(int w, int h, float s) {
    TitleLayout l;
    const float pw = std::min(560.0f * s, w - 40.0f * s);
    const float ph = 232.0f * s;
    l.panel = {w * 0.5f - pw * 0.5f, h - ph - 26.0f * s, pw, ph};
    const float ab = 46.0f * s;
    l.prev = {l.panel.x + 14.0f * s, l.panel.y + 14.0f * s, ab, ab};
    l.next = {l.panel.x + pw - ab - 14.0f * s, l.panel.y + 14.0f * s, ab, ab};
    const float bw = 190.0f * s;
    const float bh = 54.0f * s;
    l.play = {w * 0.5f - bw - 8.0f * s, l.panel.y + ph - bh - 16.0f * s, bw, bh};
    l.help = {w * 0.5f + 8.0f * s, l.panel.y + ph - bh - 16.0f * s, bw, bh};
    return l;
}

void draw_title(const TitleLayout& l, float s, int w, int h, const MapInfo& map, const std::string& best,
                Vector2 mouse, float time_s) {
    (void)h;
    const float bounce = std::sin(time_s * 2.2f) * 4.0f * s;
    const float size = std::min(84.0f * s, w / 8.0f);
    title_text("Where's Wobo?", w * 0.5f, 34.0f * s + bounce, size, kEnergy);
    text_centered("Keep the robot lost for as long as you can.", w * 0.5f, 40.0f * s + size * 1.12f, 20.0f * s, kInk,
                  Weight::Bold);

    panel(l.panel, s);
    button(l.prev, "<", s, Look::Normal, contains(l.prev, mouse));
    button(l.next, ">", s, Look::Normal, contains(l.next, mouse));
    const float cx = l.panel.x + l.panel.width * 0.5f;
    text_centered(map.name.c_str(), cx, l.panel.y + 18.0f * s, 28.0f * s, kInk, Weight::Bold);
    if (!map.difficulty.empty()) {
        const float pw = measure(map.difficulty.c_str(), 14.0f * s, Weight::Bold) + 22.0f * s;
        pill({cx - pw * 0.5f, l.panel.y + 54.0f * s, pw, 22.0f * s}, map.difficulty.c_str(),
             difficulty_colour(map.difficulty), kOutline, s, 14.0f * s);
    }
    const auto lines = wrap(map.description, 16.0f * s, l.panel.width - 48.0f * s);
    float y = l.panel.y + 86.0f * s;
    for (const auto& line : lines) {
        text_centered(line.c_str(), cx, y, 16.0f * s, kMuted);
        y += 20.0f * s;
    }
    text_centered(best.c_str(), cx, l.play.y - 26.0f * s, 15.0f * s, kEnergy, Weight::Bold);
    button(l.play, "PLAY", s, Look::Primary, contains(l.play, mouse), 24.0f * s);
    button(l.help, "HOW TO PLAY", s, Look::Normal, contains(l.help, mouse), 18.0f * s);
}

PauseLayout layout_pause(int w, int h, float s) {
    PauseLayout l;
    const float pw = 320.0f * s;
    const float ph = std::min(326.0f * s, h - 20.0f * s);
    l.panel = {w * 0.5f - pw * 0.5f, h * 0.5f - ph * 0.5f, pw, ph};
    const float bw = pw - 60.0f * s;
    const float bh = 50.0f * s;
    const float x = l.panel.x + 30.0f * s;
    l.resume = {x, l.panel.y + 74.0f * s, bw, bh};
    l.restart = {x, l.resume.y + bh + 12.0f * s, bw, bh};
    l.quit = {x, l.restart.y + bh + 12.0f * s, bw, bh};
    l.beliefs = {x, l.quit.y + bh + 14.0f * s, bw, 36.0f * s};
    return l;
}

void draw_pause(const PauseLayout& l, float s, int w, int h, bool show_beliefs, Vector2 mouse) {
    dim(w, h, 120);
    panel(l.panel, s);
    text_centered("Paused", l.panel.x + l.panel.width * 0.5f, l.panel.y + 20.0f * s, 34.0f * s, kInk, Weight::Bold);
    button(l.resume, "Resume", s, Look::Primary, contains(l.resume, mouse));
    button(l.restart, "Restart", s, Look::Normal, contains(l.restart, mouse));
    button(l.quit, "Quit to Title", s, Look::Normal, contains(l.quit, mouse));
    checkbox(l.beliefs, "Show beliefs", show_beliefs, s, contains(l.beliefs, mouse));
}

HelpLayout layout_help(int w, int h, float s) {
    HelpLayout l;
    const float pw = std::min(800.0f * s, w - 30.0f * s);
    const float ph = std::min(660.0f * s, h - 30.0f * s);
    l.panel = {w * 0.5f - pw * 0.5f, h * 0.5f - ph * 0.5f, pw, ph};
    l.close = {l.panel.x + pw - 130.0f * s, l.panel.y + ph - 60.0f * s, 110.0f * s, 46.0f * s};
    return l;
}

void draw_help(const HelpLayout& l, float s, int w, int h, Vector2 mouse) {
    dim(w, h, 170);
    panel(l.panel, s);
    const float x = l.panel.x + 26.0f * s;
    const float tw = l.panel.width - 52.0f * s;
    float y = l.panel.y + 20.0f * s;
    text("How to play", x, y, 30.0f * s, kEnergy, Weight::Bold);
    y += 44.0f * s;

    struct Section {
        const char* title;
        const char* body;
        Color colour;
    };
    const Section sections[] = {
        {"The goal",
         "Wobo has been switched on somewhere in a map it knows, but not where. It moves around and scans "
         "to work out its position. You can't steer it - but you can sabotage it. Survive as many days as "
         "possible before Wobo correctly finds itself.",
         kInk},
        {"Reading the view",
         "The see-through pink Wobo is where it thinks it is. Pink arrows are all its guesses (its particle "
         "cloud) and pink rings are its strongest hypotheses. "
         "White lines are its last LiDAR scan, coloured dots are the colours it saw, and the dashed ring is "
         "how far it can see. Dotted lines show where it is going next. A red tether joins Wobo to its "
         "guess. When the guess is actually right, the red ring round Wobo and the outline round its status "
         "box fill up - if they close, the run is over.",
         kInk},
        {"Your actions",
         "Spend energy (it recharges) on interference. Smoke Bomb: a cloud around Wobo cuts its scanner "
         "range. Spin It: spin Wobo round so its wheels and gyro misreport motion. Colour Blind: sunglasses "
         "that scramble the colours it sees. Kidnap (from a card): a crane drops Wobo somewhere else without "
         "telling its software. Time them for when Wobo is about to see something telling.",
         kInk},
        {"Days and cards",
         "The clock runs one day from midnight to midnight. At each midnight, draft a card: new abilities, "
         "upgrades, more energy, or passive tricks. Overnight Wobo's sensors also recalibrate, so they are "
         "noisiest on day 1 and sharpen every day after.",
         kInk},
        {"Controls",
         "1-4 or click: use an action. Kidnap: click a spot, drag to aim. Space: pause. [ ]: speed. "
         "Drag or right-drag: pan. Wheel or pinch: zoom. B: show beliefs (also in the menu). V: scan lines. Tab: top-down "
         "view. F: centre on Wobo.",
         kMuted},
    };
    for (const Section& sec : sections) {
        text(sec.title, x, y, 19.0f * s, kCyan, Weight::Bold);
        y += 24.0f * s;
        y += text_wrapped(sec.body, x, y, 15.0f * s, tw, sec.colour, Weight::Regular, 1.3f);
        y += 10.0f * s;
    }
    button(l.close, "Got it", s, Look::Primary, contains(l.close, mouse));
}

GameOverLayout layout_game_over(int w, int h, float s) {
    GameOverLayout l;
    const float pw = std::min(480.0f * s, w - 30.0f * s);
    const float ph = 370.0f * s;
    l.panel = {w * 0.5f - pw * 0.5f, h * 0.5f - ph * 0.5f, pw, ph};
    const float bw = (pw - 70.0f * s) * 0.5f;
    const float bh = 52.0f * s;
    l.again = {l.panel.x + 26.0f * s, l.panel.y + ph - bh - 22.0f * s, bw, bh};
    l.menu = {l.again.x + bw + 18.0f * s, l.again.y, bw, bh};
    return l;
}

void draw_game_over(const GameOverLayout& l, float s, int w, int h, const RunScore& score, double day_length_s,
                    const std::string& map_name, const std::string& best, bool new_best, Vector2 mouse,
                    float time_s) {
    dim(w, h, 140);
    panel(l.panel, s);
    const float cx = l.panel.x + l.panel.width * 0.5f;
    float y = l.panel.y + 20.0f * s;
    text_centered("Wobo found itself!", cx, y, 34.0f * s, kEnergy, Weight::Bold);
    y += 46.0f * s;
    text_centered(map_name.c_str(), cx, y, 16.0f * s, kMuted, Weight::Bold);
    y += 30.0f * s;

    const std::string line = "Lost until " + survival_text(score.survival_s, day_length_s);
    text_centered(line.c_str(), cx, y, 26.0f * s, kInk, Weight::Bold);
    y += 40.0f * s;

    const struct {
        const char* label;
        int value;
    } stats[] = {
        {"Actions used", score.actions_used},
        {"Kidnaps", score.kidnaps},
        {"Close calls", score.close_calls},
        {"Times Wobo got suspicious", score.spins},
        {"Cards drafted", score.cards_taken},
    };
    for (const auto& st : stats) {
        text(st.label, l.panel.x + 50.0f * s, y, 16.0f * s, kMuted);
        char v[16];
        std::snprintf(v, sizeof(v), "%d", st.value);
        text(v, l.panel.x + l.panel.width - 50.0f * s - measure(v, 16.0f * s, Weight::Bold), y, 16.0f * s, kInk,
             Weight::Bold);
        y += 22.0f * s;
    }
    y += 6.0f * s;
    if (new_best) {
        const float pulse = 1.0f + 0.05f * std::sin(time_s * 6.0f);
        text_centered("NEW BEST!", cx, y, 22.0f * s * pulse, kGood, Weight::Bold);
    } else {
        text_centered(best.c_str(), cx, y, 16.0f * s, kEnergy, Weight::Bold);
    }
    button(l.again, "Play again", s, Look::Primary, contains(l.again, mouse));
    button(l.menu, "Quit to Title", s, Look::Normal, contains(l.menu, mouse));
}

}  // namespace wobo::ui
