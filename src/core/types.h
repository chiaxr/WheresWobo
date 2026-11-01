#pragma once

// Shared math and data types for the simulation. Nothing in src/core, world,
// simulation, localization, planning, robot or gameplay may include raylib:
// those modules are the headless robotics problem and must build without it.

#include <cmath>
#include <cstdint>

namespace wobo {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 2.0f * kPi;
inline constexpr float kDegToRad = kPi / 180.0f;
inline constexpr float kRadToDeg = 180.0f / kPi;

// Wrap an angle into (-pi, pi].
inline float wrap_angle(float a) {
    a = std::fmod(a + kPi, kTwoPi);
    if (a <= 0.0f) a += kTwoPi;
    return a - kPi;
}

// Signed smallest difference a - b, in (-pi, pi].
inline float angle_diff(float a, float b) { return wrap_angle(a - b); }

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

struct Vec2 {
    float x = 0.0f;
    float z = 0.0f;

    Vec2 operator+(Vec2 o) const { return {x + o.x, z + o.z}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, z - o.z}; }
    Vec2 operator*(float s) const { return {x * s, z * s}; }
    float length() const { return std::sqrt(x * x + z * z); }
};

inline float distance(Vec2 a, Vec2 b) { return (a - b).length(); }

// Planar pose. Heading 0 points along +x; positive heading turns from +x
// towards +z. Everything (simulation, localizer, planner, renderer) uses this
// one convention.
struct Pose2D {
    float x = 0.0f;
    float z = 0.0f;
    float heading_rad = 0.0f;

    Vec2 position() const { return {x, z}; }
    Vec2 forward() const { return {std::cos(heading_rad), std::sin(heading_rad)}; }
};

// An incremental motion expressed in the frame of the pose it starts from.
struct RelativeMotion {
    float forward_m = 0.0f;
    float lateral_m = 0.0f;   // positive towards the robot's left (+heading side)
    float delta_heading_rad = 0.0f;
};

// Apply a robot-frame motion to a pose.
inline Pose2D compose(const Pose2D& p, const RelativeMotion& m) {
    const float c = std::cos(p.heading_rad);
    const float s = std::sin(p.heading_rad);
    return {p.x + c * m.forward_m - s * m.lateral_m,
            p.z + s * m.forward_m + c * m.lateral_m,
            wrap_angle(p.heading_rad + m.delta_heading_rad)};
}

// The robot-frame motion that takes `from` to `to`.
inline RelativeMotion between(const Pose2D& from, const Pose2D& to) {
    const float dx = to.x - from.x;
    const float dz = to.z - from.z;
    const float c = std::cos(from.heading_rad);
    const float s = std::sin(from.heading_rad);
    return {c * dx + s * dz, -s * dx + c * dz,
            angle_diff(to.heading_rad, from.heading_rad)};
}

// Chain two robot-frame motions: first `a`, then `b` from where `a` ended.
inline RelativeMotion chain(const RelativeMotion& a, const RelativeMotion& b) {
    const Pose2D end = compose(compose(Pose2D{}, a), b);
    return {end.x, end.z, end.heading_rad};
}

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    bool operator==(const Rgb&) const = default;
};

// Normalised colour distance in [0, 1] (Euclidean over 0..1 channels, /sqrt3).
inline float colour_distance(Rgb a, Rgb b) {
    const float dr = (static_cast<float>(a.r) - b.r) / 255.0f;
    const float dg = (static_cast<float>(a.g) - b.g) / 255.0f;
    const float db = (static_cast<float>(a.b) - b.b) / 255.0f;
    return std::sqrt(dr * dr + dg * dg + db * db) * 0.57735027f;
}

}  // namespace wobo
