#include "planning/info_gain_planner.h"

#include <algorithm>
#include <cmath>

#include "world/raycast.h"

namespace wobo {

namespace {
float beam_angle_of(int k, int n) { return kTwoPi * static_cast<float>(k) / static_cast<float>(n); }
}  // namespace

InfoGainPlannerConfig InfoGainPlannerConfig::from(const Config& cfg) {
    InfoGainPlannerConfig c;
    c.hypotheses = std::max(1, cfg.get_int("planner.hypotheses", c.hypotheses));
    c.beams = std::max(4, cfg.get_int("planner.beams", c.beams));
    c.sigma_range = cfg.get_float("planner.sigma_range", c.sigma_range);
    c.sigma_colour = cfg.get_float("planner.sigma_colour", c.sigma_colour);
    c.discrimination = cfg.get_float("planner.discrimination", c.discrimination);
    c.distance_cost = cfg.get_float("planner.distance_cost", c.distance_cost);
    c.turn_cost = cfg.get_float("planner.turn_cost", c.turn_cost);
    c.blocked_cost = cfg.get_float("planner.blocked_cost", c.blocked_cost);
    c.novelty_weight = cfg.get_float("planner.novelty_weight", c.novelty_weight);
    c.novelty_radius_m = cfg.get_float("planner.novelty_radius", c.novelty_radius_m);
    c.safety_margin_m = cfg.get_float("planner.safety_margin", c.safety_margin_m);
    c.randomness = cfg.get_float("planner.randomness", c.randomness);
    c.scan_pause_s = cfg.get_float("planner.scan_pause", c.scan_pause_s);
    return c;
}

void InfoGainPlanner::reset(std::uint64_t seed) {
    rng_.reseed(seed);
    visited_.clear();
    candidates_.clear();
    last_visit_s_ = -1e9;
}

void InfoGainPlanner::sample_hypotheses(const Map2D& map, const PoseEstimate& estimate,
                                        const LocalizationDiagnostics* diagnostics) {
    hyps_.clear();
    const int m = config_.hypotheses;
    if (diagnostics && !diagnostics->particles.empty()) {
        // Systematic sample of the particle set; repeated draws of one
        // particle merge into a heavier hypothesis.
        const auto& ps = diagnostics->particles;
        const float step = 1.0f / static_cast<float>(m);
        float u = rng_.uniform() * step;
        float c = ps[0].weight;
        std::size_t j = 0;
        std::size_t last = ps.size();
        for (int i = 0; i < m; ++i) {
            while (u > c && j + 1 < ps.size()) c += ps[++j].weight;
            if (j == last) {
                hyps_.back().weight += step;
            } else {
                hyps_.push_back({ps[j].pose, step});
                last = j;
            }
            u += step;
        }
    } else if (estimate.valid && estimate.has_covariance) {
        const float sx = std::sqrt(std::max(estimate.var_x, 1e-4f));
        const float sz = std::sqrt(std::max(estimate.var_z, 1e-4f));
        const float sh = std::sqrt(std::max(estimate.var_heading, 1e-4f));
        for (int i = 0; i < m; ++i) {
            Pose2D p{estimate.pose.x + rng_.normal(0.0f, sx), estimate.pose.z + rng_.normal(0.0f, sz),
                     wrap_angle(estimate.pose.heading_rad + rng_.normal(0.0f, sh))};
            if (!map.point_free(p.x, p.z)) p = estimate.pose;
            hyps_.push_back({p, 1.0f / static_cast<float>(m)});
        }
    } else if (estimate.valid) {
        hyps_.push_back({estimate.pose, 1.0f});
    }
}

float InfoGainPlanner::scan_clearance(const RgbLidarScan* scan, float direction, float radius) const {
    if (!scan || !scan->valid) return 0.6f;  // blind: only creep
    float best = scan->max_range_m;
    for (const LidarReturn& r : scan->returns) {
        const float d = std::fabs(angle_diff(r.angle_rad, direction));
        if (d > 0.5f * kPi) continue;
        const float range = r.hit ? r.range_m : scan->max_range_m;
        // A return at angle d from the travel direction blocks the swept
        // corridor if its lateral offset is within the robot radius.
        const float lateral = range * std::sin(d);
        if (lateral > radius + 0.05f) continue;
        best = std::min(best, range * std::cos(d));
    }
    return best;
}

MotionPlan InfoGainPlanner::choose_action(const Map2D& map, const PoseEstimate& estimate,
                                          const LocalizationDiagnostics* diagnostics,
                                          const PlannerContext& ctx) {
    sample_hypotheses(map, estimate, diagnostics);
    candidates_.clear();

    // Odometry-frame breadcrumb trail for the novelty bonus.
    if (ctx.time_s - last_visit_s_ > 1.0) {
        visited_.push_back(ctx.odom_pose.position());
        if (visited_.size() > 40) visited_.pop_front();
        last_visit_s_ = ctx.time_s;
    }

    const float radius = ctx.robot_radius_m;
    const float max_range = ctx.last_scan ? ctx.last_scan->max_range_m : ctx.nominal_max_range_m;
    const int n_hyp = static_cast<int>(hyps_.size());
    const int n_beams = config_.beams;

    float h_prior = 0.0f;
    for (const Hyp& h : hyps_) h_prior -= h.weight * std::log(h.weight);

    const float inv_sr2 = 1.0f / (config_.sigma_range * config_.sigma_range);
    const float inv_sc2 = 1.0f / (config_.sigma_colour * config_.sigma_colour);

    constexpr int kTurns = 8;
    const float distances[] = {0.5f, 1.0f, 1.7f};
    predicted_.resize(static_cast<std::size_t>(n_hyp * n_beams));

    PlanCandidate best;
    best.score = -1e9f;
    float best_raw_gain = 0.0f;

    // Candidate 0 is "stay and scan"; then every turn x distance.
    for (int t = -1; t < kTurns; ++t) {
        const float turn = t < 0 ? 0.0f : wrap_angle(kTwoPi * static_cast<float>(t) / kTurns);
        const float clear = scan_clearance(ctx.last_scan, turn, radius);
        for (float dist : distances) {
            float forward = t < 0 ? 0.0f : dist;
            if (t >= 0) {
                forward = std::min(forward, clear - radius - config_.safety_margin_m);
                if (forward < 0.25f) break;  // longer ones are blocked too
            }

            // Predict each hypothesis's view at the end of the motion, and
            // how much belief mass says this path hits a wall.
            float blocked_mass = 0.0f;
            for (int i = 0; i < n_hyp; ++i) {
                Pose2D p = compose(hyps_[i].pose, RelativeMotion{0.0f, 0.0f, turn});
                const Vec2 dir = p.forward();
                float travelled = 0.0f;
                for (float s = 0.15f; s <= forward + 1e-4f; s += 0.15f) {
                    if (!map.circle_free(p.x + dir.x * s, p.z + dir.z * s, radius)) {
                        blocked_mass += hyps_[i].weight;
                        break;
                    }
                    travelled = s;
                }
                if (forward > 0.0f && travelled < forward && travelled + 0.15f > forward) {
                    travelled = forward;  // last partial step was clear
                }
                p.x += dir.x * travelled;
                p.z += dir.z * travelled;
                for (int k = 0; k < n_beams; ++k) {
                    const RayHit e = cast_ray(map, p.x, p.z, p.heading_rad + beam_angle_of(k, n_beams),
                                              max_range);
                    predicted_[i * n_beams + k] = {e.range, e.hit ? map.material(e.material).rgb : Rgb{},
                                                   e.hit};
                }
            }

            // Expected posterior entropy: for each hypothesis i taken as the
            // truth, how much belief would its scan leave on the others?
            float h_post = 0.0f;
            for (int i = 0; i < n_hyp; ++i) {
                float denom = hyps_[i].weight;
                for (int j = 0; j < n_hyp; ++j) {
                    if (j == i) continue;
                    float d2 = 0.0f;
                    for (int k = 0; k < n_beams; ++k) {
                        const Predicted& a = predicted_[i * n_beams + k];
                        const Predicted& b = predicted_[j * n_beams + k];
                        if (a.hit != b.hit) {
                            d2 += 6.0f;
                            continue;
                        }
                        if (!a.hit) continue;
                        const float dr = a.range - b.range;
                        d2 += std::min(9.0f, dr * dr * inv_sr2);
                        const float dc = colour_distance(a.rgb, b.rgb);
                        d2 += std::min(4.0f, dc * dc * inv_sc2);
                    }
                    denom += hyps_[j].weight * std::exp(-0.5f * config_.discrimination * d2);
                }
                h_post += hyps_[i].weight * std::log(denom / hyps_[i].weight);
            }
            const float gain = std::max(0.0f, h_prior - h_post);

            // Novelty, in the odometry frame.
            const Pose2D end_odom = compose(ctx.odom_pose, RelativeMotion{0.0f, 0.0f, turn});
            const Vec2 end{end_odom.x + std::cos(end_odom.heading_rad) * forward,
                           end_odom.z + std::sin(end_odom.heading_rad) * forward};
            float nearest = config_.novelty_radius_m;
            for (const Vec2& v : visited_) nearest = std::min(nearest, distance(v, end));
            const float novelty = nearest / config_.novelty_radius_m;

            const float score = gain - config_.distance_cost * forward -
                                config_.turn_cost * std::fabs(turn) - config_.blocked_cost * blocked_mass +
                                config_.novelty_weight * novelty * (forward > 0.0f ? 1.0f : 0.0f) +
                                rng_.normal(0.0f, config_.randomness);

            PlanCandidate c{turn, forward, gain, score, false};
            candidates_.push_back(c);
            if (score > best.score) {
                best = c;
                best_raw_gain = gain;
            }
            if (t < 0) break;  // "stay" has no distances
        }
    }

    MotionPlan plan;
    if (candidates_.empty()) {
        // Boxed in according to the scan: turn around and look.
        plan.kind = MotionPlan::Kind::Scan;
        plan.turn_rad = kPi * 0.5f;
        plan.pause_s = config_.scan_pause_s;
        plan.reason = "Boxed in - turning round";
        return plan;
    }
    for (auto& c : candidates_) {
        if (c.turn_rad == best.turn_rad && c.forward_m == best.forward_m) {
            c.chosen = true;
            break;
        }
    }

    plan.turn_rad = best.turn_rad;
    plan.forward_m = best.forward_m;
    plan.expected_gain = best_raw_gain;
    plan.pause_s = config_.scan_pause_s;

    const float effective_hyps = std::exp(h_prior);
    const bool short_sighted = max_range < 0.75f * ctx.nominal_max_range_m;
    if (best.forward_m <= 0.0f) {
        plan.kind = MotionPlan::Kind::Scan;
        plan.pause_s = config_.scan_pause_s * 2.0f;
        plan.reason = "Taking a careful look";
    } else if (effective_hyps > 6.0f) {
        plan.kind = best_raw_gain > 0.05f ? MotionPlan::Kind::Disambiguate : MotionPlan::Kind::Explore;
        plan.reason = short_sighted ? "Squinting at nearby walls" : "Hunting for landmarks";
    } else if (effective_hyps > 1.6f) {
        plan.kind = MotionPlan::Kind::Disambiguate;
        const int n = std::max(2, static_cast<int>(std::lround(effective_hyps)));
        plan.reason = "Comparing " + std::to_string(n) + " guesses";
    } else {
        plan.kind = MotionPlan::Kind::Verify;
        plan.reason = "Double-checking";
    }
    return plan;
}

}  // namespace wobo
