#include "gameplay/run_director.h"

#include <algorithm>
#include <cmath>

#include "core/data.h"
#include "gameplay/map_traits.h"
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

    // The physical world starts as the known map, plus the road block's
    // material for when one is dropped.
    world_ = std::make_unique<Map2D>(*map_);
    block_ = std::nullopt;
    block_material_ = world_->add_material(
        Material{"road_block", "Road block", Rgb{255, 128, 32}, true, 0.5f, Pattern::Stripes});
    floor_label_ = label_floor(*map_);
    colour_matters_ = colourful_wall_share(*map_) >= config_.get_float("action.colour.min_colour_share", 0.1f);
    block_fits_ = barrier_fits_somewhere(*map_, floor_label_, 3);

    Rng start_rng(derive_seed(seed, "start"));
    const Pose2D start_pose = map_->sample_valid_pose(start_rng, sim_config_.dynamics.radius_m);
    sim_ = std::make_unique<WorldSimulation>(*world_, sim_config_, seed, start_pose);

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
    if (block_ && !actions_.active(ActionId::Block)) clear_block();
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

    // Gummy Wheels slows Wobo for real; its software asked for full speed.
    MotionCommand drive = command_;
    drive.linear_mps *= actions_.speed_scale(stats_);
    const StepResult result = sim_->step(drive, modifiers_);
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

bool RunDirector::suits_map(const std::string& action_key) const {
    if (action_key == "colour") return colour_matters_;
    if (action_key == "block") return block_fits_;
    return true;
}

void RunDirector::begin_draft() {
    offer_ = cards_.draw(
        3, hour_,
        [this](const std::string& key) {
            const auto id = action_from_key(key);
            return id && actions_.owned(*id, stats_);
        },
        [this](const std::string& key) { return suits_map(key); });
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

RoadBlock RunDirector::block_at(const Pose2D& target) const {
    RoadBlock b;
    b.along_x = std::fabs(std::cos(target.heading_rad)) >= std::fabs(std::sin(target.heading_rad));
    const int cx = map_->cell_x(target.x);
    const int cz = map_->cell_z(target.z);
    b.centre = map_->cell_center(cx, cz);
    for (int k = -1; k <= 1; ++k) {
        const int x = b.along_x ? cx + k : cx;
        const int z = b.along_x ? cz : cz + k;
        if (map_->in_bounds(x, z)) b.cells.push_back(map_->index(x, z));
    }
    return b;
}

float RunDirector::block_heading_at(Vec2 p) const {
    // Count open cells each way: a barrier lies across the shorter run.
    const int cx = map_->cell_x(p.x);
    const int cz = map_->cell_z(p.z);
    const auto run = [&](int dx, int dz) {
        int n = 0;
        for (int k = 1; k <= 6 && !map_->solid(cx + dx * k, cz + dz * k); ++k) ++n;
        return n;
    };
    const bool across_x = run(1, 0) + run(-1, 0) <= run(0, 1) + run(0, -1);
    const float preferred = across_x ? 0.0f : kPi * 0.5f;
    const float other = across_x ? kPi * 0.5f : 0.0f;
    if (sim_ && check_block(block_at({p.x, p.z, preferred})) != ActivateResult::Ok &&
        check_block(block_at({p.x, p.z, other})) == ActivateResult::Ok) {
        return other;
    }
    return preferred;
}

ActivateResult RunDirector::check_block(const RoadBlock& block) const {
    if (block.cells.size() != 3) return ActivateResult::InvalidTarget;
    const int w = map_->width();
    for (int i : block.cells) {
        if (map_->solid(i % w, i / w) || world_->solid(i % w, i / w)) return ActivateResult::InvalidTarget;
    }
    {
        // Not on top of Wobo, with a little room to spare.
        const Pose2D p = sim_->true_pose();
        const float r = sim_config_.dynamics.radius_m + 0.05f;
        const float cs = map_->cell_size();
        for (int i : block.cells) {
            const float minx = (i % w) * cs;
            const float minz = (i / w) * cs;
            const float dx = p.x - clampf(p.x, minx, minx + cs);
            const float dz = p.z - clampf(p.z, minz, minz + cs);
            if (dx * dx + dz * dz < r * r) return ActivateResult::TooClose;
        }
    }
    if (!stays_connected(*map_, floor_label_, block.cells)) return ActivateResult::CutsOff;
    return ActivateResult::Ok;
}

void RunDirector::place_block(const RoadBlock& block) {
    if (block_) clear_block();
    world_->paint(block.cells, block_material_);
    block_ = block;
}

void RunDirector::clear_block() {
    if (!block_) return;
    // Repaint each cell with what the known map has there.
    const int w = map_->width();
    for (int i : block_->cells) world_->paint({i}, map_->material_index(i % w, i / w));
    push({GameEvent::Type::BlockCleared, ActionId::Block, {block_->centre.x, block_->centre.z, 0.0f}, {}, ""});
    block_ = std::nullopt;
}

ActivateResult RunDirector::check_target(ActionId id, const Pose2D& target) const {
    if (!sim_) return ActivateResult::Unavailable;
    if (id == ActionId::Block) return check_block(block_at(target));
    if (!world_->valid_pose(target, sim_config_.dynamics.radius_m)) return ActivateResult::InvalidTarget;
    const float min_d = stats_.get("action.kidnap.min_distance", 2.0f);
    if (distance(target.position(), sim_->true_pose().position()) < min_d) return ActivateResult::TooClose;
    return ActivateResult::Ok;
}

ActivateResult RunDirector::activate(ActionId id, std::optional<Pose2D> target) {
    if (!sim_ || phase_ != RunPhase::Playing) return ActivateResult::Unavailable;
    if (!owns(id)) return ActivateResult::NotOwned;
    if (action_info(id).targeted) {
        if (!target) return ActivateResult::InvalidTarget;
        const ActivateResult t = check_target(id, *target);
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
    } else if (id == ActionId::Block) {
        place_block(block_at(*target));
        e.to = {block_->centre.x, block_->centre.z, 0.0f};
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
