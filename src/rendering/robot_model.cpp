#include "rendering/robot_model.h"

#include <cmath>
#include <initializer_list>

#include "rendering/primitives.h"
#include "rlgl.h"

namespace wobo {

namespace {

constexpr Color kBody{222, 225, 232, 255};
constexpr Color kJoint{70, 74, 90, 255};
constexpr Color kScreen{30, 36, 60, 255};
constexpr Color kOutline{30, 28, 40, 255};
constexpr Color kAccent{255, 196, 60, 255};
constexpr Color kAntennaTip{255, 98, 88, 255};
constexpr Color kCloth{60, 58, 84, 255};
constexpr Color kRope{196, 142, 88, 255};
constexpr float kHull = 0.014f;

struct Painter {
    const WoboStyle& style;
    bool hull;

    Color c(Color base) const {
        if (hull) return prim::with_alpha(kOutline, style.alpha);
        Color out = style.tint_amount > 0.0f ? prim::mix(base, style.tint, style.tint_amount) : base;
        out.a = static_cast<unsigned char>(base.a * clampf(style.alpha, 0.0f, 1.0f));
        return out;
    }
    void box(Vector3 p, Vector3 s, Color col) const {
        if (hull) s = {s.x + 2 * kHull, s.y + 2 * kHull, s.z + 2 * kHull};
        prim::box(p, s, c(col));
    }
    void cyl(Vector3 a, Vector3 b, float r, Color col) const {
        if (hull) {
            // Extend the ends along the axis so the caps get an outline too.
            const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (len > 1e-6f) {
                const float k = kHull / len;
                a = {a.x - dx * k, a.y - dy * k, a.z - dz * k};
                b = {b.x + dx * k, b.y + dy * k, b.z + dz * k};
            }
            r += kHull;
        }
        prim::cylinder(a, b, r, c(col), 12);
    }
    void ball(Vector3 p, float r, Color col) const {
        prim::sphere(p, hull ? r + kHull : r, c(col), 6, 10);
    }
};

}  // namespace

void RobotModel::update(float dt_s, float linear_mps, float angular_rps, bool scanning) {
    time_ += dt_s;
    wheel_angle_ += linear_mps / 0.07f * dt_s;
    lidar_angle_ += (scanning ? 9.0f : 4.0f) * dt_s;
    bob_ = std::sin(time_ * (std::fabs(linear_mps) > 0.02f ? 14.0f : 2.2f)) *
           (std::fabs(linear_mps) > 0.02f ? 0.008f : 0.004f);

    // Antenna: a damped spring pushed by acceleration and turning.
    const float accel = (linear_mps - last_speed_) / std::fmax(dt_s, 1e-4f);
    last_speed_ = linear_mps;
    antenna_v_ += (-60.0f * antenna_ - 4.0f * antenna_v_ - 0.6f * accel) * dt_s;
    antenna_ += antenna_v_ * dt_s;
    antenna_ = clampf(antenna_, -0.7f, 0.7f);
    lean_ = lerpf(lean_, clampf(accel * 0.04f, -0.12f, 0.12f), 1.0f - std::exp(-dt_s * 10.0f));
    turn_ = lerpf(turn_, angular_rps, 1.0f - std::exp(-dt_s * 6.0f));
}

void RobotModel::kick() { antenna_v_ += 9.0f; }

float RobotModel::head_tilt(Face face) const {
    return (face == Face::Confused || face == Face::Thinking) ? 9.0f * std::sin(time_ * 1.5f) : 0.0f;
}

void RobotModel::draw(const ToonShader& shader, Vec2 position, float heading, Face face,
                      const WoboStyle& style) const {
    if (style.outline && style.alpha >= 0.99f) {
        // Inverted hull: the outline shell shows only its back faces.
        rlDrawRenderBatchActive();
        rlSetCullFace(RL_CULL_FACE_FRONT);
        shader.set_flat(true);
        draw_parts(position, heading, face, style, true);
        rlDrawRenderBatchActive();
        rlSetCullFace(RL_CULL_FACE_BACK);
        shader.set_flat(false);
    }
    draw_parts(position, heading, face, style, false);
    if (style.face) {
        shader.set_flat(true);
        rlPushMatrix();
        rlTranslatef(position.x, bob_ + style.lift, position.z);
        rlRotatef(-heading * kRadToDeg, 0, 1, 0);
        rlRotatef(-lean_ * kRadToDeg, 0, 0, 1);
        rlTranslatef(0.0f, 0.37f, 0.0f);
        rlRotatef(head_tilt(face), 1, 0, 0);
        rlTranslatef(0.0f, -0.37f, 0.0f);
        draw_face(face, style);
        if (style.sunglasses) draw_sunglasses(style);
        rlPopMatrix();
        shader.set_flat(false);
    }
    if (style.dizzy_stars) {
        // Little stars circling over its head.
        shader.set_flat(true);
        for (int i = 0; i < 3; ++i) {
            const float a = time_ * 3.0f + kTwoPi * i / 3.0f;
            rlPushMatrix();
            rlTranslatef(position.x + std::cos(a) * 0.17f, 0.72f + style.lift + 0.03f * std::sin(time_ * 6.0f + i),
                         position.z + std::sin(a) * 0.17f);
            rlRotatef(45.0f, 0, 0, 1);
            rlRotatef(time_ * 200.0f, 0, 1, 0);
            // Inverted-hull outline, then the star.
            rlDrawRenderBatchActive();
            rlSetCullFace(RL_CULL_FACE_FRONT);
            prim::box({0, 0, 0}, {0.075f, 0.075f, 0.075f}, prim::with_alpha(kOutline, style.alpha));
            rlDrawRenderBatchActive();
            rlSetCullFace(RL_CULL_FACE_BACK);
            prim::box({0, 0, 0}, {0.06f, 0.06f, 0.06f}, prim::with_alpha(Color{255, 214, 64, 255}, style.alpha));
            rlPopMatrix();
        }
        shader.set_flat(false);
    }
}

void RobotModel::draw_sunglasses(const WoboStyle& style) const {
    // In the head frame, over the eyes: a red lens and a blue lens in a dark
    // frame, with arms running back along the sides of the head.
    const float fx = 0.141f;
    const float ey = 0.385f;
    const Color frame = prim::with_alpha(kOutline, style.alpha);
    prim::box({fx, ey + 0.036f, 0.0f}, {0.012f, 0.016f, 0.25f}, frame);  // brow bar
    for (float side : {-1.0f, 1.0f}) {
        const Color lens = prim::with_alpha(side < 0 ? Color{255, 70, 80, 255} : Color{60, 140, 255, 255}, style.alpha);
        prim::box({fx, ey, side * 0.058f}, {0.012f, 0.07f, 0.098f}, frame);
        prim::box({fx + 0.003f, ey, side * 0.058f}, {0.012f, 0.056f, 0.084f}, lens);
        // A glint on each lens.
        prim::box({fx + 0.008f, ey + 0.014f, side * 0.058f - 0.022f}, {0.006f, 0.012f, 0.016f},
                  prim::with_alpha(Color{255, 255, 255, 200}, style.alpha));
        // Arm along the side of the head.
        prim::box({0.06f, ey + 0.02f, side * 0.147f}, {0.17f, 0.014f, 0.01f}, frame);
    }
}

void RobotModel::draw_xray(const ToonShader& shader, Vec2 position, float heading, Color colour, float lift) const {
    WoboStyle style;
    style.lift = lift;
    style.tint = colour;
    style.tint_amount = 1.0f;
    style.alpha = colour.a / 255.0f;
    style.outline = false;
    style.face = false;
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    rlDisableDepthMask();
    shader.set_flat(true);
    draw_parts(position, heading, Face::Curious, style, false);
    rlDrawRenderBatchActive();
    shader.set_flat(false);
    rlEnableDepthMask();
    rlEnableDepthTest();
}

void RobotModel::draw_parts(Vec2 position, float heading, Face face, const WoboStyle& style,
                            bool hull) const {
    const Painter p{style, hull};
    rlPushMatrix();
    rlTranslatef(position.x, style.lift, position.z);
    rlRotatef(-heading * kRadToDeg, 0, 1, 0);

    // Wheels, with a hub and a spoke that turns.
    for (float side : {-1.0f, 1.0f}) {
        p.cyl({0.0f, 0.075f, side * 0.118f}, {0.0f, 0.075f, side * 0.172f}, 0.075f, kJoint);
        if (!hull) {
            p.cyl({0.0f, 0.075f, side * 0.172f}, {0.0f, 0.075f, side * 0.178f}, 0.032f, kBody);
            // Rolling forward (+x) turns both wheels the same way: top moves forward.
            const float a = -wheel_angle_;
            p.box({std::cos(a) * 0.05f, 0.075f + std::sin(a) * 0.05f, side * 0.176f}, {0.02f, 0.02f, 0.008f},
                  kAccent);
        }
    }

    rlTranslatef(0.0f, bob_, 0.0f);
    rlRotatef(-lean_ * kRadToDeg, 0, 0, 1);

    // Body.
    p.box({0.0f, 0.112f, 0.0f}, {0.30f, 0.04f, 0.23f}, kJoint);
    p.box({0.0f, 0.19f, 0.0f}, {0.29f, 0.13f, 0.22f}, kBody);
    if (!hull) {
        p.box({0.146f, 0.20f, 0.0f}, {0.006f, 0.06f, 0.11f}, kScreen);
        p.box({0.150f, 0.20f, 0.03f}, {0.006f, 0.022f, 0.022f}, kAccent);
        p.box({0.150f, 0.20f, -0.02f}, {0.006f, 0.016f, 0.04f}, Color{120, 230, 255, 255});
    }
    // Stubby arms that swing as it rolls.
    for (float side : {-1.0f, 1.0f}) {
        const float swing = std::sin(time_ * 7.0f + (side > 0 ? 0.0f : kPi)) * std::fabs(last_speed_) * 0.25f;
        p.cyl({0.0f, 0.21f, side * 0.12f}, {0.03f + swing, 0.14f, side * 0.16f}, 0.022f, kBody);
        p.ball({0.03f + swing, 0.135f, side * 0.162f}, 0.03f, kJoint);
    }

    if (style.tied_up) {
        // Two loops of rope round the body that pin the arms, and a cross on
        // the chest.
        const float rx = 0.153f;
        const float rz = 0.18f;
        for (float y : {0.155f, 0.225f}) {
            p.cyl({-rx, y, -rz}, {rx, y, -rz}, 0.013f, kRope);
            p.cyl({-rx, y, rz}, {rx, y, rz}, 0.013f, kRope);
            p.cyl({rx, y, -rz}, {rx, y, rz}, 0.013f, kRope);
            p.cyl({-rx, y, -rz}, {-rx, y, rz}, 0.013f, kRope);
        }
        p.cyl({rx, 0.155f, -0.1f}, {rx, 0.225f, 0.1f}, 0.011f, kRope);
        p.cyl({rx, 0.225f, -0.1f}, {rx, 0.155f, 0.1f}, 0.011f, kRope);
    }

    // Neck and head (tilted a touch when confused or thinking).
    p.cyl({0.0f, 0.25f, 0.0f}, {0.0f, 0.29f, 0.0f}, 0.04f, kJoint);
    rlPushMatrix();
    rlTranslatef(0.0f, 0.37f, 0.0f);
    rlRotatef(head_tilt(face), 1, 0, 0);
    p.box({0.0f, 0.0f, 0.0f}, {0.25f, 0.17f, 0.28f}, kBody);
    if (!hull) p.box({0.127f, 0.0f, 0.0f}, {0.008f, 0.125f, 0.225f}, kScreen);
    if (style.blindfold) {
        // A band over the eyes (proud of the face so the eyes stay hidden),
        // knotted at the side with two tails.
        p.box({0.0f, 0.015f, 0.0f}, {0.29f, 0.08f, 0.30f}, kCloth);
        p.box({-0.07f, 0.015f, -0.158f}, {0.05f, 0.05f, 0.03f}, kCloth);
        for (float angle : {25.0f, 60.0f}) {
            rlPushMatrix();
            rlTranslatef(-0.08f, 0.0f, -0.165f);
            rlRotatef(angle, 0, 0, 1);
            p.box({0.0f, -0.05f, 0.0f}, {0.045f, 0.1f, 0.012f}, kCloth);
            rlPopMatrix();
        }
    }
    // Ear bolts.
    for (float side : {-1.0f, 1.0f}) {
        p.cyl({0.0f, 0.0f, side * 0.14f}, {0.0f, 0.0f, side * 0.165f}, 0.035f, kJoint);
    }
    // LiDAR puck with a marker that sweeps round.
    p.cyl({-0.02f, 0.085f, 0.0f}, {-0.02f, 0.125f, 0.0f}, 0.062f, kJoint);
    if (!hull) {
        p.cyl({-0.02f, 0.125f, 0.0f}, {-0.02f, 0.132f, 0.0f}, 0.05f, Color{120, 230, 255, 255});
        p.box({-0.02f + std::cos(lidar_angle_) * 0.058f, 0.105f, std::sin(lidar_angle_) * 0.058f},
              {0.02f, 0.025f, 0.02f}, kAccent);
    }
    // Antenna on a spring.
    {
        const Vector3 base{-0.09f, 0.085f, 0.09f};
        const Vector3 tip{base.x + std::sin(antenna_) * 0.11f, base.y + std::cos(antenna_) * 0.11f,
                          base.z + std::sin(antenna_ * 0.6f) * 0.04f};
        p.cyl(base, tip, 0.009f, kJoint);
        const bool blink = std::fmod(time_, 1.2f) < 0.6f;
        p.ball(tip, 0.024f, blink ? kAntennaTip : prim::shade(kAntennaTip, 0.75f));
    }
    rlPopMatrix();
    rlPopMatrix();
}

void RobotModel::draw_face(Face face, const WoboStyle& style) const {
    // Face plane: x = 0.132 in the head frame (head centre at y = 0.37).
    const float fx = 0.133f;
    const float ey = 0.385f;
    const float ez = 0.055f;
    Color eye{120, 230, 255, 255};
    if (face == Face::Confident || face == Face::Hopeful) eye = Color{130, 255, 170, 255};
    if (face == Face::Confused || face == Face::Dizzy) eye = Color{255, 220, 90, 255};
    if (face == Face::Blind) eye = Color{255, 110, 100, 255};
    eye.a = static_cast<unsigned char>(255 * clampf(style.alpha, 0.0f, 1.0f));

    const auto bar = [&](float y, float z, float w, float h, float angle_deg) {
        rlPushMatrix();
        rlTranslatef(fx, y, z);
        rlRotatef(angle_deg, 1, 0, 0);
        prim::box({0, 0, 0}, {0.006f, h, w}, eye);
        rlPopMatrix();
    };

    // Glance in the direction it is turning.
    const float glance = clampf(-turn_ * 0.012f, -0.012f, 0.012f);
    const bool blinking = std::fmod(time_ + 0.7f, 3.7f) < 0.12f;

    for (float side : {-1.0f, 1.0f}) {
        if (style.blindfold) break;  // nothing to see under the cloth
        const float z = side * ez + glance;
        switch (face) {
            case Face::Curious:
                if (blinking) bar(ey, z, 0.05f, 0.012f, 0);
                else bar(ey, z, 0.042f, 0.055f, 0);
                break;
            case Face::Thinking:
                bar(ey + 0.012f, z - 0.01f, 0.036f, 0.036f, 0);
                break;
            case Face::Hopeful:
                bar(ey, z, 0.042f, blinking ? 0.012f : 0.048f, 0);
                break;
            case Face::Confident:  // happy ^ ^
                bar(ey + 0.004f, z - 0.012f, 0.032f, 0.011f, -40);
                bar(ey + 0.004f, z + 0.012f, 0.032f, 0.011f, 40);
                break;
            case Face::Confused:  // one big, one small
                if (side < 0) bar(ey, z, 0.05f, 0.058f, 0);
                else bar(ey - 0.01f, z, 0.024f, 0.024f, 0);
                break;
            case Face::Squint:
                bar(ey, z, 0.054f, 0.011f, side * 12.0f);
                break;
            case Face::Blind:  // X X
                bar(ey, z, 0.05f, 0.011f, 45);
                bar(ey, z, 0.05f, 0.011f, -45);
                break;
            case Face::Dizzy: {  // spinning crosses
                const float a = time_ * 540.0f * side;
                bar(ey, z, 0.048f, 0.011f, a);
                bar(ey, z, 0.048f, 0.011f, a + 90.0f);
                break;
            }
            case Face::Surprised:
                bar(ey + 0.006f, z, 0.05f, 0.066f, 0);
                break;
        }
    }

    // Mouth.
    const float my = 0.335f;
    switch (face) {
        case Face::Confident:
        case Face::Hopeful:
            bar(my, -0.03f, 0.022f, 0.01f, 30);
            bar(my - 0.006f, 0.0f, 0.04f, 0.01f, 0);
            bar(my, 0.03f, 0.022f, 0.01f, -30);
            break;
        case Face::Confused:
            bar(my, -0.02f, 0.03f, 0.01f, 20);
            bar(my, 0.015f, 0.03f, 0.01f, -20);
            break;
        case Face::Surprised:
        case Face::Dizzy:
            bar(my, 0.0f, 0.024f, 0.024f, 0);
            break;
        case Face::Blind:
        case Face::Squint:
            bar(my, 0.0f, 0.05f, 0.01f, 0);
            break;
        default:
            bar(my, 0.0f, 0.03f, 0.01f, 0);
            break;
    }
}

}  // namespace wobo
