#pragma once

// Text drawing from embedded DejaVu Sans faces (regular and bold), rasterised
// once at a size that covers the UI, so text stays smooth at every scale
// instead of being a blown-up bitmap font.

#include <string>
#include <vector>

#include "raylib.h"

namespace wobo::ui {

enum class Weight { Regular, Bold };

void load_fonts();
void unload_fonts();
const Font& font(Weight w = Weight::Regular);

void text(const char* s, float x, float y, float size, Color color, Weight w = Weight::Regular);
float measure(const char* s, float size, Weight w = Weight::Regular);
// Text centred horizontally on cx.
void text_centered(const char* s, float cx, float y, float size, Color color, Weight w = Weight::Regular);
// Greedy word wrap into lines no wider than max_width.
std::vector<std::string> wrap(const std::string& s, float size, float max_width, Weight w = Weight::Regular);
// Draws wrapped text; returns the height used.
float text_wrapped(const std::string& s, float x, float y, float size, float max_width, Color color,
                   Weight w = Weight::Regular, float line_spacing = 1.25f);

}  // namespace wobo::ui
