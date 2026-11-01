#include "rendering/iso_renderer.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "raymath.h"
#include "rendering/primitives.h"

namespace wobo {

namespace {

struct MeshBuilder {
    std::vector<float> verts;
    std::vector<float> normals;
    std::vector<unsigned char> colors;

    void vertex(Vector3 p, Vector3 n, Color c) {
        verts.insert(verts.end(), {p.x, p.y, p.z});
        normals.insert(normals.end(), {n.x, n.y, n.z});
        colors.insert(colors.end(), {c.r, c.g, c.b, c.a});
    }
    // p0..p3 counter-clockwise seen from the side the normal points to.
    void quad(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, Vector3 n, Color c) {
        vertex(p0, n, c); vertex(p1, n, c); vertex(p2, n, c);
        vertex(p0, n, c); vertex(p2, n, c); vertex(p3, n, c);
    }
    // Axis-aligned rectangle on a horizontal plane.
    void flat_rect(float x0, float z0, float x1, float z1, float y, Color c) {
        quad({x0, y, z0}, {x0, y, z1}, {x1, y, z1}, {x1, y, z0}, {0, 1, 0}, c);
    }

    Mesh upload() {
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
};

float height_at(const Map2D& map, int cx, int cz) {
    return map.solid(cx, cz) ? map.material_at(cx, cz).height : 0.0f;
}

// Is the floor point (x, z) shadowed by a block, for light along kLightDir?
bool in_shadow(const Map2D& map, float x, float z) {
    const float lxz = std::sqrt(kLightDir.x * kLightDir.x + kLightDir.z * kLightDir.z);
    const float dx = kLightDir.x / lxz;
    const float dz = kLightDir.z / lxz;
    const float rise = kLightDir.y / lxz;  // ray height per metre travelled
    for (float t = 0.02f; t < 2.2f; t += 0.04f) {
        const float px = x + dx * t;
        const float pz = z + dz * t;
        const int cx = map.cell_x(px);
        const int cz = map.cell_z(pz);
        if (!map.in_bounds(cx, cz)) return false;
        if (map.solid(cx, cz) && map.material_at(cx, cz).height > t * rise) return true;
    }
    return false;
}

void add_pattern(MeshBuilder& b, Pattern pattern, float x0, float z0, float cs, float y, Color c) {
    const float u = cs / 8.0f;
    switch (pattern) {
        case Pattern::None: break;
        case Pattern::Stripes:
            b.flat_rect(x0 + 2 * u, z0 + 1.5f * u, x0 + 6 * u, z0 + 2.5f * u, y, c);
            b.flat_rect(x0 + 2 * u, z0 + 3.5f * u, x0 + 6 * u, z0 + 4.5f * u, y, c);
            b.flat_rect(x0 + 2 * u, z0 + 5.5f * u, x0 + 6 * u, z0 + 6.5f * u, y, c);
            break;
        case Pattern::Dots:
            b.flat_rect(x0 + 3 * u, z0 + 3 * u, x0 + 5 * u, z0 + 5 * u, y, c);
            break;
        case Pattern::Checker:
            b.flat_rect(x0 + 1.5f * u, z0 + 1.5f * u, x0 + 4 * u, z0 + 4 * u, y, c);
            b.flat_rect(x0 + 4 * u, z0 + 4 * u, x0 + 6.5f * u, z0 + 6.5f * u, y, c);
            break;
        case Pattern::Chevron:
            b.flat_rect(x0 + 2 * u, z0 + 2 * u, x0 + 3 * u, z0 + 6 * u, y, c);
            b.flat_rect(x0 + 3 * u, z0 + 5 * u, x0 + 6 * u, z0 + 6 * u, y, c);
            break;
        case Pattern::Grid:
            b.flat_rect(x0 + 3.5f * u, z0 + 1.5f * u, x0 + 4.5f * u, z0 + 6.5f * u, y, c);
            b.flat_rect(x0 + 1.5f * u, z0 + 3.5f * u, x0 + 6.5f * u, z0 + 4.5f * u, y, c);
            break;
    }
}

}  // namespace

void IsoRenderer::build(const Map2D& map, const ToonShader& shader) {
    unload();
    MeshBuilder lit;
    MeshBuilder marks;
    const float cs = map.cell_size();
    const Color outline{14, 16, 30, 255};
    const float band = 0.035f;

    // Plinth: the board the map sits on.
    {
        const float pad = 0.12f;
        const float x0 = -pad, z0 = -pad, x1 = map.world_width() + pad, z1 = map.world_depth() + pad;
        const float y0 = -0.35f, y1 = -0.001f;
        const Color side{30, 35, 60, 255};
        lit.flat_rect(x0, z0, x1, z1, y1, Color{43, 49, 80, 255});
        lit.quad({x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, {1, 0, 0}, side);
        lit.quad({x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {x0, y0, z1}, {0, 0, 1}, side);
        lit.quad({x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, {x0, y0, z0}, {-1, 0, 0}, side);
        lit.quad({x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}, {0, 0, -1}, side);
        // Outline along the plinth's visible bottom edges.
        marks.quad({x1, y0, z0}, {x1, y0 + 0.03f, z0}, {x1, y0 + 0.03f, z1}, {x1, y0, z1}, {1, 0, 0}, outline);
        marks.quad({x1, y0, z1}, {x1, y0 + 0.03f, z1}, {x0, y0 + 0.03f, z1}, {x0, y0, z1}, {0, 0, 1}, outline);
    }

    // Floor, subdivided so the baked shadows have crisp-ish edges.
    constexpr int kSub = 4;
    const float sub = cs / kSub;
    const Color shadow_tint{196, 200, 232, 255};
    for (int cz = 0; cz < map.height(); ++cz) {
        for (int cx = 0; cx < map.width(); ++cx) {
            if (map.solid(cx, cz)) continue;
            Color base = prim::to_color(map.material_at(cx, cz).rgb);
            if ((cx + cz) % 2 == 1) base = prim::shade(base, 0.965f);
            for (int sz = 0; sz < kSub; ++sz) {
                for (int sx = 0; sx < kSub; ++sx) {
                    const float x0 = cx * cs + sx * sub;
                    const float z0 = cz * cs + sz * sub;
                    Color c = base;
                    if (in_shadow(map, x0 + sub * 0.5f, z0 + sub * 0.5f)) {
                        c = Color{static_cast<unsigned char>(base.r * shadow_tint.r / 255 * 0.86f),
                                  static_cast<unsigned char>(base.g * shadow_tint.g / 255 * 0.86f),
                                  static_cast<unsigned char>(base.b * shadow_tint.b / 255 * 0.86f), 255};
                    }
                    lit.flat_rect(x0, z0, x0 + sub, z0 + sub, 0.0f, c);
                }
            }
        }
    }

    // Blocks.
    for (int cz = 0; cz < map.height(); ++cz) {
        for (int cx = 0; cx < map.width(); ++cx) {
            if (!map.solid(cx, cz)) continue;
            const Material& m = map.material_at(cx, cz);
            const float h = m.height;
            const float x0 = cx * cs, x1 = x0 + cs, z0 = cz * cs, z1 = z0 + cs;
            const Color col = prim::to_color(m.rgb);

            lit.flat_rect(x0, z0, x1, z1, h, prim::shade(col, 1.06f));
            add_pattern(marks, m.pattern, x0, z0, cs, h + 0.003f, prim::shade(col, 0.72f));

            // Exposed sides: from the neighbour's top (or the floor) to ours.
            const float hxp = height_at(map, cx + 1, cz);
            const float hxn = height_at(map, cx - 1, cz);
            const float hzp = height_at(map, cx, cz + 1);
            const float hzn = height_at(map, cx, cz - 1);
            if (hxp < h) lit.quad({x1, hxp, z0}, {x1, h, z0}, {x1, h, z1}, {x1, hxp, z1}, {1, 0, 0}, col);
            if (hxn < h) lit.quad({x0, hxn, z1}, {x0, h, z1}, {x0, h, z0}, {x0, hxn, z0}, {-1, 0, 0}, col);
            if (hzp < h) lit.quad({x1, hzp, z1}, {x1, h, z1}, {x0, h, z1}, {x0, hzp, z1}, {0, 0, 1}, col);
            if (hzn < h) lit.quad({x0, hzn, z0}, {x0, h, z0}, {x1, h, z0}, {x1, hzn, z0}, {0, 0, -1}, col);

            // Drawn outlines: a band round the top wherever a side shows, a
            // line where the visible faces meet the floor, and strips down the
            // convex vertical corners.
            const float y = h + 0.002f;
            if (hxp < h) marks.flat_rect(x1 - band, z0, x1, z1, y, outline);
            if (hxn < h) marks.flat_rect(x0, z0, x0 + band, z1, y, outline);
            if (hzp < h) marks.flat_rect(x0, z1 - band, x1, z1, y, outline);
            if (hzn < h) marks.flat_rect(x0, z0, x1, z0 + band, y, outline);
            const float e = 0.002f;
            if (hxp <= 0.0f) {
                marks.quad({x1 + e, 0, z0}, {x1 + e, band, z0}, {x1 + e, band, z1}, {x1 + e, 0, z1}, {1, 0, 0}, outline);
            }
            if (hzp <= 0.0f) {
                marks.quad({x1, 0, z1 + e}, {x1, band, z1 + e}, {x0, band, z1 + e}, {x0, 0, z1 + e}, {0, 0, 1}, outline);
            }
            // Corner between the +x and +z faces (facing the camera).
            if (hxp < h && hzp < h && !map.solid(cx + 1, cz + 1)) {
                const float lo = std::max(hxp, hzp);
                marks.quad({x1 + e, lo, z1 - band}, {x1 + e, h, z1 - band}, {x1 + e, h, z1}, {x1 + e, lo, z1}, {1, 0, 0}, outline);
                marks.quad({x1, lo, z1 + e}, {x1, h, z1 + e}, {x1 - band, h, z1 + e}, {x1 - band, lo, z1 + e}, {0, 0, 1}, outline);
            }
            // Silhouette corners at the far ends of the visible faces.
            if (hxp < h && hzn < h) {
                const float lo = std::max(hxp, hzn);
                marks.quad({x1 + e, lo, z0}, {x1 + e, h, z0}, {x1 + e, h, z0 + band}, {x1 + e, lo, z0 + band}, {1, 0, 0}, outline);
            }
            if (hzp < h && hxn < h) {
                const float lo = std::max(hzp, hxn);
                marks.quad({x0 + band, lo, z1 + e}, {x0 + band, h, z1 + e}, {x0, h, z1 + e}, {x0, lo, z1 + e}, {0, 0, 1}, outline);
            }
        }
    }

    lit_ = lit.upload();
    marks_ = marks.upload();
    material_ = LoadMaterialDefault();
    if (shader.valid()) material_.shader = shader.shader();
    built_ = true;
}

void IsoRenderer::unload() {
    if (!built_) return;
    UnloadMesh(lit_);
    UnloadMesh(marks_);
    // The material's shader belongs to ToonShader; detach it before raylib
    // would unload it along with the material.
    material_.shader = Shader{};
    RL_FREE(material_.maps);
    material_ = ::Material{};
    lit_ = Mesh{};
    marks_ = Mesh{};
    built_ = false;
}

void IsoRenderer::draw(const ToonShader& shader) const {
    if (!built_) return;
    // DrawMesh binds the material's shader itself; set the uniforms on it.
    shader.set_flat(false);
    if (lit_.vertexCount > 0) DrawMesh(lit_, material_, MatrixIdentity());
    shader.set_flat(true);
    if (marks_.vertexCount > 0) DrawMesh(marks_, material_, MatrixIdentity());
    shader.set_flat(false);
}

}  // namespace wobo
