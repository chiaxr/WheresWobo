#pragma once

// The known, static 2D world: a grid of cells, each either floor or a solid
// block, and each carrying a material (semantic class + RGB colour), plus
// solid shapes off the grid (round tables, hay bales, sculptures) and the
// props they belong to. The same map drives collision, the LiDAR simulator,
// the robot's localizer and the 3D renderer; the renderer only ever reads it.

#include <cstdint>
#include <map>
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
    std::string look;    // how the renderer treats it beyond colour: "water"
};

// A solid footprint off the grid: a disc, or a box turned by `yaw`. Beams
// and collision test it exactly, so a round table reads as round.
struct Shape {
    enum class Kind : std::uint8_t { Disc, Box };
    Kind kind = Kind::Disc;
    Vec2 centre;
    float radius = 0.0f;  // Disc
    Vec2 half;            // Box: half extents along its own x and z
    float yaw = 0.0f;     // Box: radians, same convention as Pose2D
    float height = 0.5f;  // how tall it stands (baked shadows)
    std::uint8_t material = 0;

    static Shape disc(Vec2 c, float r, std::uint8_t mat, float height);
    static Shape box(Vec2 c, Vec2 half, float yaw, std::uint8_t mat, float height);

    // Distance from p to the shape's edge (negative inside).
    float distance(Vec2 p) const;
    // Distance along the unit ray (o, d) to the first hit, if within max.
    bool ray(Vec2 o, Vec2 d, float max, float* t) const;
    // Axis-aligned bounds.
    void bounds(Vec2* lo, Vec2* hi) const;
};

// Something placed on the map from the catalogue in world/props.h. The map
// keeps it for the renderer; its solid footprint is already in shapes().
struct Prop {
    std::string kind;
    Vec2 position;
    float yaw = 0.0f;  // radians
    std::uint8_t material = 0;
    std::map<std::string, std::string> params;

    std::string get(const std::string& key, const std::string& fallback = "") const;
    float get_float(const std::string& key, float fallback) const;
};

class Map2D {
public:
    Map2D() = default;
    Map2D(std::string name, int width, int height, float cell_size,
          std::vector<Material> materials, std::vector<std::uint8_t> cells,
          std::vector<Shape> shapes = {}, std::vector<Prop> props = {});

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
    // Out-of-bounds cells read as solid, so the map is always closed. Only
    // the grid: shapes off the grid are tested by the queries below.
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

    bool point_free(float x, float z) const;
    // True if a disc of `radius` at (x, z) overlaps no solid cell or shape.
    bool circle_free(float x, float z, float radius) const;
    bool valid_pose(const Pose2D& p, float radius) const { return circle_free(p.x, p.z, radius); }

    // Distance from (x, z) to the nearest solid cell edge or shape
    // (approximate, from a precomputed per-cell distance transform). Used by
    // planners as a clearance cost; collision itself uses circle_free.
    float clearance(float x, float z) const;
    float cell_clearance(int i) const { return clearance_[static_cast<std::size_t>(i)]; }

    const std::vector<Shape>& shapes() const { return shapes_; }
    const std::vector<Prop>& props() const { return props_; }
    // Indices into shapes() of those whose bounds touch cell index i.
    const std::vector<int>& shapes_in(int i) const { return cell_shapes_[static_cast<std::size_t>(i)]; }

    // Moving obstacles (the physical world only: a donkey, a guard). Discs,
    // tested by every query above but never part of the distance transform.
    void set_movers(std::vector<Shape> movers) { movers_ = std::move(movers); }
    const std::vector<Shape>& movers() const { return movers_; }

    // Uniformly sample a collision-free pose with a uniform heading.
    Pose2D sample_valid_pose(Rng& rng, float radius) const;

    // Floor cells whose centre no shape covers.
    const std::vector<int>& free_cells() const { return free_cells_; }

    // Runtime changes, for the physical world only (the road block): add a
    // material, and repaint cells, by index(), with one.
    std::uint8_t add_material(Material m);
    void paint(const std::vector<int>& cells, std::uint8_t material);
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
    std::vector<Shape> shapes_;
    std::vector<Prop> props_;
    std::vector<Shape> movers_;
    std::vector<std::vector<int>> cell_shapes_;
    std::vector<int> free_cells_;
    std::vector<float> clearance_;  // per cell, metres
};

}  // namespace wobo
