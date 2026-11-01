#pragma once

// Cel shading for the 3D scene: one directional light, quantised into a few
// flat bands, so surfaces read as solid volumes while staying flat-coloured
// and cartoon-like. A "flat" mode draws unlit colour, for outlines, markings
// and effects.
//
// Geometry reaches the shader in world space -- rlgl transforms immediate-mode
// vertices and normals by the current matrix on the CPU, and the map meshes
// are built in world space -- so it needs only the batch's mvp. Every lit face
// must set a real normal (see primitives.h). Changing mode flushes the batch:
// rlgl draws recorded geometry later, with whatever uniforms are set then.
//
// If the shader fails to compile, every call is a no-op and the scene draws
// with raylib's default unlit shader: still playable, just flatter.

#include "raylib.h"

namespace wobo {

// World-space direction towards the light. The camera looks from +x +z, so
// a light slightly towards +x and away towards -z keeps the tops brightest,
// the +x faces mid-toned and the +z faces in shade: three readable tones on
// every block. Shadows fall towards -x +z (the lower left of the screen).
inline constexpr Vector3 kLightDir{0.36f, 0.80f, -0.48f};

class ToonShader {
public:
    void load();
    void unload();
    bool valid() const { return loaded_; }

    void begin() const;
    void end() const;
    // true: unlit flat colour. false: cel-shaded.
    void set_flat(bool flat) const;

    Shader shader() const { return shader_; }

private:
    Shader shader_{};
    bool loaded_ = false;
    int loc_light_dir_ = -1;
    int loc_flat_ = -1;
};

}  // namespace wobo
