#pragma once

// Draws the 3D presentation of the 2D map: extruded blocks for solid cells,
// a floor with baked hard-edged shadows, a plinth under the whole board, and
// flat dark bands and pattern marks that give the blocks drawn outlines and
// colour-independent identities. Built once per map as static meshes.

#include "raylib.h"

#include "rendering/toon_shader.h"
#include "world/map2d.h"

namespace wobo {

class IsoRenderer {
public:
    // Needs the GL context. Rebuilding replaces any previous meshes.
    void build(const Map2D& map, const ToonShader& shader);
    void unload();

    void draw(const ToonShader& shader) const;

private:
    Mesh lit_{};      // blocks, floor, plinth (cel-shaded)
    Mesh marks_{};    // outline bands and patterns (flat)
    ::Material material_{};  // raylib material (not the map's wobo::Material)
    bool built_ = false;
};

}  // namespace wobo
