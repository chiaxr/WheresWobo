#pragma once

// One run of the game, headless. Owns the map, the physical simulation,
// Wobo's software, and the player-side systems (energy, actions, cards,
// referee), and advances them all in lockstep, one fixed step at a time.
// The renderer and UI read from it and send it commands; tests and the
// benchmark drive it directly.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/config.h"
#include "gameplay/actions.h"
#include "gameplay/cards.h"
#include "gameplay/energy.h"
#include "gameplay/referee.h"
#include "gameplay/run_stats.h"
#include "robot/robot_agent.h"
#include "simulation/world_simulation.h"
#include "world/map2d.h"

namespace wobo {

enum class RunPhase { Playing, Drafting, Over };

struct GameEvent {
    enum class Type { ActionUsed, Teleported, DayEnded, CardChosen, CloseCall, Localized, Spin };
    Type type;
    ActionId action = ActionId::Range;
    Pose2D from;
    Pose2D to;
    std::string text;
};

struct RunScore {
    double survival_s = 0.0;
    int day = 1;             // current day (days survived = day - 1)
    int actions_used = 0;
    int kidnaps = 0;
    int close_calls = 0;
    int spins = 0;           // times Wobo noticed something was wrong
    int cards_taken = 0;
};

// Load the game configuration (data/configs/game.cfg).
Config load_game_config();

class RunDirector {
public:
    explicit RunDirector(Config config);

    // Starts a fresh run. Returns false (with a message) if the map or the
    // configured algorithms cannot be loaded.
    bool start(const std::string& map_id, std::uint64_t seed, std::string* error = nullptr);

    // Advance one fixed simulation step. Does nothing while drafting or over.
    void step();

    // Player commands.
    ActivateResult activate(ActionId id, std::optional<Pose2D> target = std::nullopt);
    ActivateResult can_activate(ActionId id) const;
    // Checks a kidnap destination without spending anything.
    ActivateResult check_target(const Pose2D& target) const;
    bool choose_card(int index);
    void skip_draft();
    // Kidnap with no cost or checks beyond validity (tools and tests).
    bool force_teleport(const Pose2D& target) { return sim_ && sim_->teleport(target); }

    std::vector<GameEvent> drain_events();

    RunPhase phase() const { return phase_; }
    bool started() const { return sim_ != nullptr; }
    double step_s() const { return sim_config_.step_s; }
    std::uint64_t seed() const { return seed_; }
    const std::string& map_id() const { return map_id_; }
    const Map2D& map() const { return *map_; }
    const WorldSimulation& sim() const { return *sim_; }
    const RobotAgent& agent() const { return *agent_; }
    const EnergySystem& energy() const { return energy_; }
    const PlayerActionSystem& actions() const { return actions_; }
    const RunStats& stats() const { return stats_; }
    const LocalizationReferee& referee() const { return referee_; }
    const CardDraftSystem& cards() const { return cards_; }
    const std::vector<const CardDef*>& offer() const { return offer_; }
    const std::vector<std::string>& picked_cards() const { return picked_; }
    const SensorModifiers& modifiers() const { return modifiers_; }
    const Config& config() const { return config_; }
    RunScore score() const;

    bool owns(ActionId id) const { return actions_.owned(id, stats_); }
    float action_cost(ActionId id) const { return actions_.cost(id, stats_, danger()); }
    // 0..1: how close Wobo is to being declared found. Drives Brinkmanship
    // and the HUD's danger meter.
    float danger() const;
    float day_progress() const { return static_cast<float>(day_time_s_ / day_length_s_); }
    // How far Wobo's sensors are from their rated noise today: 1 on day 1,
    // shrinking each day as it "calibrates". 0 means fully calibrated.
    float calibration_noise() const;
    double day_length_s() const { return day_length_s_; }

private:
    void push(GameEvent e) { events_.push_back(std::move(e)); }
    void begin_draft();
    void apply_card(const CardDef& card);

    Config config_;
    SimConfig sim_config_;
    std::uint64_t seed_ = 0;
    std::string map_id_;

    std::unique_ptr<Map2D> map_;
    std::unique_ptr<WorldSimulation> sim_;
    std::unique_ptr<RobotAgent> agent_;
    MotionCommand command_;
    SensorModifiers modifiers_;

    RunStats stats_;
    EnergySystem energy_;
    PlayerActionSystem actions_;
    CardDraftSystem cards_;
    LocalizationReferee referee_;

    RunPhase phase_ = RunPhase::Playing;
    std::vector<const CardDef*> offer_;
    std::vector<std::string> picked_;
    std::vector<GameEvent> events_;
    double day_length_s_ = 40.0;
    double day_time_s_ = 0.0;
    int day_ = 1;
    int kidnaps_ = 0;
    int last_close_calls_ = 0;
    int last_spins_ = 0;
};

}  // namespace wobo
