#pragma once

// Shared UI palette and immediate-mode drawing helpers. Layout is computed
// separately (see hud.h, menus.h) so the same rectangles drive both hit
// testing and drawing. Every size is multiplied by the UI scale `s`.

#include "raylib.h"

namespace wobo::ui {

// Night Shift: a dark room with one spotlight on the board. The action and
// card colours (energy, danger, belief, good, cyan, outline) are shared with
// the action bar and the draft cards and stay put.
inline constexpr Color kPanel{18, 23, 41, 235};
inline constexpr Color kPanelEdge{42, 49, 82, 255};
inline constexpr Color kRaised{28, 33, 56, 255};     // buttons and wells on a panel
inline constexpr Color kInk{238, 240, 250, 255};
inline constexpr Color kMuted{141, 147, 179, 255};
inline constexpr Color kShadow{4, 5, 12, 150};
inline constexpr Color kEnergy{255, 210, 63, 255};
inline constexpr Color kDanger{255, 90, 95, 255};
inline constexpr Color kBelief{236, 72, 153, 255};
inline constexpr Color kGood{6, 214, 160, 255};
inline constexpr Color kCyan{76, 201, 240, 255};
inline constexpr Color kGummy{176, 132, 255, 255};     // Gummy Wheels' button
inline constexpr Color kRoadBlock{170, 220, 60, 255};  // Road Block's button
inline constexpr Color kOverload{196, 214, 255, 255};  // Overload's button: an electric white-blue
inline constexpr Color kOutline{30, 28, 40, 255};
inline constexpr Color kBackground{12, 15, 29, 255};
inline constexpr Color kSpot{42, 51, 88, 255};       // a lifted navy (the poster's glow)

bool contains(Rectangle r, Vector2 p);
Rectangle inset(Rectangle r, float d);

void panel(Rectangle r, float s, Color fill = kPanel, Color edge = kPanelEdge, float radius = 12.0f);
// A rounded meter; `frac` in 0..1.
void meter(Rectangle r, float frac, Color fill, float s, Color back = kBackground);

enum class Look { Normal, Primary, Danger, Disabled, Toggled };
// A chunky cartoon button. Hit testing is the caller's; returns the raised
// face (which moves when hovered) so callers can draw extra content on it.
Rectangle button(Rectangle r, const char* label, float s, Look look, bool hovered, float font_size = 0.0f);
// The colour content on a button face uses for a given look.
Color button_ink(Look look);

enum class Icon { Pause, Play, FastForward, Menu };
// Draw an icon centred on `centre`, `size` pixels across.
void icon(Icon which, Vector2 centre, float size, Color colour);
Rectangle icon_button(Rectangle r, Icon which, float s, Look look, bool hovered);
// A tick box with its label to the right; the whole row `r` is the hit area.
void checkbox(Rectangle r, const char* label, bool checked, float s, bool hovered);
// Small rounded label.
void pill(Rectangle r, const char* label, Color fill, Color ink, float s, float font_size);
// A rounded-rectangle outline drawn only partly: it grows from the top centre
// in both directions at once, and at t = 1 the two ends meet at the bottom
// centre, closing the loop.
void growing_outline(Rectangle r, float radius, float t, float thickness, Color colour);
// Dim the whole screen.
void dim(int w, int h, unsigned char alpha = 150);
// Darken everything except an ellipse round `c`: clear inside `r_clear`,
// fading to `dark` at `r_dark` and beyond. Radii are horizontal; `squash`
// scales them vertically (so a circle on the iso floor stays one).
void spotlight(Vector2 c, float r_clear, float r_dark, float squash, Color dark, int w, int h);

}  // namespace wobo::ui
