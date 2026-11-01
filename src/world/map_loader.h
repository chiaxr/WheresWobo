#pragma once

// Loads maps from text. Each map lives in its own folder, data/maps/<id>/,
// as map.txt (room for assets of its own beside it). A map file is a config
// header (name, description, cell size), a [legend] mapping characters to
// material ids, optional [material.<id>] sections that add to (or replace
// entries in) the shared palette, data/maps/materials.cfg, and three raw
// sections:
//
//   [grid]    rows of legend characters; row 0 is the smallest z.
//   [props]   one per line: `kind x z key=value ...` (metres; yaw= in
//             degrees; mat= a material id; values may be "quoted"). Kinds
//             and their footprints are in world/props.h. They are part of
//             the known map.
//   [movers]  the same syntax: things that move about (a donkey, a guard).
//             They exist only in the physical world, never in the known map
//             Wobo localizes against; see simulation/movers.h.
//
//   name = Atrium
//   cell_size = 0.5
//   [legend]
//   hash = wall
//   . = floor
//   [props]
//   plant 1.25 1.25
//   [grid]
//   #####
//   #...#
//   #####

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "world/map2d.h"

namespace wobo {

std::vector<Material> parse_materials(std::string_view text, std::string* error);

// A mover as the map places it; simulation/movers.h brings it to life.
struct MoverSpec {
    std::string kind;
    Vec2 position;
    std::map<std::string, std::string> params;
};

// A map plus what lives in it but is not part of it.
struct Level {
    Map2D map;
    std::vector<MoverSpec> movers;
};

std::optional<Level> parse_level(std::string_view text, const std::vector<Material>& palette,
                                 std::string* error);
std::optional<Map2D> parse_map(std::string_view text, const std::vector<Material>& palette,
                               std::string* error);

// Loads "maps/<id>/map.txt" with the embedded palette.
std::optional<Level> load_level(const std::string& id, std::string* error = nullptr);
std::optional<Map2D> load_map(const std::string& id, std::string* error = nullptr);

struct MapInfo {
    std::string id;
    std::string name;
    std::string description;
    std::string difficulty;  // for the player: how quickly Wobo tends to find itself
    int order = 0;
    // A spot worth a close look (metres), for screenshots: `focus = x, z`.
    Vec2 focus;
    bool has_focus = false;
};

// Every map in data/maps/, in menu order.
std::vector<MapInfo> list_maps();

}  // namespace wobo
