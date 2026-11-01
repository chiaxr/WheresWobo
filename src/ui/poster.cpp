#include "ui/poster.h"

#include <algorithm>

#include "ui/menus.h"
#include "ui/ui_text.h"
#include "ui/widgets.h"

namespace wobo::ui {

void draw_poster_text(Rectangle area, float s) {
    const float big = std::min(area.height * 0.26f, area.width * 0.9f / measure("WOBO?", 1.0f, Weight::Bold));
    const float tag = std::min(34.0f * s, big * 0.24f);
    const char* lines[] = {"You've kidnapped the robot.", "Keep it lost."};

    // Centre the stack vertically.
    const float wordmark_h = big * 0.3f * 0.95f + big * 1.02f;
    const float stack = wordmark_h + tag * 1.3f * 2.0f;
    float y = area.y + std::max(0.0f, (area.height - stack) * 0.5f);
    y = wordmark(area.x, y, big, s, 0.0f);
    for (const char* line : lines) {
        text(line, area.x + big * 0.03f, y, tag, kInk, Weight::Bold);
        y += tag * 1.3f;
    }
}

}  // namespace wobo::ui
