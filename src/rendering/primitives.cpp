#include "rendering/primitives.h"

#include <cmath>

#include "raymath.h"
#include "rlgl.h"

namespace wobo::prim {

Color shade(Color c, float k) {
    const auto ch = [k](unsigned char v) {
        return static_cast<unsigned char>(clampf(static_cast<float>(v) * k, 0.0f, 255.0f));
    };
    return Color{ch(c.r), ch(c.g), ch(c.b), c.a};
}

Color mix(Color a, Color b, float t) {
    const auto ch = [t](unsigned char x, unsigned char y) {
        return static_cast<unsigned char>(lerpf(static_cast<float>(x), static_cast<float>(y), t));
    };
    return Color{ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), ch(a.a, b.a)};
}

void box(Vector3 c, Vector3 size, Color col) { DrawCube(c, size.x, size.y, size.z, col); }

namespace {

void vertex(Vector3 p) { rlVertex3f(p.x, p.y, p.z); }
void normal(Vector3 n) { rlNormal3f(n.x, n.y, n.z); }

}  // namespace

void cylinder(Vector3 a, Vector3 b, float radius, Color col, int sides, bool caps) {
    const Vector3 axis = Vector3Subtract(b, a);
    const float len = Vector3Length(axis);
    if (len < 1e-6f) return;
    const Vector3 u = Vector3Scale(axis, 1.0f / len);
    // Basis p, q with p x q = u.
    const Vector3 helper = std::fabs(u.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
    const Vector3 p = Vector3Normalize(Vector3CrossProduct(helper, u));
    const Vector3 q = Vector3CrossProduct(u, p);

    rlCheckRenderBatchLimit(sides * (caps ? 12 : 6));
    rlBegin(RL_TRIANGLES);
    rlColor4ub(col.r, col.g, col.b, col.a);
    for (int i = 0; i < sides; ++i) {
        const float t0 = kTwoPi * static_cast<float>(i) / sides;
        const float t1 = kTwoPi * static_cast<float>(i + 1) / sides;
        const Vector3 d0 = Vector3Add(Vector3Scale(p, std::cos(t0)), Vector3Scale(q, std::sin(t0)));
        const Vector3 d1 = Vector3Add(Vector3Scale(p, std::cos(t1)), Vector3Scale(q, std::sin(t1)));
        const Vector3 a0 = Vector3Add(a, Vector3Scale(d0, radius));
        const Vector3 a1 = Vector3Add(a, Vector3Scale(d1, radius));
        const Vector3 b0 = Vector3Add(b, Vector3Scale(d0, radius));
        const Vector3 b1 = Vector3Add(b, Vector3Scale(d1, radius));

        normal(d0); vertex(a0);
        normal(d1); vertex(a1);
        normal(d1); vertex(b1);
        normal(d0); vertex(a0);
        normal(d1); vertex(b1);
        normal(d0); vertex(b0);

        if (caps) {
            normal(u);
            vertex(b); vertex(b0); vertex(b1);
            normal(Vector3Negate(u));
            vertex(a); vertex(a1); vertex(a0);
        }
    }
    rlEnd();
}

void sphere(Vector3 c, float radius, Color col, int rings, int slices) {
    DrawSphereEx(c, radius, rings, slices, col);
}

void disc(Vector3 c, float radius, Color col, int segments) {
    rlCheckRenderBatchLimit(segments * 3);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(col.r, col.g, col.b, col.a);
    rlNormal3f(0, 1, 0);
    for (int i = 0; i < segments; ++i) {
        const float t0 = kTwoPi * static_cast<float>(i) / segments;
        const float t1 = kTwoPi * static_cast<float>(i + 1) / segments;
        // Seen from above (+y), increasing angle in x-z runs clockwise, so
        // emit centre, t1, t0 to wind counter-clockwise.
        rlVertex3f(c.x, c.y, c.z);
        rlVertex3f(c.x + std::cos(t1) * radius, c.y, c.z + std::sin(t1) * radius);
        rlVertex3f(c.x + std::cos(t0) * radius, c.y, c.z + std::sin(t0) * radius);
    }
    rlEnd();
}

void arc_ring(Vector3 c, float r0, float r1, float a0, float a1, Color col, int segments) {
    rlCheckRenderBatchLimit(segments * 6);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(col.r, col.g, col.b, col.a);
    rlNormal3f(0, 1, 0);
    for (int i = 0; i < segments; ++i) {
        const float t0 = lerpf(a0, a1, static_cast<float>(i) / segments);
        const float t1 = lerpf(a0, a1, static_cast<float>(i + 1) / segments);
        const Vector3 i0{c.x + std::cos(t0) * r0, c.y, c.z + std::sin(t0) * r0};
        const Vector3 i1{c.x + std::cos(t1) * r0, c.y, c.z + std::sin(t1) * r0};
        const Vector3 o0{c.x + std::cos(t0) * r1, c.y, c.z + std::sin(t0) * r1};
        const Vector3 o1{c.x + std::cos(t1) * r1, c.y, c.z + std::sin(t1) * r1};
        vertex(i0); vertex(o1); vertex(o0);
        vertex(i0); vertex(i1); vertex(o1);
    }
    rlEnd();
}

void ring(Vector3 c, float r_inner, float r_outer, Color col, int segments) {
    arc_ring(c, r_inner, r_outer, 0.0f, kTwoPi, col, segments);
}

void quad_up(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col) {
    rlCheckRenderBatchLimit(6);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(col.r, col.g, col.b, col.a);
    rlNormal3f(0, 1, 0);
    vertex(a); vertex(b); vertex(c);
    vertex(a); vertex(c); vertex(d);
    rlEnd();
}

void arrow(Vector3 c, float heading, float length, float width, Color col) {
    const float fx = std::cos(heading), fz = std::sin(heading);
    const float lx = -fz, lz = fx;
    const Vector3 tip{c.x + fx * length * 0.6f, c.y, c.z + fz * length * 0.6f};
    const Vector3 left{c.x - fx * length * 0.4f + lx * width * 0.5f, c.y, c.z - fz * length * 0.4f + lz * width * 0.5f};
    const Vector3 right{c.x - fx * length * 0.4f - lx * width * 0.5f, c.y, c.z - fz * length * 0.4f - lz * width * 0.5f};
    rlCheckRenderBatchLimit(3);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(col.r, col.g, col.b, col.a);
    rlNormal3f(0, 1, 0);
    // left is at +90 deg (towards +z when heading is +x): tip, right, left
    // winds counter-clockwise seen from above.
    vertex(tip); vertex(right); vertex(left);
    rlEnd();
}

void dashed_ring(Vector3 c, float radius, float width, int dashes, float phase, Color col) {
    const float step = kTwoPi / static_cast<float>(dashes);
    for (int i = 0; i < dashes; ++i) {
        const float a0 = phase + step * static_cast<float>(i);
        arc_ring(c, radius - width * 0.5f, radius + width * 0.5f, a0, a0 + step * 0.55f, col, 3);
    }
}

bool ground_point(Vector2 screen, const Camera3D& cam, Vec2* out) {
    const Ray ray = GetScreenToWorldRay(screen, cam);
    if (std::fabs(ray.direction.y) < 1e-6f) return false;
    const float t = -ray.position.y / ray.direction.y;
    if (t < 0.0f) return false;
    out->x = ray.position.x + ray.direction.x * t;
    out->z = ray.position.z + ray.direction.z * t;
    return true;
}

}  // namespace wobo::prim
