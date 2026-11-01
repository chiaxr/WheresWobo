#pragma once

// The text half of the README / social-preview poster (`WheresWobo --poster`):
// the wordmark and its tagline, centred vertically in `area`.

#include "raylib.h"

namespace wobo::ui {

void draw_poster_text(Rectangle area, float s);

}  // namespace wobo::ui
