#include "gameplay/look_alikes.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "world/raycast.h"

namespace wobo {

namespace {

// Two views match when their ranges differ by this fraction of the scanner's
// reach on average (as in GoalNavigator).
constexpr float kMatch = 0.06f;
// Surfaces this far apart in colour (colour_distance) read as different.
constexpr float kColourApart = 0.15f;

}  // namespace

LookAlikes::LookAlikes(const Map2D& map, float max_range, float radius) : map_(&map), max_range_(max_range) {
    slot_.assign(static_cast<std::size_t>(map.width() * map.height()), -1);
    for (int i : map.free_cells()) {
        if (map.cell_clearance(i) < radius) continue;
        slot_[i] = static_cast<int>(cells_.size());
        cells_.push_back(i);
    }
    const std::size_t n = cells_.size() * kBeams;
    ranges_.resize(n);
    materials_.resize(n);
    for (std::size_t s = 0; s < cells_.size(); ++s) {
        const Vec2 c = map.cell_center(cells_[s] % map.width(), cells_[s] / map.width());
        for (int k = 0; k < kBeams; ++k) {
            const RayHit hit = cast_ray(map, c.x, c.z, kTwoPi * k / kBeams, max_range);
            ranges_[s * kBeams + k] = hit.range;
            materials_[s * kBeams + k] = hit.hit ? hit.material : -1;
        }
    }
    sorted_ = ranges_;
    for (std::size_t s = 0; s < cells_.size(); ++s) {
        std::sort(sorted_.begin() + s * kBeams, sorted_.begin() + (s + 1) * kBeams);
    }
}

int LookAlikes::nearest_cell(Vec2 p) const {
    const int w = map_->width();
    const int cx = map_->cell_x(p.x);
    const int cz = map_->cell_z(p.z);
    int best = -1;
    float best_d = std::numeric_limits<float>::infinity();
    for (int r = 0; r <= 3 && best < 0; ++r) {
        for (int z = cz - r; z <= cz + r; ++z) {
            for (int x = cx - r; x <= cx + r; ++x) {
                if (x < 0 || z < 0 || x >= w || z >= map_->height() || slot_[z * w + x] < 0) continue;
                const float d = distance(map_->cell_center(x, z), p);
                if (d < best_d) {
                    best_d = d;
                    best = z * w + x;
                }
            }
        }
    }
    return best;
}

float LookAlikes::match_cost(int a, int b, int shift, float give_up) const {
    float cost = 0.0f;
    for (int k = 0; k < kBeams && cost < give_up; ++k) {
        cost += std::fabs(ranges_[a * kBeams + k] - ranges_[b * kBeams + (k + shift) % kBeams]);
    }
    return cost;
}

std::vector<LookAlikes::Twin> LookAlikes::twins(int cell, float min_distance, int max) const {
    std::vector<Twin> found;
    if (cell < 0 || slot_[cell] < 0) return found;
    const int a = slot_[cell];
    const int w = map_->width();
    const Vec2 at = map_->cell_center(cell % w, cell / w);
    const float limit = kMatch * kBeams * max_range_;
    for (int b = 0; b < static_cast<int>(cells_.size()); ++b) {
        const Vec2 c = map_->cell_center(cells_[b] % w, cells_[b] / w);
        if (distance(c, at) < min_distance) continue;
        // Sorted ranges differ least of any pairing, so no turn can beat them.
        float bound = 0.0f;
        for (int k = 0; k < kBeams && bound <= limit; ++k) {
            bound += std::fabs(sorted_[a * kBeams + k] - sorted_[b * kBeams + k]);
        }
        if (bound > limit) continue;
        Twin t;
        t.cell = cells_[b];
        float best = limit;
        for (int sh = 0; sh < kBeams; ++sh) {
            const float cost = match_cost(a, b, sh, best);
            if (cost < best) {
                best = cost;
                t.shift = sh;
            }
        }
        if (best >= limit) continue;
        t.cost = best / (kBeams * max_range_);
        for (int k = 0; k < kBeams; ++k) {
            const int ma = materials_[a * kBeams + k];
            const int mb = materials_[b * kBeams + (k + t.shift) % kBeams];
            if (ma >= 0 && mb >= 0 &&
                colour_distance(map_->material(static_cast<std::uint8_t>(ma)).rgb,
                                map_->material(static_cast<std::uint8_t>(mb)).rgb) > kColourApart) {
                ++t.colour_beams;
            }
        }
        found.push_back(t);
    }
    std::sort(found.begin(), found.end(), [](const Twin& x, const Twin& y) { return x.cost < y.cost; });
    // Keep them a metre apart: neighbouring cells are one place.
    std::vector<Twin> out;
    for (const Twin& t : found) {
        const Vec2 c = map_->cell_center(t.cell % w, t.cell / w);
        bool close = false;
        for (const Twin& o : out) close = close || distance(map_->cell_center(o.cell % w, o.cell / w), c) < 1.0f;
        if (!close) out.push_back(t);
        if (static_cast<int>(out.size()) >= max) break;
    }
    return out;
}

bool LookAlikes::colour_twin(int cell, float min_distance, int min_colour_beams, Twin* out) const {
    // The one that differs most in colour: the more of Wobo's view the
    // repaint changes, the less its true place fits.
    bool found = false;
    for (const Twin& t : twins(cell, min_distance, 16)) {
        if (t.colour_beams >= min_colour_beams && (!found || t.colour_beams > out->colour_beams)) {
            *out = t;
            found = true;
        }
    }
    return found;
}

std::vector<std::uint8_t> LookAlikes::repaint(int cell, const Twin& twin) const {
    const std::size_t n = map_->materials().size();
    std::vector<std::uint8_t> table(n);
    for (std::size_t i = 0; i < n; ++i) table[i] = static_cast<std::uint8_t>(i);
    if (cell < 0 || slot_[cell] < 0 || twin.cell < 0 || slot_[twin.cell] < 0) return table;
    const int a = slot_[cell];
    const int b = slot_[twin.cell];
    // Each beam votes for what its surface should look like: the twin's
    // surface, or itself where the two already agree in colour.
    std::vector<int> votes(n * n, 0);
    for (int k = 0; k < kBeams; ++k) {
        const int ma = materials_[a * kBeams + k];
        const int mb = materials_[b * kBeams + (k + twin.shift) % kBeams];
        if (ma < 0 || mb < 0) continue;
        const bool apart = colour_distance(map_->material(static_cast<std::uint8_t>(ma)).rgb,
                                           map_->material(static_cast<std::uint8_t>(mb)).rgb) > kColourApart;
        ++votes[static_cast<std::size_t>(ma) * n + static_cast<std::size_t>(apart ? mb : ma)];
    }
    for (std::size_t m = 0; m < n; ++m) {
        int best = 0;
        for (std::size_t to = 0; to < n; ++to) {
            if (votes[m * n + to] > best) {
                best = votes[m * n + to];
                table[m] = static_cast<std::uint8_t>(to);
            }
        }
    }
    return table;
}

bool LookAlikes::unique_nearby(Vec2 p, float radius, float min_distance) const {
    if (unique_distance_ != min_distance) {
        unique_.assign(cells_.size(), -1);
        unique_distance_ = min_distance;
    }
    const int w = map_->width();
    const int reach = static_cast<int>(std::ceil(radius / map_->cell_size()));
    const int cx = map_->cell_x(p.x);
    const int cz = map_->cell_z(p.z);
    for (int z = std::max(0, cz - reach); z <= std::min(map_->height() - 1, cz + reach); ++z) {
        for (int x = std::max(0, cx - reach); x <= std::min(w - 1, cx + reach); ++x) {
            const int slot = slot_[z * w + x];
            if (slot < 0 || distance(map_->cell_center(x, z), p) > radius) continue;
            if (unique_[slot] < 0) unique_[slot] = twins(z * w + x, min_distance, 1).empty() ? 1 : 0;
            if (unique_[slot] > 0) return true;
        }
    }
    return false;
}

float LookAlikes::colour_twin_share(float min_distance, int min_colour_beams, int samples) const {
    if (cells_.empty() || samples <= 0) return 0.0f;
    const int step = std::max(1, static_cast<int>(cells_.size()) / samples);
    int tried = 0, found = 0;
    Twin t;
    for (std::size_t s = 0; s < cells_.size(); s += static_cast<std::size_t>(step)) {
        ++tried;
        found += colour_twin(cells_[s], min_distance, min_colour_beams, &t) ? 1 : 0;
    }
    return static_cast<float>(found) / static_cast<float>(tried);
}

}  // namespace wobo
