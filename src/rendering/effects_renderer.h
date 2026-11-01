#pragma once

// Visual feedback for player interventions: one-off bursts (teleport poofs,
// floating text) and the ongoing look of each active effect on Wobo, so the
// player can always see what is interfering with which sensor.

#include <string>
#include <vector>

#include "raylib.h"

#include "core/types.h"
#include "gameplay/run_director.h"
#include "rendering/robot_model.h"
#include "rendering/toon_shader.h"

namespace wobo {

// The Kidnap crane: a hook drops from the sky, grabs Wobo, carries it to the
// destination and lets go. Purely presentational: the simulation teleports
// instantly, and the game holds the simulation while the crane works so the
// scene stays consistent.
class CraneAnimation {
public:
    void start(const Pose2D& from, const Pose2D& to);
    // Advances real time; returns true on the frame Wobo touches down.
    bool update(float dt_s);
    bool active() const { return active_; }
    // True until Wobo is back on the ground and released.
    bool holding() const;

    struct Frame {
        Vec2 hook;          // hook position over the ground
        float hook_y;       // height of the hook block
        float open;         // claw opening, 0 closed .. 1 open
        Pose2D wobo;        // where to draw Wobo
        float wobo_lift;    // and how high
        bool carrying;      // Wobo is in the claw
    };
    Frame frame() const;
    void draw() const;

private:
    bool active_ = false;
    float t_ = 0.0f;
    bool landed_ = false;
    Pose2D from_;
    Pose2D to_;
};

// Road Block: a striped barricade on a chunky base, filling its cells (which
// is what the LiDAR sees). `grow` 0..1 raises it out of the floor; a ghost is
// the translucent placement preview, tinted by `tint`.
void draw_road_block(const RoadBlock& block, const Map2D& map, float alpha, float grow, bool ghost,
                     Color tint = WHITE);

// Magnet: a red horseshoe magnet with steel tips on a stand, its open end
// facing `facing` (radians). `grow` 0..1 raises it out of the floor; a ghost
// is the translucent placement preview, tinted by `tint`.
void draw_magnet(Vec2 at, float facing, float alpha, float grow, bool ghost, Color tint = WHITE);

class EffectsRenderer {
public:
    void clear() {
        bursts_.clear();
        texts_.clear();
        shards_.clear();
    }
    void poof(Vec2 at, Color colour);
    void float_text(Vec2 at, std::string text, Color colour);
    // The capture ring breaking apart when a lock-on slips.
    void shatter(Vec2 at, float radius);
    void update(float dt_s);

    void draw_bursts() const;
    // Ongoing effects of active actions around Wobo at its rendered pose
    // (Smoke Bomb's cloud; the other abilities show on Wobo itself).
    void draw_active(const RunDirector& run, const Pose2D& wobo, float time_s) const;
    // Floating text, after EndMode3D.
    void draw_texts(const Camera3D& camera, float ui_scale) const;

private:
    struct Burst {
        Vec2 at;
        Color colour;
        float t = 0.0f;
        float life = 0.9f;
    };
    struct FloatText {
        Vec2 at;
        std::string text;
        Color colour;
        float t = 0.0f;
        float life = 1.6f;
    };
    struct Shard {
        Vec2 at;        // where it started on the ring
        float angle;    // outward direction
        float spin;
        float t = 0.0f;
        float life = 0.8f;
    };
    std::vector<Burst> bursts_;
    std::vector<Shard> shards_;
    std::vector<FloatText> texts_;
};

}  // namespace wobo
