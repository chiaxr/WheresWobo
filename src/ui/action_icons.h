#pragma once

// Drawn icons for the player's abilities, used on the action bar: a smoke
// bomb, a dizzy robot, a blindfolded and tied-up robot hanging from a crane
// hook, and "RGB" over a swap arrow. Built from raylib shapes so they stay
// sharp at any UI scale.

#include "raylib.h"

#include "gameplay/actions.h"

namespace wobo::ui {

// `size` is the icon's width and height in pixels; `time_s` animates sparks.
void action_icon(ActionId id, Vector2 centre, float size, float time_s);

}  // namespace wobo::ui
