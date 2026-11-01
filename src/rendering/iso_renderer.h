#pragma once

// Draws the 3D presentation of the 2D map: extruded blocks for solid cells,
// water, a floor with baked hard-edged shadows, a plinth under the whole
// board, flat dark bands and pattern marks that give the blocks drawn
// outlines and colour-independent identities, and the map's props (with
// inverted-hull outlines) and writing. Built once per map as static meshes.

#include "raylib.h"

#include "rendering/text_decals.h"
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
    Mesh hull_{};     // props' outlines (flat, front faces culled)
    TextDecals decals_;
    ::Material material_{};  // raylib material (not the map's wobo::Material)
    bool built_ = false;
};

}  // namespace wobo
