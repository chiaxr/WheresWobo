#include "gameplay/energy.h"

#include <algorithm>

namespace wobo {

void EnergySystem::reset(const RunStats& stats) {
    capacity_ = std::max(1.0f, stats.get("energy.capacity", 100.0f));
    current_ = std::clamp(stats.get("energy.start", 50.0f), 0.0f, capacity_);
    regen_rate_ = stats.get("energy.regen", 3.0f);
}

void EnergySystem::step(float dt_s, const RunStats& stats, float regen_multiplier) {
    capacity_ = std::max(1.0f, stats.get("energy.capacity", 100.0f));
    regen_rate_ = std::max(0.0f, stats.get("energy.regen", 3.0f) * regen_multiplier);
    current_ = std::min(capacity_, current_ + regen_rate_ * dt_s);
}

bool EnergySystem::spend(float cost) {
    if (!can_afford(cost)) return false;
    current_ = std::max(0.0f, current_ - cost);
    return true;
}

void EnergySystem::add(float amount) { current_ = std::clamp(current_ + amount, 0.0f, capacity_); }

}  // namespace wobo
