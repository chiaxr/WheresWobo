#pragma once

// The end-of-day draft: three cards, pick one.

#include <vector>

#include "raylib.h"

#include "gameplay/cards.h"

namespace wobo::ui {

struct CardLayout {
    std::vector<Rectangle> cards;
};

CardLayout layout_cards(int w, int h, float s, int count);
void draw_cards(const CardLayout& l, float s, int w, int h, int day_survived,
                const std::vector<const CardDef*>& offer, const CardDraftSystem& draft, Vector2 mouse,
                float time_s);

}  // namespace wobo::ui
