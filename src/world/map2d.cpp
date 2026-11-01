#include "world/map2d.h"

#include <algorithm>
#include <limits>

namespace wobo {

Map2D::Map2D(std::string name, int width, int height, float cell_size,
             std::vector<Material> materials, std::vector<std::uint8_t> cells)
    : name_(std::move(name)),
      width_(width),
      height_(height),
      cell_size_(cell_size),
      materials_(std::move(materials)),
      cells_(std::move(cells)) {
    // The boundary reads as the first solid material.
    for (std::size_t i = 0; i < materials_.size(); ++i) {
        if (materials_[i].solid) {
            boundary_material_ = static_cast<std::uint8_t>(i);
            break;
        }
    }
    build_derived();
}

bool Map2D::circle_free(float x, float z, float radius) const {
    const int x0 = cell_x(x - radius);
    const int x1 = cell_x(x + radius);
    const int z0 = cell_z(z - radius);
    const int z1 = cell_z(z + radius);
    const float r2 = radius * radius;
    for (int cz = z0; cz <= z1; ++cz) {
        for (int cx = x0; cx <= x1; ++cx) {
            if (!solid(cx, cz)) continue;
            // Distance from the centre to the cell's box.
            const float minx = cx * cell_size_;
            const float minz = cz * cell_size_;
            const float px = clampf(x, minx, minx + cell_size_);
            const float pz = clampf(z, minz, minz + cell_size_);
            const float dx = x - px;
            const float dz = z - pz;
            if (dx * dx + dz * dz < r2) return false;
        }
    }
    return true;
}

float Map2D::clearance(float x, float z) const {
    const int cx = cell_x(x);
    const int cz = cell_z(z);
    if (!in_bounds(cx, cz)) return 0.0f;
    return clearance_[index(cx, cz)];
}

Pose2D Map2D::sample_valid_pose(Rng& rng, float radius) const {
    for (int attempt = 0; attempt < 10000; ++attempt) {
        const float x = rng.uniform(0.0f, world_width());
        const float z = rng.uniform(0.0f, world_depth());
        if (circle_free(x, z, radius)) {
            return {x, z, rng.uniform(-kPi, kPi)};
        }
    }
    // Pathological map: fall back to the centre of any free cell.
    if (!free_cells_.empty()) {
        const int i = free_cells_[rng.below(static_cast<std::uint32_t>(free_cells_.size()))];
        const Vec2 c = cell_center(i % width_, i / width_);
        return {c.x, c.z, rng.uniform(-kPi, kPi)};
    }
    return {};
}

std::uint8_t Map2D::add_material(Material m) {
    materials_.push_back(std::move(m));
    return static_cast<std::uint8_t>(materials_.size() - 1);
}

void Map2D::paint(const std::vector<int>& cells, std::uint8_t material) {
    for (int i : cells) {
        if (i >= 0 && i < width_ * height_) cells_[static_cast<std::size_t>(i)] = material;
    }
    build_derived();
}

void Map2D::build_derived() {
    // Brute-force distance transform: for each free cell, the distance from
    // its centre to the nearest solid cell's box. Maps are small (a few
    // thousand cells), so this is instant.
    const int n = width_ * height_;
    clearance_.assign(static_cast<std::size_t>(n), 0.0f);
    std::vector<int> solids;
    for (int cz = -1; cz <= height_; ++cz) {
        for (int cx = -1; cx <= width_; ++cx) {
            if (solid(cx, cz)) solids.push_back((cz + 1) * (width_ + 2) + (cx + 1));
        }
    }
    free_cells_.clear();
    for (int cz = 0; cz < height_; ++cz) {
        for (int cx = 0; cx < width_; ++cx) {
            if (solid(cx, cz)) continue;
            const Vec2 c = cell_center(cx, cz);
            float best = std::numeric_limits<float>::max();
            for (int s : solids) {
                const int sx = s % (width_ + 2) - 1;
                const int sz = s / (width_ + 2) - 1;
                const float minx = sx * cell_size_;
                const float minz = sz * cell_size_;
                const float dx = c.x - clampf(c.x, minx, minx + cell_size_);
                const float dz = c.z - clampf(c.z, minz, minz + cell_size_);
                best = std::min(best, dx * dx + dz * dz);
            }
            clearance_[index(cx, cz)] = std::sqrt(best);
            free_cells_.push_back(index(cx, cz));
        }
    }
}

}  // namespace wobo
