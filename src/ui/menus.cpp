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

void map_card(Rectangle r, float s, const MapInfo& map, const std::string& best, bool chosen, bool hovered) {
    const float rad = 14.0f;
    Color edge = kPanelEdge;
    if (hovered && !chosen) edge = Color{70, 80, 124, 255};
    panel(r, s, chosen ? Color{24, 32, 58, 245} : kPanel, edge, rad);
    if (chosen) {
        const float k = std::min(1.0f, 2.0f * rad * s / std::min(r.width, r.height));
        DrawRectangleRoundedLinesEx(r, k, 8, 2.0f * s, kCyan);
    }
    const float pad = 14.0f * s;
    const float tw = r.width - 2.0f * pad;
    const auto name = wrap(map.name, 17.0f * s, tw, Weight::Bold);
    text(name.empty() ? "" : name[0].c_str(), r.x + pad, r.y + pad - 2.0f * s, 17.0f * s, kInk, Weight::Bold);
    if (!map.difficulty.empty()) {
        const float dy = r.y + pad + 28.0f * s;
        DrawCircleV({r.x + pad + 4.0f * s, dy + 7.0f * s}, 4.0f * s, difficulty_colour(map.difficulty));
        text(map.difficulty.c_str(), r.x + pad + 14.0f * s, dy, 13.0f * s, kMuted);
    }
    const bool played = best.rfind("Best", 0) == 0;
    const auto lines = wrap(best, 12.0f * s, tw);
    text(lines.empty() ? "" : lines[0].c_str(), r.x + pad, r.y + r.height - pad - 12.0f * s, 12.0f * s,
         played ? kEnergy : kMuted, played ? Weight::Bold : Weight::Regular);
}

}  // namespace

TitleLayout layout_title(int w, int h, float s, int map_count) {
    TitleLayout l;
    const float m = 36.0f * s;
    const float gap = 12.0f * s;
    const float ch = 100.0f * s;
    const float colw = 180.0f * s;
    const float y = h - m - ch;
    const int n = std::max(1, map_count);

    l.play = {w - m - colw, y, colw, ch - 44.0f * s};
    l.help = {l.play.x, y + ch - 36.0f * s, colw, 36.0f * s};
    const float row = l.play.x - gap - m;
    const float cw = (row - gap * (n - 1)) / n;
    if (cw >= 128.0f * s) {
        for (int i = 0; i < n; ++i) l.cards.push_back({m + i * (cw + gap), y, cw, ch});
    } else {
        const float ab = 44.0f * s;
        l.prev = {m, y + (ch - ab) * 0.5f, ab, ab};
        l.next = {l.play.x - gap - ab, l.prev.y, ab, ab};
        l.card = {l.prev.x + ab + gap, y, l.next.x - gap - (l.prev.x + ab + gap), ch};
    }
    l.panel = {m, y - 52.0f * s, w - 2.0f * m, ch + 52.0f * s};

    const float big = std::min(112.0f * s, w * 0.15f);
    l.title = {m, 28.0f * s, w - 2.0f * m, big * 1.6f + 24.0f * s};
    return l;
}

void draw_title(const TitleLayout& l, float s, int w, int h, const std::vector<MapInfo>& maps, int selected,
                const std::vector<std::string>& bests, Vector2 mouse, float time_s) {
    (void)h;
    // Wordmark: a quiet "where's" over a big WOBO, its question mark bobbing.
    const float big = std::min(112.0f * s, w * 0.15f);
    const float small = big * 0.3f;
    const float x = l.title.x;
    float y = l.title.y;
    text("where's", x + big * 0.03f, y, small, kMuted);
    y += small * 0.95f;
    text("WOBO", x, y, big, kInk, Weight::Bold);
    const float bob = std::sin(time_s * 2.2f) * 3.0f * s;
    text("?", x + measure("WOBO", big, Weight::Bold) + big * 0.02f, y + bob, big, kEnergy, Weight::Bold);
    y += big * 1.02f;
    text("You've kidnapped the robot. Keep it lost.", x + big * 0.03f, y, 16.0f * s, kMuted);

    if (maps.empty()) return;
    selected = std::clamp(selected, 0, static_cast<int>(maps.size()) - 1);
    const auto best = [&](int i) { return i < static_cast<int>(bests.size()) ? bests[i] : std::string(); };

    // The chosen map's description, just above the cards.
    const float fs = 15.0f * s;
    const Rectangle first = l.cards.empty() ? l.card : l.cards.front();
    const auto lines = wrap(maps[selected].description, fs, l.play.x + l.play.width - l.panel.x);
    float dy = first.y - 12.0f * s - fs * 1.3f * std::min<std::size_t>(lines.size(), 2);
    for (std::size_t i = 0; i < lines.size() && i < 2; ++i) {
        text(lines[i].c_str(), l.panel.x, dy, fs, kMuted);
        dy += fs * 1.3f;
    }

    if (!l.cards.empty()) {
        for (std::size_t i = 0; i < l.cards.size() && i < maps.size(); ++i) {
            map_card(l.cards[i], s, maps[i], best(static_cast<int>(i)), static_cast<int>(i) == selected,
                     contains(l.cards[i], mouse));
        }
    } else {
        button(l.prev, "<", s, Look::Normal, contains(l.prev, mouse));
        button(l.next, ">", s, Look::Normal, contains(l.next, mouse));
        map_card(l.card, s, maps[selected], best(selected), true, false);
    }
    button(l.play, "PLAY", s, Look::Primary, contains(l.play, mouse), 22.0f * s);
    button(l.help, "How to play", s, Look::Normal, contains(l.help, mouse), 14.0f * s);
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
