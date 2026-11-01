#include "gameplay/run_director.h"

#include <algorithm>
#include <cmath>

#include "core/data.h"
#include "localization/localizer_registry.h"
#include "planning/planner_registry.h"
#include "world/map_loader.h"

namespace wobo {

Config load_game_config() {
    const auto text = data::read("configs/game.cfg");
    return text ? Config::parse(*text) : Config{};
}

RunDirector::RunDirector(Config config) : config_(std::move(config)) {}

bool RunDirector::start(const std::string& map_id, std::uint64_t seed, std::string* error) {
    auto map = load_map(map_id, error);
    if (!map) return false;

    auto localizer = make_localizer(config_.get_string("localizer.type", "particle_filter"), config_);
    auto planner = make_planner(config_.get_string("planner.type", "info_gain"), config_);
    if (!localizer || !planner) {
        if (error) *error = "unknown localizer or planner type in game.cfg";
        return false;
    }

    seed_ = seed;
    map_id_ = map_id;
    map_ = std::make_unique<Map2D>(std::move(*map));
    sim_config_ = SimConfig::from(config_);

    Rng start_rng(derive_seed(seed, "start"));
    const Pose2D start_pose = map_->sample_valid_pose(start_rng, sim_config_.dynamics.radius_m);
    sim_ = std::make_unique<WorldSimulation>(*map_, sim_config_, seed, start_pose);

    // The prior is what the robot knows: by default nothing (uniform).
    InitialBelief prior;
    if (config_.get_string("run.prior", "uniform") == "gaussian") {
        prior.kind = InitialBelief::Kind::Gaussian;
        prior.mean = start_pose;
        prior.sigma_position_m = config_.get_float("run.prior_sigma", 1.0f);
    }
    agent_ = std::make_unique<RobotAgent>(std::move(localizer), std::move(planner), AgentConfig::from(config_));
    agent_->reset(*map_, prior, seed);

    stats_.load(config_);
    energy_.reset(stats_);
    actions_.reset(derive_seed(seed, "actions"));
    cards_.reset(load_cards(), derive_seed(seed, "cards"));
    referee_ = LocalizationReferee(RefereeConfig::from(config_));

    command_ = {};
    modifiers_ = {};
    phase_ = RunPhase::Playing;
    offer_.clear();
    picked_.clear();
    events_.clear();
    hour_length_s_ = std::max(5.0f, config_.get_float("run.hour_length", 40.0f));
    hour_time_s_ = 0.0;
    hour_ = 1;
    kidnaps_ = 0;
    last_close_calls_ = 0;
    last_spins_ = 0;
    return true;
}

float RunDirector::danger() const {
    if (!agent_) return 0.0f;
    // Lock progress is the hard signal; a correct, confident estimate that
    // has not started locking yet is the soft one.
    const float lock = referee_.lock_progress();
    const float close = referee_.within_tolerance() ? 0.5f : 0.0f;
    return clampf(std::max(lock, close * agent_->estimate().confidence), 0.0f, 1.0f);
}

void RunDirector::step() {
    if (!sim_ || phase_ != RunPhase::Playing) return;
    const float dt = static_cast<float>(sim_config_.step_s);

    actions_.step(dt);
    const float regen_mult =
        1.0f + (actions_.active_count() > 0
                    ? stats_.get("passive.overclock") * stats_.get("passive.overclock_bonus", 0.5f)
                    : 0.0f);
    energy_.step(dt, stats_, regen_mult);
    modifiers_ = actions_.modifiers(stats_);

    // Calibration: early on Wobo's sensors are noisier than their rating (and
    // its localizer does not know it); each hour they settle a little more.
    const float k = calibration_noise();
    modifiers_.range_noise_add += k * config_.get_float("calibration.range_noise", 0.0f);
    modifiers_.colour_noise_add += k * config_.get_float("calibration.colour_noise", 0.0f);
    modifiers_.odom_noise_scale *= 1.0f + k * config_.get_float("calibration.odometry_noise", 0.0f);

    const StepResult result = sim_->step(command_, modifiers_);
    AgentInput input;
    input.odometry = result.odometry;
    input.scan = result.scan ? &*result.scan : nullptr;
    input.bumped = result.bumped;
    input.time_s = sim_->time_s();
    command_ = agent_->update(input);

    if (agent_->spins() != last_spins_) {
        last_spins_ = agent_->spins();
        push({GameEvent::Type::Spin, ActionId::Range, {}, {}, "Wobo noticed something is off"});
    }

    if (referee_.step(dt, sim_->true_pose(), agent_->estimate())) {
        phase_ = RunPhase::Over;
        push({GameEvent::Type::Localized, ActionId::Range, {}, {}, "Wobo found itself!"});
        return;
    }
    if (referee_.close_calls() != last_close_calls_) {
        last_close_calls_ = referee_.close_calls();
        push({GameEvent::Type::CloseCall, ActionId::Range, {}, {}, "Close call!"});
    }

    hour_time_s_ += sim_config_.step_s;
    if (hour_time_s_ >= hour_length_s_) {
        hour_time_s_ = 0.0;
        push({GameEvent::Type::HourEnded, ActionId::Range, {}, {}, "Hour " + std::to_string(hour_) + " survived"});
        ++hour_;
        begin_draft();
    }
}

void RunDirector::begin_draft() {
    offer_ = cards_.draw(3, hour_, [this](const std::string& key) {
        const auto id = action_from_key(key);
        return id && actions_.owned(*id, stats_);
    });
    if (!offer_.empty()) phase_ = RunPhase::Drafting;
}

float RunDirector::calibration_noise() const {
    return std::pow(clampf(config_.get_float("calibration.hourly_factor", 1.0f), 0.0f, 1.0f),
                    static_cast<float>(hour_ - 1));
}

ActivateResult RunDirector::can_activate(ActionId id) const {
    if (!sim_ || phase_ != RunPhase::Playing) return ActivateResult::Unavailable;
    return actions_.check(id, stats_, energy_, danger());
}

ActivateResult RunDirector::check_target(const Pose2D& target) const {
    if (!sim_) return ActivateResult::Unavailable;
    if (!map_->valid_pose(target, sim_config_.dynamics.radius_m)) return ActivateResult::InvalidTarget;
    const float min_d = stats_.get("action.kidnap.min_distance", 2.0f);
    if (distance(target.position(), sim_->true_pose().position()) < min_d) return ActivateResult::TooClose;
    return ActivateResult::Ok;
}

ActivateResult RunDirector::activate(ActionId id, std::optional<Pose2D> target) {
    if (!sim_ || phase_ != RunPhase::Playing) return ActivateResult::Unavailable;
    if (!owns(id)) return ActivateResult::NotOwned;
    if (action_info(id).targeted) {
        if (!target) return ActivateResult::InvalidTarget;
        const ActivateResult t = check_target(*target);
        if (t != ActivateResult::Ok) return t;
    }
    const ActivateResult r = actions_.activate(id, stats_, energy_, danger());
    if (r != ActivateResult::Ok) return r;

    GameEvent e{GameEvent::Type::ActionUsed, id, sim_->true_pose(), sim_->true_pose(), action_info(id).name};
    if (id == ActionId::Kidnap) {
        const Pose2D from = sim_->true_pose();
        sim_->teleport(*target);
        ++kidnaps_;
        push({GameEvent::Type::Teleported, id, from, *target, "Kidnapped!"});
        if (stats_.get("passive.aftershock") > 0.5f) {
            actions_.trigger_free(ActionId::Drift, stats_.get("passive.aftershock_duration", 4.0f), stats_);
        }
    }
    push(std::move(e));
    return ActivateResult::Ok;
}

void RunDirector::apply_card(const CardDef& card) {
    for (const std::string& effect : card.effects) {
        const auto parts = split(effect, ' ');
        if (parts.empty()) continue;
        const std::string& verb = parts[0];
        if (verb == "grant" && parts.size() >= 2) {
            stats_.set("action." + parts[1] + ".owned", 1.0f);
        } else if (verb == "stat" && parts.size() >= 3) {
            stats_.apply(parts[1], parts[2]);
        } else if (verb == "passive" && parts.size() >= 2) {
            stats_.add("passive." + parts[1], 1.0f);
        } else if (verb == "oneshot" && parts.size() >= 2) {
            if (parts[1] == "power_surge") {
                energy_.step(0.0f, stats_, 1.0f);  // pick up any capacity change first
                energy_.refill();
            } else if (parts[1] == "spare_kidnap") {
                stats_.add("action.kidnap.free", 1.0f);
            } else if (parts[1] == "cold_snap") {
                actions_.reset_cooldowns();
            }
        }
    }
}

bool RunDirector::choose_card(int index) {
    if (phase_ != RunPhase::Drafting || index < 0 || index >= static_cast<int>(offer_.size())) return false;
    const CardDef& card = *offer_[index];
    cards_.record_pick(card);
    apply_card(card);
    picked_.push_back(card.id);
    push({GameEvent::Type::CardChosen, ActionId::Range, {}, {}, card.name});
    offer_.clear();
    phase_ = RunPhase::Playing;
    return true;
}

void RunDirector::skip_draft() {
    if (phase_ != RunPhase::Drafting) return;
    offer_.clear();
    phase_ = RunPhase::Playing;
}

std::vector<GameEvent> RunDirector::drain_events() {
    std::vector<GameEvent> out;
    out.swap(events_);
    return out;
}

RunScore RunDirector::score() const {
    RunScore s;
    if (!sim_) return s;
    s.survival_s = sim_->time_s();
    s.hour = hour_;
    s.actions_used = actions_.total_uses();
    s.kidnaps = kidnaps_;
    s.close_calls = referee_.close_calls();
    s.spins = agent_->spins();
    s.cards_taken = static_cast<int>(picked_.size());
    return s;
}

}  // namespace wobo
