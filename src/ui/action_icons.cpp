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
    // A still swirl arrow round the head; only the eyes spin.
    const float spin = std::fmod(t * 240.0f, 360.0f);
    constexpr float kArc = 150.0f;
    DrawRing(c, 0.92f * u, 1.0f * u, kArc, kArc + 250.0f, 24, kOutline);
    const float a = (kArc + 250.0f) * DEG2RAD;
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
    // Sunglasses: a red lens and a green lens, a bridge, and arms.
    const Color lenses[] = {Color{255, 70, 80, 255}, Color{60, 200, 90, 255}};
    const float lw = 0.78f * u;
    const float lh = 0.62f * u;
    const float cy = c.y + 0.05f * u;
    DrawLineEx({c.x - 0.96f * u, cy - 0.3f * u}, {c.x - 0.8f * u, cy - 0.12f * u}, line * 2.4f, kOutline);
    DrawLineEx({c.x + 0.96f * u, cy - 0.3f * u}, {c.x + 0.8f * u, cy - 0.12f * u}, line * 2.4f, kOutline);
    DrawLineEx({c.x - 0.14f * u, cy - 0.16f * u}, {c.x + 0.14f * u, cy - 0.16f * u}, line * 2.6f, kOutline);
    for (int i = 0; i < 2; ++i) {
        const float side = i == 0 ? -1.0f : 1.0f;
        const Rectangle lens{c.x + side * 0.5f * u - lw * 0.5f, cy - lh * 0.5f, lw, lh};
        rounded(lens, 0.55f, lenses[i], line * 1.4f);
        // A glint.
        DrawLineEx({lens.x + lw * 0.22f, lens.y + lh * 0.3f}, {lens.x + lw * 0.42f, lens.y + lh * 0.18f}, line * 1.6f,
                   Color{255, 255, 255, 190});
    }
}

void gummy_wheel(Vector2 c, float u, float line) {
    // A tyre and hub, with pink bubblegum wrapped round the top and a strand
    // stretched off to the side.
    const Color gum{255, 128, 196, 255};
    const Color gum_light{255, 190, 226, 255};
    blob(c, 0.82f * u, Color{56, 58, 76, 255}, line);
    // Tread blocks round the tyre.
    for (int i = 0; i < 12; ++i) {
        const float a = kTwoPi * i / 12.0f;
        const Vector2 dir{std::cos(a), std::sin(a)};
        DrawLineEx({c.x + dir.x * 0.6f * u, c.y + dir.y * 0.6f * u}, {c.x + dir.x * 0.8f * u, c.y + dir.y * 0.8f * u},
                   0.09f * u, Color{96, 100, 122, 255});
    }
    blob(c, 0.4f * u, Color{170, 176, 196, 255}, line);
    DrawCircleV(c, 0.12f * u, kOutline);
    DrawRing(c, 0.56f * u, 0.98f * u + line, 200.0f, 340.0f, 20, kOutline);
    DrawRing(c, 0.56f * u + line, 0.98f * u, 203.0f, 337.0f, 20, gum);
    blob({c.x - 0.62f * u, c.y - 0.42f * u}, 0.2f * u, gum, line);
    blob({c.x + 0.7f * u, c.y - 0.5f * u}, 0.16f * u, gum, line);
    DrawCircleV({c.x - 0.1f * u, c.y - 0.86f * u}, 0.09f * u, gum_light);
    // The strand.
    DrawLineEx({c.x + 0.74f * u, c.y + 0.3f * u}, {c.x + 1.0f * u, c.y + 0.86f * u}, 0.1f * u + 2 * line, kOutline);
    DrawLineEx({c.x + 0.74f * u, c.y + 0.3f * u}, {c.x + 1.0f * u, c.y + 0.86f * u}, 0.1f * u, gum);
    blob({c.x + 0.74f * u, c.y + 0.3f * u}, 0.13f * u, gum, line);
}

void road_block(Vector2 c, float u, float line) {
    // A barricade: two legs, and a board with diagonal orange-and-white
    // stripes, a lamp on top.
    const Color orange{255, 128, 32, 255};
    const Color white{240, 240, 236, 255};
    for (float side : {-1.0f, 1.0f}) {
        const float x = c.x + side * 0.62f * u;
        DrawLineEx({x, c.y - 0.1f * u}, {x - side * 0.14f * u, c.y + 0.9f * u}, 0.14f * u + 2 * line, kOutline);
        DrawLineEx({x, c.y - 0.1f * u}, {x - side * 0.14f * u, c.y + 0.9f * u}, 0.14f * u, white);
    }
    const Rectangle board{c.x - 0.95f * u, c.y - 0.38f * u, 1.9f * u, 0.56f * u};
    rounded(board, 0.2f, white, line);
    BeginScissorMode(static_cast<int>(board.x), static_cast<int>(board.y), static_cast<int>(board.width + 1),
                     static_cast<int>(board.height + 1));
    for (int i = -1; i < 6; i += 2) {
        const float x0 = board.x + i * 0.34f * u;
        DrawTriangle({x0, board.y + board.height}, {x0 + 0.34f * u, board.y + board.height}, {x0 + 0.34f * u + board.height, board.y}, orange);
        DrawTriangle({x0, board.y + board.height}, {x0 + 0.34f * u + board.height, board.y}, {x0 + board.height, board.y}, orange);
    }
    EndScissorMode();
    DrawRectangleRoundedLinesEx(board, 0.2f, 6, line, kOutline);
    blob({c.x, c.y - 0.58f * u}, 0.17f * u, Color{255, 196, 60, 255}, line);
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
        case ActionId::Gummy: gummy_wheel(centre, u, line); break;
        case ActionId::Block: road_block(centre, u, line); break;
        case ActionId::Count: break;
    }
}

}  // namespace wobo::ui
