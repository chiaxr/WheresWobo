#pragma once

// Orthographic isometric camera (fixed 45-degree yaw, ~35-degree pitch) with
// smooth pan and zoom, plus a top-down debug view. Purely presentational:
// nothing in the simulation ever reads it.

#include "raylib.h"

#include "core/types.h"

namespace wobo {

class CameraRig {
public:
    // Frame the whole map (width x depth metres) in a viewport.
    void fit(float map_width, float map_depth, int screen_w, int screen_h);

    void update(float dt_s);

    // Pan by a screen-space delta in pixels (drag), zoom about a screen point.
    void pan_pixels(Vector2 delta, int screen_h);
    void zoom_at(float factor, Vector2 screen_point, int screen_w, int screen_h);
    void focus(Vec2 world_point);
    void set_top_down(bool top_down) { top_down_ = top_down; }
    bool top_down() const { return top_down_; }
    // Shift the framing so a band of the screen (e.g. under the HUD) is not
    // counted as usable: fractions of screen height at the top and bottom.
    void set_insets(float top, float bottom) { inset_top_ = top; inset_bottom_ = bottom; }

    const Camera3D& camera() const { return camera_; }
    float zoom() const { return zoom_; }

private:
    void rebuild();
    Vector3 screen_right() const;
    Vector3 screen_up_on_ground() const;

    Camera3D camera_{};
    Vec2 target_{};          // smoothed
    Vec2 target_goal_{};
    float fit_height_ = 10.0f;  // ortho view height that frames the map
    float zoom_ = 1.0f;
    float zoom_goal_ = 1.0f;
    float map_w_ = 10.0f;
    float map_d_ = 10.0f;
    bool top_down_ = false;
    float inset_top_ = 0.0f;
    float inset_bottom_ = 0.0f;
};

}  // namespace wobo
