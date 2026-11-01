#include "gameplay/map_traits.h"

#include <algorithm>

namespace wobo {

std::vector<int> label_floor(const Map2D& map, const std::vector<int>& blocked) {
    const int w = map.width();
    const int h = map.height();
    std::vector<int> label(static_cast<std::size_t>(w * h), -1);
    std::vector<char> open(label.size(), 0);
    for (int cz = 0; cz < h; ++cz) {
        for (int cx = 0; cx < w; ++cx) open[map.index(cx, cz)] = map.solid(cx, cz) ? 0 : 1;
    }
    for (int i : blocked) {
        if (i >= 0 && i < w * h) open[i] = 0;
    }
    std::vector<int> stack;
    int next = 0;
    for (int start = 0; start < w * h; ++start) {
        if (!open[start] || label[start] >= 0) continue;
        label[start] = next;
        stack.push_back(start);
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            const int cx = i % w;
            const int cz = i / w;
            const int nbrs[4][2] = {{cx + 1, cz}, {cx - 1, cz}, {cx, cz + 1}, {cx, cz - 1}};
            for (const auto& n : nbrs) {
                if (!map.in_bounds(n[0], n[1])) continue;
                const int j = map.index(n[0], n[1]);
                if (!open[j] || label[j] >= 0) continue;
                label[j] = next;
                stack.push_back(j);
            }
        }
        ++next;
    }
    return label;
}

bool stays_connected(const Map2D& map, const std::vector<int>& labels, const std::vector<int>& blocked) {
    const std::vector<int> after = label_floor(map, blocked);
    // Each old component must map onto exactly one new one.
    std::vector<int> seen;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (labels[i] < 0 || after[i] < 0) continue;
        const auto old = static_cast<std::size_t>(labels[i]);
        if (seen.size() <= old) seen.resize(old + 1, -1);
        if (seen[old] < 0) seen[old] = after[i];
        else if (seen[old] != after[i]) return false;
    }
    return true;
}

bool barrier_fits_somewhere(const Map2D& map, const std::vector<int>& labels, int length) {
    const int half = length / 2;
    std::vector<int> cells;
    for (int cz = 0; cz < map.height(); ++cz) {
        for (int cx = 0; cx < map.width(); ++cx) {
            for (bool along_x : {true, false}) {
                cells.clear();
                for (int k = -half; k < length - half; ++k) {
                    const int x = along_x ? cx + k : cx;
                    const int z = along_x ? cz : cz + k;
                    if (map.solid(x, z)) break;
                    cells.push_back(map.index(x, z));
                }
                if (static_cast<int>(cells.size()) == length && stays_connected(map, labels, cells)) return true;
            }
        }
    }
    return false;
}

float colourful_wall_share(const Map2D& map) {
    int faces = 0;
    int colourful = 0;
    for (int cz = 0; cz < map.height(); ++cz) {
        for (int cx = 0; cx < map.width(); ++cx) {
            if (map.solid(cx, cz)) continue;
            const int nbrs[4][2] = {{cx + 1, cz}, {cx - 1, cz}, {cx, cz + 1}, {cx, cz - 1}};
            for (const auto& n : nbrs) {
                if (!map.solid(n[0], n[1])) continue;
                const Rgb c = map.material_at(n[0], n[1]).rgb;
                const int hi = std::max({c.r, c.g, c.b});
                const int lo = std::min({c.r, c.g, c.b});
                // Saturation: plaster and slate greys sit near 0.1-0.2,
                // painted walls and crates above 0.4.
                const float sat = hi > 0 ? static_cast<float>(hi - lo) / hi : 0.0f;
                ++faces;
                if (sat >= 0.3f) ++colourful;
            }
        }
    }
    return faces > 0 ? static_cast<float>(colourful) / faces : 0.0f;
}

}  // namespace wobo
