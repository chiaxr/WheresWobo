#pragma once

// Player interventions. Each action attacks one part of Wobo's estimation
// pipeline and nothing else: the LiDAR's reach (Smoke Bomb), the gyro's
// heading (Gyro Jam), the wheels' distance count (Gummy Wheels, which also
// slows Wobo down), the LiDAR (Colour Blind: its colours, and dead ahead;
// with the Repainting Works upgrade, sometimes the colours of a look-alike
// place instead -- see RunDirector), the world itself (Road Block:
// an obstacle that is not on Wobo's map), its onboard computer (Overload:
// a cap on the compute budget, applied by RunDirector) or -- Kidnap --
// the robot's true physical pose. Parameters come from RunStats, so cards
// can change them mid-run. Internal keys ("range", "drift", ...) name the
// data path, and stay stable while display names change.

#include <array>
#include <optional>
#include <string>

#include "core/rng.h"
#include "core/types.h"
#include "gameplay/energy.h"
#include "gameplay/run_stats.h"
#include "simulation/sensor_modifiers.h"

namespace wobo {

enum class ActionId : int { Range, Drift, Kidnap, Colour, Gummy, Block, Overload, Count };
inline constexpr int kActionCount = static_cast<int>(ActionId::Count);

struct ActionInfo {
    ActionId id;
    const char* key;          // stat key, e.g. "range" -> action.range.*
    const char* name;         // display name
    const char* stream;       // which data path it attacks, for the HUD
    const char* description;
    bool targeted;            // needs a spot picked on the map
    bool instant;             // no duration
};

const ActionInfo& action_info(ActionId id);
std::optional<ActionId> action_from_key(const std::string& key);

enum class ActivateResult { Ok, NotOwned, NoEnergy, Cooldown, InvalidTarget, TooClose, CutsOff, Unavailable };
const char* activate_result_text(ActivateResult r);

struct ActionState {
    float cooldown_left = 0.0f;
    float active_left = 0.0f;   // remaining effect duration
    float active_total = 0.0f;
    int uses = 0;
    float sign = 1.0f;          // random direction for biases, chosen on use
};

class PlayerActionSystem {
public:
    void reset(std::uint64_t seed);

    bool owned(ActionId id, const RunStats& stats) const;
    // Energy cost right now, after discounts. `danger` is 0..1, how close
    // Wobo is to finding itself (used by Brinkmanship).
    float cost(ActionId id, const RunStats& stats, float danger) const;

    // Checks everything except a target on the map (the run director
    // validates that), and on success spends energy and starts the effect.
    ActivateResult activate(ActionId id, RunStats& stats, EnergySystem& energy, float danger);
    ActivateResult check(ActionId id, const RunStats& stats, const EnergySystem& energy,
                         float danger) const;

    // Start an effect without paying or touching cooldowns (Aftershock).
    void trigger_free(ActionId id, float duration, const RunStats& stats);

    void step(float dt_s);
    void reset_cooldowns();

    // The measurement-path changes all active effects add up to.
    SensorModifiers modifiers(const RunStats& stats) const;
    // Gummy Wheels' physical slow-down: a multiplier on Wobo's forward speed.
    float speed_scale(const RunStats& stats) const;

    const ActionState& state(ActionId id) const { return states_[static_cast<int>(id)]; }
    bool active(ActionId id) const { return state(id).active_left > 0.0f; }
    int active_count() const;
    int total_uses() const;

private:
    void start_effect(ActionId id, float duration);

    std::array<ActionState, kActionCount> states_{};
    Rng rng_;
};

}  // namespace wobo
