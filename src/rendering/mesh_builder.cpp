#include "rendering/mesh_builder.h"

#include <cmath>
#include <cstring>

#include "core/types.h"
#include "raymath.h"

namespace wobo {

namespace {

// Turn (x, z) by yaw (the Pose2D way: +x towards +z).
Vector3 turn(Vector3 v, float yaw) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    return {c * v.x - s * v.z, v.y, s * v.x + c * v.z};
}

}  // namespace

void MeshBuilder::vertex(Vector3 p, Vector3 n, Color c) {
    verts.insert(verts.end(), {p.x, p.y, p.z});
    normals.insert(normals.end(), {n.x, n.y, n.z});
    colors.insert(colors.end(), {c.r, c.g, c.b, c.a});
}

void MeshBuilder::tri(Vector3 a, Vector3 b, Vector3 c, Vector3 n, Color col) {
    vertex(a, n, col); vertex(b, n, col); vertex(c, n, col);
}

void MeshBuilder::quad(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, Vector3 n, Color c) {
    vertex(p0, n, c); vertex(p1, n, c); vertex(p2, n, c);
    vertex(p0, n, c); vertex(p2, n, c); vertex(p3, n, c);
}

void MeshBuilder::flat_rect(float x0, float z0, float x1, float z1, float y, Color c) {
    quad({x0, y, z0}, {x0, y, z1}, {x1, y, z1}, {x1, y, z0}, {0, 1, 0}, c);
}

void MeshBuilder::box(Vector3 c, Vector3 size, float yaw, Color col) {
    const float hx = 0.5f * size.x, hy = 0.5f * size.y, hz = 0.5f * size.z;
    const auto P = [&](float x, float y, float z) { return Vector3Add(c, turn({x, y, z}, yaw)); };
    const auto N = [&](float x, float y, float z) { return turn({x, y, z}, yaw); };
    // Top and bottom.
    quad(P(-hx, hy, -hz), P(-hx, hy, hz), P(hx, hy, hz), P(hx, hy, -hz), N(0, 1, 0), col);
    quad(P(-hx, -hy, -hz), P(hx, -hy, -hz), P(hx, -hy, hz), P(-hx, -hy, hz), N(0, -1, 0), col);
    // Sides.
    quad(P(hx, -hy, -hz), P(hx, hy, -hz), P(hx, hy, hz), P(hx, -hy, hz), N(1, 0, 0), col);
    quad(P(-hx, -hy, hz), P(-hx, hy, hz), P(-hx, hy, -hz), P(-hx, -hy, -hz), N(-1, 0, 0), col);
    quad(P(hx, -hy, hz), P(hx, hy, hz), P(-hx, hy, hz), P(-hx, -hy, hz), N(0, 0, 1), col);
    quad(P(-hx, -hy, -hz), P(-hx, hy, -hz), P(hx, hy, -hz), P(hx, -hy, -hz), N(0, 0, -1), col);
}

void MeshBuilder::cylinder(Vector3 a, Vector3 b, float radius, Color col, int sides, bool caps) {
    const Vector3 axis = Vector3Subtract(b, a);
    const float len = Vector3Length(axis);
    if (len < 1e-6f) return;
    const Vector3 u = Vector3Scale(axis, 1.0f / len);
    const Vector3 helper = std::fabs(u.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
    const Vector3 p = Vector3Normalize(Vector3CrossProduct(helper, u));
    const Vector3 q = Vector3CrossProduct(u, p);
    for (int i = 0; i < sides; ++i) {
        const float t0 = kTwoPi * static_cast<float>(i) / sides;
        const float t1 = kTwoPi * static_cast<float>(i + 1) / sides;
        const Vector3 d0 = Vector3Add(Vector3Scale(p, std::cos(t0)), Vector3Scale(q, std::sin(t0)));
        const Vector3 d1 = Vector3Add(Vector3Scale(p, std::cos(t1)), Vector3Scale(q, std::sin(t1)));
        const Vector3 a0 = Vector3Add(a, Vector3Scale(d0, radius));
        const Vector3 a1 = Vector3Add(a, Vector3Scale(d1, radius));
        const Vector3 b0 = Vector3Add(b, Vector3Scale(d0, radius));
        const Vector3 b1 = Vector3Add(b, Vector3Scale(d1, radius));
        vertex(a0, d0, col); vertex(a1, d1, col); vertex(b1, d1, col);
        vertex(a0, d0, col); vertex(b1, d1, col); vertex(b0, d0, col);
        if (caps) {
            tri(b, b0, b1, u, col);
            tri(a, a1, a0, Vector3Negate(u), col);
        }
    }
}

void MeshBuilder::sphere(Vector3 c, Vector3 radii, Color col, int rings, int slices) {
    const auto point = [&](int ring, int slice, Vector3* n) {
        const float th = kPi * static_cast<float>(ring) / rings - 0.5f * kPi;
        const float ph = kTwoPi * static_cast<float>(slice) / slices;
        const Vector3 d{std::cos(th) * std::cos(ph), std::sin(th), std::cos(th) * std::sin(ph)};
        *n = Vector3Normalize({d.x / radii.x, d.y / radii.y, d.z / radii.z});
        return Vector3{c.x + d.x * radii.x, c.y + d.y * radii.y, c.z + d.z * radii.z};
    };
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < slices; ++s) {
            Vector3 n00, n01, n10, n11;
            const Vector3 p00 = point(r, s, &n00), p01 = point(r, s + 1, &n01);
            const Vector3 p10 = point(r + 1, s, &n10), p11 = point(r + 1, s + 1, &n11);
            vertex(p00, n00, col); vertex(p10, n10, col); vertex(p11, n11, col);
            vertex(p00, n00, col); vertex(p11, n11, col); vertex(p01, n01, col);
        }
    }
}

void MeshBuilder::roof(Vector3 c, float w, float d, float h, float yaw, Color col) {
    const float hx = 0.5f * w, hz = 0.5f * d;
    const auto P = [&](float x, float y, float z) { return Vector3Add(c, turn({x, y, z}, yaw)); };
    const float slope = std::sqrt(h * h + hz * hz);
    const Vector3 n_pos = turn({0, hz / slope, h / slope}, yaw);
    const Vector3 n_neg = turn({0, hz / slope, -h / slope}, yaw);
    quad(P(-hx, 0, hz), P(hx, 0, hz), P(hx, h, 0), P(-hx, h, 0), n_pos, col);
    quad(P(hx, 0, -hz), P(-hx, 0, -hz), P(-hx, h, 0), P(hx, h, 0), n_neg, col);
    tri(P(hx, 0, -hz), P(hx, h, 0), P(hx, 0, hz), turn({1, 0, 0}, yaw), col);
    tri(P(-hx, 0, hz), P(-hx, h, 0), P(-hx, 0, -hz), turn({-1, 0, 0}, yaw), col);
    quad(P(-hx, 0, -hz), P(hx, 0, -hz), P(hx, 0, hz), P(-hx, 0, hz), {0, -1, 0}, col);
}

void MeshBuilder::pyramid(Vector3 c, float w, float h, float yaw, Color col) {
    const float k = 0.5f * w;
    const auto P = [&](float x, float y, float z) { return Vector3Add(c, turn({x, y, z}, yaw)); };
    const Vector3 apex = P(0, h, 0);
    const float s = std::sqrt(h * h + k * k);
    tri(P(k, 0, -k), apex, P(k, 0, k), turn({h / s, k / s, 0}, yaw), col);
    tri(P(-k, 0, k), apex, P(-k, 0, -k), turn({-h / s, k / s, 0}, yaw), col);
    tri(P(k, 0, k), apex, P(-k, 0, k), turn({0, k / s, h / s}, yaw), col);
    tri(P(-k, 0, -k), apex, P(k, 0, -k), turn({0, k / s, -h / s}, yaw), col);
}

Mesh MeshBuilder::upload() {
    Mesh m{};
    m.vertexCount = static_cast<int>(verts.size() / 3);
    m.triangleCount = m.vertexCount / 3;
    if (m.vertexCount == 0) return m;
    m.vertices = static_cast<float*>(MemAlloc(static_cast<unsigned int>(verts.size() * sizeof(float))));
    m.normals = static_cast<float*>(MemAlloc(static_cast<unsigned int>(normals.size() * sizeof(float))));
    m.colors = static_cast<unsigned char*>(MemAlloc(static_cast<unsigned int>(colors.size())));
    std::memcpy(m.vertices, verts.data(), verts.size() * sizeof(float));
    std::memcpy(m.normals, normals.data(), normals.size() * sizeof(float));
    std::memcpy(m.colors, colors.data(), colors.size());
    UploadMesh(&m, false);
    return m;
}

}  // namespace wobo
