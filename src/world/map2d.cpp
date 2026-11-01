#include "world/map2d.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace wobo {

Shape Shape::disc(Vec2 c, float r, std::uint8_t mat, float height) {
    Shape s;
    s.kind = Kind::Disc;
    s.centre = c;
    s.radius = r;
    s.material = mat;
    s.height = height;
    return s;
}

Shape Shape::box(Vec2 c, Vec2 half, float yaw, std::uint8_t mat, float height) {
    Shape s;
    s.kind = Kind::Box;
    s.centre = c;
    s.half = half;
    s.yaw = yaw;
    s.material = mat;
    s.height = height;
    return s;
}

float Shape::distance(Vec2 p) const {
    const Vec2 d = p - centre;
    if (kind == Kind::Disc) return d.length() - radius;
    const float c = std::cos(yaw), s = std::sin(yaw);
    const float lx = std::fabs(c * d.x + s * d.z) - half.x;
    const float lz = std::fabs(-s * d.x + c * d.z) - half.z;
    const float ox = std::max(lx, 0.0f), oz = std::max(lz, 0.0f);
    return std::sqrt(ox * ox + oz * oz) + std::min(std::max(lx, lz), 0.0f);
}

bool Shape::ray(Vec2 o, Vec2 d, float max, float* t) const {
    const Vec2 rel = o - centre;
    if (kind == Kind::Disc) {
        // |rel + t d|^2 = r^2 with |d| = 1.
        const float b = rel.x * d.x + rel.z * d.z;
        const float c = rel.x * rel.x + rel.z * rel.z - radius * radius;
        if (c <= 0.0f) { *t = 0.0f; return true; }
        const float disc = b * b - c;
        if (disc < 0.0f || b > 0.0f) return false;
        const float hit = -b - std::sqrt(disc);
        if (hit > max) return false;
        *t = hit;
        return true;
    }
    // Slabs in the box's own frame.
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float ox = cy * rel.x + sy * rel.z, oz = -sy * rel.x + cy * rel.z;
    const float dx = cy * d.x + sy * d.z, dz = -sy * d.x + cy * d.z;
    float t0 = 0.0f, t1 = max;
    const float o2[2] = {ox, oz}, d2[2] = {dx, dz}, h2[2] = {half.x, half.z};
    for (int a = 0; a < 2; ++a) {
        if (std::fabs(d2[a]) < 1e-9f) {
            if (std::fabs(o2[a]) > h2[a]) return false;
            continue;
        }
        float ta = (-h2[a] - o2[a]) / d2[a];
        float tb = (h2[a] - o2[a]) / d2[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    *t = t0;
    return true;
}

void Shape::bounds(Vec2* lo, Vec2* hi) const {
    if (kind == Kind::Disc) {
        *lo = {centre.x - radius, centre.z - radius};
        *hi = {centre.x + radius, centre.z + radius};
        return;
    }
    const float c = std::fabs(std::cos(yaw)), s = std::fabs(std::sin(yaw));
    const float ex = c * half.x + s * half.z, ez = s * half.x + c * half.z;
    *lo = {centre.x - ex, centre.z - ez};
    *hi = {centre.x + ex, centre.z + ez};
}

std::string Prop::get(const std::string& key, const std::string& fallback) const {
    const auto it = params.find(key);
    return it == params.end() ? fallback : it->second;
}

float Prop::get_float(const std::string& key, float fallback) const {
    const auto it = params.find(key);
    return it == params.end() ? fallback : static_cast<float>(std::atof(it->second.c_str()));
}

Map2D::Map2D(std::string name, int width, int height, float cell_size,
             std::vector<Material> materials, std::vector<std::uint8_t> cells,
             std::vector<Shape> shapes, std::vector<Prop> props)
    : name_(std::move(name)),
      width_(width),
      height_(height),
      cell_size_(cell_size),
      materials_(std::move(materials)),
      cells_(std::move(cells)),
      shapes_(std::move(shapes)),
      props_(std::move(props)) {
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
    const Vec2 p{x, z};
    for (int cz = std::max(z0, 0); cz <= std::min(z1, height_ - 1); ++cz) {
        for (int cx = std::max(x0, 0); cx <= std::min(x1, width_ - 1); ++cx) {
            for (int s : cell_shapes_[static_cast<std::size_t>(index(cx, cz))]) {
                if (shapes_[static_cast<std::size_t>(s)].distance(p) < radius) return false;
            }
        }
    }
    for (const Shape& m : movers_) {
        if (m.distance(p) < radius) return false;
    }
    return true;
}

bool Map2D::point_free(float x, float z) const {
    const int cx = cell_x(x);
    const int cz = cell_z(z);
    if (solid(cx, cz)) return false;
    const Vec2 p{x, z};
    for (int s : cell_shapes_[static_cast<std::size_t>(index(cx, cz))]) {
        if (shapes_[static_cast<std::size_t>(s)].distance(p) < 0.0f) return false;
    }
    for (const Shape& m : movers_) {
        if (m.distance(p) < 0.0f) return false;
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
    const int n = width_ * height_;
    cell_shapes_.assign(static_cast<std::size_t>(n), {});
    for (std::size_t s = 0; s < shapes_.size(); ++s) {
        Vec2 lo, hi;
        shapes_[s].bounds(&lo, &hi);
        for (int cz = std::max(0, cell_z(lo.z)); cz <= std::min(height_ - 1, cell_z(hi.z)); ++cz) {
            for (int cx = std::max(0, cell_x(lo.x)); cx <= std::min(width_ - 1, cell_x(hi.x)); ++cx) {
                cell_shapes_[static_cast<std::size_t>(index(cx, cz))].push_back(static_cast<int>(s));
            }
        }
    }

    // Brute-force distance transform: for each free cell, the distance from
    // its centre to the nearest solid cell's box or shape. Maps are small (a
    // few thousand cells), so this is quick.
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
            float clear = std::sqrt(best);
            for (const Shape& sh : shapes_) clear = std::min(clear, sh.distance(c));
            clearance_[index(cx, cz)] = std::max(0.0f, clear);
            if (clear > 0.0f) free_cells_.push_back(index(cx, cz));
        }
    }
}

}  // namespace wobo
