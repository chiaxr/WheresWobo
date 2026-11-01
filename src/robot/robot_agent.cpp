#include "robot/robot_agent.h"

#include <cmath>

namespace wobo {

const char* mood_name(RobotMood mood) {
    switch (mood) {
        case RobotMood::Curious: return "Curious";
        case RobotMood::Thinking: return "Thinking";
        case RobotMood::Hopeful: return "Hopeful";
        case RobotMood::Confident: return "Confident";
        case RobotMood::Confused: return "Confused";
        case RobotMood::ShortSighted: return "Squinting";
        case RobotMood::Blind: return "Blind";
    }
    return "";
}

AgentConfig AgentConfig::from(const Config& cfg) {
    AgentConfig c;
    c.radius_m = cfg.get_float("robot.radius", c.radius_m);
    c.max_speed_mps = cfg.get_float("robot.max_speed", c.max_speed_mps);
    c.max_turn_rps = cfg.get_float("robot.max_turn_rate", c.max_turn_rps);
    c.nominal_max_range_m = cfg.get_float("sensor.max_range", c.nominal_max_range_m);
    c.front_stop_m = cfg.get_float("agent.front_stop", c.front_stop_m);
    c.confusion_threshold = cfg.get_float("agent.confusion_threshold", c.confusion_threshold);
    c.confusion_cooldown_s = cfg.get_float("agent.confusion_cooldown", c.confusion_cooldown_s);
    c.cautious_speed = cfg.get_float("agent.cautious_speed", c.cautious_speed);
    c.boot_s = cfg.get_float("agent.boot", c.boot_s);
    return c;
}

RobotAgent::RobotAgent(std::unique_ptr<ILocalizer> localizer, std::unique_ptr<IActivePlanner> planner,
                       const AgentConfig& config)
    : localizer_(std::move(localizer)), planner_(std::move(planner)), config_(config) {}

void RobotAgent::reset(const Map2D& known_map, const InitialBelief& prior, std::uint64_t seed) {
    map_ = &known_map;
    localizer_->reset(known_map, prior, derive_seed(seed, "localizer"));
    planner_->reset(derive_seed(seed, "planner"));
    pending_odometry_ = {};
    last_update_s_ = -1e9;
    odom_pose_ = {};
    last_scan_.reset();
    blind_ = false;
    phase_ = Phase::Idle;
    plan_ = {};
    plan_.reason = "Waking up";
    turned_ = driven_ = pause_left_ = 0.0f;
    last_spin_s_ = -1e9;
    confused_until_s_ = -1.0;
    spins_ = 0;
    mood_ = RobotMood::Curious;
}

void RobotAgent::start_plan(const MotionPlan& plan) {
    plan_ = plan;
    turned_ = 0.0f;
    driven_ = 0.0f;
    pause_left_ = plan.pause_s;
    phase_ = Phase::Turn;
}

RelativeMotion RobotAgent::remaining_motion() const {
    switch (phase_) {
        case Phase::Turn:
            return chain({0.0f, 0.0f, plan_.turn_rad - turned_}, {plan_.forward_m, 0.0f, 0.0f});
        case Phase::Drive:
            return {plan_.forward_m - driven_, 0.0f, 0.0f};
        default:
            return {};
    }
}

void RobotAgent::set_budget(float share, float update_hz) {
    localizer_->set_budget(share);
    update_interval_s_ = update_hz > 0.0f ? 1.0f / update_hz : 0.0f;
}

MotionCommand RobotAgent::update(const AgentInput& in) {
    if (in.time_s < config_.boot_s) {
        mood_ = RobotMood::Thinking;
        return {};
    }
    // Dead reckoning and plan progress come from odometry alone.
    odom_pose_ = compose(odom_pose_, in.odometry.motion());
    pending_odometry_ = accumulate(pending_odometry_, in.odometry);
    turned_ += in.odometry.delta_heading_rad;
    driven_ += in.odometry.forward_m;

    if (in.scan) {
        // With less compute, the localizer skips some scans (odometry keeps
        // adding up meanwhile); the planner and the safety stop see them all.
        const bool think = update_interval_s_ <= 0.0f || in.time_s - last_update_s_ >= update_interval_s_ - 1e-6;
        if (think) {
            localizer_->predict(pending_odometry_);
            pending_odometry_ = {};
            last_update_s_ = in.time_s;
        }
        blind_ = !in.scan->valid;
        if (in.scan->valid) {
            if (think) localizer_->observe(*in.scan);
            last_scan_ = *in.scan;

            // Reactive safety: something right in front while driving.
            if (phase_ == Phase::Drive && plan_.forward_m > 0.0f) {
                for (const LidarReturn& r : in.scan->returns) {
                    if (r.hit && std::fabs(r.angle_rad) < 0.45f &&
                        r.range_m < config_.radius_m + config_.front_stop_m) {
                        phase_ = Phase::Pause;
                        break;
                    }
                }
            }

            // Observations stopped fitting: stop and spin round for a full look.
            const PoseEstimate est = localizer_->estimate();
            if (think && est.consistency < config_.confusion_threshold &&
                in.time_s - last_spin_s_ > config_.confusion_cooldown_s) {
                last_spin_s_ = in.time_s;
                confused_until_s_ = in.time_s + 4.0;
                ++spins_;
                MotionPlan spin;
                spin.kind = MotionPlan::Kind::Spin;
                spin.turn_rad = 1.95f * kPi;
                spin.pause_s = 0.6f;
                spin.reason = "Wait... where am I?";
                start_plan(spin);
            }
        }
    }

    if (in.bumped && phase_ == Phase::Drive && plan_.kind == MotionPlan::Kind::BackOff) {
        phase_ = Phase::Pause;
    } else if (in.bumped && phase_ == Phase::Drive) {
        MotionPlan back;
        back.kind = MotionPlan::Kind::BackOff;
        back.forward_m = -0.2f;
        back.pause_s = 0.3f;
        back.reason = "Oof! Backing off";
        start_plan(back);
        phase_ = Phase::Drive;
    }

    // Blacked out: hold still until the scanner comes back.
    if (blind_) {
        if (phase_ != Phase::Wait) {
            phase_ = Phase::Wait;
            plan_ = {};
            plan_.reason = "I can't see!";
        }
    } else if (phase_ == Phase::Wait) {
        phase_ = Phase::Idle;
    }

    if (phase_ == Phase::Idle) {
        PlannerContext ctx;
        ctx.last_scan = last_scan_ ? &*last_scan_ : nullptr;
        ctx.robot_radius_m = config_.radius_m;
        ctx.nominal_max_range_m = config_.nominal_max_range_m;
        ctx.odom_pose = odom_pose_;
        ctx.time_s = in.time_s;
        start_plan(planner_->choose_action(*map_, localizer_->estimate(), localizer_->diagnostics(), ctx));
    }

    const bool degraded = last_scan_ && last_scan_->max_range_m < 0.75f * config_.nominal_max_range_m;
    const float speed = config_.max_speed_mps * (degraded ? config_.cautious_speed : 1.0f);
    const float turn_rate =
        plan_.kind == MotionPlan::Kind::Spin ? 0.6f * config_.max_turn_rps : config_.max_turn_rps;

    MotionCommand cmd;
    if (phase_ == Phase::Turn) {
        const float remaining = plan_.turn_rad - turned_;
        if (std::fabs(remaining) < 0.03f) {
            phase_ = Phase::Drive;
        } else {
            float w = 3.0f * remaining;
            if (std::fabs(w) < 0.25f) w = std::copysign(0.25f, remaining);
            cmd.angular_rps = clampf(w, -turn_rate, turn_rate);
        }
    }
    if (phase_ == Phase::Drive) {
        const float remaining = plan_.forward_m - driven_;
        if (remaining * (plan_.forward_m >= 0.0f ? 1.0f : -1.0f) < 0.02f) {
            phase_ = Phase::Pause;
        } else {
            const float v = std::min(speed, 1.5f * std::fabs(remaining) + 0.08f);
            cmd.linear_mps = std::copysign(v, remaining);
            // Hold the heading the turn ended on.
            cmd.angular_rps = clampf(-2.0f * (turned_ - plan_.turn_rad), -0.5f, 0.5f);
        }
    }
    if (phase_ == Phase::Pause) {
        pause_left_ -= in.odometry.dt_s;
        if (pause_left_ <= 0.0f) phase_ = Phase::Idle;
    }

    update_mood(in.time_s);
    return cmd;
}

void RobotAgent::update_mood(double time_s) {
    const PoseEstimate est = localizer_->estimate();
    if (blind_) {
        mood_ = RobotMood::Blind;
    } else if (time_s < confused_until_s_ || est.consistency < config_.confusion_threshold) {
        mood_ = RobotMood::Confused;
    } else if (last_scan_ && last_scan_->max_range_m < 0.75f * config_.nominal_max_range_m) {
        mood_ = RobotMood::ShortSighted;
    } else if (est.confidence > 0.75f) {
        mood_ = RobotMood::Confident;
    } else if (est.confidence > 0.4f) {
        mood_ = RobotMood::Hopeful;
    } else if (phase_ == Phase::Pause) {
        mood_ = RobotMood::Thinking;
    } else {
        mood_ = RobotMood::Curious;
    }
}

}  // namespace wobo
