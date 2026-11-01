#pragma once

// Turns raw mouse, touch and wheel input into the gestures the game uses:
// taps (clicks), primary drags, secondary-button pans, and zoom from the
// wheel or a two-finger pinch -- plus the keys pressed this frame. Clicks and
// key taps that begin and end between two frames are recovered rather than
// lost (see click_capture.h).

#include <algorithm>
#include <vector>

#include "raylib.h"

namespace wobo {

struct PointerFrame {
    Vector2 pos{};
    bool pressed = false;      // primary went down this frame
    bool down = false;
    bool released = false;     // primary went up this frame
    bool tap = false;          // released without having dragged
    Vector2 press_pos{};
    bool dragging = false;     // primary has moved past the tap threshold
    Vector2 drag_delta{};      // primary movement this frame, once dragging
    Vector2 pan_delta{};       // secondary / middle drag this frame
    bool cancel = false;       // secondary click without drag
    float zoom = 1.0f;         // multiply the view size by this
    Vector2 zoom_centre{};
    bool multi_touch = false;  // a pinch is (or was, this gesture) in progress
    std::vector<int> keys;     // keys pressed since last frame (from raylib's queue)

    // Pressed this frame. Uses the queue as well as raylib's per-frame state,
    // so a key tapped and released between two frames still counts.
    bool key(int k) const { return IsKeyPressed(k) || std::find(keys.begin(), keys.end(), k) != keys.end(); }
};

class InputRouter {
public:
    PointerFrame poll(float drag_threshold_px);

private:
    bool primary_down_ = false;
    bool dragged_ = false;
    Vector2 press_pos_{};
    Vector2 last_pos_{};
    bool secondary_down_ = false;
    bool secondary_dragged_ = false;
    Vector2 secondary_press_{};
    Vector2 secondary_last_{};
    float pinch_dist_ = 0.0f;
    bool gesture_multi_ = false;
    int seen_presses_ = 0;  // presses accounted for, vs click_capture
};

}  // namespace wobo
