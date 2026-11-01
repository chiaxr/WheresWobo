#include "ui/card_screen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui/ui_text.h"
#include "ui/widgets.h"

namespace wobo::ui {

namespace {

Color category_colour(CardCategory c) {
    switch (c) {
        case CardCategory::Ability: return kCyan;
        case CardCategory::Upgrade: return Color{255, 150, 50, 255};
        case CardCategory::Energy: return kEnergy;
        case CardCategory::Passive: return Color{170, 110, 240, 255};
        case CardCategory::Special: return kBelief;
    }
    return kInk;
}

// A colour-independent mark for each category.
const char* category_symbol(CardCategory c) {
    switch (c) {
        case CardCategory::Ability: return "+";
        case CardCategory::Upgrade: return "^";
        case CardCategory::Energy: return "E";
        case CardCategory::Passive: return "~";
        case CardCategory::Special: return "!";
    }
    return "";
}

}  // namespace

CardLayout layout_cards(int w, int h, float s, int count) {
    CardLayout l;
    float cw = 260.0f * s;
    float ch = 330.0f * s;
    const float gap = 22.0f * s;
    const float max_w = static_cast<float>(w) - 40.0f * s;
    if (count * cw + (count - 1) * gap > max_w) cw = (max_w - (count - 1) * gap) / count;
    ch = std::min(ch, static_cast<float>(h) - 170.0f * s);
    const float total = count * cw + (count - 1) * gap;
    float x = w * 0.5f - total * 0.5f;
    const float y = h * 0.5f - ch * 0.5f + 34.0f * s;
    for (int i = 0; i < count; ++i) {
        l.cards.push_back({x, y, cw, ch});
        x += cw + gap;
    }
    return l;
}

void draw_cards(const CardLayout& l, float s, int w, int h, int day_survived,
                const std::vector<const CardDef*>& offer, const CardDraftSystem& draft, Vector2 mouse,
                float time_s) {
    dim(w, h, 170);
    char title[64];
    std::snprintf(title, sizeof(title), "Day %d survived!", day_survived);
    const float top = l.cards.empty() ? h * 0.3f : l.cards[0].y;
    text_centered(title, w * 0.5f, top - 86.0f * s, 38.0f * s, kEnergy, Weight::Bold);
    text_centered("Wobo is still lost - but overnight its sensors recalibrate. Pick an upgrade.", w * 0.5f,
                  top - 42.0f * s, 18.0f * s,
                  kInk);

    for (std::size_t i = 0; i < offer.size() && i < l.cards.size(); ++i) {
        const CardDef& card = *offer[i];
        const bool hover = contains(l.cards[i], mouse);
        Rectangle r = l.cards[i];
        // Cards bob gently; the hovered one lifts.
        r.y += std::sin(time_s * 2.0f + i * 1.3f) * 3.0f * s - (hover ? 10.0f * s : 0.0f);
        const Color cat = category_colour(card.category);

        DrawRectangleRounded({r.x + 5 * s, r.y + 8 * s, r.width, r.height}, 0.08f, 8, kShadow);
        DrawRectangleRounded(r, 0.08f, 8, kOutline);
        const Rectangle in = inset(r, 3.0f * s);
        DrawRectangleRounded(in, 0.08f, 8, Color{250, 244, 232, 255});
        // Header band.
        const float hb = 64.0f * s;
        DrawRectangleRounded({in.x, in.y, in.width, hb}, 0.25f, 8, cat);
        DrawRectangle(static_cast<int>(in.x), static_cast<int>(in.y + hb * 0.5f), static_cast<int>(in.width),
                      static_cast<int>(hb * 0.5f), cat);
        DrawCircleV({in.x + 30.0f * s, in.y + hb * 0.5f}, 20.0f * s, kOutline);
        text_centered(category_symbol(card.category), in.x + 30.0f * s, in.y + hb * 0.5f - 15.0f * s, 28.0f * s,
                      cat, Weight::Bold);
        text(card_category_name(card.category), in.x + 58.0f * s, in.y + hb * 0.5f - 9.0f * s, 16.0f * s, kOutline,
             Weight::Bold);

        float y = in.y + hb + 14.0f * s;
        const float pad = 16.0f * s;
        y += text_wrapped(card.name, in.x + pad, y, 24.0f * s, in.width - 2 * pad, kOutline, Weight::Bold, 1.15f);
        y += 8.0f * s;
        text_wrapped(card.description, in.x + pad, y, 16.0f * s, in.width - 2 * pad, Color{70, 66, 90, 255},
                     Weight::Regular, 1.3f);

        // Footer: stacks and key hint.
        char foot[48];
        const int taken = draft.taken(card.id);
        if (card.max_stack > 1 && card.max_stack < 99) {
            std::snprintf(foot, sizeof(foot), "Taken %d / %d", taken, card.max_stack);
        } else if (card.max_stack >= 99) {
            std::snprintf(foot, sizeof(foot), "Taken %d", taken);
        } else {
            std::snprintf(foot, sizeof(foot), "Unique");
        }
        text(foot, in.x + pad, in.y + in.height - 30.0f * s, 14.0f * s, kMuted, Weight::Bold);
        char key[8];
        std::snprintf(key, sizeof(key), "%d", static_cast<int>(i + 1));
        pill({in.x + in.width - pad - 30.0f * s, in.y + in.height - 34.0f * s, 30.0f * s, 24.0f * s}, key, cat,
             kOutline, s, 15.0f * s);
        if (hover) DrawRectangleRoundedLinesEx(inset(r, -3.0f * s), 0.08f, 8, 4.0f * s, kEnergy);
    }
}

}  // namespace wobo::ui
