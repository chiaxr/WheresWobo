#include "gameplay/actions.h"

#include <algorithm>
#include <cmath>

namespace wobo {

namespace {

const ActionInfo kActions[kActionCount] = {
    {ActionId::Range, "range", "Smoke Bomb", "LiDAR range",
     "A cloud of smoke round Wobo: its scanner can only see a short way.", false, false},
    {ActionId::Drift, "drift", "Gyro Jam", "Gyro heading",
     "Spin Wobo round until it's dizzy: its gyro loses track of which way it is facing.", false, false},
    {ActionId::Kidnap, "kidnap", "Kidnap", "Physical position",
     "A crane carries Wobo somewhere else. Its software is not told.", true, true},
    {ActionId::Colour, "colour", "Colour Blind", "LiDAR colour",
     "Red-green shades: the colours Wobo's scanner reports are swapped round, and it can't see straight ahead.", false, false},
    {ActionId::Gummy, "gummy", "Gummy Wheels", "Wheel odometry",
     "Bubblegum on the wheels: Wobo crawls, and its wheels slip, so it thinks it has gone further than it has.",
     false, false},
    {ActionId::Block, "block", "Road Block", "The world",
     "Drop a barrier that isn't on Wobo's map. Its scans stop matching, and its route is cut. Every spot "
     "must stay reachable.",
     true, false},
    {ActionId::Overload, "overload", "Overload", "Onboard computer",
     "A power surge fries Wobo's circuits: for a while it thinks slowly, on a fraction of its compute, and "
     "forgets most of the places it was weighing up.",
     false, false},
};

std::string stat_key(ActionId id, const char* field) {
    return std::string("action.") + action_info(id).key + "." + field;
}

}  // namespace

const ActionInfo& action_info(ActionId id) { return kActions[static_cast<int>(id)]; }

std::optional<ActionId> action_from_key(const std::string& key) {
    for (const ActionInfo& a : kActions) {
        if (key == a.key) return a.id;
    }
    return std::nullopt;
}

const char* activate_result_text(ActivateResult r) {
    switch (r) {
        case ActivateResult::Ok: return "";
        case ActivateResult::NotOwned: return "You don't have that action yet";
        case ActivateResult::NoEnergy: return "Not enough energy";
        case ActivateResult::Cooldown: return "Still recharging";
        case ActivateResult::InvalidTarget: return "That spot won't do";
        case ActivateResult::TooClose: return "Too close - pick somewhere farther away";
        case ActivateResult::CutsOff: return "That would cut part of the map off";
        case ActivateResult::Unavailable: return "Not right now";
    }
    return "";
}

void PlayerActionSystem::reset(std::uint64_t seed) {
    states_ = {};
    rng_.reseed(seed);
}

bool PlayerActionSystem::owned(ActionId id, const RunStats& stats) const {
    return stats.get(stat_key(id, "owned")) > 0.5f;
}

float PlayerActionSystem::cost(ActionId id, const RunStats& stats, float danger) const {
    if (id == ActionId::Kidnap && stats.get("action.kidnap.free") > 0.5f) return 0.0f;
    float c = std::max(0.0f, stats.get(stat_key(id, "cost")));
    if (stats.get("passive.brinkmanship") > 0.5f &&
        danger >= stats.get("passive.brinkmanship_threshold", 0.4f)) {
        c *= 1.0f - stats.get("passive.brinkmanship_discount", 0.3f);
    }
    return std::round(c);
}

ActivateResult PlayerActionSystem::check(ActionId id, const RunStats& stats, const EnergySystem& energy,
                                         float danger) const {
    if (!owned(id, stats)) return ActivateResult::NotOwned;
    const bool free_kidnap = id == ActionId::Kidnap && stats.get("action.kidnap.free") > 0.5f;
    if (state(id).cooldown_left > 0.0f && !free_kidnap) return ActivateResult::Cooldown;
    if (!energy.can_afford(cost(id, stats, danger))) return ActivateResult::NoEnergy;
    return ActivateResult::Ok;
}

ActivateResult PlayerActionSystem::activate(ActionId id, RunStats& stats, EnergySystem& energy,
                                            float danger) {
    const ActivateResult r = check(id, stats, energy, danger);
    if (r != ActivateResult::Ok) return r;
    energy.spend(cost(id, stats, danger));
    if (id == ActionId::Kidnap && stats.get("action.kidnap.free") > 0.5f) {
        stats.add("action.kidnap.free", -1.0f);
    }

    ActionState& s = states_[static_cast<int>(id)];
    s.cooldown_left = std::max(0.0f, stats.get(stat_key(id, "cooldown")));
    ++s.uses;
    if (!action_info(id).instant) start_effect(id, stats.get(stat_key(id, "duration"), 5.0f));

    // Chain Reaction: every use shortens the other cooldowns.
    const float chain = stats.get("passive.chain_reaction") * stats.get("passive.chain_reaction_seconds", 2.0f);
    if (chain > 0.0f) {
        for (int i = 0; i < kActionCount; ++i) {
            if (i == static_cast<int>(id)) continue;
            states_[i].cooldown_left = std::max(0.0f, states_[i].cooldown_left - chain);
        }
    }
    return ActivateResult::Ok;
}

void PlayerActionSystem::start_effect(ActionId id, float duration) {
    ActionState& s = states_[static_cast<int>(id)];
    s.active_left = std::max(s.active_left, duration);
    s.active_total = s.active_left;
    s.sign = rng_.chance(0.5f) ? 1.0f : -1.0f;
}

void PlayerActionSystem::trigger_free(ActionId id, float duration, const RunStats&) {
    start_effect(id, duration);
}

void PlayerActionSystem::step(float dt_s) {
    // The cooldown runs once the effect is over, so no effect, however
    // lengthened by cards, can be kept up without a break.
    for (ActionState& s : states_) {
        const float idle = std::max(0.0f, dt_s - s.active_left);
        s.active_left = std::max(0.0f, s.active_left - dt_s);
        s.cooldown_left = std::max(0.0f, s.cooldown_left - idle);
    }
}

void PlayerActionSystem::reset_cooldowns() {
    for (ActionState& s : states_) s.cooldown_left = 0.0f;
}

int PlayerActionSystem::active_count() const {
    int n = 0;
    for (const ActionState& s : states_) n += s.active_left > 0.0f ? 1 : 0;
    return n;
}

int PlayerActionSystem::total_uses() const {
    int n = 0;
    for (const ActionState& s : states_) n += s.uses;
    return n;
}

float PlayerActionSystem::speed_scale(const RunStats& stats) const {
    return active(ActionId::Gummy) ? clampf(stats.get("action.gummy.speed", 0.5f), 0.1f, 1.0f) : 1.0f;
}

SensorModifiers PlayerActionSystem::modifiers(const RunStats& stats) const {
    SensorModifiers m;
    if (active(ActionId::Range)) {
        m.range_scale *= clampf(stats.get("action.range.scale", 0.35f), 0.05f, 1.0f);
        if (stats.get("passive.static_cling") > 0.5f) {
            m.range_noise_add += stats.get("passive.static_cling_noise", 0.15f);
        }
    }
    if (active(ActionId::Drift)) {
        // Heading only: the gyro over- or under-reads turns and drifts.
        const float sign = state(ActionId::Drift).sign;
        m.odom_turn_noise_scale *= std::max(1.0f, stats.get("action.drift.noise_scale", 4.0f));
        m.odom_turn_scale_bias += sign * stats.get("action.drift.turn_scale_bias", 0.3f);
        m.odom_turn_bias += sign * stats.get("action.drift.turn_bias", 0.35f);
        m.odom_heading_bias_rps += sign * stats.get("action.drift.heading_bias", 0.10f);
        if (stats.get("passive.vertigo") > 0.5f) {
            m.odom_heading_bias_rps += sign * stats.get("passive.vertigo_bias", 0.2f);
        }
    }
    if (active(ActionId::Gummy)) {
        // Distance only: the wheels slip in the gum, so they count more turns
        // than Wobo travels. (The slow-down is physical; see speed_scale.)
        m.odom_distance_noise_scale *= std::max(1.0f, stats.get("action.gummy.noise_scale", 4.0f));
        m.odom_scale_bias += stats.get("action.gummy.scale_bias", 0.5f);
    }
    if (active(ActionId::Colour)) {
        m.colour_desaturate = std::max(m.colour_desaturate, stats.get("action.colour.desaturate", 0.6f));
        m.colour_hue_shift = std::max(m.colour_hue_shift, stats.get("action.colour.hue_shift", 0.7f));
        m.colour_noise_add += stats.get("action.colour.noise", 20.0f);
        m.blind_cone_rad = std::max(m.blind_cone_rad, stats.get("action.colour.blind_cone", 0.0f) * kDegToRad);
        m.blind_chance = std::max(m.blind_chance, stats.get("action.colour.blind_chance", 1.0f));
    }
    return m;
}

}  // namespace wobo
