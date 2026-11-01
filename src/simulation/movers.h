#pragma once

// Things that move about the physical world on their own: a gallery guard on
// patrol, a townsperson trying to cross every bridge of Königsberg exactly
// once, Buridan's donkey. They are discs to the LiDAR and to collision (the
// RunDirector puts them into the world map each step) but never part of the
// known map Wobo localizes against, so to Wobo they are surprises.
//
// Movers walk A* paths over the world grid, give way to Wobo rather than
// push it, and re-plan round a Road Block. Everything is deterministic: one
// seeded stream, fixed steps.
//
// Behaviours, by kind (map file params):
//   guard   route=x,z;x,z;...  walks the loop, pausing at each stop.
//   walker  bridges=A,B,ax,az,bx,bz;...  land=A  crosses a bridge it has not
//           crossed yet from the land it is on, until none is left; then
//           shrugs and starts again. On Königsberg's graph it never finishes.
//   donkey  hay=x,z;x,z  rest=x,z  wander=x,z;...  grazes about, and now and
//           then stops at the rest spot exactly between two identical piles
//           of hay, torn, before it picks one.

#include <cstdint>
#include <string>
#include <vector>

#include "core/rng.h"
#include "core/types.h"
#include "world/map2d.h"
#include "world/map_loader.h"

namespace wobo {

enum class MoverMood : std::uint8_t {
    Walking,
    Pausing,   // at a stop, looking about
    Waiting,   // giving way to Wobo
    Eating,    // the donkey, at a pile
    Torn,      // the donkey, between the piles
    Shrugging, // the walker, out of bridges
};

struct Mover {
    std::string kind;
    Pose2D pose;
    Pose2D prev_pose;
    float radius = 0.2f;
    float speed_mps = 0.35f;
    std::uint8_t material = 0;  // in the world map, for the LiDAR's colour
    MoverMood mood = MoverMood::Pausing;
    float mood_s = 0.0f;        // time in the current mood
    float stride_m = 0.0f;      // distance walked, for the gait

    // Behaviour state.
    std::vector<Vec2> path;     // waypoints still to go
    Vec2 goal;
    bool has_goal = false;
    float wait_s = 0.0f;        // how long the current mood should last
    int stop = 0;               // guard: index into route
    int land = 0;               // walker: the land it stands on
    int bridge = -1;            // walker: the bridge it is heading over
    bool crossing = false;      // walker: on its way over `bridge`
    std::vector<char> crossed;  // walker: per bridge
    int best_crossings = 0;     // walker: most bridges in one attempt
    int stage = 0;              // donkey: 0 wander (or eat), 1 to rest, 2 torn, 3 to hay
    int pile = 0;               // donkey: which pile it chose

    // From the map file.
    std::vector<Vec2> route;
    struct Bridge { int a, b; Vec2 end_a, end_b; };
    std::vector<Bridge> bridges;
    std::vector<Vec2> hay;
    Vec2 rest;
    std::vector<Vec2> wander;
};

class MoverSystem {
public:
    // `materials` gives each spec's material index in the world map.
    void reset(const std::vector<MoverSpec>& specs, const std::vector<std::uint8_t>& materials,
               std::uint64_t seed);

    // Advance one fixed step. `world` must not contain the movers themselves.
    void step(float dt, const Map2D& world, const Pose2D& wobo, float wobo_radius);

    const std::vector<Mover>& movers() const { return movers_; }
    // The movers as discs, for Map2D::set_movers.
    std::vector<Shape> discs() const;
    // True if a disc at p of radius r would overlap any mover.
    bool overlaps(Vec2 p, float r) const;

private:
    void think(Mover& m, const Map2D& world);
    void walk(Mover& m, float dt, const Map2D& world, const Pose2D& wobo, float wobo_radius);
    bool plan(Mover& m, const Map2D& world, Vec2 goal);
    void set_mood(Mover& m, MoverMood mood, float duration);

    std::vector<Mover> movers_;
    Rng rng_{1};
};

// The default colour the LiDAR sees on a mover of this kind.
Rgb mover_colour(const std::string& kind);
// Its radius.
float mover_radius(const std::string& kind);

}  // namespace wobo
