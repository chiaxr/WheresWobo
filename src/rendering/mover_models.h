#pragma once

// How the movers look: Buridan's donkey, the gallery guard, and the
// townsperson walking Königsberg's bridges. Cartoon figures from primitives
// with inverted-hull outlines, animated from what the mover is doing.

#include "core/types.h"
#include "rendering/toon_shader.h"
#include "simulation/movers.h"

namespace wobo {

// Draws `m` at `pose` (interpolated between simulation steps); `time_s`
// drives idle animation.
void draw_mover(const ToonShader& shader, const Mover& m, const Pose2D& pose, float time_s);

}  // namespace wobo
