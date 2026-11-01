#pragma once

// Where else on a map looks like here by shape alone, for the player's side:
// Repainting Works repaints what Wobo's scanner sees so that its
// surroundings read, colour for colour, like a shape-alike place elsewhere,
// and its card is only offered on maps that have such places differing in
// colour. Built from the known map, once per run. (Wobo's own look-alike
// search, in planning/goal_navigator.h, compares materials too: a place that
// differs in colour is no twin to Wobo, which is what the repaint exploits.)

#include <cstdint>
#include <vector>

#include "world/map2d.h"

namespace wobo {

class LookAlikes {
public:
    static constexpr int kBeams = 32;

    // Views from the centre of every cell Wobo fits in (clearance >= radius).
    LookAlikes(const Map2D& map, float max_range, float radius);

    struct Twin {
        int cell = -1;
        int shift = 0;         // the twin's view, turned by shift beams, matches
        float cost = 0.0f;     // mean range difference, as a fraction of max_range
        int colour_beams = 0;  // beams that hit surfaces of clearly different colour
    };

    // The cell nearest to `p` with a view, or -1.
    int nearest_cell(Vec2 p) const;
    // Cells at least `min_distance` from `cell` whose view matches its view
    // in shape under some turn, best first, at most `max` of them.
    std::vector<Twin> twins(int cell, float min_distance, int max) const;
    // Of the twins that differ in colour on at least `min_colour_beams`
    // beams, the one that differs on most, if there is one.
    bool colour_twin(int cell, float min_distance, int min_colour_beams, Twin* out) const;
    // A material remap (index -> index whose colour the scanner reports)
    // that makes the view from `cell` read in the twin's colours. Materials
    // the twin agrees on keep their own colour.
    std::vector<std::uint8_t> repaint(int cell, const Twin& twin) const;
    // Of `samples` cells spread over the map, the fraction with a colour twin.
    float colour_twin_share(float min_distance, int min_colour_beams, int samples) const;

    int cells() const { return static_cast<int>(cells_.size()); }

private:
    float match_cost(int a, int b, int shift, float give_up) const;

    const Map2D* map_;
    float max_range_;
    std::vector<int> cells_;          // slot -> cell index
    std::vector<int> slot_;           // cell index -> slot, -1 for none
    std::vector<float> ranges_;       // [slot][beam]
    std::vector<float> sorted_;       // [slot][beam], ascending: a lower bound for any turn
    std::vector<int> materials_;      // [slot][beam], -1 for no hit
};

}  // namespace wobo
