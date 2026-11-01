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
        if (t > max_range) return {false, max_range, 0};
        if (map.solid(cx, cz)) {
            return {true, t, map.material_index(cx, cz)};
        }
    }
}

}  // namespace wobo
