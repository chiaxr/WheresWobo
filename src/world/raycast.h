#pragma once

// 2D ray casting against the grid map (Amanatides-Woo DDA), its shapes and
// any movers. This is the one place beams meet geometry: the LiDAR
// simulator, the localizer's expected scans and the planner's predicted
// scans all call it, never the 3D meshes.

#include <cstdint>

#include "world/map2d.h"

namespace wobo {

struct RayHit {
    bool hit = false;
    float range = 0.0f;           // max_range when !hit
    std::uint8_t material = 0;    // valid when hit
};

RayHit cast_ray(const Map2D& map, float x, float z, float angle_rad, float max_range);

}  // namespace wobo
