#pragma once

// The known, static 2D world: a grid of cells, each either floor or a solid
// block, and each carrying a material (semantic class + RGB colour). The
// same map drives collision, the LiDAR simulator, the robot's localizer and
// the 3D renderer; the renderer only ever reads it.

#include <cstdint>
#include <string>
#include <vector>

#include "core/rng.h"
#include "core/types.h"

namespace wobo {

// A colour-independent mark drawn on a material, so surfaces (and the UI)
// never rely on colour alone.
enum class Pattern : std::uint8_t { None, Stripes, Dots, Checker, Chevron, Grid };

struct Material {
    std::string id;      // e.g. "wall_red"
    std::string name;    // display name
    Rgb rgb;
    bool solid = false;  // blocks motion and LiDAR beams
    float height = 1.0f; // render height of a solid block (metres)
    Pattern pattern = Pattern::None;
};

class Map2D {
public:
    Map2D() = default;
    Map2D(std::string name, int width, int height, float cell_size,
          std::vector<Material> materials, std::vector<std::uint8_t> cells);

    const std::string& name() const { return name_; }
    const std::string& description() const { return description_; }
    void set_description(std::string d) { description_ = std::move(d); }

    int width() const { return width_; }    // cells along x
    int height() const { return height_; }  // cells along z
    float cell_size() const { return cell_size_; }
    float world_width() const { return width_ * cell_size_; }
    float world_depth() const { return height_ * cell_size_; }

    bool in_bounds(int cx, int cz) const {
        return cx >= 0 && cz >= 0 && cx < width_ && cz < height_;
    }
    // Out-of-bounds cells read as solid, so the map is always closed.
    bool solid(int cx, int cz) const {
        return !in_bounds(cx, cz) || materials_[cells_[index(cx, cz)]].solid;
    }
    std::uint8_t material_index(int cx, int cz) const {
        return in_bounds(cx, cz) ? cells_[index(cx, cz)] : boundary_material_;
    }
    const Material& material(std::uint8_t i) const { return materials_[i]; }
    const Material& material_at(int cx, int cz) const { return materials_[material_index(cx, cz)]; }
    const std::vector<Material>& materials() const { return materials_; }

    int cell_x(float x) const { return static_cast<int>(std::floor(x / cell_size_)); }
    int cell_z(float z) const { return static_cast<int>(std::floor(z / cell_size_)); }
    Vec2 cell_center(int cx, int cz) const {
        return {(cx + 0.5f) * cell_size_, (cz + 0.5f) * cell_size_};
    }

    bool point_free(float x, float z) const { return !solid(cell_x(x), cell_z(z)); }
    // True if a disc of `radius` at (x, z) overlaps no solid cell.
    bool circle_free(float x, float z, float radius) const;
    bool valid_pose(const Pose2D& p, float radius) const { return circle_free(p.x, p.z, radius); }

    // Distance from (x, z) to the nearest solid cell edge (approximate, from a
    // precomputed per-cell distance transform). Used by planners as a
    // clearance cost; collision itself uses circle_free.
    float clearance(float x, float z) const;

    // Uniformly sample a collision-free pose with a uniform heading.
    Pose2D sample_valid_pose(Rng& rng, float radius) const;

    // Cells a robot of the given radius can stand in the centre of.
    const std::vector<int>& free_cells() const { return free_cells_; }
    int index(int cx, int cz) const { return cz * width_ + cx; }

private:
    void build_derived();

    std::string name_;
    std::string description_;
    int width_ = 0;
    int height_ = 0;
    float cell_size_ = 0.5f;
    std::vector<Material> materials_;
    std::vector<std::uint8_t> cells_;
    std::uint8_t boundary_material_ = 0;
    std::vector<int> free_cells_;
    std::vector<float> clearance_;  // per cell, metres
};

}  // namespace wobo
