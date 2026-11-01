#include "rendering/prop_models.h"

#include <cmath>

#include "raymath.h"
#include "rendering/primitives.h"
#include "world/props.h"

namespace wobo {

namespace {

constexpr Color kOutline{14, 16, 30, 255};
constexpr float kHull = 0.012f;

constexpr Color kWhite{240, 240, 236, 255};
constexpr Color kDark{40, 44, 58, 255};
constexpr Color kMetal{150, 158, 170, 255};
constexpr Color kWood{150, 104, 66, 255};
constexpr Color kLeaf{72, 152, 84, 255};
constexpr Color kPot{196, 112, 76, 255};
constexpr Color kGlass{150, 200, 230, 255};
constexpr Color kWarm{255, 214, 120, 255};
constexpr Color kRedLight{240, 70, 60, 255};
constexpr Color kStraw{236, 206, 118, 255};
constexpr Color kPasta{246, 222, 140, 255};
constexpr Color kSauce{200, 54, 40, 255};
constexpr Color kChalkBoard{40, 70, 56, 255};

// One prop's frame: local x along its yaw, z across, y up.
struct Sculptor {
    MeshBuilder& lit;
    MeshBuilder& hull;
    MeshBuilder& marks;
    Vec2 at;
    float yaw;

    Vector3 P(float x, float y, float z) const {
        const float c = std::cos(yaw), s = std::sin(yaw);
        return {at.x + c * x - s * z, y, at.z + s * x + c * z};
    }
    Vector3 dir(float x, float y, float z) const {
        const float c = std::cos(yaw), s = std::sin(yaw);
        return {c * x - s * z, y, s * x + c * z};
    }
    void box(Vector3 c, Vector3 size, Color col, float local_yaw = 0.0f, bool outline = true) {
        lit.box(P(c.x, c.y, c.z), size, yaw + local_yaw, col);
        if (outline) {
            hull.box(P(c.x, c.y, c.z), {size.x + 2 * kHull, size.y + 2 * kHull, size.z + 2 * kHull}, yaw + local_yaw,
                     kOutline);
        }
    }
    void cyl(Vector3 a, Vector3 b, float r, Color col, int sides = 12, bool outline = true) {
        const Vector3 wa = P(a.x, a.y, a.z), wb = P(b.x, b.y, b.z);
        lit.cylinder(wa, wb, r, col, sides);
        if (outline) {
            const Vector3 axis = Vector3Subtract(wb, wa);
            const float len = Vector3Length(axis);
            const Vector3 e = len > 1e-6f ? Vector3Scale(axis, kHull / len) : Vector3{};
            hull.cylinder(Vector3Subtract(wa, e), Vector3Add(wb, e), r + kHull, kOutline, sides);
        }
    }
    void ball(Vector3 c, Vector3 radii, Color col, bool outline = true) {
        lit.sphere(P(c.x, c.y, c.z), radii, col);
        if (outline) hull.sphere(P(c.x, c.y, c.z), {radii.x + kHull, radii.y + kHull, radii.z + kHull}, kOutline);
    }
    void roof(Vector3 c, float w, float d, float h, Color col, float local_yaw = 0.0f) {
        lit.roof(P(c.x, c.y, c.z), w, d, h, yaw + local_yaw, col);
        hull.roof(P(c.x, c.y - kHull, c.z), w + 2 * kHull, d + 2 * kHull, h + 2 * kHull, yaw + local_yaw, kOutline);
    }
    void spire(Vector3 c, float w, float h, Color col) {
        lit.pyramid(P(c.x, c.y, c.z), w, h, yaw, col);
        hull.pyramid(P(c.x, c.y - kHull, c.z), w + 2 * kHull, h + 3 * kHull, yaw, kOutline);
    }
    // A flat rectangle facing local direction n (one of +-x, +-z), centred
    // on c, `w` wide and `h` tall: windows, frames, door slots.
    void plate(Vector3 c, bool along_x, float sign, float w, float h, Color col) {
        const Vector3 n = along_x ? dir(sign, 0, 0) : dir(0, 0, sign);
        const Vector3 right{0.5f * w * n.z, 0, -0.5f * w * n.x};  // the viewer's right, half a width
        const Vector3 up{0, 0.5f * h, 0};
        const Vector3 o = P(c.x, c.y, c.z);
        marks.quad(Vector3Subtract(Vector3Subtract(o, right), up), Vector3Subtract(Vector3Add(o, right), up),
                   Vector3Add(Vector3Add(o, right), up), Vector3Add(Vector3Subtract(o, right), up), n, col);
    }
    void flat(Vector3 c, float w, float d, Color col, float local_yaw = 0.0f) {
        // Horizontal rectangle (on a table top, a deck).
        const float cy = std::cos(local_yaw), sy = std::sin(local_yaw);
        const auto L = [&](float x, float z) { return P(c.x + cy * x - sy * z, c.y, c.z + sy * x + cy * z); };
        const float hx = 0.5f * w, hz = 0.5f * d;
        marks.quad(L(-hx, -hz), L(-hx, hz), L(hx, hz), L(hx, -hz), {0, 1, 0}, col);
    }
};

Color mat_colour(const Map2D& map, const Prop& p) { return prim::to_color(map.material(p.material).rgb); }

Color material_by_id(const Map2D& map, const std::string& id, Color fallback) {
    for (const Material& m : map.materials()) {
        if (m.id == id) return prim::to_color(m.rgb);
    }
    return fallback;
}

void legs(Sculptor& s, float w, float d, float h, float t, Color col) {
    for (float sx : {-1.0f, 1.0f}) {
        for (float sz : {-1.0f, 1.0f}) {
            s.box({sx * (0.5f * w - t), 0.5f * h, sz * (0.5f * d - t)}, {t, h, t}, col);
        }
    }
}

void table(Sculptor& s, const Map2D& map, const Prop& p) {
    const float r = p.get_float("r", 0.5f);
    const Color top = mat_colour(map, p);
    s.cyl({0, 0.0f, 0}, {0, 0.03f, 0}, 0.22f, prim::shade(top, 0.8f), 14);
    s.cyl({0, 0.0f, 0}, {0, 0.38f, 0}, 0.06f, prim::shade(top, 0.8f), 10);
    s.cyl({0, 0.38f, 0}, {0, 0.42f, 0}, r, top, 24);
    // Chairs, facing in.
    const Color chair = material_by_id(map, p.get("chair_mat", "chair"), kWood);
    const auto seats = table_seats(p);
    for (const Vec2 seat : seats) {
        const Vec2 d = seat - p.position;
        Sculptor c{s.lit, s.hull, s.marks, seat, std::atan2(d.z, d.x)};
        c.box({0, 0.26f, 0}, {0.3f, 0.04f, 0.3f}, chair);
        c.box({0.14f, 0.42f, 0}, {0.04f, 0.32f, 0.3f}, chair);
        legs(c, 0.3f, 0.3f, 0.24f, 0.035f, prim::shade(chair, 0.8f));
    }
    if (p.get("setting") != "spaghetti") return;
    // Five philosophers: a plate at each seat, a fork between each pair.
    const int n = static_cast<int>(seats.size());
    for (int i = 0; i < n; ++i) {
        const float a = kTwoPi * static_cast<float>(i) / static_cast<float>(n);
        const float pr = r - 0.17f;
        Sculptor plate{s.lit, s.hull, s.marks, p.position, p.yaw + a};
        plate.cyl({pr, 0.42f, 0}, {pr, 0.435f, 0}, 0.11f, kWhite, 16, false);
        plate.ball({pr, 0.445f, 0}, {0.07f, 0.03f, 0.07f}, kPasta, false);
        plate.ball({pr, 0.47f, 0}, {0.03f, 0.018f, 0.03f}, kSauce, false);
        // The fork halfway round to the next plate, handle outwards.
        Sculptor fork{s.lit, s.hull, s.marks, p.position, p.yaw + a + kPi / static_cast<float>(n)};
        fork.box({r - 0.14f, 0.43f, 0}, {0.13f, 0.012f, 0.02f}, kMetal, 0.0f, false);
        for (float t : {-0.012f, 0.0f, 0.012f}) {
            fork.box({r - 0.235f, 0.43f, t}, {0.06f, 0.012f, 0.005f}, kMetal, 0.0f, false);
        }
    }
}

void desk(Sculptor& s, const Map2D& map, const Prop& p) {
    const float w = p.get_float("w", 1.0f), d = p.get_float("d", 0.5f);
    const Color col = mat_colour(map, p);
    s.box({0, 0.39f, 0}, {w, 0.04f, d}, col);
    legs(s, w, d, 0.37f, 0.04f, prim::shade(col, 0.75f));
    if (p.get("top", "monitor") == "monitor") {
        s.box({0.05f, 0.43f, 0}, {0.08f, 0.04f, 0.14f}, kDark);
        s.box({0.05f, 0.56f, 0}, {0.03f, 0.22f, 0.36f}, kDark);
        s.plate({0.068f, 0.57f, 0}, true, 1.0f, 0.32f, 0.17f, Color{90, 140, 200, 255});
        s.box({-0.12f, 0.42f, 0.0f}, {0.12f, 0.015f, 0.34f}, Color{70, 74, 90, 255}, 0.0f, false);
    }
}

void counter(Sculptor& s, const Map2D& map, const Prop& p) {
    const float w = p.get_float("w", 1.5f), d = p.get_float("d", 0.5f);
    const Color col = mat_colour(map, p);
    s.box({0, 0.2f, 0}, {w, 0.4f, d}, prim::shade(col, 0.9f));
    s.box({0, 0.42f, 0}, {w + 0.04f, 0.04f, d + 0.04f}, prim::shade(col, 1.08f));
    const std::string top = p.get("top");
    if (top == "coffee") {
        s.box({-0.3f * w, 0.56f, 0}, {0.24f, 0.24f, 0.22f}, kDark);
        s.ball({-0.3f * w + 0.13f, 0.6f, 0.06f}, {0.015f, 0.015f, 0.015f}, kRedLight, false);
        s.cyl({0.1f, 0.44f, 0}, {0.1f, 0.52f, 0}, 0.04f, kWhite, 10);
    } else if (top == "sink") {
        s.flat({0.1f, 0.442f, 0}, 0.4f, 0.3f, Color{120, 128, 144, 255});
        s.cyl({0.1f, 0.44f, -0.18f}, {0.1f, 0.58f, -0.18f}, 0.015f, kMetal, 8);
    } else if (top == "stove") {
        for (float x : {-0.35f, 0.35f}) {
            for (float z : {-0.1f, 0.1f}) {
                s.cyl({x * w * 0.5f, 0.44f, z}, {x * w * 0.5f, 0.448f, z}, 0.075f, kDark, 14, false);
            }
        }
        s.cyl({0.0f, 0.44f, 0}, {0.0f, 0.56f, 0}, 0.11f, kMetal, 14);
    } else if (top == "food") {
        for (int i = 0; i < 4; ++i) {
            const float x = (static_cast<float>(i) - 1.5f) * 0.4f;
            s.cyl({x, 0.44f, 0}, {x, 0.47f, 0}, 0.12f, kWhite, 14, false);
            s.ball({x, 0.48f, 0}, {0.08f, 0.035f, 0.08f}, i % 2 ? kPasta : Color{120, 180, 90, 255}, false);
        }
    }
}

void fridge(Sculptor& s, const Map2D& map, const Prop& p) {
    const Color col = mat_colour(map, p);
    s.box({0, 0.45f, 0}, {0.58f, 0.9f, 0.58f}, col);
    s.plate({0.292f, 0.6f, 0}, true, 1.0f, 0.56f, 0.012f, prim::shade(col, 0.7f));
    s.box({0.3f, 0.75f, 0.2f}, {0.03f, 0.18f, 0.03f}, kMetal, 0.0f, false);
    s.box({0.3f, 0.35f, 0.2f}, {0.03f, 0.22f, 0.03f}, kMetal, 0.0f, false);
}

void plant(Sculptor& s) {
    s.cyl({0, 0, 0}, {0, 0.22f, 0}, 0.13f, kPot, 12);
    s.ball({0, 0.36f, 0}, {0.17f, 0.17f, 0.17f}, kLeaf);
    s.ball({0.07f, 0.5f, 0.04f}, {0.12f, 0.12f, 0.12f}, prim::shade(kLeaf, 1.12f));
    s.ball({-0.06f, 0.46f, -0.06f}, {0.11f, 0.11f, 0.11f}, prim::shade(kLeaf, 0.9f));
}

void bench(Sculptor& s, const Map2D& map, const Prop& p) {
    const float w = p.get_float("w", 1.2f);
    const Color col = mat_colour(map, p);
    s.box({0, 0.2f, 0}, {w, 0.05f, 0.38f}, col);
    for (float x : {-0.4f * w, 0.4f * w}) s.box({x, 0.09f, 0}, {0.05f, 0.18f, 0.32f}, prim::shade(col, 0.7f));
}

void cooler(Sculptor& s) {
    s.box({0, 0.25f, 0}, {0.26f, 0.5f, 0.26f}, kWhite);
    s.cyl({0, 0.5f, 0}, {0, 0.68f, 0}, 0.1f, kGlass, 12);
    s.box({0.14f, 0.32f, 0}, {0.03f, 0.04f, 0.04f}, Color{70, 120, 220, 255}, 0.0f, false);
}

void sculpture(Sculptor& s, const Map2D& map, const Prop& p) {
    const std::string form = p.get("form", "pillar");
    const float r = p.get_float("r", 0.25f);
    const float h = p.get_float("h", 0.8f);
    const Color col = mat_colour(map, p);
    const Color stone{226, 222, 214, 255};
    if (form == "pillar") {
        s.cyl({0, 0, 0}, {0, h, 0}, r, col, 16);
        s.cyl({0, h, 0}, {0, h + 0.05f, 0}, r + 0.03f, prim::shade(col, 1.1f), 16);
        return;
    }
    // Everything else stands on a plinth.
    const float base = 0.35f;
    if (form == "orb") {
        s.cyl({0, 0, 0}, {0, base, 0}, r * 0.8f, stone, 16);
        s.ball({0, base + r, 0}, {r, r, r}, col);
    } else if (form == "cube") {
        s.box({0, 0.5f * base, 0}, {1.8f * r, base, 1.8f * r}, stone);
        s.box({0, base + 0.55f * r, 0}, {1.1f * r, 1.1f * r, 1.1f * r}, col, 0.6f);
    } else if (form == "stack") {
        s.box({0, 0.5f * base, 0}, {1.8f * r, base, 1.8f * r}, stone);
        float y = base;
        for (int i = 0; i < 3; ++i) {
            const float k = 1.3f - 0.3f * static_cast<float>(i);
            s.box({0, y + 0.15f * k, 0}, {k * r, 0.3f * k, k * r}, prim::shade(col, 1.0f + 0.1f * static_cast<float>(i)),
                  0.5f * static_cast<float>(i));
            y += 0.3f * k;
        }
    } else {  // arch
        s.box({0, 0.5f * base, 0}, {1.8f * r, base, 1.8f * r}, stone);
        for (float z : {-0.6f * r, 0.6f * r}) s.box({0, base + 0.25f, z}, {0.5f * r, 0.5f, 0.3f * r}, col);
        s.box({0, base + 0.55f, 0}, {0.5f * r, 0.1f, 1.6f * r}, col);
    }
}

void guard_post(Sculptor& s) {
    s.cyl({0, 0, 0}, {0, 0.04f, 0}, 0.12f, kDark, 12);
    s.cyl({0, 0, 0}, {0, 1.0f, 0}, 0.03f, kMetal, 8);
    s.box({0.06f, 1.0f, 0}, {0.18f, 0.08f, 0.08f}, kWhite, 0.0f);
    s.box({0.15f, 0.99f, 0}, {0.02f, 0.05f, 0.05f}, kDark, 0.0f, false);
    s.ball({0.0f, 1.06f, 0}, {0.018f, 0.018f, 0.018f}, kRedLight, false);
}

void hay_bale(Sculptor& s, const Map2D& map, const Prop& p) {
    const float w = p.get_float("w", 1.0f), d = p.get_float("d", 0.5f);
    const int layers = static_cast<int>(p.get_float("layers", 1.0f));
    const Color col = mat_colour(map, p);
    for (int i = 0; i < layers; ++i) {
        const float y = 0.18f + 0.36f * static_cast<float>(i);
        s.box({0, y, 0}, {w, 0.34f, d}, i % 2 ? prim::shade(col, 1.06f) : col);
        // Twine round it.
        for (float x : {-0.25f * w, 0.25f * w}) {
            s.plate({x, y, 0.5f * d + 0.003f}, false, 1.0f, 0.03f, 0.34f, prim::shade(col, 0.6f));
            s.plate({x, y, -0.5f * d - 0.003f}, false, -1.0f, 0.03f, 0.34f, prim::shade(col, 0.6f));
        }
        s.flat({0, y + 0.172f, 0}, 0.03f, d, prim::shade(col, 0.6f), 0.0f);
    }
}

void hay_pile(Sculptor& s, const Map2D& map, const Prop& p) {
    const float r = p.get_float("r", 0.5f);
    const Color col = mat_colour(map, p);
    s.ball({0, 0.0f, 0}, {r, 0.45f, r}, col);
    s.ball({0.2f * r, 0.25f, 0.15f * r}, {0.5f * r, 0.25f, 0.5f * r}, prim::shade(col, 1.08f));
    for (int i = 0; i < 6; ++i) {
        const float a = static_cast<float>(i) * 1.1f;
        s.box({0.9f * r * std::cos(a), 0.01f, 0.9f * r * std::sin(a)}, {0.18f, 0.01f, 0.02f}, kStraw, a, false);
    }
}

void tractor(Sculptor& s, const Map2D& map, const Prop& p) {
    const Color body = mat_colour(map, p);
    // Local x is its length: bonnet forward (+x), cab and big wheels behind.
    s.box({0.25f, 0.42f, 0}, {0.85f, 0.32f, 0.5f}, body);
    s.box({-0.35f, 0.5f, 0}, {0.55f, 0.18f, 0.6f}, prim::shade(body, 0.9f));
    s.box({-0.4f, 0.62f, 0}, {0.18f, 0.12f, 0.3f}, kDark);                           // seat
    s.cyl({0.0f, 0.62f, 0}, {-0.1f, 0.74f, 0}, 0.015f, kDark, 6);
    s.cyl({-0.1f, 0.74f, -0.1f}, {-0.1f, 0.74f, 0.1f}, 0.06f, kDark, 10, false);   // steering wheel
    s.cyl({0.55f, 0.58f, 0.15f}, {0.55f, 0.82f, 0.15f}, 0.03f, kDark, 8);            // exhaust
    s.box({0.68f, 0.42f, 0}, {0.03f, 0.2f, 0.36f}, kMetal, 0.0f, false);              // grille
    for (float z : {-0.38f, 0.38f}) {
        s.cyl({-0.4f, 0.32f, z - 0.08f * (z > 0 ? -1.0f : 1.0f)}, {-0.4f, 0.32f, z}, 0.32f, kDark, 18);
        s.cyl({-0.4f, 0.32f, z * 1.02f}, {-0.4f, 0.32f, z * 1.03f}, 0.14f, Color{230, 190, 60, 255}, 12, false);
        s.cyl({0.45f, 0.18f, z * 0.85f - 0.06f * (z > 0 ? -1.0f : 1.0f)}, {0.45f, 0.18f, z * 0.85f}, 0.18f, kDark, 14);
    }
}

void cart(Sculptor& s, const Map2D& map, const Prop& p) {
    const Color wood = mat_colour(map, p);
    s.box({0, 0.36f, 0}, {1.2f, 0.06f, 0.7f}, wood);
    for (float z : {-0.34f, 0.34f}) s.box({0, 0.48f, z}, {1.2f, 0.2f, 0.04f}, prim::shade(wood, 1.08f));
    s.box({-0.58f, 0.48f, 0}, {0.04f, 0.2f, 0.7f}, prim::shade(wood, 1.08f));
    for (float z : {-0.4f, 0.4f}) {
        s.cyl({0, 0.26f, z}, {0, 0.26f, z + (z > 0 ? 0.05f : -0.05f)}, 0.26f, prim::shade(wood, 0.7f), 14);
        s.box({0.85f, 0.3f, z * 0.6f}, {0.6f, 0.04f, 0.04f}, wood);  // shafts
    }
    // A sign on the front board (see text_plate).
    s.box({0.62f, 0.62f, 0}, {0.03f, 0.2f, 0.5f}, Color{214, 196, 160, 255});
    // Something in it.
    s.ball({-0.15f, 0.4f, 0}, {0.35f, 0.18f, 0.25f}, kStraw);
}

void trough(Sculptor& s, const Map2D& map, const Prop& p) {
    const float w = p.get_float("w", 1.0f);
    const Color wood = mat_colour(map, p);
    s.box({0, 0.15f, 0}, {w, 0.3f, 0.34f}, wood);
    s.flat({0, 0.302f, 0}, w - 0.08f, 0.24f, Color{90, 140, 170, 255});
}

void barrel(Sculptor& s, const Map2D& map, const Prop& p) {
    const Color wood = mat_colour(map, p);
    s.cyl({0, 0, 0}, {0, 0.5f, 0}, 0.19f, wood, 14);
    for (float y : {0.08f, 0.42f}) s.cyl({0, y, 0}, {0, y + 0.03f, 0}, 0.195f, kDark, 14, false);
}

void house(Sculptor& s, const Map2D& map, const Prop& p, bool church) {
    float w = p.get_float("w", 2.0f), d = p.get_float("d", 1.5f);
    const float h = church ? 1.3f : p.get_float("h", 1.1f);
    const Color wall = mat_colour(map, p);
    const Color roof = material_by_id(map, p.get("roof", "roof_tile"), Color{176, 82, 60, 255});
    s.box({0, 0.5f * h, 0}, {w, h, d}, wall);
    // Ridge along the longer side.
    const bool ridge_x = w >= d;
    s.roof({0, h, 0}, (ridge_x ? w : d) + 0.1f, (ridge_x ? d : w) + 0.1f, 0.45f * std::min(w, d), roof,
           ridge_x ? 0.0f : 0.5f * kPi);
    // Windows and a door on the sides the camera sees (+x and +z).
    const Color glass{60, 70, 100, 255};
    const int nx = std::max(1, static_cast<int>(d / 0.6f));
    for (int i = 0; i < nx; ++i) {
        const float z = (static_cast<float>(i) + 0.5f) / static_cast<float>(nx) * d - 0.5f * d;
        s.plate({0.5f * w + 0.004f, 0.7f * h, z}, true, 1.0f, 0.16f, 0.2f, glass);
        s.plate({-0.5f * w - 0.004f, 0.7f * h, z}, true, -1.0f, 0.16f, 0.2f, glass);
    }
    const int nz = std::max(1, static_cast<int>(w / 0.6f));
    for (int i = 0; i < nz; ++i) {
        const float x = (static_cast<float>(i) + 0.5f) / static_cast<float>(nz) * w - 0.5f * w;
        const bool door = !church && i == nz / 2;
        s.plate({x, door ? 0.2f : 0.7f * h, 0.5f * d + 0.004f}, false, 1.0f, door ? 0.2f : 0.16f, door ? 0.4f : 0.2f,
                door ? kWood : glass);
        s.plate({x, 0.7f * h, -0.5f * d - 0.004f}, false, -1.0f, 0.16f, 0.2f, glass);
    }
    if (church) {
        // A tower at the +x end with a copper spire.
        const float tw = std::min(0.9f, 0.6f * d);
        s.box({0.5f * w + 0.5f * tw - 0.05f, 1.0f, 0}, {tw, 2.0f, tw}, prim::shade(wall, 0.95f));
        s.spire({0.5f * w + 0.5f * tw - 0.05f, 2.0f, 0}, tw + 0.08f, 1.2f, roof);
        s.plate({0.5f * w + tw - 0.05f + 0.004f, 1.6f, 0}, true, 1.0f, 0.2f, 0.3f, glass);
    }
}

void tree(Sculptor& s, const Map2D& map, const Prop& p) {
    const Color trunk = mat_colour(map, p);
    s.cyl({0, 0, 0}, {0, 0.9f, 0}, 0.1f, trunk, 10);
    s.ball({0, 1.15f, 0}, {0.5f, 0.42f, 0.5f}, Color{86, 150, 82, 255});
    s.ball({0.18f, 1.45f, 0.1f}, {0.32f, 0.3f, 0.32f}, Color{104, 170, 96, 255});
    s.ball({-0.2f, 1.3f, -0.15f}, {0.3f, 0.28f, 0.3f}, Color{78, 136, 76, 255});
}

void lamp(Sculptor& s) {
    s.cyl({0, 0, 0}, {0, 1.0f, 0}, 0.03f, kDark, 8);
    s.box({0, 1.06f, 0}, {0.12f, 0.14f, 0.12f}, kWarm);
    s.spire({0, 1.13f, 0}, 0.16f, 0.08f, kDark);
}

void fountain(Sculptor& s, const Map2D& map, const Prop& p) {
    const float r = p.get_float("r", 0.5f);
    const Color stone = mat_colour(map, p);
    s.cyl({0, 0, 0}, {0, 0.25f, 0}, r, stone, 24);
    s.cyl({0, 0.25f, 0}, {0, 0.255f, 0}, r - 0.07f, Color{100, 160, 200, 255}, 24, false);
    s.cyl({0, 0.25f, 0}, {0, 0.7f, 0}, 0.07f, stone, 10);
    s.cyl({0, 0.7f, 0}, {0, 0.76f, 0}, 0.22f, stone, 16);
    s.ball({0, 0.8f, 0}, {0.06f, 0.08f, 0.06f}, Color{140, 200, 230, 255}, false);
}

// Wall-mounted: local +x is the wall's outward normal; the prop's position
// is on the wall face.
void blackboard(Sculptor& s, const Prop& p) {
    const float w = p.get_float("w", 1.5f);
    s.box({0.015f, 0.42f, 0}, {0.03f, 0.5f, w + 0.06f}, kWood, 0.0f, false);
    s.box({0.025f, 0.42f, 0}, {0.03f, 0.46f, w}, kChalkBoard, 0.0f, false);
    s.box({0.05f, 0.18f, 0}, {0.05f, 0.02f, w * 0.9f}, kWood, 0.0f, false);  // chalk tray
    s.box({0.06f, 0.195f, 0.3f * w}, {0.012f, 0.012f, 0.06f}, kWhite, 0.0f, false);
}

void door(Sculptor& s, const Prop& p) {
    const float w = p.get_float("w", 0.5f);
    for (float side : {1.0f, -1.0f}) {
        const float face = side * 0.25f + side * 0.004f;
        s.plate({face, 0.35f, 0}, true, side, w * 0.8f, 0.6f, Color{120, 76, 46, 255});
        s.ball({face + side * 0.015f, 0.34f, -0.3f * w}, {0.02f, 0.02f, 0.02f}, Color{230, 190, 70, 255}, false);
        if (p.get_float("slot", 0.0f) > 0.5f) {
            s.plate({face + side * 0.002f, 0.45f, 0}, true, side, 0.2f * w + 0.08f, 0.025f, kDark);
        }
    }
}

void elevator(Sculptor& s, const Prop& p) {
    const float w = p.get_float("w", 1.0f);
    s.plate({0.004f, 0.32f, 0}, true, 1.0f, w * 0.96f, 0.6f, Color{100, 106, 120, 255});
    s.plate({0.006f, 0.31f, -0.235f * w}, true, 1.0f, w * 0.44f, 0.56f, Color{196, 202, 212, 255});
    s.plate({0.006f, 0.31f, 0.235f * w}, true, 1.0f, w * 0.44f, 0.56f, Color{196, 202, 212, 255});
    s.plate({0.008f, 0.66f, 0}, true, 1.0f, 0.14f, 0.05f, kDark);
    s.plate({0.009f, 0.66f, 0}, true, 1.0f, 0.05f, 0.03f, kRedLight);
}

void painting(Sculptor& s, const Map2D& map, const Prop& p) {
    const float w = p.get_float("w", 0.9f);
    const Color col = mat_colour(map, p);
    s.box({0.012f, 0.4f, 0}, {0.024f, 0.36f, w}, Color{196, 160, 80, 255}, 0.0f, false);
    s.plate({0.026f, 0.4f, 0}, true, 1.0f, w - 0.06f, 0.3f, prim::shade(col, 1.15f));
    // A few shapes in the picture, from its position so each one differs.
    const float seed = p.position.x * 7.1f + p.position.z * 3.3f;
    for (int i = 0; i < 3; ++i) {
        const float u = std::sin(seed + static_cast<float>(i) * 2.1f);
        const float v = std::cos(seed * 1.3f + static_cast<float>(i));
        const Color c = i == 0 ? kWhite : (i == 1 ? kDark : prim::shade(col, 0.6f));
        s.plate({0.028f + 0.001f * static_cast<float>(i), 0.4f + 0.08f * v, 0.3f * (w - 0.2f) * u}, true, 1.0f,
                0.08f + 0.06f * std::fabs(v), 0.06f + 0.05f * std::fabs(u), c);
    }
}

void bridge(Sculptor& s, const Prop& p) {
    const float w = p.get_float("w", 2.0f), d = p.get_float("d", 1.0f);
    const Color rail{110, 80, 56, 255};
    for (float z : {-0.5f * d + 0.03f, 0.5f * d - 0.03f}) {
        s.box({0, 0.3f, z}, {w + 0.3f, 0.05f, 0.05f}, rail);
        const int posts = static_cast<int>(w / 0.4f) + 1;
        for (int i = 0; i <= posts; ++i) {
            const float x = -0.5f * (w + 0.3f) + (w + 0.3f) * static_cast<float>(i) / static_cast<float>(posts);
            s.box({x, 0.15f, z}, {0.05f, 0.3f, 0.05f}, rail);
        }
    }
    for (int i = 0; i < static_cast<int>(w / 0.25f); ++i) {
        s.flat({-0.5f * w + 0.125f + 0.25f * static_cast<float>(i), 0.004f, 0}, 0.02f, d, Color{130, 96, 66, 255});
    }
}

void sign(Sculptor& s, const Map2D& map, const Prop& p) {
    const Color wood = mat_colour(map, p);
    s.cyl({-0.03f, 0, 0}, {-0.03f, 0.55f, 0}, 0.025f, prim::shade(wood, 0.8f), 8);
    s.box({0.0f, 0.55f, 0}, {0.03f, 0.22f, 0.7f}, Color{214, 196, 160, 255});
}

}  // namespace

void build_props(const Map2D& map, MeshBuilder& lit, MeshBuilder& hull, MeshBuilder& marks) {
    for (const Prop& p : map.props()) {
        Sculptor s{lit, hull, marks, p.position, p.yaw};
        const std::string& k = p.kind;
        if (k == "table") table(s, map, p);
        else if (k == "desk") desk(s, map, p);
        else if (k == "counter") counter(s, map, p);
        else if (k == "fridge") fridge(s, map, p);
        else if (k == "plant") plant(s);
        else if (k == "bench") bench(s, map, p);
        else if (k == "cooler") cooler(s);
        else if (k == "sculpture") sculpture(s, map, p);
        else if (k == "guard_post") guard_post(s);
        else if (k == "hay_bale") hay_bale(s, map, p);
        else if (k == "hay_pile") hay_pile(s, map, p);
        else if (k == "tractor") tractor(s, map, p);
        else if (k == "cart") cart(s, map, p);
        else if (k == "trough") trough(s, map, p);
        else if (k == "barrel") barrel(s, map, p);
        else if (k == "house") house(s, map, p, false);
        else if (k == "church") house(s, map, p, true);
        else if (k == "tree") tree(s, map, p);
        else if (k == "lamp") lamp(s);
        else if (k == "fountain") fountain(s, map, p);
        else if (k == "blackboard") blackboard(s, p);
        else if (k == "door") door(s, p);
        else if (k == "elevator") elevator(s, p);
        else if (k == "painting") painting(s, map, p);
        else if (k == "bridge") bridge(s, p);
        else if (k == "sign") sign(s, map, p);
    }
}

bool text_plate(const Prop& p, TextPlate* out) {
    const std::string text = p.get("text");
    if (text.empty()) return false;
    const auto at = [&](float x, float y, float z) {
        const float c = std::cos(p.yaw), s = std::sin(p.yaw);
        return Vector3{p.position.x + c * x - s * z, y, p.position.z + s * x + c * z};
    };
    if (p.kind == "blackboard") {
        const float w = p.get_float("w", 1.5f);
        *out = {at(0.042f, 0.43f, 0), p.yaw, w - 0.08f, 0.4f, Color{236, 240, 230, 255}, Color{0, 0, 0, 0}};
        return true;
    }
    if (p.kind == "sign") {
        *out = {at(0.017f, 0.55f, 0), p.yaw, 0.66f, 0.18f, Color{60, 40, 30, 255}, Color{0, 0, 0, 0}};
        return true;
    }
    if (p.kind == "cart") {
        *out = {at(0.637f, 0.62f, 0), p.yaw, 0.46f, 0.17f, Color{60, 40, 30, 255}, Color{0, 0, 0, 0}};
        return true;
    }
    return false;
}

}  // namespace wobo
