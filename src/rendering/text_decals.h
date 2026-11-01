#pragma once

// Writing on the map: blackboards, signs, the cart's "Property of Buridan".
// Each piece of writing is rendered once, in a handwritten (chalk) face, into
// a texture, then drawn every frame as a quad on its prop. Text is purely for
// the player: Wobo's scanner reads colour and range, never words.

#include <vector>

#include "raylib.h"
#include "world/map2d.h"

namespace wobo {

class TextDecals {
public:
    // Needs the GL context. Rebuilding replaces any previous decals.
    void build(const Map2D& map);
    void unload();
    // Inside 3D mode, after the map (depth test on).
    void draw() const;

private:
    struct Decal {
        Texture2D texture{};
        Vector3 corners[4];  // left-bottom, right-bottom, right-top, left-top
    };
    std::vector<Decal> decals_;
};

}  // namespace wobo
