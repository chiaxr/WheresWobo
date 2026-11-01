#pragma once

// Facts about a map that decide which player actions make sense on it.

#include <vector>

#include "world/map2d.h"

namespace wobo {

// 4-connected components of the floor (cells Wobo can stand in), treating
// `blocked` cells as solid too. Solid cells are labelled -1. Wobo fits in
// any floor cell and can always cross into a side neighbour, while two
// cells that only touch at a corner leave it no gap, so 4-connectivity is
// exactly where it can drive.
std::vector<int> label_floor(const Map2D& map, const std::vector<int>& blocked = {});

// True if, after blocking `blocked`, every floor cell that shared a component
// in `labels` (from label_floor without them) still can reach the others.
bool stays_connected(const Map2D& map, const std::vector<int>& labels, const std::vector<int>& blocked);

// Whether a straight barrier `length` cells long fits anywhere, along x or z,
// on floor only and without breaking any component in `labels` apart.
bool barrier_fits_somewhere(const Map2D& map, const std::vector<int>& labels, int length);

// Of the wall faces Wobo's scanner can see (solid cell sides facing floor),
// the fraction that are clearly coloured rather than grey. Colour Blind
// only does something to those.
float colourful_wall_share(const Map2D& map);

}  // namespace wobo
