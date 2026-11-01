#pragma once

// Accumulates triangles (positions, normals, colours) for a static mesh built
// once per map: the board, its blocks and its props. Front faces wind
// counter-clockwise. Solids come in pairs for the toon look: the lit shape,
// and (optionally) an inflated copy for the inverted-hull outline, drawn with
// front faces culled.

#include <vector>

#include "raylib.h"

namespace wobo {

struct MeshBuilder {
    std::vector<float> verts;
    std::vector<float> normals;
    std::vector<unsigned char> colors;

    void vertex(Vector3 p, Vector3 n, Color c);
    void tri(Vector3 a, Vector3 b, Vector3 c, Vector3 n, Color col);
    // p0..p3 counter-clockwise seen from the side the normal points to.
    void quad(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, Vector3 n, Color c);
    // Axis-aligned rectangle on a horizontal plane.
    void flat_rect(float x0, float z0, float x1, float z1, float y, Color c);

    // Solids. `yaw` turns about the vertical axis the Pose2D way (from +x
    // towards +z). Boxes are centred on `c`.
    void box(Vector3 c, Vector3 size, float yaw, Color col);
    void cylinder(Vector3 a, Vector3 b, float r, Color col, int sides = 12, bool caps = true);
    void sphere(Vector3 c, Vector3 radii, Color col, int rings = 6, int slices = 10);
    // A gable roof: ridge along the local x axis, eaves at `c.y`.
    void roof(Vector3 c, float w, float d, float h, float yaw, Color col);
    // A four-sided pyramid (spire), base centred on `c`.
    void pyramid(Vector3 c, float w, float h, float yaw, Color col);

    bool empty() const { return verts.empty(); }
    Mesh upload();
};

}  // namespace wobo
