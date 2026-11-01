#pragma once

// The player's single energy pool: capacity and continuous regeneration in
// simulation time.

#include "gameplay/run_stats.h"

namespace wobo {

class EnergySystem {
public:
    void reset(const RunStats& stats);

    // `regen_multiplier` carries passive bonuses.
    void step(float dt_s, const RunStats& stats, float regen_multiplier);

    float current() const { return current_; }
    float capacity() const { return capacity_; }
    float regen_rate() const { return regen_rate_; }  // last effective rate, per second
    bool can_afford(float cost) const { return current_ + 1e-4f >= cost; }
    bool spend(float cost);
    void refill() { current_ = capacity_; }
    void add(float amount);

private:
    float current_ = 0.0f;
    float capacity_ = 100.0f;
    float regen_rate_ = 0.0f;
};

}  // namespace wobo
