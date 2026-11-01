#include "rendering/mover_models.h"

#include <cmath>

#include "rendering/primitives.h"
#include "rlgl.h"

namespace wobo {

namespace {

constexpr Color kOutline{30, 28, 40, 255};
constexpr float kHull = 0.014f;
constexpr Color kSkin{236, 196, 160, 255};
constexpr Color kDark{44, 44, 56, 255};

// Draws either the outline shell or the figure itself; the same calls build
// both, as in RobotModel.
struct Pen {
    bool hull;
    Color c(Color base) const { return hull ? kOutline : base; }
    void box(Vector3 p, Vector3 s, Color col) const {
        if (hull) s = {s.x + 2 * kHull, s.y + 2 * kHull, s.z + 2 * kHull};
        prim::box(p, s, c(col));
    }
    void cyl(Vector3 a, Vector3 b, float r, Color col) const { prim::cylinder(a, b, hull ? r + kHull : r, c(col), 10); }
    void ball(Vector3 p, float r, Color col) const { prim::sphere(p, hull ? r + kHull : r, c(col), 6, 10); }
};

// Rotate about the vertical axis at `pivot` (local coordinates) by `deg`.
template <typename F>
void turned(Vector3 pivot, float deg, F draw) {
    rlPushMatrix();
    rlTranslatef(pivot.x, pivot.y, pivot.z);
    rlRotatef(deg, 0, 1, 0);
    rlTranslatef(-pivot.x, -pivot.y, -pivot.z);
    draw();
    rlPopMatrix();
}

template <typename F>
void tilted(Vector3 pivot, float deg, F draw) {
    rlPushMatrix();
    rlTranslatef(pivot.x, pivot.y, pivot.z);
    rlRotatef(deg, 0, 0, 1);
    rlTranslatef(-pivot.x, -pivot.y, -pivot.z);
    draw();
    rlPopMatrix();
}

void donkey(const Pen& pen, const Mover& m, float t) {
    const Color coat{142, 126, 116, 255};
    const Color belly{206, 196, 186, 255};
    const float gait = std::sin(m.stride_m * 9.0f);
    const bool walking = m.mood == MoverMood::Walking;
    // Torn: the head swings from one pile to the other. Eating: head down.
    float look = 0.0f, nod = 0.0f;
    if (m.mood == MoverMood::Torn) look = 70.0f * std::sin(t * 1.6f);
    if (m.mood == MoverMood::Eating) nod = -40.0f + 6.0f * std::sin(t * 5.0f);
    if (m.mood == MoverMood::Pausing) look = 20.0f * std::sin(t * 0.7f);
    // Body along local x.
    pen.box({0.0f, 0.38f, 0}, {0.56f, 0.24f, 0.26f}, coat);
    pen.box({0.0f, 0.3f, 0}, {0.44f, 0.08f, 0.22f}, belly);
    for (float x : {-0.2f, 0.2f}) {
        for (float z : {-0.09f, 0.09f}) {
            const float swing = walking ? 0.06f * gait * (x * z > 0 ? 1.0f : -1.0f) : 0.0f;
            pen.cyl({x, 0.3f, z}, {x + swing, 0.02f, z}, 0.035f, coat);
            pen.box({x + swing, 0.02f, z}, {0.06f, 0.04f, 0.06f}, kDark);
        }
    }
    pen.cyl({-0.28f, 0.44f, 0}, {-0.38f, 0.26f, 0}, 0.02f, coat);   // tail
    pen.ball({-0.39f, 0.24f, 0}, 0.035f, kDark);
    turned({0.24f, 0.48f, 0}, look, [&] {
        tilted({0.24f, 0.48f, 0}, nod, [&] {
            pen.box({0.3f, 0.52f, 0}, {0.16f, 0.2f, 0.12f}, coat);                 // neck
            pen.box({0.27f, 0.6f, 0}, {0.1f, 0.06f, 0.04f}, kDark);                // mane
            pen.box({0.42f, 0.6f, 0}, {0.22f, 0.12f, 0.13f}, coat);                // head
            pen.box({0.53f, 0.57f, 0}, {0.08f, 0.09f, 0.12f}, belly);              // muzzle
            for (float z : {-0.045f, 0.045f}) {
                pen.box({0.36f, 0.73f, z}, {0.04f, 0.16f, 0.035f}, coat);          // ears
                if (!pen.hull) pen.ball({0.47f, 0.63f, z * 1.45f}, 0.014f, kDark); // eyes
            }
        });
    });
}

void person(const Pen& pen, const Mover& m, float t, Color coat, Color trousers, bool tricorn) {
    const bool walking = m.mood == MoverMood::Walking;
    const float gait = walking ? std::sin(m.stride_m * 8.0f) : 0.0f;
    const bool shrug = m.mood == MoverMood::Shrugging;
    float look = 0.0f;
    if (m.mood == MoverMood::Pausing) look = 35.0f * std::sin(t * 1.2f);
    // Legs.
    for (float z : {-0.06f, 0.06f}) {
        const float swing = 0.09f * gait * (z > 0 ? 1.0f : -1.0f);
        pen.cyl({0.0f, 0.42f, z}, {swing, 0.04f, z}, 0.04f, trousers);
        pen.box({swing + 0.03f, 0.03f, z}, {0.12f, 0.06f, 0.07f}, kDark);
    }
    // Body and arms.
    pen.box({0.0f, 0.6f, 0}, {0.17f, 0.36f, 0.26f}, coat);
    for (float z : {-0.16f, 0.16f}) {
        const float swing = -0.08f * gait * (z > 0 ? 1.0f : -1.0f);
        const float raise = shrug ? 0.12f * (0.5f + 0.5f * std::sin(t * 6.0f)) : 0.0f;
        pen.cyl({0.0f, 0.74f, z}, {swing, 0.46f + raise, z * (shrug ? 1.5f : 1.1f)}, 0.035f, coat);
        pen.ball({swing, 0.44f + raise, z * (shrug ? 1.5f : 1.1f)}, 0.035f, kSkin);
    }
    turned({0, 0.85f, 0}, look, [&] {
        pen.ball({0.0f, 0.9f, 0}, 0.1f, kSkin);
        if (tricorn) {
            // 1736: a tricorn hat.
            pen.cyl({0, 0.97f, 0}, {0, 1.02f, 0}, 0.13f, kDark);
            pen.box({0, 1.04f, 0}, {0.1f, 0.06f, 0.1f}, kDark);
        } else {
            // A guard's peaked cap.
            pen.cyl({0, 0.96f, 0}, {0, 1.04f, 0}, 0.1f, coat);
            pen.box({0.09f, 0.965f, 0}, {0.08f, 0.015f, 0.14f}, kDark);
        }
        if (!pen.hull) {
            for (float z : {-0.035f, 0.035f}) pen.ball({0.09f, 0.92f, z}, 0.012f, kDark);
        }
    });
}

void parts(const Pen& pen, const Mover& m, const Pose2D& pose, float t) {
    rlPushMatrix();
    rlTranslatef(pose.x, 0.0f, pose.z);
    rlRotatef(-pose.heading_rad * kRadToDeg, 0, 1, 0);
    if (m.kind == "donkey") {
        donkey(pen, m, t);
    } else if (m.kind == "guard") {
        person(pen, m, t, Color{44, 58, 108, 255}, Color{36, 40, 60, 255}, false);
    } else {
        person(pen, m, t, prim::to_color(mover_colour(m.kind)), Color{70, 60, 50, 255}, true);
    }
    rlPopMatrix();
}

}  // namespace

void draw_mover(const ToonShader& shader, const Mover& m, const Pose2D& pose, float time_s) {
    rlDrawRenderBatchActive();
    rlSetCullFace(RL_CULL_FACE_FRONT);
    shader.set_flat(true);
    parts(Pen{true}, m, pose, time_s);
    rlDrawRenderBatchActive();
    rlSetCullFace(RL_CULL_FACE_BACK);
    shader.set_flat(false);
    parts(Pen{false}, m, pose, time_s);
}

}  // namespace wobo
