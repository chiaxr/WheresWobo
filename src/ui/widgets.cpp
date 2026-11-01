#include "ui/widgets.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "rlgl.h"

#include "ui/ui_text.h"

namespace wobo::ui {

bool contains(Rectangle r, Vector2 p) {
    return p.x >= r.x && p.y >= r.y && p.x <= r.x + r.width && p.y <= r.y + r.height;
}

Rectangle inset(Rectangle r, float d) { return {r.x + d, r.y + d, r.width - 2 * d, r.height - 2 * d}; }

namespace {
float roundness(Rectangle r, float radius) {
    const float m = std::min(r.width, r.height);
    return m > 0.0f ? std::min(1.0f, 2.0f * radius / m) : 0.0f;
}
}  // namespace

void panel(Rectangle r, float s, Color fill, Color edge, float radius) {
    const float rad = radius * s;
    const float line = std::max(1.0f, 1.2f * s);
    DrawRectangleRounded({r.x, r.y + 4 * s, r.width, r.height}, roundness(r, rad), 8, kShadow);
    DrawRectangleRounded(r, roundness(r, rad), 8, edge);
    const Rectangle in = inset(r, line);
    DrawRectangleRounded(in, roundness(in, rad - line), 8, fill);
}

void meter(Rectangle r, float frac, Color fill, float s, Color back) {
    frac = std::clamp(frac, 0.0f, 1.0f);
    const float rad = r.height * 0.5f;
    DrawRectangleRounded(r, 1.0f, 8, back);
    if (frac > 0.0f) {
        const Rectangle f{r.x, r.y, std::max(r.height, r.width * frac), r.height};
        DrawRectangleRounded(f, roundness(f, rad), 8, fill);
        // Highlight stripe for a glossy cartoon look.
        DrawRectangleRounded({f.x + r.height * 0.3f, f.y + r.height * 0.18f, std::max(0.0f, f.width - r.height * 0.6f),
                              r.height * 0.22f},
                             1.0f, 4, Color{255, 255, 255, 70});
    }
    DrawRectangleRoundedLinesEx(r, 1.0f, 8, std::max(1.0f, 1.5f * s), kOutline);
}

Color button_ink(Look look) {
    switch (look) {
        case Look::Primary:
        case Look::Toggled: return kOutline;
        case Look::Disabled: return kMuted;
        default: return kInk;
    }
}

namespace {
Color lighten(Color c, int by) {
    return Color{static_cast<unsigned char>(std::min(255, c.r + by)), static_cast<unsigned char>(std::min(255, c.g + by)),
                 static_cast<unsigned char>(std::min(255, c.b + by)), c.a};
}
}  // namespace

Rectangle button(Rectangle r, const char* label, float s, Look look, bool hovered, float font_size) {
    Color face = kRaised;
    Color edge = kPanelEdge;
    const Color ink = button_ink(look);
    switch (look) {
        case Look::Normal: break;
        case Look::Primary: face = edge = Color{255, 196, 60, 255}; break;
        case Look::Danger: face = edge = kDanger; break;
        case Look::Disabled: face = Color{22, 27, 46, 255}; edge = Color{34, 40, 66, 255}; break;
        case Look::Toggled: face = edge = kCyan; break;
    }
    const bool live = hovered && look != Look::Disabled;
    if (live) {
        face = lighten(face, 18);
        edge = lighten(edge, 40);
    }
    // Flat and slim: a hairline edge and a soft drop shadow, lifting a touch
    // under the pointer.
    const float rad = 10.0f * s;
    const float line = std::max(1.0f, 1.2f * s);
    const float lift = live ? 1.5f * s : 0.0f;
    const Rectangle top{r.x, r.y - lift, r.width, r.height};
    DrawRectangleRounded({r.x, r.y + 2.0f * s, r.width, r.height}, roundness(r, rad), 8, kShadow);
    DrawRectangleRounded(top, roundness(top, rad), 8, edge);
    const Rectangle in = inset(top, line);
    DrawRectangleRounded(in, roundness(in, rad - line), 8, face);
    if (label && *label) {
        const float size = font_size > 0.0f ? font_size : std::min(22.0f * s, top.height * 0.46f);
        const float w = measure(label, size, Weight::Bold);
        text(label, top.x + (top.width - w) * 0.5f, top.y + (top.height - size) * 0.5f, size, ink, Weight::Bold);
    }
    return top;
}

void checkbox(Rectangle r, const char* label, bool checked, float s, bool hovered) {
    const float b = std::min(r.height, 28.0f * s);
    const Rectangle box{r.x, r.y + (r.height - b) * 0.5f, b, b};
    DrawRectangleRounded(box, 0.3f, 6, checked ? kCyan : kPanelEdge);
    const Rectangle in = inset(box, std::max(1.0f, 1.2f * s));
    Color fill = checked ? kCyan : kRaised;
    if (hovered) fill = lighten(fill, 22);
    DrawRectangleRounded(in, 0.3f, 6, fill);
    if (checked) {
        const float t = 3.5f * s;
        const Vector2 a{box.x + b * 0.24f, box.y + b * 0.52f};
        const Vector2 m{box.x + b * 0.43f, box.y + b * 0.72f};
        const Vector2 e{box.x + b * 0.78f, box.y + b * 0.3f};
        DrawLineEx(a, m, t, kOutline);
        DrawLineEx(m, e, t, kOutline);
        DrawCircleV(m, t * 0.5f, kOutline);
    }
    const float fs = 19.0f * s;
    text(label, box.x + b + 12.0f * s, r.y + (r.height - fs) * 0.5f, fs, hovered ? kInk : kMuted, Weight::Bold);
}

void icon(Icon which, Vector2 c, float size, Color colour) {
    const float h = size * 0.5f;
    switch (which) {
        case Icon::Pause: {
            const float bw = size * 0.28f;
            DrawRectangleRounded({c.x - h * 0.75f, c.y - h, bw, size}, 0.4f, 4, colour);
            DrawRectangleRounded({c.x + h * 0.75f - bw, c.y - h, bw, size}, 0.4f, 4, colour);
            break;
        }
        case Icon::Play:
            // raylib wants counter-clockwise vertices on screen.
            DrawTriangle({c.x - h * 0.7f, c.y - h}, {c.x - h * 0.7f, c.y + h}, {c.x + h * 0.9f, c.y}, colour);
            break;
        case Icon::Menu: {
            const float bh = size * 0.17f;
            for (int i = -1; i <= 1; ++i) {
                DrawRectangleRounded({c.x - h, c.y + i * size * 0.36f - bh * 0.5f, size, bh}, 1.0f, 4, colour);
            }
            break;
        }
        case Icon::FastForward:
            // Two play triangles nose to tail.
            DrawTriangle({c.x - h, c.y - h * 0.8f}, {c.x - h, c.y + h * 0.8f}, {c.x, c.y}, colour);
            DrawTriangle({c.x, c.y - h * 0.8f}, {c.x, c.y + h * 0.8f}, {c.x + h, c.y}, colour);
            break;
    }
}

Rectangle icon_button(Rectangle r, Icon which, float s, Look look, bool hovered) {
    const Rectangle face = button(r, "", s, look, hovered);
    icon(which, {face.x + face.width * 0.5f, face.y + face.height * 0.5f}, std::min(face.width, face.height) * 0.42f,
         button_ink(look));
    return face;
}

void pill(Rectangle r, const char* label, Color fill, Color ink, float s, float font_size) {
    DrawRectangleRounded(r, 1.0f, 8, fill);
    DrawRectangleRoundedLinesEx(r, 1.0f, 8, std::max(1.0f, 1.5f * s), kOutline);
    const float w = measure(label, font_size, Weight::Bold);
    text(label, r.x + (r.width - w) * 0.5f, r.y + (r.height - font_size) * 0.5f, font_size, ink, Weight::Bold);
}

void growing_outline(Rectangle r, float radius, float t, float thickness, Color colour) {
    t = std::clamp(t, 0.0f, 1.0f);
    if (t <= 0.0f) return;
    radius = std::min(radius, std::min(r.width, r.height) * 0.5f);

    // The perimeter as a closed polyline, clockwise from the top centre.
    std::vector<Vector2> pts;
    const auto corner = [&](float cx, float cy, float a0) {
        for (int i = 0; i <= 8; ++i) {
            const float a = (a0 + 90.0f * i / 8.0f) * DEG2RAD;
            pts.push_back({cx + std::cos(a) * radius, cy + std::sin(a) * radius});
        }
    };
    const float x0 = r.x, y0 = r.y, x1 = r.x + r.width, y1 = r.y + r.height;
    pts.push_back({(x0 + x1) * 0.5f, y0});
    corner(x1 - radius, y0 + radius, -90.0f);  // top right
    corner(x1 - radius, y1 - radius, 0.0f);    // bottom right
    corner(x0 + radius, y1 - radius, 90.0f);   // bottom left
    corner(x0 + radius, y0 + radius, 180.0f);  // top left
    pts.push_back({(x0 + x1) * 0.5f, y0});

    float perimeter = 0.0f;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        perimeter += std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    }
    const float reach = perimeter * 0.5f * t;

    // Walk `reach` along the polyline, forwards or backwards from the start.
    const auto walk = [&](bool forward) {
        float left = reach;
        const int n = static_cast<int>(pts.size());
        int i = forward ? 0 : n - 1;
        while (left > 0.0f) {
            const int j = forward ? i + 1 : i - 1;
            if (j < 0 || j >= n) break;
            const Vector2 a = pts[i], b = pts[j];
            const float len = std::hypot(b.x - a.x, b.y - a.y);
            const float k = len > 0.0f ? std::min(1.0f, left / len) : 1.0f;
            const Vector2 end{a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k};
            DrawLineEx(a, end, thickness, colour);
            DrawCircleV(end, thickness * 0.5f, colour);  // round joints
            left -= len;
            i = j;
        }
    };
    DrawCircleV(pts.front(), thickness * 0.5f, colour);
    walk(true);
    walk(false);
}

void dim(int w, int h, unsigned char alpha) {
    DrawRectangle(0, 0, w, h, Color{6, 8, 16, alpha});
}

namespace {
// An elliptical band from r0 to r1, its colour blending from c0 to c1.
void band(Vector2 c, float r0, float r1, float squash, Color c0, Color c1) {
    constexpr int kSegments = 72;
    const auto at = [&](int i, float r) {
        const float a = 2.0f * PI * static_cast<float>(i) / kSegments;
        return Vector2{c.x + std::cos(a) * r, c.y + std::sin(a) * r * squash};
    };
    const auto put = [](Vector2 p, Color col) {
        rlColor4ub(col.r, col.g, col.b, col.a);
        rlVertex2f(p.x, p.y);
    };
    rlCheckRenderBatchLimit(kSegments * 6);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < kSegments; ++i) {
        // raylib's DrawRing winding, so the triangles face the camera.
        put(at(i, r0), c0);
        put(at(i + 1, r0), c0);
        put(at(i, r1), c1);
        put(at(i + 1, r0), c0);
        put(at(i + 1, r1), c1);
        put(at(i, r1), c1);
    }
    rlEnd();
}
}  // namespace

void spotlight(Vector2 c, float r_clear, float r_dark, float squash, Color dark, int w, int h) {
    const auto shade = [&](float k) {
        Color col = dark;
        col.a = static_cast<unsigned char>(dark.a * k);
        return col;
    };
    const float span = r_dark - r_clear;
    const float far = (std::hypot(static_cast<float>(w), static_cast<float>(h)) + r_dark) / std::max(squash, 0.2f);
    // An eased falloff in three steps, then solid dark past every corner.
    band(c, r_clear, r_clear + span * 0.35f, squash, shade(0.0f), shade(0.18f));
    band(c, r_clear + span * 0.35f, r_clear + span * 0.7f, squash, shade(0.18f), shade(0.6f));
    band(c, r_clear + span * 0.7f, r_dark, squash, shade(0.6f), shade(1.0f));
    band(c, r_dark, far, squash, shade(1.0f), shade(1.0f));
}

}  // namespace wobo::ui
