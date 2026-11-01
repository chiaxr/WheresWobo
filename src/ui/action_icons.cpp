#include "ui/action_icons.h"

#include <cmath>
#include <initializer_list>

#include "ui/ui_text.h"
#include "ui/widgets.h"

namespace wobo::ui {

namespace {

constexpr Color kRobot{222, 225, 232, 255};
constexpr Color kScreen{30, 36, 60, 255};

// A filled circle with the cartoon outline.
void blob(Vector2 c, float r, Color fill, float line) {
    DrawCircleV(c, r + line, kOutline);
    DrawCircleV(c, r, fill);
}

void rounded(Rectangle r, float roundness, Color fill, float line) {
    DrawRectangleRounded({r.x - line, r.y - line, r.width + 2 * line, r.height + 2 * line}, roundness, 6, kOutline);
    DrawRectangleRounded(r, roundness, 6, fill);
}

// A little four-point twinkle.
void spark(Vector2 c, float r, Color col) {
    DrawTriangle({c.x, c.y - r}, {c.x - r * 0.3f, c.y}, {c.x + r * 0.3f, c.y}, col);
    DrawTriangle({c.x - r * 0.3f, c.y}, {c.x, c.y + r}, {c.x + r * 0.3f, c.y}, col);
    DrawTriangle({c.x - r, c.y}, {c.x, c.y + r * 0.3f}, {c.x, c.y - r * 0.3f}, col);
    DrawTriangle({c.x, c.y - r * 0.3f}, {c.x, c.y + r * 0.3f}, {c.x + r, c.y}, col);
}

void smoke_bomb(Vector2 c, float u, float line, float t) {
    // Puffs of smoke billowing from the fuse.
    const Color smoke{206, 208, 220, 255};
    blob({c.x + 0.62f * u, c.y - 0.62f * u}, 0.24f * u, smoke, line);
    blob({c.x + 0.30f * u, c.y - 0.86f * u}, 0.2f * u, smoke, line);
    blob({c.x + 0.82f * u, c.y - 0.30f * u}, 0.17f * u, smoke, line);
    // The bomb.
    const Vector2 body{c.x - 0.15f * u, c.y + 0.2f * u};
    const Vector2 cap{body.x + 0.42f * u, body.y - 0.42f * u};
    DrawLineEx(cap, {c.x + 0.42f * u, c.y - 0.5f * u}, 0.12f * u + 2 * line, kOutline);
    DrawLineEx(cap, {c.x + 0.42f * u, c.y - 0.5f * u}, 0.12f * u, Color{196, 142, 88, 255});
    DrawRectanglePro({cap.x, cap.y, 0.34f * u + 2 * line, 0.24f * u + 2 * line},
                     {(0.34f * u + 2 * line) * 0.5f, (0.24f * u + 2 * line) * 0.5f}, 45.0f, kOutline);
    DrawRectanglePro({cap.x, cap.y, 0.34f * u, 0.24f * u}, {0.17f * u, 0.12f * u}, 45.0f, Color{90, 92, 110, 255});
    blob(body, 0.6f * u, Color{44, 44, 62, 255}, line);
    DrawCircleV({body.x - 0.24f * u, body.y - 0.24f * u}, 0.13f * u, Color{255, 255, 255, 140});
    // Fizzing spark on the fuse.
    const float flicker = 0.85f + 0.15f * std::sin(t * 18.0f);
    spark({c.x + 0.45f * u, c.y - 0.52f * u}, 0.24f * u * flicker, kEnergy);
}

void robot_head(Rectangle head, float line) {
    // Antenna, then the head with its screen.
    const Vector2 top{head.x + head.width * 0.5f, head.y};
    DrawLineEx(top, {top.x, top.y - head.height * 0.3f}, line * 2.0f, kOutline);
    blob({top.x, top.y - head.height * 0.34f}, head.height * 0.1f, kDanger, line);
    rounded(head, 0.3f, kRobot, line);
    const Rectangle screen{head.x + head.width * 0.14f, head.y + head.height * 0.2f, head.width * 0.72f,
                           head.height * 0.6f};
    DrawRectangleRounded(screen, 0.3f, 6, kScreen);
}

void dizzy_robot(Vector2 c, float u, float line, float t) {
    const Rectangle head{c.x - 0.62f * u, c.y - 0.3f * u, 1.24f * u, 0.95f * u};
    // A swirl round the head.
    const float spin = std::fmod(t * 240.0f, 360.0f);
    DrawRing(c, 0.92f * u, 1.0f * u, spin, spin + 250.0f, 24, kOutline);
    const float a = (spin + 250.0f) * DEG2RAD;
    const Vector2 tip{c.x + std::cos(a) * 0.96f * u, c.y + std::sin(a) * 0.96f * u};
    const Vector2 dir{-std::sin(a), std::cos(a)};
    const Vector2 n{std::cos(a), std::sin(a)};
    DrawTriangle({tip.x + dir.x * 0.22f * u, tip.y + dir.y * 0.22f * u},
                 {tip.x - n.x * 0.14f * u, tip.y - n.y * 0.14f * u},
                 {tip.x + n.x * 0.14f * u, tip.y + n.y * 0.14f * u}, kOutline);
    DrawTriangle({tip.x + dir.x * 0.22f * u, tip.y + dir.y * 0.22f * u},
                 {tip.x + n.x * 0.14f * u, tip.y + n.y * 0.14f * u},
                 {tip.x - n.x * 0.14f * u, tip.y - n.y * 0.14f * u}, kOutline);
    robot_head(head, line);
    // Spiral eyes.
    const Color eye{255, 220, 90, 255};
    for (float side : {-1.0f, 1.0f}) {
        const Vector2 e{c.x + side * 0.27f * u, c.y + 0.18f * u};
        const float r = 0.17f * u;
        DrawRing(e, r * 0.72f, r, spin * side, spin * side + 290.0f, 16, eye);
        DrawRing(e, r * 0.25f, r * 0.5f, spin * side + 180.0f, spin * side + 400.0f, 12, eye);
    }
    spark({c.x - 0.8f * u, c.y - 0.7f * u}, 0.16f * u, kEnergy);
}

void kidnapped_robot(Vector2 c, float u, float line) {
    const Color rope{196, 142, 88, 255};
    // Hanging from a hook.
    DrawLineEx({c.x, c.y - 1.0f * u}, {c.x, c.y - 0.62f * u}, line * 2.2f, kOutline);
    DrawRing({c.x - 0.08f * u, c.y - 0.6f * u}, 0.08f * u, 0.08f * u + line * 2.2f, 0.0f, 200.0f, 10, kOutline);
    // Body, tied up.
    const Rectangle body{c.x - 0.5f * u, c.y + 0.15f * u, 1.0f * u, 0.72f * u};
    rounded(body, 0.25f, kRobot, line);
    const float rw = 0.13f * u;
    DrawLineEx({body.x, body.y + body.height * 0.25f}, {body.x + body.width, body.y + body.height * 0.75f}, rw, rope);
    DrawLineEx({body.x, body.y + body.height * 0.75f}, {body.x + body.width, body.y + body.height * 0.25f}, rw, rope);
    DrawLineEx({body.x - line, body.y + body.height * 0.5f}, {body.x + body.width + line, body.y + body.height * 0.5f},
               rw, rope);
    // Head with a blindfold, knot trailing off to one side.
    const Rectangle head{c.x - 0.56f * u, c.y - 0.52f * u, 1.12f * u, 0.7f * u};
    rounded(head, 0.3f, kRobot, line);
    const Rectangle band{head.x - line, head.y + head.height * 0.3f, head.width + 2 * line, head.height * 0.36f};
    DrawRectangleRec(band, kOutline);
    DrawRectangleRec({band.x, band.y + line, band.width, band.height - 2 * line}, Color{60, 58, 84, 255});
    const Vector2 knot{band.x + band.width, band.y + band.height * 0.5f};
    DrawTriangle(knot, {knot.x + 0.3f * u, knot.y + 0.26f * u}, {knot.x + 0.34f * u, knot.y - 0.04f * u}, kOutline);
    DrawTriangle(knot, {knot.x + 0.32f * u, knot.y - 0.14f * u}, {knot.x + 0.16f * u, knot.y - 0.3f * u}, kOutline);
}

void colour_blind(Vector2 c, float u, float line) {
    // "RGB", each letter in its own colour.
    const float fs = 0.95f * u;
    const char* letters[] = {"R", "G", "B"};
    const Color colours[] = {Color{235, 64, 64, 255}, Color{40, 190, 90, 255}, Color{60, 120, 240, 255}};
    float total = 0.0f;
    for (const char* l : letters) total += measure(l, fs, Weight::Bold);
    float x = c.x - total * 0.5f;
    const float y = c.y - 0.82f * u;
    for (int i = 0; i < 3; ++i) {
        for (const Vector2 o : {Vector2{-1, 0}, Vector2{1, 0}, Vector2{0, -1}, Vector2{0, 1}}) {
            text(letters[i], x + o.x * line * 1.5f, y + o.y * line * 1.5f, fs, kOutline, Weight::Bold);
        }
        text(letters[i], x, y, fs, colours[i], Weight::Bold);
        x += measure(letters[i], fs, Weight::Bold);
    }
    // Swap arrows: right along the top, left along the bottom.
    const float w = 0.7f * u;
    const float t = 0.12f * u;
    const float y1 = c.y + 0.42f * u, y2 = c.y + 0.75f * u;
    DrawLineEx({c.x - w, y1}, {c.x + w - 0.2f * u, y1}, t, kOutline);
    DrawTriangle({c.x + w, y1}, {c.x + w - 0.26f * u, y1 - 0.17f * u}, {c.x + w - 0.26f * u, y1 + 0.17f * u}, kOutline);
    DrawLineEx({c.x - w + 0.2f * u, y2}, {c.x + w, y2}, t, kOutline);
    DrawTriangle({c.x - w, y2}, {c.x - w + 0.26f * u, y2 + 0.17f * u}, {c.x - w + 0.26f * u, y2 - 0.17f * u}, kOutline);
}

}  // namespace

void action_icon(ActionId id, Vector2 centre, float size, float time_s) {
    const float u = size * 0.5f;
    const float line = std::fmax(1.0f, size * 0.045f);
    switch (id) {
        case ActionId::Range: smoke_bomb(centre, u, line, time_s); break;
        case ActionId::Drift: dizzy_robot(centre, u, line, time_s); break;
        case ActionId::Kidnap: kidnapped_robot(centre, u, line); break;
        case ActionId::Colour: colour_blind(centre, u, line); break;
        case ActionId::Count: break;
    }
}

}  // namespace wobo::ui
