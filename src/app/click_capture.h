#pragma once

// raylib samples the mouse button once per frame, so a click whose press and
// release both land between two frames -- a trackpad tap, a quick touch, an
// automated click -- is never seen as "down". This counts presses as they
// happen (a chained GLFW callback on desktop, a pointer listener on the web)
// so the input router can recover clicks it would otherwise drop.

#include "raylib.h"

namespace wobo::click_capture {

void install();  // after InitWindow
// Primary presses seen so far, or -1 if capture is unavailable.
int presses();
// Where the most recent primary press ended (screen pixels, as GetMousePosition).
Vector2 last_position();

}  // namespace wobo::click_capture
