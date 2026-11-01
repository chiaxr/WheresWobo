#pragma once

// Wobo: a chunky, light-grey cartoon robot assembled from primitives, with
// thick dark outlines (inverted hulls), a face screen whose expression tracks
// how Wobo feels, spinning wheels, a spinning LiDAR puck and a springy
// antenna. Also draws translucent "ghost" copies for Wobo's estimated pose and
// the kidnap preview, and the props the player's abilities put on it.

#include "raylib.h"

#include "core/types.h"
#include "rendering/toon_shader.h"

namespace wobo {

enum class Face { Curious, Thinking, Hopeful, Confident, Confused, Squint, Blind, Dizzy, Surprised };

struct WoboStyle {
    float alpha = 1.0f;
    Color tint = WHITE;      // multiplied into body colours (ghosts)
    float tint_amount = 0.0f;
    bool outline = true;
    bool face = true;
    float lift = 0.0f;        // height off the ground (carried by the crane)
    bool sunglasses = false;  // Colour Blind: odd-coloured shades
    bool dizzy_stars = false; // Spin It: stars circling its head
};

class RobotModel {
public:
    // Animate from the robot's motion (true velocities, render time).
    void update(float dt_s, float linear_mps, float angular_rps, bool scanning);
    void kick();  // jolt the springs (teleport, bump)

    void draw(const ToonShader& shader, Vec2 position, float heading, Face face,
              const WoboStyle& style = {}) const;
    // A flat translucent silhouette drawn through walls. Call before draw():
    // the normal robot then covers the visible parts, leaving the silhouette
    // only where Wobo is hidden.
    void draw_xray(const ToonShader& shader, Vec2 position, float heading, Color colour, float lift = 0.0f) const;

private:
    void draw_parts(Vec2 position, float heading, Face face, const WoboStyle& style, bool hull) const;
    void draw_face(Face face, const WoboStyle& style) const;
    void draw_sunglasses(const WoboStyle& style) const;
    float head_tilt(Face face) const;  // degrees

    float time_ = 0.0f;
    float wheel_angle_ = 0.0f;
    float lidar_angle_ = 0.0f;
    float bob_ = 0.0f;
    float antenna_ = 0.0f;      // sway angle
    float antenna_v_ = 0.0f;
    float lean_ = 0.0f;         // forward lean with acceleration
    float last_speed_ = 0.0f;
    float turn_ = 0.0f;         // smoothed angular velocity, for eye glances
};

}  // namespace wobo
