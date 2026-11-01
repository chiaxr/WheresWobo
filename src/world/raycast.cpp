#include "world/raycast.h"

#include <cmath>
#include <limits>

namespace wobo {

RayHit cast_ray(const Map2D& map, float x, float z, float angle_rad, float max_range) {
    const float cs = map.cell_size();
    int cx = map.cell_x(x);
    int cz = map.cell_z(z);
    if (map.solid(cx, cz)) {
        return {true, 0.0f, map.material_index(cx, cz)};
    }

    const float dx = std::cos(angle_rad);
    const float dz = std::sin(angle_rad);
    constexpr float inf = std::numeric_limits<float>::infinity();

    // The nearest shape hit so far; the walk stops once it passes it. A
    // shape spans several cells, so it may be tested more than once.
    const Vec2 o{x, z};
    const Vec2 d{dx, dz};
    RayHit best{false, max_range, 0};
    const auto test = [&](const Shape& s) {
        float t;
        if (s.ray(o, d, best.range, &t) && (!best.hit || t < best.range)) best = {true, t, s.material};
    };
    for (const Shape& m : map.movers()) test(m);
    const auto test_cell = [&](int i) {
        for (int s : map.shapes_in(i)) test(map.shapes()[static_cast<std::size_t>(s)]);
    };
    if (map.in_bounds(cx, cz)) test_cell(map.index(cx, cz));

    const int step_x = dx > 0.0f ? 1 : -1;
    const int step_z = dz > 0.0f ? 1 : -1;
    const float t_delta_x = dx != 0.0f ? std::fabs(cs / dx) : inf;
    const float t_delta_z = dz != 0.0f ? std::fabs(cs / dz) : inf;

    const float next_x = (dx > 0.0f ? (cx + 1) * cs : cx * cs);
    const float next_z = (dz > 0.0f ? (cz + 1) * cs : cz * cs);
    float t_max_x = dx != 0.0f ? (next_x - x) / dx : inf;
    float t_max_z = dz != 0.0f ? (next_z - z) / dz : inf;

    while (true) {
        float t;
        if (t_max_x < t_max_z) {
            t = t_max_x;
            t_max_x += t_delta_x;
            cx += step_x;
        } else {
            t = t_max_z;
            t_max_z += t_delta_z;
            cz += step_z;
        }
        if (t > best.range) return best;
        if (map.solid(cx, cz)) {
            return {true, t, map.material_index(cx, cz)};
        }
        test_cell(map.index(cx, cz));
    }
}

}  // namespace wobo
