#include "rendering/text_decals.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "core/embedded.h"
#include "rendering/prop_models.h"
#include "rlgl.h"

extern const wobo::EmbeddedFile wobo_font_files[];
extern const std::size_t wobo_font_files_count;

namespace wobo {

namespace {

constexpr float kPixelsPerMetre = 420.0f;
constexpr int kFontSize = 96;

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t bar = text.find('|', start);
        out.push_back(text.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    return out;
}

// The chalk face with just the characters in `texts`.
Font load_chalk(const std::vector<std::string>& texts) {
    std::vector<int> codepoints;
    for (int c = 32; c < 127; ++c) codepoints.push_back(c);
    for (const std::string& t : texts) {
        int count = 0;
        int* cps = LoadCodepoints(t.c_str(), &count);
        for (int i = 0; i < count; ++i) {
            if (cps[i] > 126 && std::find(codepoints.begin(), codepoints.end(), cps[i]) == codepoints.end()) {
                codepoints.push_back(cps[i]);
            }
        }
        UnloadCodepoints(cps);
    }
    for (std::size_t i = 0; i < wobo_font_files_count; ++i) {
        if (std::strcmp(wobo_font_files[i].path, "chalk_subset.ttf") == 0) {
            return LoadFontFromMemory(".ttf", wobo_font_files[i].data, static_cast<int>(wobo_font_files[i].size),
                                      kFontSize, codepoints.data(), static_cast<int>(codepoints.size()));
        }
    }
    return GetFontDefault();
}

}  // namespace

void TextDecals::build(const Map2D& map) {
    unload();
    std::vector<std::string> texts;
    for (const Prop& p : map.props()) {
        TextPlate plate;
        if (text_plate(p, &plate)) texts.push_back(p.get("text"));
    }
    if (texts.empty()) return;
    const Font font = load_chalk(texts);

    for (const Prop& p : map.props()) {
        TextPlate plate;
        if (!text_plate(p, &plate)) continue;
        const int w = static_cast<int>(plate.width * kPixelsPerMetre);
        const int h = static_cast<int>(plate.height * kPixelsPerMetre);
        Image img = GenImageColor(w, h, plate.ground);
        // Lines share the height; each is as big as fits, all the same size.
        const auto lines = lines_of(p.get("text"));
        float size = 0.82f * static_cast<float>(h) / static_cast<float>(lines.size());
        for (const std::string& line : lines) {
            const Vector2 m = MeasureTextEx(font, line.c_str(), size, 0.0f);
            if (m.x > 0.94f * static_cast<float>(w)) size *= 0.94f * static_cast<float>(w) / m.x;
        }
        const float line_h = size * 1.12f;
        float y = 0.5f * (static_cast<float>(h) - line_h * static_cast<float>(lines.size()));
        for (const std::string& line : lines) {
            const Vector2 m = MeasureTextEx(font, line.c_str(), size, 0.0f);
            // Written by hand: start at the left, not centred.
            const float x = lines.size() > 1 ? 0.04f * static_cast<float>(w) : 0.5f * (static_cast<float>(w) - m.x);
            ImageDrawTextEx(&img, font, line.c_str(), {x, y + 0.06f * size}, size, 0.0f, plate.ink);
            y += line_h;
        }
        Decal d;
        d.texture = LoadTextureFromImage(img);
        GenTextureMipmaps(&d.texture);
        SetTextureFilter(d.texture, TEXTURE_FILTER_TRILINEAR);
        UnloadImage(img);

        // The plate faces along its yaw; "right" as seen by someone facing it.
        const Vector3 n{std::cos(plate.yaw), 0.0f, std::sin(plate.yaw)};
        const Vector3 r{0.5f * plate.width * n.z, 0.0f, -0.5f * plate.width * n.x};
        const float hh = 0.5f * plate.height;
        // Up the plate: straight up, leaning back by its tilt.
        const Vector3 u{-hh * std::sin(plate.tilt) * n.x, hh * std::cos(plate.tilt), -hh * std::sin(plate.tilt) * n.z};
        const Vector3 c = plate.centre;
        d.corners[0] = {c.x - r.x - u.x, c.y - u.y, c.z - r.z - u.z};
        d.corners[1] = {c.x + r.x - u.x, c.y - u.y, c.z + r.z - u.z};
        d.corners[2] = {c.x + r.x + u.x, c.y + u.y, c.z + r.z + u.z};
        d.corners[3] = {c.x - r.x + u.x, c.y + u.y, c.z - r.z + u.z};
        decals_.push_back(d);
    }
    if (font.texture.id != GetFontDefault().texture.id) UnloadFont(font);
}

void TextDecals::unload() {
    for (Decal& d : decals_) UnloadTexture(d.texture);
    decals_.clear();
}

void TextDecals::draw() const {
    for (const Decal& d : decals_) {
        rlSetTexture(d.texture.id);
        rlBegin(RL_QUADS);
        rlColor4ub(255, 255, 255, 255);
        rlNormal3f(0.0f, 1.0f, 0.0f);
        const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        for (int i = 0; i < 4; ++i) {
            rlTexCoord2f(uv[i][0], uv[i][1]);
            rlVertex3f(d.corners[i].x, d.corners[i].y, d.corners[i].z);
        }
        rlEnd();
        rlSetTexture(0);
    }
}

}  // namespace wobo
