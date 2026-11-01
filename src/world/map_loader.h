#pragma once

// Loads grid maps from text. A map file is a config header (name,
// description, cell size, and a [legend] mapping characters to material ids),
// then a [grid] section of rows. Row 0 is the smallest z. Materials come from
// a shared palette file (data/maps/materials.cfg) so every map uses the same
// limited set of colours.
//
//   name = Atrium
//   cell_size = 0.5
//   [legend]
//   # = wall_gray
//   . = floor
//   [grid]
//   #####
//   #...#
//   #####

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "world/map2d.h"

namespace wobo {

std::vector<Material> parse_materials(std::string_view text, std::string* error);

std::optional<Map2D> parse_map(std::string_view text, const std::vector<Material>& palette,
                               std::string* error);

// Loads "maps/<id>.txt" with the embedded palette.
std::optional<Map2D> load_map(const std::string& id, std::string* error = nullptr);

struct MapInfo {
    std::string id;
    std::string name;
    std::string description;
    std::string difficulty;  // for the player: how quickly Wobo tends to find itself
    int order = 0;
};

// Every map in data/maps/, in menu order.
std::vector<MapInfo> list_maps();

}  // namespace wobo
