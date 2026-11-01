#pragma once

// Immediate-mode 3D primitives that set proper normals (raylib's cylinders
// and capsules do not, which the toon shader needs), plus flat floor shapes
// for overlays. All draw through rlgl, so they respect rlPushMatrix
// transforms. Front faces wind counter-clockwise.

#include "raylib.h"

#include "core/types.h"

namespace wobo::prim {

inline Vector3 v3(float x, float y, float z) { return Vector3{x, y, z}; }
inline Vector3 on_floor(Vec2 p, float y = 0.0f) { return Vector3{p.x, y, p.z}; }
inline Color with_alpha(Color c, float a) {
    return Color{c.r, c.g, c.b, static_cast<unsigned char>(clampf(a, 0.0f, 1.0f) * 255.0f)};
}
inline Color to_color(Rgb c, unsigned char a = 255) { return Color{c.r, c.g, c.b, a}; }
Color shade(Color c, float k);  // multiply RGB, keep alpha
Color mix(Color a, Color b, float t);

// Axis-aligned box centred at `c` (raylib's DrawCube, which has normals).
void box(Vector3 c, Vector3 size, Color col);
// Cylinder between two points, with caps.
void cylinder(Vector3 a, Vector3 b, float radius, Color col, int sides = 14, bool caps = true);
void sphere(Vector3 c, float radius, Color col, int rings = 8, int slices = 12);

// Flat shapes on a horizontal plane at height y (normal up).
void disc(Vector3 c, float radius, Color col, int segments = 24);
void ring(Vector3 c, float r_inner, float r_outer, Color col, int segments = 32);
void arc_ring(Vector3 c, float r_inner, float r_outer, float a0, float a1, Color col, int segments = 12);
void quad_up(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col);  // a-b-c-d CCW seen from above
void arrow(Vector3 c, float heading, float length, float width, Color col);
// Dashed circle of `dashes` segments.
void dashed_ring(Vector3 c, float radius, float width, int dashes, float phase, Color col);

// The point on the ground plane (y = 0) under a screen position, if any.
bool ground_point(Vector2 screen, const Camera3D& cam, Vec2* out);

}  // namespace wobo::prim
