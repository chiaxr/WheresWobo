#include "rendering/iso_renderer.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "raymath.h"
#include "rendering/mesh_builder.h"
#include "rendering/primitives.h"
#include "rendering/prop_models.h"
#include "rlgl.h"

namespace wobo {

namespace {

bool is_water(const Map2D& map, int cx, int cz) {
    return map.solid(cx, cz) && map.material_at(cx, cz).look == "water";
}

// Water lies flat, at floor level.
float height_at(const Map2D& map, int cx, int cz) {
    return map.solid(cx, cz) && !is_water(map, cx, cz) ? map.material_at(cx, cz).height : 0.0f;
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
        if (height_at(map, cx, cz) > t * rise) return true;
        for (int s : map.shapes_in(map.index(cx, cz))) {
            const Shape& sh = map.shapes()[static_cast<std::size_t>(s)];
            if (sh.height > t * rise && sh.distance({px, pz}) < 0.0f) return true;
        }
    }
    return false;
}

// Bookshelves (look = books): boards across an exposed side, and spines of
// every height and colour between them. `base` is the side's bottom corner
// (at height lo), `along` runs the cell's width across it, `n` faces out.
void add_books(MeshBuilder& b, Vector3 base, Vector3 along, Vector3 n, float lo, float h, Color shelf, int seed) {
    constexpr Color kSpines[] = {{150, 52, 46, 255},  {62, 92, 140, 255},  {70, 116, 74, 255},  {196, 170, 112, 255},
                                 {112, 70, 110, 255}, {178, 110, 54, 255}, {46, 60, 84, 255},   {222, 210, 180, 255}};
    const float e = 0.003f;
    const float width = Vector3Length(along);
    const Vector3 t = Vector3Scale(along, 1.0f / width);
    const auto at = [&](float u, float y) {
        return Vector3Add(Vector3Add(base, Vector3Scale(t, u)), Vector3{n.x * e, y - base.y, n.z * e});
    };
    const auto rect = [&](float u0, float u1, float y0, float y1, Color c) {
        b.quad(at(u0, y0), at(u0, y1), at(u1, y1), at(u1, y0), n, c);
    };
    std::uint32_t r = static_cast<std::uint32_t>(seed) * 2654435761u + 0x9e3779b9u;
    const auto next = [&]() {
        r ^= r << 13;
        r ^= r >> 17;
        r ^= r << 5;
        return static_cast<float>(r & 0xffff) / 65535.0f;
    };
    const Color board = prim::shade(shelf, 0.62f);
    constexpr float kBoard = 0.025f;
    const float rows = std::max(1.0f, std::round((h - 0.06f) / 0.21f));
    const float pitch = (h - 0.06f) / rows;
    for (int row = 0; row < static_cast<int>(rows); ++row) {
        const float y0 = 0.03f + pitch * static_cast<float>(row);
        if (y0 + pitch <= lo) continue;
        rect(0.0f, width, std::max(lo, y0), std::max(lo, y0 + kBoard), board);
        float u = 0.02f + 0.03f * next();
        while (u < width - 0.03f) {
            const float w = std::min(0.035f + 0.04f * next(), width - 0.02f - u);
            const float top = y0 + kBoard + (pitch - kBoard - 0.02f) * (0.68f + 0.3f * next());
            Color c = kSpines[static_cast<int>(next() * 7.99f)];
            c = prim::mix(c, shelf, 0.3f);
            if (top > lo) rect(u, u + w, std::max(lo, y0 + kBoard), top, c);
            u += w + (next() < 0.15f ? 0.03f : 0.004f);
        }
    }
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

    // Water: flat, with ripples and a stone lip where it meets the land.
    for (int cz = 0; cz < map.height(); ++cz) {
        for (int cx = 0; cx < map.width(); ++cx) {
            if (!is_water(map, cx, cz)) continue;
            const Color col = prim::to_color(map.material_at(cx, cz).rgb);
            const float x0 = cx * cs, x1 = x0 + cs, z0 = cz * cs, z1 = z0 + cs;
            lit.flat_rect(x0, z0, x1, z1, 0.0f, col);
            const float k = 0.5f + 0.5f * std::sin(static_cast<float>(cx * 7 + cz * 13));
            marks.flat_rect(x0 + cs * (0.15f + 0.3f * k), z0 + cs * 0.3f, x0 + cs * (0.45f + 0.3f * k), z0 + cs * 0.36f,
                            0.003f, prim::shade(col, 1.25f));
            const Color lip{176, 170, 160, 255};
            const float t = 0.05f;
            if (!map.solid(cx + 1, cz)) marks.flat_rect(x1 - t, z0, x1, z1, 0.004f, lip);
            if (!map.solid(cx - 1, cz)) marks.flat_rect(x0, z0, x0 + t, z1, 0.004f, lip);
            if (!map.solid(cx, cz + 1)) marks.flat_rect(x0, z1 - t, x1, z1, 0.004f, lip);
            if (!map.solid(cx, cz - 1)) marks.flat_rect(x0, z0, x1, z0 + t, 0.004f, lip);
        }
    }

    // Blocks.
    for (int cz = 0; cz < map.height(); ++cz) {
        for (int cx = 0; cx < map.width(); ++cx) {
            if (!map.solid(cx, cz) || is_water(map, cx, cz)) continue;
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
            if (m.look == "books") {
                const int seed = cx * 7919 + cz * 104729;
                if (hxp < h) add_books(marks, {x1, hxp, z0}, {0, 0, cs}, {1, 0, 0}, hxp, h, col, seed);
                if (hxn < h) add_books(marks, {x0, hxn, z1}, {0, 0, -cs}, {-1, 0, 0}, hxn, h, col, seed + 1);
                if (hzp < h) add_books(marks, {x1, hzp, z1}, {-cs, 0, 0}, {0, 0, 1}, hzp, h, col, seed + 2);
                if (hzn < h) add_books(marks, {x0, hzn, z0}, {cs, 0, 0}, {0, 0, -1}, hzn, h, col, seed + 3);
            }

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
            if (hxp < h && hzp < h && height_at(map, cx + 1, cz + 1) <= 0.0f) {
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

    MeshBuilder hull;
    build_props(map, lit, hull, marks);

    lit_ = lit.upload();
    marks_ = marks.upload();
    hull_ = hull.upload();
    decals_.build(map);
    material_ = LoadMaterialDefault();
    if (shader.valid()) material_.shader = shader.shader();
    built_ = true;
}

void IsoRenderer::unload() {
    if (!built_) return;
    UnloadMesh(lit_);
    UnloadMesh(marks_);
    if (hull_.vertexCount > 0) UnloadMesh(hull_);
    decals_.unload();
    // The material's shader belongs to ToonShader; detach it before raylib
    // would unload it along with the material.
    material_.shader = Shader{};
    RL_FREE(material_.maps);
    material_ = ::Material{};
    lit_ = Mesh{};
    marks_ = Mesh{};
    hull_ = Mesh{};
    built_ = false;
}

void IsoRenderer::draw(const ToonShader& shader) const {
    if (!built_) return;
    // DrawMesh binds the material's shader itself; set the uniforms on it.
    shader.set_flat(false);
    if (lit_.vertexCount > 0) DrawMesh(lit_, material_, MatrixIdentity());
    shader.set_flat(true);
    if (marks_.vertexCount > 0) DrawMesh(marks_, material_, MatrixIdentity());
    // Props' outlines: their inflated copies, inside out.
    if (hull_.vertexCount > 0) {
        rlDrawRenderBatchActive();
        rlSetCullFace(RL_CULL_FACE_FRONT);
        DrawMesh(hull_, material_, MatrixIdentity());
        rlDrawRenderBatchActive();
        rlSetCullFace(RL_CULL_FACE_BACK);
    }
    decals_.draw();
    rlDrawRenderBatchActive();
    shader.set_flat(false);
}

}  // namespace wobo
