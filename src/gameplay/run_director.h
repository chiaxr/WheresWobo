#pragma once

// One run of the game, headless. Owns the map, the physical simulation,
// Wobo's software, and the player-side systems (energy, actions, cards,
// referee), and advances them all in lockstep, one fixed step at a time.
// The renderer and UI read from it and send it commands; tests and the
// benchmark drive it directly.
//
// There are two copies of the map: the known map Wobo was given (and the
// renderer draws), and the physical world the simulation runs in. They are
// the same until the player drops a Road Block, which exists only in the
// world. The world also holds the map's movers (a donkey, a guard), which
// Wobo's known map never shows.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/config.h"
#include "gameplay/actions.h"
#include "gameplay/cards.h"
#include "gameplay/energy.h"
#include "gameplay/look_alikes.h"
#include "gameplay/referee.h"
#include "gameplay/run_stats.h"
#include "robot/robot_agent.h"
#include "simulation/movers.h"
#include "simulation/world_simulation.h"
#include "world/map2d.h"

namespace wobo {

enum class RunPhase { Playing, Drafting, Over };

struct GameEvent {
    enum class Type { ActionUsed, Teleported, HourEnded, CardChosen, CloseCall, Localized, Spin, BlockCleared, Repainted, DayEnded, MagnetCleared, Learned };
    Type type;
    ActionId action = ActionId::Range;
    Pose2D from;
    Pose2D to;
    std::string text;
};

struct RunScore {
    double survival_s = 0.0;
    int hour = 1;            // current hour (hours survived = hour - 1)
    bool survived = false;   // the run ended with the day, Wobo still lost
    int actions_used = 0;
    int kidnaps = 0;
    int close_calls = 0;
    int spins = 0;           // times Wobo noticed something was wrong
    int cards_taken = 0;
};

// Road Block: a barrier three cells long, lying along x or z.
struct RoadBlock {
    std::vector<int> cells;  // map cell indices (Map2D::index)
    bool along_x = true;
    Vec2 centre;
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
    // Checks a target without spending anything: a kidnap destination, where
    // a Road Block would go (`target.heading_rad` picks its axis), or where a
    // Magnet would stand (on the floor, within action.magnet.range of Wobo).
    ActivateResult check_target(ActionId id, const Pose2D& target) const;
    // The barrier a Road Block aimed at `target` would be.
    RoadBlock block_at(const Pose2D& target) const;
    // The natural axis for a Road Block at `p` (as a heading for block_at):
    // across the corridor, unless only the other way fits.
    float block_heading_at(Vec2 p) const;
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
    const Map2D& map() const { return *map_; }      // what Wobo knows
    const Map2D& world() const { return *world_; }  // what is physically there
    const std::optional<RoadBlock>& road_block() const { return block_; }
    // Where the Magnet stands while it pulls.
    const std::optional<Vec2>& magnet() const { return magnet_; }
    // Where a Magnet at `target` would leave Wobo if it stood still: as far
    // as the pull reaches in its duration, or against whatever is in the way.
    Vec2 magnet_stop(Vec2 target) const;
    const std::vector<Mover>& movers() const { return movers_.movers(); }
    // Whether a card can be offered on this map: Colour Blind needs colourful
    // walls ("colour"), Road Block somewhere it fits ("block"), and Colour
    // Blind's Repainting Works upgrade places of the same shape that differ
    // in colour ("repaint").
    bool suits_map(const std::string& action_key) const;
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
    // While a Colour Blind use has set Repainting Works off: the place (a
    // cell centre) whose colours Wobo's surroundings now read in.
    std::optional<Vec2> repaint_twin() const;
    const Config& config() const { return config_; }
    RunScore score() const;

    bool owns(ActionId id) const { return actions_.owned(id, stats_); }
    float action_cost(ActionId id) const { return actions_.cost(id, stats_, danger()); }
    // 0..1: how close Wobo is to being declared found. Drives Brinkmanship
    // and the HUD's danger meter.
    float danger() const;
    float hour_progress() const { return static_cast<float>(hour_time_s_ / hour_length_s_); }
    // How far Wobo's sensors are from their rated noise this hour: 1 in hour 1,
    // shrinking each hour as it "calibrates". 0 means fully calibrated.
    float calibration_noise() const;
    double hour_length_s() const { return hour_length_s_; }
    // Hours in the day ([run] hours): a run still going at the end of the
    // last one is the player's win. 0: the day never ends.
    int day_hours() const { return day_hours_; }
    // Wobo's compute right now ([compute] in game.cfg): the share of its
    // localizer's full budget, and its updates per second (0: every scan).
    // Both grow towards full as its computer warms up, hour by hour, and
    // drop to Overload's caps ([action.overload]) while it lasts.
    float compute_share() const;
    float compute_update_hz() const;
    // How sharply Wobo re-seeds a lost belief from the map's views this hour
    // ([learning] in game.cfg): 0 before `view_hour`, then growing towards
    // `view_final`. Negative when [learning] is off and the localizer's own
    // setting stands.
    float view_temperature() const;

private:
    void push(GameEvent e) { events_.push_back(std::move(e)); }
    void begin_draft();
    // InvalidTarget (off the floor), TooClose (on Wobo or a mover), CutsOff, or Ok.
    ActivateResult check_block(const RoadBlock& block) const;
    void place_block(const RoadBlock& block);
    void clear_block();
    void apply_card(const CardDef& card);
    // Repainting Works: roll its chance on a Colour Blind use, and if it
    // comes up and somewhere else is Wobo's shape, repaint.
    void maybe_repaint();
    void apply_compute_budget();

    Config config_;
    SimConfig sim_config_;
    std::uint64_t seed_ = 0;
    std::string map_id_;

    std::unique_ptr<Map2D> map_;
    std::unique_ptr<Map2D> world_;
    std::uint8_t block_material_ = 0;
    std::vector<int> floor_label_;  // 4-connected floor components of the known map, -1 for solid
    std::optional<RoadBlock> block_;
    std::optional<Vec2> magnet_;
    MoverSystem movers_;
    bool colour_matters_ = true;
    bool block_fits_ = true;
    std::unique_ptr<LookAlikes> look_alikes_;
    bool repaint_fits_ = true;
    std::vector<std::uint8_t> repaint_;  // the active repaint's material table, empty for none
    int repaint_twin_cell_ = -1;
    Rng repaint_rng_;
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
    double hour_length_s_ = 40.0;
    double hour_time_s_ = 0.0;
    int hour_ = 1;
    bool overloaded_ = false;  // the compute budget last applied had Overload's caps
    int day_hours_ = 0;
    bool survived_ = false;
    int kidnaps_ = 0;
    int last_close_calls_ = 0;
    int last_spins_ = 0;
};

}  // namespace wobo
