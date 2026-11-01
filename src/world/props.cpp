#include "world/props.h"

#include <cmath>

namespace wobo {

namespace {

constexpr PropKind kKinds[] = {
    // Furniture.
    {"table", "table", true},          // round; r, seats (chairs round it)
    {"desk", "desk", true},            // w, d
    {"counter", "counter", true},      // w, d; top = coffee | sink | food
    {"fridge", "fridge", true},
    {"plant", "plant", true},
    {"bench", "bench", true},          // w
    {"cooler", "steel", true},         // water cooler
    // Gallery.
    {"sculpture", "wall_red", true},   // form = pillar | cube | orb | stack | arch; r
    {"guard_post", "steel", true},     // a security camera on a pole
    // Barn.
    {"hay_bale", "hay", true},         // w, d, layers
    {"hay_pile", "hay", true},         // r
    {"tractor", "tractor", true},
    {"cart", "cart", true},            // text on its sign
    {"trough", "cart", true},          // w
    {"barrel", "cart", true},
    // Town.
    {"house", "house", true},          // w, d, h, roof (a material id)
    {"church", "house", true},         // w, d; a tower at its +x end
    {"tree", "trunk", true},
    {"lamp", "steel", true},
    {"fountain", "wall_light", true},  // r
    // Decoration only.
    {"blackboard", "board", false},    // on a wall face: w, text
    {"door", "door", false},           // handle and letter slot on a door cell
    {"elevator", "steel", false},      // doors and call buttons on a wall face
    {"painting", "wall_red", false},   // a frame on a wall face: w
    {"bridge", "planks", false},       // railings: w (span), d (deck width)
    {"sign", "cart", false},           // a plate on a post: text
};

}  // namespace

const PropKind* find_prop_kind(const std::string& kind) {
    for (const PropKind& k : kKinds) {
        if (kind == k.kind) return &k;
    }
    return nullptr;
}

std::vector<Vec2> table_seats(const Prop& table) {
    const int seats = static_cast<int>(table.get_float("seats", 0.0f));
    const float ring = table.get_float("r", 0.5f) + 0.24f;
    std::vector<Vec2> out;
    for (int i = 0; i < seats; ++i) {
        const float a = table.yaw + kTwoPi * static_cast<float>(i) / static_cast<float>(seats);
        out.push_back(table.position + Vec2{std::cos(a), std::sin(a)} * ring);
    }
    return out;
}

float prop_height(const Prop& p) {
    const std::string& k = p.kind;
    if (k == "table") return 0.42f;
    if (k == "desk" || k == "counter") return 0.42f;
    if (k == "fridge") return 0.9f;
    if (k == "plant") return 0.6f;
    if (k == "bench") return 0.22f;
    if (k == "cooler") return 0.62f;
    if (k == "sculpture") return p.get_float("h", 0.8f);
    if (k == "guard_post") return 1.0f;
    if (k == "hay_bale") return 0.36f * p.get_float("layers", 1.0f);
    if (k == "hay_pile") return 0.45f;
    if (k == "tractor") return 0.85f;
    if (k == "cart") return 0.55f;
    if (k == "trough" || k == "barrel") return 0.35f;
    if (k == "house") return p.get_float("h", 1.2f) + 0.5f;
    if (k == "church") return 2.4f;
    if (k == "tree") return 1.6f;
    if (k == "lamp") return 1.1f;
    if (k == "fountain") return 0.35f;
    return 0.0f;
}

std::vector<Shape> prop_footprint(const Prop& p, std::uint8_t chair_material) {
    std::vector<Shape> out;
    const PropKind* kind = find_prop_kind(p.kind);
    if (!kind || !kind->solid) return out;
    const std::string& k = p.kind;
    const float h = prop_height(p);
    const auto box = [&](float w, float d) {
        out.push_back(Shape::box(p.position, {0.5f * w, 0.5f * d}, p.yaw, p.material, h));
    };
    const auto disc = [&](float r) { out.push_back(Shape::disc(p.position, r, p.material, h)); };

    if (k == "table") {
        disc(p.get_float("r", 0.5f));
        for (const Vec2 seat : table_seats(p)) {
            const Vec2 d = seat - p.position;
            out.push_back(Shape::box(seat, {0.16f, 0.16f}, std::atan2(d.z, d.x), chair_material, 0.5f));
        }
    } else if (k == "desk") {
        box(p.get_float("w", 1.0f), p.get_float("d", 0.5f));
    } else if (k == "counter") {
        box(p.get_float("w", 1.5f), p.get_float("d", 0.5f));
    } else if (k == "fridge") {
        box(0.6f, 0.6f);
    } else if (k == "plant") {
        disc(0.18f);
    } else if (k == "bench") {
        box(p.get_float("w", 1.2f), 0.4f);
    } else if (k == "cooler") {
        disc(0.15f);
    } else if (k == "sculpture") {
        const std::string form = p.get("form", "pillar");
        const float r = p.get_float("r", 0.25f);
        if (form == "cube" || form == "stack" || form == "arch") box(2.0f * r, 2.0f * r);
        else disc(r);
    } else if (k == "guard_post") {
        disc(0.1f);
    } else if (k == "hay_bale") {
        box(p.get_float("w", 1.0f), p.get_float("d", 0.5f));
    } else if (k == "hay_pile") {
        disc(p.get_float("r", 0.5f));
    } else if (k == "tractor") {
        box(1.7f, 0.95f);
    } else if (k == "cart") {
        box(1.3f, 0.8f);
    } else if (k == "trough") {
        box(p.get_float("w", 1.0f), 0.35f);
    } else if (k == "barrel") {
        disc(0.2f);
    } else if (k == "house" || k == "church") {
        box(p.get_float("w", 2.0f), p.get_float("d", 1.5f));
    } else if (k == "tree") {
        disc(0.16f);
    } else if (k == "lamp") {
        disc(0.07f);
    } else if (k == "fountain") {
        disc(p.get_float("r", 0.5f));
    }
    return out;
}

}  // namespace wobo
