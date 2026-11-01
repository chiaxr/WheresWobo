#include "app/input_router.h"

#include <cmath>

#include "app/click_capture.h"

namespace wobo {

namespace {
float dist(Vector2 a, Vector2 b) { return std::hypot(a.x - b.x, a.y - b.y); }
}  // namespace

PointerFrame InputRouter::poll(float threshold) {
    PointerFrame f;
    f.pos = GetMousePosition();

    // Two fingers: pinch zoom, and the gesture never counts as a tap.
    const int touches = GetTouchPointCount();
    if (touches >= 2) {
        const Vector2 a = GetTouchPosition(0);
        const Vector2 b = GetTouchPosition(1);
        const float d = dist(a, b);
        if (pinch_dist_ > 0.0f && d > 1.0f) {
            f.zoom = pinch_dist_ / d;
            f.zoom_centre = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
        }
        pinch_dist_ = d;
        gesture_multi_ = true;
    } else {
        pinch_dist_ = 0.0f;
    }
    f.multi_touch = gesture_multi_;

    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        f.zoom *= std::pow(0.88f, wheel);
        f.zoom_centre = f.pos;
    }

    // Primary pointer (left mouse or first touch).
    const bool down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if (down && !primary_down_) {
        f.pressed = true;
        press_pos_ = f.pos;
        last_pos_ = f.pos;
        dragged_ = false;
        if (touches <= 1) gesture_multi_ = touches >= 2;
    }
    if (down && primary_down_) {
        if (!dragged_ && dist(f.pos, press_pos_) > threshold) dragged_ = true;
        if (dragged_ && !gesture_multi_) {
            f.drag_delta = {f.pos.x - last_pos_.x, f.pos.y - last_pos_.y};
        }
        last_pos_ = f.pos;
    }
    if (!down && primary_down_) {
        f.released = true;
        f.tap = !dragged_ && !gesture_multi_;
    }
    if (f.pressed) ++seen_presses_;

    // A whole click that started and ended between two frames: raylib never
    // saw the button down, but the capture counted the press. Replay it as a
    // tap at the position it ended.
    const int captured = click_capture::presses();
    if (captured >= 0) {
        if (captured > seen_presses_ && !down && !primary_down_ && touches < 2) {
            f.pressed = f.released = f.tap = true;
        }
        seen_presses_ = captured;
        // Where the button went down / came up, as the event reported it.
        // raylib's cursor only moves on mousemove, so a click that arrives
        // without one (a tap, a jump of the pointer) would otherwise land
        // wherever the cursor last was.
        if (f.pressed || f.released) {
            f.pos = click_capture::last_position();
            if (f.pressed) press_pos_ = f.pos;
        }
    }

    primary_down_ = down;
    f.down = down;
    f.dragging = down && dragged_ && !gesture_multi_;
    f.press_pos = press_pos_;
    if (!down && touches == 0) gesture_multi_ = false;

    // Secondary pointer: right or middle mouse pans, a plain right click cancels.
    const bool sdown = IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
    if (sdown && !secondary_down_) {
        secondary_press_ = f.pos;
        secondary_last_ = f.pos;
        secondary_dragged_ = false;
    }
    if (sdown && secondary_down_) {
        if (dist(f.pos, secondary_press_) > threshold) secondary_dragged_ = true;
        if (secondary_dragged_) f.pan_delta = {f.pos.x - secondary_last_.x, f.pos.y - secondary_last_.y};
        secondary_last_ = f.pos;
    }
    if (!sdown && secondary_down_ && !secondary_dragged_) f.cancel = true;
    secondary_down_ = sdown;

    for (int k = GetKeyPressed(); k != 0; k = GetKeyPressed()) f.keys.push_back(k);
    return f;
}

}  // namespace wobo
