#include "ui/hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui/action_icons.h"
#include "ui/menus.h"
#include "ui/ui_text.h"
#include "ui/widgets.h"

namespace wobo::ui {

namespace {

Color action_colour(ActionId id) {
    switch (id) {
        case ActionId::Range: return kCyan;
        case ActionId::Drift: return Color{255, 150, 50, 255};
        case ActionId::Kidnap: return kDanger;
        case ActionId::Colour: return kBelief;
        case ActionId::Count: break;
    }
    return kInk;
}

Color mood_colour(RobotMood m) {
    switch (m) {
        case RobotMood::Confident: return kGood;
        case RobotMood::Hopeful: return Color{170, 230, 120, 255};
        case RobotMood::Confused: return kEnergy;
        case RobotMood::Blind: return kDanger;
        case RobotMood::ShortSighted: return Color{255, 150, 50, 255};
        default: return kCyan;
    }
}

// A 24-hour clock for the day: one full turn of the hand is one day, from
// midnight at the top round to midnight again, where the card marks the
// draft. The night half of the face is tinted, the elapsed part fills green,
// and the last stretch turns yellow and pulses so a draft never arrives
// unannounced.
void day_clock(Vector2 c, float radius, float progress, float s, float time_s) {
    progress = std::clamp(progress, 0.0f, 1.0f);
    const bool closing = progress > 0.85f;
    const float pulse = closing ? 1.0f + 0.06f * std::sin(time_s * 10.0f) : 1.0f;
    const float r = radius * pulse;

    DrawCircleV(c, r, kPanelEdge);
    DrawCircleV(c, r - 1.5f * s, kRaised);
    // Night: 18:00 through midnight to 06:00 is the top half of the dial.
    DrawCircleSector(c, r - 1.5f * s, 180.0f, 360.0f, 32, kSpot);

    // Elapsed wedge, from twelve o'clock clockwise.
    if (progress > 0.0f) {
        Color wedge = closing ? kEnergy : kGood;
        wedge.a = 200;
        DrawCircleSector(c, r - 3.5f * s, -90.0f, -90.0f + 360.0f * progress, 48, wedge);
    }

    // Ticks: twelve, with the quarters longer.
    for (int i = 1; i < 12; ++i) {
        const float a = (-90.0f + 30.0f * i) * DEG2RAD;
        const float len = (i % 3 == 0 ? 6.0f : 3.5f) * s;
        const Vector2 dir{std::cos(a), std::sin(a)};
        const float outer = r - 3.0f * s;
        DrawLineEx({c.x + dir.x * outer, c.y + dir.y * outer},
                   {c.x + dir.x * (outer - len), c.y + dir.y * (outer - len)}, (i % 3 == 0 ? 1.6f : 1.0f) * s,
                   kMuted);
    }

    // A little card at twelve: the draft.
    const Rectangle card{c.x - 3.5f * s, c.y - r + 3.5f * s, 7.0f * s, 9.0f * s};
    DrawRectangleRounded(card, 0.3f, 4, kBelief);
    DrawRectangleRoundedLinesEx(card, 0.3f, 4, 1.2f * s, kOutline);

    // The hand.
    const float a = (-90.0f + 360.0f * progress) * DEG2RAD;
    const float len = r - 6.0f * s;
    DrawLineEx(c, {c.x + std::cos(a) * len, c.y + std::sin(a) * len}, 2.4f * s, kInk);
    DrawCircleV(c, 3.0f * s, kInk);
}

// One button of the time controls: the current mode is lit cyan.
void segment(Rectangle r, Icon which, bool on, bool hovered, float s) {
    const float k = std::min(1.0f, 18.0f * s / std::min(r.width, r.height));
    if (on) DrawRectangleRounded(r, k, 8, kCyan);
    else if (hovered) DrawRectangleRounded(r, k, 8, kRaised);
    icon(which, {r.x + r.width * 0.5f, r.y + r.height * 0.5f}, std::min(r.width, r.height) * 0.36f,
         on ? kBackground : kInk);
}

// A thin rounded track with no outline, for the bar's quieter meters.
void slim_meter(Rectangle r, float frac, Color fill) {
    frac = std::clamp(frac, 0.0f, 1.0f);
    DrawRectangleRounded(r, 1.0f, 8, kBackground);
    if (frac > 0.0f) DrawRectangleRounded({r.x, r.y, std::max(r.height, r.width * frac), r.height}, 1.0f, 8, fill);
}

}  // namespace

double speed_value(int index) {
    static const double values[kSpeedCount] = {1.0, 2.0};
    return values[std::clamp(index, 0, kSpeedCount - 1)];
}

bool hud_contains(const HudLayout& l, Vector2 p) {
    for (const Rectangle& r : {l.bar, l.energy}) {
        if (contains(r, p)) return true;
    }
    for (const auto& [id, r] : l.actions) {
        if (contains(r, p)) return true;
    }
    return false;
}

HudLayout layout_hud(int w, int h, float s, const RunDirector& run) {
    HudLayout l;
    l.s = s;
    const float m = 12.0f * s;
    const float row = 72.0f * s;
    const float pad = 12.0f * s;
    const float g = 14.0f * s;
    const float bb = 44.0f * s;  // help and menu buttons
    l.bar = {m, m, w - 2.0f * m, row};
    const float cy = l.bar.y + row * 0.5f;

    // Left: clock, day and time, then the time controls in a well.
    float x = l.bar.x + pad;
    l.clock_r = 26.0f * s;
    l.clock = {x + l.clock_r, cy};
    l.day_panel = {x, l.bar.y + 6.0f * s, 2.0f * l.clock_r + 10.0f * s + 84.0f * s, row - 12.0f * s};
    x += l.day_panel.width + g;
    const float tw = 44.0f * s;
    const float tg = 3.0f * s;
    l.transport = {x, cy - 22.0f * s, 3.0f * tw + 4.0f * tg, 44.0f * s};
    l.pause = {x + tg, cy - 19.0f * s, tw, 38.0f * s};
    l.play = {l.pause.x + tw + tg, l.pause.y, tw, l.pause.height};
    l.fast = {l.play.x + tw + tg, l.pause.y, tw, l.pause.height};
    x = l.transport.x + l.transport.width + g;
    l.dividers.push_back(x);
    x += g;

    // Right: help and menu.
    float rx = l.bar.x + l.bar.width - pad;
    l.menu = {rx - bb, cy - bb * 0.5f, bb, bb};
    l.help = {l.menu.x - 8.0f * s - bb, l.menu.y, bb, bb};
    rx = l.help.x - g;
    l.dividers.push_back(rx);
    rx -= g;

    // Middle: Wobo's status, or a second row of its own when there is no room.
    Rectangle st{x, l.bar.y, rx - x, row};
    if (st.width < 360.0f * s) {
        l.two_rows = true;
        l.bar.height += 64.0f * s;
        st = {l.bar.x + pad, l.bar.y + row, l.bar.width - 2.0f * pad, 64.0f * s};
    }
    l.status_panel = {st.x - 8.0f * s, st.y + 6.0f * s, st.width + 16.0f * s, st.height - 12.0f * s};
    const float cw = std::min(240.0f * s, st.width * 0.45f);
    l.confidence = {st.x + st.width - cw, st.y, cw, st.height};
    l.mood = {st.x, st.y, st.width - cw - g, st.height};

    // Bottom: action bar with the energy meter above it.
    std::vector<ActionId> owned;
    for (int i = 0; i < kActionCount; ++i) {
        if (run.owns(static_cast<ActionId>(i))) owned.push_back(static_cast<ActionId>(i));
    }
    float bw = 104.0f * s;
    const float bgap = 8.0f * s;
    const float max_total = static_cast<float>(w) - 2.0f * m;
    float total = owned.size() * bw + (owned.size() - 1) * bgap;
    if (total > max_total) {
        bw = (max_total - (owned.size() - 1) * bgap) / owned.size();
        total = max_total;
    }
    const float abh = std::min(80.0f * s, bw * 0.85f);
    const float ay = static_cast<float>(h) - m - abh;
    float ax = w * 0.5f - total * 0.5f;
    for (ActionId id : owned) {
        l.actions.push_back({id, Rectangle{ax, ay, bw, abh}});
        ax += bw + bgap;
    }
    const float ew = std::max(total, 380.0f * s);
    l.energy = {w * 0.5f - ew * 0.5f, ay - 12.0f * s - 24.0f * s, ew, 24.0f * s};
    l.effects_origin = {m, l.energy.y - 10.0f * s, 0, 0};

    l.top_inset = (l.bar.y + l.bar.height + m) / h;
    l.bottom_inset = (h - l.energy.y + m) / h;
    return l;
}

void draw_hud(const HudLayout& l, const RunDirector& run, const HudState& st) {
    const float s = l.s;
    const RunScore score = run.score();

    // --- The bar --------------------------------------------------------
    panel(l.bar, s, kPanel, kPanelEdge, 18.0f);
    const float line = std::max(1.0f, 1.0f * s);
    for (float dx : l.dividers) {
        DrawRectangleRec({dx, l.bar.y + 14.0f * s, line, 72.0f * s - 28.0f * s}, kPanelEdge);
    }
    if (l.two_rows) {
        DrawRectangleRec({l.bar.x + 14.0f * s, l.bar.y + 72.0f * s, l.bar.width - 28.0f * s, line}, kPanelEdge);
    }

    // --- Day and time ---------------------------------------------------
    {
        day_clock(l.clock, l.clock_r, run.day_progress(), s, st.time_s);
        const float tx = l.clock.x + l.clock_r + 10.0f * s;
        char day[32];
        std::snprintf(day, sizeof(day), "DAY %d", score.day);
        text(day, tx, l.clock.y - 22.0f * s, 20.0f * s, kInk, Weight::Bold);
        const std::string t = time_of_day(run.day_progress());
        text(t.c_str(), tx, l.clock.y + 2.0f * s, 17.0f * s, kEnergy, Weight::Bold);

        if (contains(l.day_panel, st.mouse)) {
            char tip[48];
            const double left = (1.0 - run.day_progress()) * run.day_length_s();
            std::snprintf(tip, sizeof(tip), "Card draft at midnight, in %.0fs", std::ceil(left));
            const float fs = 14.0f * s;
            const Rectangle tr{l.day_panel.x, l.bar.y + l.bar.height + 6.0f * s,
                               measure(tip, fs, Weight::Bold) + 24.0f * s, 28.0f * s};
            panel(tr, s);
            text(tip, tr.x + 12.0f * s, tr.y + (tr.height - fs) * 0.5f, fs, kInk, Weight::Bold);
        }
    }

    // --- Time controls: the current mode is lit ----------------------------
    {
        const float k = std::min(1.0f, 24.0f * s / l.transport.height);
        DrawRectangleRounded(l.transport, k, 8, kBackground);
        DrawRectangleRoundedLinesEx(l.transport, k, 8, line, kPanelEdge);
        segment(l.pause, Icon::Pause, st.paused, contains(l.pause, st.mouse), s);
        segment(l.play, Icon::Play, !st.paused && st.speed_index == 0, contains(l.play, st.mouse), s);
        segment(l.fast, Icon::FastForward, !st.paused && st.speed_index == 1, contains(l.fast, st.mouse), s);
    }

    // --- Wobo's status ---------------------------------------------------
    {
        const RobotAgent& agent = run.agent();
        const PoseEstimate est = agent.estimate();
        const RobotMood mood = agent.mood();
        const Rectangle r = l.mood;
        const float top = r.y + r.height * 0.5f - 20.0f * s;

        DrawCircleV({r.x + 5.0f * s, top + 10.0f * s}, 5.0f * s, mood_colour(mood));
        std::string title = std::string("Wobo is ") + mood_name(mood);
        text(title.c_str(), r.x + 17.0f * s, top, 18.0f * s, kInk, Weight::Bold);
        const std::string reason = "\"" + agent.plan().reason + "\"";
        const auto lines = wrap(reason, 13.0f * s, r.width);
        if (!lines.empty()) text(lines[0].c_str(), r.x, top + 25.0f * s, 13.0f * s, kMuted);

        const Rectangle c = l.confidence;
        const float ctop = c.y + c.height * 0.5f - 21.0f * s;
        text("CONFIDENCE", c.x, ctop, 11.0f * s, kMuted, Weight::Bold);
        char pct[16];
        std::snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(std::round(est.confidence * 100.0f)));
        text(pct, c.x + c.width - measure(pct, 11.0f * s, Weight::Bold), ctop, 11.0f * s, kMuted, Weight::Bold);
        slim_meter({c.x, ctop + 17.0f * s, c.width, 8.0f * s}, est.confidence, kBelief);

        const LocalizationReferee& ref = run.referee();
        char lock[64];
        Color lock_col = kMuted;
        if (ref.lock_progress() > 0.0f && st.alarm > 0.5f) {
            std::snprintf(lock, sizeof(lock), "LOCKING ON - %.1fs left",
                          std::max(0.0f, (1.0f - ref.lock_progress()) * ref.config().stability_s));
            lock_col = (std::fmod(st.time_s, 0.5f) < 0.25f) ? kDanger : kInk;
        } else {
            std::snprintf(lock, sizeof(lock), "Its best guess is %.1f m off", ref.position_error());
            lock_col = kGood;
        }
        text(lock, c.x, ctop + 31.0f * s, 12.0f * s, lock_col, Weight::Bold);

        // Lock-on: a red outline creeps round the status as the lock builds,
        // and closes the loop when Wobo is found. Its length eases towards
        // the real progress (and slides back when a lock slips); it pulses
        // only once the lock has held long enough to sound the alarm.
        const float lock_t = st.lock_shown;
        if (lock_t > 0.002f) {
            const float beat = st.alarm * (0.5f + 0.5f * std::sin(st.time_s * 12.0f));
            const float thick = (3.0f + 2.0f * beat) * s;
            Color glow = kDanger;
            glow.a = static_cast<unsigned char>(50 + 70 * beat);
            growing_outline(l.status_panel, 12.0f * s, lock_t, thick * 2.6f, glow);
            growing_outline(l.status_panel, 12.0f * s, lock_t, thick, kDanger);
        }
    }

    // --- Controls ----------------------------------------------------------
    button(l.help, "?", s, Look::Normal, contains(l.help, st.mouse));
    icon_button(l.menu, Icon::Menu, s, Look::Normal, contains(l.menu, st.mouse));

    // --- Energy --------------------------------------------------------------
    {
        const EnergySystem& e = run.energy();
        const float frac = e.current() / e.capacity();
        meter(l.energy, frac, kEnergy, s);
        // Labels are drawn twice, clipped: dark ink over the yellow fill and
        // light ink over the empty track, so they read at any level.
        char label[64];
        std::snprintf(label, sizeof(label), "ENERGY  %d / %d", static_cast<int>(e.current()),
                      static_cast<int>(e.capacity()));
        char regen[32];
        std::snprintf(regen, sizeof(regen), "+%.1f/s", e.regen_rate());
        const float fs = l.energy.height * 0.62f;
        const float ty = l.energy.y + (l.energy.height - fs) * 0.5f;
        const float rx = l.energy.x + l.energy.width - measure(regen, fs, Weight::Bold) - 12.0f * s;
        const int split = static_cast<int>(l.energy.x + l.energy.width * std::clamp(frac, 0.0f, 1.0f));
        const int top = static_cast<int>(l.energy.y);
        const int height = static_cast<int>(l.energy.height + 1);
        BeginScissorMode(static_cast<int>(l.energy.x), top, split - static_cast<int>(l.energy.x), height);
        text(label, l.energy.x + 12.0f * s, ty, fs, kOutline, Weight::Bold);
        text(regen, rx, ty, fs, kOutline, Weight::Bold);
        EndScissorMode();
        BeginScissorMode(split, top, static_cast<int>(l.energy.x + l.energy.width) - split + 1, height);
        text(label, l.energy.x + 12.0f * s, ty, fs, kInk, Weight::Bold);
        text(regen, rx, ty, fs, kInk, Weight::Bold);
        EndScissorMode();
    }

    // --- Action bar --------------------------------------------------------
    int key = 1;
    const ActionInfo* hovered = nullptr;
    Rectangle hovered_rect{};
    for (const auto& [id, rect] : l.actions) {
        const ActionInfo& info = action_info(id);
        const ActionState& state = run.actions().state(id);
        const ActivateResult can = run.can_activate(id);
        const float cost = run.action_cost(id);
        const bool hover = contains(rect, st.mouse);
        const bool active = state.active_left > 0.0f;
        const bool targeting = st.targeting && id == ActionId::Kidnap;
        if (hover) {
            hovered = &info;
            hovered_rect = rect;
        }

        const Color col = action_colour(id);
        const float lift = hover ? 3.0f * s : 0.0f;
        const Rectangle r{rect.x, rect.y - lift, rect.width, rect.height};
        DrawRectangleRounded({r.x, r.y + 4 * s, r.width, r.height}, 0.25f, 8, kOutline);
        DrawRectangleRounded(r, 0.25f, 8, kOutline);
        const Color face = can == ActivateResult::Ok || active || targeting ? col : Color{80, 76, 104, 255};
        DrawRectangleRounded(inset(r, 2.5f * s), 0.25f, 8, face);
        if (targeting || active) {
            DrawRectangleRoundedLinesEx(inset(r, -3.0f * s), 0.25f, 8, 3.0f * s,
                                        std::fmod(st.time_s, 0.6f) < 0.3f ? kInk : col);
        }

        // Cooldown shade from the top.
        float cd_total = run.stats().get(std::string("action.") + info.key + ".cooldown");
        if (state.cooldown_left > 0.0f && cd_total > 0.0f) {
            const float frac = std::clamp(state.cooldown_left / cd_total, 0.0f, 1.0f);
            const Rectangle in = inset(r, 2.5f * s);
            BeginScissorMode(static_cast<int>(in.x), static_cast<int>(in.y), static_cast<int>(in.width),
                             static_cast<int>(in.height * frac + 1));
            DrawRectangleRounded(in, 0.25f, 8, Color{20, 18, 32, 150});
            EndScissorMode();
        }

        const Color ink = kOutline;
        const float big = std::min(26.0f * s, r.height * 0.32f);
        char keybuf[8];
        std::snprintf(keybuf, sizeof(keybuf), "%d", key++);
        text(keybuf, r.x + 8.0f * s, r.y + 5.0f * s, 14.0f * s, ink, Weight::Bold);
        if (state.cooldown_left > 0.0f) {
            // While recharging, the countdown takes the glyph's place.
            char cdbuf[16];
            std::snprintf(cdbuf, sizeof(cdbuf), "%.0fs", std::ceil(state.cooldown_left));
            text_centered(cdbuf, r.x + r.width * 0.5f, r.y + r.height * 0.24f, big, kInk, Weight::Bold);
        } else {
            const float icon_size = std::min(r.width * 0.5f, r.height * 0.5f);
            action_icon(id, {r.x + r.width * 0.5f, r.y + r.height * 0.42f}, icon_size, st.time_s);
        }
        const auto name_lines = wrap(info.name, 11.0f * s, r.width - 8.0f * s, Weight::Bold);
        float ny = r.y + r.height - 6.0f * s - 13.0f * s * name_lines.size();
        for (const auto& line : name_lines) {
            text_centered(line.c_str(), r.x + r.width * 0.5f, ny, 11.0f * s, ink, Weight::Bold);
            ny += 13.0f * s;
        }

        // Cost badge.
        char costbuf[16];
        std::snprintf(costbuf, sizeof(costbuf), "%d", static_cast<int>(cost));
        const float cw = std::max(26.0f * s, measure(costbuf, 13.0f * s, Weight::Bold) + 12.0f * s);
        const bool afford = run.energy().can_afford(cost);
        pill({r.x + r.width - cw - 4.0f * s, r.y + 4.0f * s, cw, 18.0f * s}, costbuf, afford ? kEnergy : kDanger,
             kOutline, s, 13.0f * s);

        if (active && state.active_total > 0.0f) {
            // Remaining duration, in the gap between the button and the energy meter.
            meter({r.x + 6.0f * s, rect.y - 10.0f * s, r.width - 12.0f * s, 7.0f * s},
                  state.active_left / state.active_total, col, s);
        }
    }

    // Tooltip.
    if (hovered) {
        const float tw = 270.0f * s;
        const float tx = std::clamp(hovered_rect.x + hovered_rect.width * 0.5f - tw * 0.5f, 8.0f * s,
                                    static_cast<float>(GetScreenWidth()) - tw - 8.0f * s);
        const auto lines = wrap(hovered->description, 14.0f * s, tw - 24.0f * s);
        const float th = (54.0f + 17.0f * lines.size()) * s;
        const Rectangle tip{tx, l.energy.y - th - 10.0f * s, tw, th};
        panel(tip, s);
        text(hovered->name, tip.x + 12.0f * s, tip.y + 10.0f * s, 17.0f * s, kInk, Weight::Bold);
        std::string stream = std::string("Affects: ") + hovered->stream;
        text(stream.c_str(), tip.x + 12.0f * s, tip.y + 30.0f * s, 13.0f * s, action_colour(hovered->id), Weight::Bold);
        float y = tip.y + 48.0f * s;
        for (const auto& line : lines) {
            text(line.c_str(), tip.x + 12.0f * s, y, 14.0f * s, kMuted);
            y += 17.0f * s;
        }
    }

    // --- Active effects (bottom-left, stacking upwards) ---------------------
    {
        float y = l.effects_origin.y;
        const float ew = 250.0f * s;
        const float eh = 34.0f * s;
        for (int i = kActionCount - 1; i >= 0; --i) {
            const auto id = static_cast<ActionId>(i);
            const ActionState& state = run.actions().state(id);
            if (state.active_left <= 0.0f) continue;
            y -= eh + 6.0f * s;
            const Rectangle r{l.effects_origin.x, y, ew, eh};
            panel(r, s);
            DrawRectangleRounded({r.x + 6.0f * s, r.y + 6.0f * s, 8.0f * s, r.height - 12.0f * s}, 1.0f, 4,
                                 action_colour(id));
            const ActionInfo& info = action_info(id);
            text(info.name, r.x + 22.0f * s, r.y + 3.0f * s, 14.0f * s, kInk, Weight::Bold);
            std::string stream = std::string("on ") + info.stream;
            text(stream.c_str(), r.x + 22.0f * s, r.y + 18.0f * s, 11.0f * s, kMuted);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1fs", state.active_left);
            text(buf, r.x + r.width - measure(buf, 16.0f * s, Weight::Bold) - 10.0f * s, r.y + 9.0f * s, 16.0f * s,
                 action_colour(id), Weight::Bold);
        }
    }

    // --- Toast / targeting banner ------------------------------------------
    const float cx = l.bar.x + l.bar.width * 0.5f;
    const float ty = l.bar.y + l.bar.height + 12.0f * s;
    if (st.targeting) {
        const char* msg = "Pick a spot to drop Wobo - drag to aim it. Right-click or Esc cancels.";
        const float fs = 16.0f * s;
        const float w = measure(msg, fs, Weight::Bold) + 28.0f * s;
        panel({cx - w * 0.5f, ty, w, 34.0f * s}, s, Color{120, 30, 40, 235}, kDanger);
        text_centered(msg, cx, ty + 9.0f * s, fs, kInk, Weight::Bold);
    } else if (st.toast_alpha > 0.0f && !st.toast.empty()) {
        const float fs = 17.0f * s;
        const float w = measure(st.toast.c_str(), fs, Weight::Bold) + 28.0f * s;
        const unsigned char a = static_cast<unsigned char>(255 * std::clamp(st.toast_alpha, 0.0f, 1.0f));
        Color fill = kPanel;
        fill.a = static_cast<unsigned char>(fill.a * st.toast_alpha);
        Color edge = st.toast_colour;
        edge.a = a;
        panel({cx - w * 0.5f, ty, w, 34.0f * s}, s, fill, edge);
        Color ink = st.toast_colour;
        ink.a = a;
        text_centered(st.toast.c_str(), cx, ty + 8.0f * s, fs, ink, Weight::Bold);
    }
}

}  // namespace wobo::ui
