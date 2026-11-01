#pragma once

// The catalogue of things a map can place off the grid. Each kind has a
// default material and a solid footprint (zero or more shapes) that beams
// and collision see; how it looks is up to the renderer, which draws the
// same kinds. Kinds with no footprint are decoration only: a blackboard's
// writing, a painting's frame, a bridge's railings. Whatever Wobo's
// scanner should notice about them is in the grid's materials instead.
//
// Every size is in metres; a prop's `yaw` turns its own x axis (its width)
// the Pose2D way.

#include <string>
#include <vector>

#include "world/map2d.h"

namespace wobo {

struct PropKind {
    const char* kind;
    const char* material;  // default material id (`mat=` overrides it)
    bool solid;            // false: decoration only
};

// The kind, or null if unknown.
const PropKind* find_prop_kind(const std::string& kind);

// The solid footprint of `prop` (with its material resolved). Tables use
// `chair_material` for the chairs round them.
std::vector<Shape> prop_footprint(const Prop& prop, std::uint8_t chair_material);

// Where the chairs round a table stand (seat centres, in order round the
// table), shared with the renderer so plates and forks line up with them.
std::vector<Vec2> table_seats(const Prop& table);

// How tall the prop stands (for baked shadows), from its kind and params.
float prop_height(const Prop& prop);

}  // namespace wobo
