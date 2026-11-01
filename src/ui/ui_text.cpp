#include "ui/ui_text.h"

#include <cstring>

#include "core/embedded.h"

extern const wobo::EmbeddedFile wobo_font_files[];
extern const std::size_t wobo_font_files_count;

namespace wobo::ui {

namespace {

// Rasterised once at a size that covers the largest UI text, so everything
// is a downscale (which stays sharp).
constexpr int kAtlasSize = 72;

Font g_regular{};
Font g_bold{};
bool g_loaded = false;

Font load(const char* name) {
    for (std::size_t i = 0; i < wobo_font_files_count; ++i) {
        if (std::strcmp(wobo_font_files[i].path, name) == 0) {
            Font f = LoadFontFromMemory(".ttf", wobo_font_files[i].data,
                                        static_cast<int>(wobo_font_files[i].size), kAtlasSize, nullptr, 0);
            SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
            return f;
        }
    }
    return GetFontDefault();
}

}  // namespace

void load_fonts() {
    if (g_loaded) return;
    g_regular = load("dejavu_sans_subset.ttf");
    g_bold = load("dejavu_sans_bold_subset.ttf");
    g_loaded = true;
}

void unload_fonts() {
    if (!g_loaded) return;
    if (g_regular.texture.id != GetFontDefault().texture.id) UnloadFont(g_regular);
    if (g_bold.texture.id != GetFontDefault().texture.id) UnloadFont(g_bold);
    g_loaded = false;
}

const Font& font(Weight w) { return w == Weight::Bold ? g_bold : g_regular; }

void text(const char* s, float x, float y, float size, Color color, Weight w) {
    DrawTextEx(font(w), s, Vector2{x, y}, size, size * 0.02f, color);
}

float measure(const char* s, float size, Weight w) { return MeasureTextEx(font(w), s, size, size * 0.02f).x; }

void text_centered(const char* s, float cx, float y, float size, Color color, Weight w) {
    text(s, cx - measure(s, size, w) * 0.5f, y, size, color, w);
}

std::vector<std::string> wrap(const std::string& s, float size, float max_width, Weight w) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t i = 0;
    while (i <= s.size()) {
        std::size_t j = s.find_first_of(" \n", i);
        if (j == std::string::npos) j = s.size();
        const std::string word = s.substr(i, j - i);
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && measure(candidate.c_str(), size, w) > max_width) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        if (j < s.size() && s[j] == '\n') {
            lines.push_back(line);
            line.clear();
        }
        i = j + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

float text_wrapped(const std::string& s, float x, float y, float size, float max_width, Color color, Weight w,
                   float line_spacing) {
    const auto lines = wrap(s, size, max_width, w);
    float yy = y;
    for (const std::string& l : lines) {
        text(l.c_str(), x, yy, size, color, w);
        yy += size * line_spacing;
    }
    return yy - y;
}

}  // namespace wobo::ui
