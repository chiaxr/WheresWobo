#pragma once

// How each kind of prop in world/props.h looks: built from boxes, cylinders
// and spheres in the props' own frames and baked, with their outlines, into
// the map's static meshes. Writing (a blackboard's, a sign's) is not here:
// see rendering/text_decals.h.

#include "rendering/mesh_builder.h"
#include "world/map2d.h"

namespace wobo {

// Adds every prop on `map`: `lit` gets the cel-shaded solids, `hull` their
// inflated copies for the outline (draw with front faces culled), `marks`
// flat details (windows, frames, twine).
void build_props(const Map2D& map, MeshBuilder& lit, MeshBuilder& hull, MeshBuilder& marks);

// Where a decoration that carries writing puts it: the centre of the text
// area, the direction it faces, its width and height (metres). False for
// props without writing.
struct TextPlate {
    Vector3 centre;
    float yaw;  // the plate faces along this heading
    float width;
    float height;
    Color ink;
    Color ground;  // background, or transparent to draw straight onto the prop
    float tilt = 0.0f;  // radians its top leans back from upright (pi/2: lying face up)
};
bool text_plate(const Prop& prop, TextPlate* out);

}  // namespace wobo
