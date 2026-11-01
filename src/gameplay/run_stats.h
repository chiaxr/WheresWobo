#pragma once

// The numbers a run plays by: action costs, durations and strengths, energy
// capacity and regeneration, passive stacks. Seeded from game.cfg and then
// modified by drafted cards, so every effect reads its parameters from here
// at the moment it is used.

#include <map>
#include <string>

#include "core/config.h"

namespace wobo {

class RunStats {
public:
    // Copies every numeric value under action.*, energy.* and passive.*.
    void load(const Config& cfg);

    float get(const std::string& key, float fallback = 0.0f) const;
    void set(const std::string& key, float value) { values_[key] = value; }
    void add(const std::string& key, float delta) { values_[key] = get(key) + delta; }

    // Applies "<op><value>" (op is + * or =). Returns false if malformed.
    bool apply(const std::string& key, const std::string& op_value);

    const std::map<std::string, float>& values() const { return values_; }

private:
    std::map<std::string, float> values_;
};

}  // namespace wobo
