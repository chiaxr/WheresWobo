#include "localization/particle_filter.h"

#include <algorithm>
#include <cmath>

#include "world/raycast.h"

namespace wobo {

ParticleFilterConfig ParticleFilterConfig::from(const Config& cfg) {
    ParticleFilterConfig c;
    c.particles = std::max(50, cfg.get_int("localizer.particles", c.particles));
    c.robot_radius = cfg.get_float("robot.radius", c.robot_radius);
    c.beam_stride = std::max(1, cfg.get_int("localizer.beam_stride", c.beam_stride));
    c.sigma_range = cfg.get_float("localizer.sigma_range", c.sigma_range);
    c.sigma_colour = cfg.get_float("localizer.sigma_colour", c.sigma_colour);
    c.colour_floor = cfg.get_float("localizer.colour_floor", c.colour_floor);
    c.p_random = cfg.get_float("localizer.p_random", c.p_random);
    c.p_unexpected_miss = cfg.get_float("localizer.p_unexpected_miss", c.p_unexpected_miss);
    c.temperature = cfg.get_float("localizer.temperature", c.temperature);
    c.forward_var_per_m = cfg.get_float("localizer.forward_var_per_m", c.forward_var_per_m);
    c.lateral_var_per_m = cfg.get_float("localizer.lateral_var_per_m", c.lateral_var_per_m);
    c.turn_var_per_rad = cfg.get_float("localizer.turn_var_per_rad", c.turn_var_per_rad);
    c.turn_var_per_m = cfg.get_float("localizer.turn_var_per_m", c.turn_var_per_m);
    c.jitter_m = cfg.get_float("localizer.jitter_m", c.jitter_m);
    c.jitter_rad = cfg.get_float("localizer.jitter_rad", c.jitter_rad);
    c.alpha_slow = cfg.get_float("localizer.alpha_slow", c.alpha_slow);
    c.alpha_fast = cfg.get_float("localizer.alpha_fast", c.alpha_fast);
    c.max_injection = cfg.get_float("localizer.max_injection", c.max_injection);
    c.inject_candidates = std::max(1, cfg.get_int("localizer.inject_candidates", c.inject_candidates));
    c.view_injection = cfg.get_bool("localizer.view_injection", c.view_injection);
    c.view_range = cfg.get_float("sensor.max_range", c.view_range);
    c.view_temperature = cfg.get_float("localizer.view_temperature", c.temperature);
    c.resample_ess = cfg.get_float("localizer.resample_ess", c.resample_ess);
    c.cluster_radius_m = cfg.get_float("localizer.cluster_radius", c.cluster_radius_m);
    c.cluster_heading_rad =
        cfg.get_float("localizer.cluster_heading_deg", c.cluster_heading_rad * kRadToDeg) * kDegToRad;
    return c;
}

Pose2D ParticleFilter::sample_prior() {
    if (prior_.kind == InitialBelief::Kind::Gaussian) {
        for (int attempt = 0; attempt < 100; ++attempt) {
            Pose2D p{prior_.mean.x + rng_.normal(0.0f, prior_.sigma_position_m),
                     prior_.mean.z + rng_.normal(0.0f, prior_.sigma_position_m),
                     wrap_angle(prior_.mean.heading_rad + rng_.normal(0.0f, prior_.sigma_heading_rad))};
            if (map_->valid_pose(p, config_.robot_radius)) return p;
        }
    }
    return map_->sample_valid_pose(rng_, config_.robot_radius);
}

void ParticleFilter::reset(const Map2D& known_map, const InitialBelief& prior, std::uint64_t seed) {
    map_ = &known_map;
    prior_ = prior;
    rng_.reseed(seed);
    const int n = static_cast<int>(target_particles());
    diag_ = LocalizationDiagnostics{};
    diag_.particles.resize(static_cast<std::size_t>(n));
    for (auto& p : diag_.particles) {
        p.pose = sample_prior();
        p.weight = 1.0f / static_cast<float>(n);
    }
    log_lik_.assign(static_cast<std::size_t>(n), 0.0f);
    have_averages_ = false;
    w_slow_ = w_fast_ = 0.0f;
    beam_offset_ = 0;
    views_scored_ = false;
    if (view_temperature_ > 0.0f && views_for_ != map_) build_views();
    update_estimate();
}

void ParticleFilter::set_view_injection(float temperature) {
    view_temperature_ = std::max(0.0f, temperature);
    if (view_temperature_ > 0.0f && map_ && views_for_ != map_) build_views();
}

void ParticleFilter::build_views() {
    views_for_ = map_;
    view_materials_ = static_cast<int>(map_->materials().size());
    view_cells_.clear();
    view_keys_.clear();
    const int w = map_->width();
    const float bin_m = config_.view_range / kRangeBins;
    const auto nothing = static_cast<std::uint16_t>(kRangeBins * view_materials_);
    for (int i : map_->free_cells()) {
        if (map_->cell_clearance(i) < config_.robot_radius) continue;
        view_cells_.push_back(i);
        const Vec2 c = map_->cell_center(i % w, i / w);
        for (int k = 0; k < kViews; ++k) {
            const RayHit e = cast_ray(*map_, c.x, c.z, kTwoPi * k / kViews, config_.view_range);
            const int bin = std::min(kRangeBins - 1, static_cast<int>(e.range / bin_m));
            view_keys_.push_back(e.hit ? static_cast<std::uint16_t>(bin * view_materials_ + e.material) : nothing);
        }
    }
}

// The fit of every (cell, heading) state to the whole scan, with the beam
// model of log_likelihood, as a cumulative distribution of its tempered
// likelihood. The scan's beams are tabulated against every possible view
// entry first, so each state costs a lookup per beam.
void ParticleFilter::score_views(const RgbLidarScan& scan) {
    if (views_for_ != map_) build_views();
    const int n_keys = kRangeBins * view_materials_ + 1;
    const float bin_m = config_.view_range / kRangeBins;
    const float eps = config_.p_random;
    const float inv_sr2 = 1.0f / (config_.sigma_range * config_.sigma_range);
    const float inv_sc2 = 1.0f / (config_.sigma_colour * config_.sigma_colour);
    const float max_range = scan.max_range_m;

    std::vector<int> dirs;
    std::vector<float> table;
    for (const LidarReturn& r : scan.returns) {
        int d = static_cast<int>(std::lround(r.angle_rad / kTwoPi * kViews)) % kViews;
        dirs.push_back(d < 0 ? d + kViews : d);
        for (int key = 0; key < n_keys; ++key) {
            const bool view_hit = key < n_keys - 1;
            const float view_range = view_hit ? (static_cast<float>(key / view_materials_) + 0.5f) * bin_m : max_range;
            const bool expect_hit = view_hit && view_range < max_range;  // the scan may be cut short
            float p;
            if (r.hit) {
                const float dr = r.range_m - (expect_hit ? view_range : max_range);
                const float pr = std::exp(-0.5f * dr * dr * inv_sr2);
                float pc = 1.0f;
                if (expect_hit) {
                    const auto mat = static_cast<std::uint8_t>(key % view_materials_);
                    const float dc = colour_distance(r.rgb, map_->material(mat).rgb);
                    pc = config_.colour_floor + (1.0f - config_.colour_floor) * std::exp(-0.5f * dc * dc * inv_sc2);
                }
                p = (1.0f - eps) * pr * pc + eps;
            } else {
                p = expect_hit ? config_.p_unexpected_miss : 1.0f;
            }
            table.push_back(std::log(p));
        }
    }

    const std::size_t n_beams = dirs.size();
    const std::size_t n_states = view_cells_.size() * kViews;
    view_cdf_.resize(n_states);
    float best = -1e30f;
    for (std::size_t s = 0; s < view_cells_.size(); ++s) {
        const std::uint16_t* keys = &view_keys_[s * kViews];
        for (int h = 0; h < kViews; ++h) {
            float ll = 0.0f;
            for (std::size_t j = 0; j < n_beams; ++j) {
                ll += table[j * n_keys + keys[(h + dirs[j]) % kViews]];
            }
            view_cdf_[s * kViews + h] = ll;
            best = std::max(best, ll);
        }
    }
    float sum = 0.0f;
    for (float& v : view_cdf_) {
        sum += std::exp(view_temperature_ * (v - best));
        v = sum;
    }
    for (float& v : view_cdf_) v /= sum;
    views_scored_ = n_states > 0;
}

// The pose of the state at `u` (0..1) of the view distribution: its cell
// centre and view direction.
Pose2D ParticleFilter::draw_from_views(float u) {
    const auto it = std::lower_bound(view_cdf_.begin(), view_cdf_.end(), u);
    const auto state = static_cast<std::size_t>(std::min<std::ptrdiff_t>(
        it - view_cdf_.begin(), static_cast<std::ptrdiff_t>(view_cdf_.size()) - 1));
    const int cell = view_cells_[state / kViews];
    const int h = static_cast<int>(state % kViews);
    const Vec2 c = map_->cell_center(cell % map_->width(), cell / map_->width());
    return {c.x, c.z, wrap_angle(kTwoPi * h / kViews)};
}

void ParticleFilter::set_budget(float share) { budget_ = clampf(share, 0.0f, 4.0f); }

int ParticleFilter::inject_candidates() const {
    return std::max(1, static_cast<int>(std::lround(config_.inject_candidates * budget_)));
}

// Up to the configured budget, compute goes on particles; beyond it, on
// reading more of each scan. Injection tries more poses throughout.
std::size_t ParticleFilter::target_particles() const {
    const float share = std::min(budget_, 1.0f);
    return static_cast<std::size_t>(std::max(50, static_cast<int>(std::lround(config_.particles * share))));
}

int ParticleFilter::beam_stride() const {
    return std::max(1, static_cast<int>(std::lround(config_.beam_stride / std::max(budget_, 1.0f))));
}

void ParticleFilter::predict(const OdometryDelta& odom) {
    if (!map_) return;
    const float dist = std::fabs(odom.forward_m) + std::fabs(odom.lateral_m);
    const float rot = std::fabs(odom.delta_heading_rad);
    if (dist < 1e-6f && rot < 1e-6f) return;

    const float sf = std::sqrt(config_.forward_var_per_m * dist);
    const float sl = std::sqrt(config_.lateral_var_per_m * dist);
    const float st = std::sqrt(config_.turn_var_per_rad * rot + config_.turn_var_per_m * dist);
    for (auto& p : diag_.particles) {
        const RelativeMotion m{odom.forward_m + rng_.normal(0.0f, sf),
                               odom.lateral_m + rng_.normal(0.0f, sl),
                               odom.delta_heading_rad + rng_.normal(0.0f, st)};
        p.pose = compose(p.pose, m);
    }
    // Keep the estimate moving with the robot between scans.
    estimate_.pose = compose(estimate_.pose, odom.motion());
    for (auto& h : diag_.hypotheses) h.pose = compose(h.pose, odom.motion());
}

float ParticleFilter::log_likelihood(const Pose2D& pose) const {
    const float eps = config_.p_random;
    if (!map_->point_free(pose.x, pose.z)) {
        return static_cast<float>(beams_.size()) * std::log(eps * 0.2f);
    }
    const float inv_sr2 = 1.0f / (config_.sigma_range * config_.sigma_range);
    const float inv_sc2 = 1.0f / (config_.sigma_colour * config_.sigma_colour);
    float ll = 0.0f;
    for (const Beam& b : beams_) {
        const RayHit e = cast_ray(*map_, pose.x, pose.z, pose.heading_rad + b.angle, max_range_);
        float p;
        if (b.hit) {
            const float dr = b.range - e.range;  // e.range == max_range for a miss
            const float pr = std::exp(-0.5f * dr * dr * inv_sr2);
            float pc = 1.0f;
            if (e.hit) {
                const float dc = colour_distance(b.rgb, map_->material(e.material).rgb);
                pc = config_.colour_floor + (1.0f - config_.colour_floor) * std::exp(-0.5f * dc * dc * inv_sc2);
            }
            p = (1.0f - eps) * pr * pc + eps;
        } else {
            p = e.hit ? config_.p_unexpected_miss : 1.0f;
        }
        ll += std::log(p);
    }
    return ll;
}

void ParticleFilter::observe(const RgbLidarScan& scan) {
    if (!map_ || !scan.valid || scan.returns.empty()) return;

    // The beams used this update. The offset rotates so every beam
    // contributes over successive scans.
    beams_.clear();
    const int n_returns = static_cast<int>(scan.returns.size());
    const int stride = beam_stride();
    for (int i = beam_offset_ % stride; i < n_returns; i += stride) {
        const LidarReturn& r = scan.returns[i];
        beams_.push_back({r.angle_rad, r.range_m, r.hit, r.rgb});
    }
    beam_offset_ = (beam_offset_ + 1) % stride;
    if (beams_.empty()) return;
    max_range_ = scan.max_range_m;

    const float n_beams = static_cast<float>(beams_.size());
    const std::size_t n = diag_.particles.size();
    float w_avg = 0.0f;
    float max_ll = -1e30f;
    for (std::size_t i = 0; i < n; ++i) {
        const float ll = log_likelihood(diag_.particles[i].pose);
        log_lik_[i] = ll;
        w_avg += diag_.particles[i].weight * std::exp(ll / n_beams);
        max_ll = std::max(max_ll, ll);
    }

    // Augmented MCL: compare short- and long-term average fit.
    if (!have_averages_) {
        w_slow_ = w_fast_ = w_avg;
        have_averages_ = true;
    } else {
        w_slow_ += config_.alpha_slow * (w_avg - w_slow_);
        w_fast_ += config_.alpha_fast * (w_avg - w_fast_);
    }
    const float ratio = w_slow_ > 1e-9f ? w_fast_ / w_slow_ : 1.0f;
    const float inject = clampf(1.0f - ratio, 0.0f, config_.max_injection);
    estimate_.consistency = clampf(ratio, 0.0f, 1.0f);
    views_scored_ = false;
    if (view_temperature_ > 0.0f && inject > 0.005f) score_views(scan);

    // Reweight (tempered), normalise, and measure degeneracy.
    float sum = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        auto& p = diag_.particles[i];
        p.weight *= std::exp(config_.temperature * (log_lik_[i] - max_ll));
        sum += p.weight;
    }
    float sum_sq = 0.0f;
    if (sum <= 0.0f || !std::isfinite(sum)) {
        for (auto& p : diag_.particles) p.weight = 1.0f / static_cast<float>(n);
        sum_sq = 1.0f / static_cast<float>(n);
    } else {
        for (auto& p : diag_.particles) {
            p.weight /= sum;
            sum_sq += p.weight * p.weight;
        }
    }
    diag_.effective_sample_size = (1.0f / sum_sq) / static_cast<float>(n);

    diag_.injected_fraction = 0.0f;
    if (diag_.effective_sample_size < config_.resample_ess || inject > 0.005f ||
        diag_.particles.size() != target_particles()) {
        resample(inject);
    }
    update_estimate();
}

void ParticleFilter::resample(float inject_fraction) {
    // Draws from the current set; produces the budget's worth of particles.
    const std::size_t old_n = diag_.particles.size();
    const std::size_t n = target_particles();
    scratch_.resize(n);
    const auto n_inject = static_cast<std::size_t>(inject_fraction * static_cast<float>(n));
    const std::size_t n_keep = n - n_inject;

    // Low-variance (systematic) resampling.
    const float step = 1.0f / static_cast<float>(n_keep);
    float u = rng_.uniform() * step;
    float c = diag_.particles[0].weight;
    std::size_t j = 0;
    for (std::size_t i = 0; i < n_keep; ++i) {
        while (u > c && j + 1 < old_n) c += diag_.particles[++j].weight;
        Pose2D p = diag_.particles[j].pose;
        p.x += rng_.normal(0.0f, config_.jitter_m);
        p.z += rng_.normal(0.0f, config_.jitter_m);
        p.heading_rad = wrap_angle(p.heading_rad + rng_.normal(0.0f, config_.jitter_rad));
        scratch_[i] = {p, 0.0f};
        u += step;
    }
    // Injected particles: the best fit to the latest scan among a few random
    // poses (sensor-resetting), so recovery lands near places that look right.
    // From the view table, the poses tried lie around one state drawn from it
    // (systematically, so look-alike places share the draws).
    if (views_scored_ && n_inject > 0) {
        const float cell = map_->cell_size();
        const float turn = kTwoPi / kViews;
        const float draw_step = 1.0f / static_cast<float>(n_inject);
        const float u0 = rng_.uniform() * draw_step;
        for (std::size_t i = n_keep; i < n; ++i) {
            const Pose2D centre = draw_from_views(u0 + draw_step * static_cast<float>(i - n_keep));
            Pose2D best = centre;
            float best_ll = -1e30f;
            for (int k = 0; k < inject_candidates(); ++k) {
                const Pose2D p{centre.x + (rng_.uniform() - 0.5f) * cell, centre.z + (rng_.uniform() - 0.5f) * cell,
                               wrap_angle(centre.heading_rad + (rng_.uniform() - 0.5f) * turn)};
                if (!map_->valid_pose(p, config_.robot_radius)) continue;
                const float ll = log_likelihood(p);
                if (ll > best_ll) {
                    best_ll = ll;
                    best = p;
                }
            }
            scratch_[i] = {best, 0.0f};
        }
    }
    for (std::size_t i = views_scored_ ? n : n_keep; i < n; ++i) {
        Pose2D best = map_->sample_valid_pose(rng_, config_.robot_radius);
        const int candidates = inject_candidates();
        float best_ll = candidates > 1 ? log_likelihood(best) : 0.0f;
        for (int k = 1; k < candidates; ++k) {
            const Pose2D p = map_->sample_valid_pose(rng_, config_.robot_radius);
            const float ll = log_likelihood(p);
            if (ll > best_ll) {
                best_ll = ll;
                best = p;
            }
        }
        scratch_[i] = {best, 0.0f};
    }
    const float w = 1.0f / static_cast<float>(n);
    for (auto& p : scratch_) p.weight = w;
    diag_.particles.swap(scratch_);
    log_lik_.resize(n);
    diag_.injected_fraction = inject_fraction;
}

void ParticleFilter::update_estimate() {
    // Find up to four distinct pose clusters: histogram the particles over
    // (cell, heading octant), seed at the heaviest neighbourhood, refine with
    // two mean-shift iterations, claim the members, repeat.
    const int w = map_->width();
    const int h = map_->height();
    constexpr int kHeadingBins = 8;
    const auto bin_of = [&](const Pose2D& p) {
        const int cx = std::clamp(map_->cell_x(p.x), 0, w - 1);
        const int cz = std::clamp(map_->cell_z(p.z), 0, h - 1);
        int hb = static_cast<int>(std::floor((p.heading_rad + kPi) / kTwoPi * kHeadingBins));
        hb = (hb % kHeadingBins + kHeadingBins) % kHeadingBins;
        return (cz * w + cx) * kHeadingBins + hb;
    };

    const std::size_t n = diag_.particles.size();
    claimed_.assign(n, 0);
    diag_.hypotheses.clear();
    const float r2 = config_.cluster_radius_m * config_.cluster_radius_m;

    for (int round = 0; round < 4; ++round) {
        histogram_.assign(static_cast<std::size_t>(w * h * kHeadingBins), 0.0f);
        float remaining = 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            if (claimed_[i]) continue;
            histogram_[bin_of(diag_.particles[i].pose)] += diag_.particles[i].weight;
            remaining += diag_.particles[i].weight;
        }
        if (remaining < 0.03f) break;

        // Heaviest 3x3x3 neighbourhood.
        float best = -1.0f;
        int best_bin = 0;
        for (int cz = 0; cz < h; ++cz) {
            for (int cx = 0; cx < w; ++cx) {
                for (int hb = 0; hb < kHeadingBins; ++hb) {
                    if (histogram_[(cz * w + cx) * kHeadingBins + hb] <= 0.0f) continue;
                    float s = 0.0f;
                    for (int dz = -1; dz <= 1; ++dz) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int x = cx + dx;
                            const int z = cz + dz;
                            if (x < 0 || z < 0 || x >= w || z >= h) continue;
                            for (int dh = -1; dh <= 1; ++dh) {
                                const int b = (hb + dh + kHeadingBins) % kHeadingBins;
                                s += histogram_[(z * w + x) * kHeadingBins + b];
                            }
                        }
                    }
                    if (s > best) {
                        best = s;
                        best_bin = (cz * w + cx) * kHeadingBins + hb;
                    }
                }
            }
        }
        const int hb = best_bin % kHeadingBins;
        const int cell = best_bin / kHeadingBins;
        const Vec2 c = map_->cell_center(cell % w, cell / w);
        Pose2D mean{c.x, c.z, -kPi + (hb + 0.5f) * kTwoPi / kHeadingBins};

        // Mean shift within the cluster window.
        float mass = 0.0f;
        float vxx = 0.0f, vzz = 0.0f, vxz = 0.0f, vhh = 0.0f;
        const float window_r2 = 4.0f * r2;
        for (int iter = 0; iter < 3; ++iter) {
            const float rr = iter == 0 ? window_r2 : r2;
            const float hh = iter == 0 ? 2.0f * config_.cluster_heading_rad : config_.cluster_heading_rad;
            float sx = 0.0f, sz = 0.0f, sc = 0.0f, ss = 0.0f;
            mass = 0.0f;
            for (std::size_t i = 0; i < n; ++i) {
                if (claimed_[i]) continue;
                const auto& p = diag_.particles[i];
                const float dx = p.pose.x - mean.x;
                const float dz = p.pose.z - mean.z;
                if (dx * dx + dz * dz > rr) continue;
                if (std::fabs(angle_diff(p.pose.heading_rad, mean.heading_rad)) > hh) continue;
                sx += p.weight * p.pose.x;
                sz += p.weight * p.pose.z;
                sc += p.weight * std::cos(p.pose.heading_rad);
                ss += p.weight * std::sin(p.pose.heading_rad);
                mass += p.weight;
            }
            if (mass <= 0.0f) break;
            mean = {sx / mass, sz / mass, std::atan2(ss, sc)};
        }
        if (mass <= 0.0f) break;

        // Claim members and measure spread.
        for (std::size_t i = 0; i < n; ++i) {
            if (claimed_[i]) continue;
            const auto& p = diag_.particles[i];
            const float dx = p.pose.x - mean.x;
            const float dz = p.pose.z - mean.z;
            if (dx * dx + dz * dz > r2) continue;
            const float dh = angle_diff(p.pose.heading_rad, mean.heading_rad);
            if (std::fabs(dh) > config_.cluster_heading_rad) continue;
            claimed_[i] = 1;
            vxx += p.weight * dx * dx;
            vzz += p.weight * dz * dz;
            vxz += p.weight * dx * dz;
            vhh += p.weight * dh * dh;
        }
        diag_.hypotheses.push_back({mean, mass});
        if (round == 0) {
            estimate_.pose = mean;
            estimate_.valid = true;
            estimate_.confidence = clampf(mass, 0.0f, 1.0f);
            estimate_.has_covariance = true;
            estimate_.var_x = vxx / mass;
            estimate_.var_z = vzz / mass;
            estimate_.cov_xz = vxz / mass;
            estimate_.var_heading = vhh / mass;
        }
    }
    std::sort(diag_.hypotheses.begin(), diag_.hypotheses.end(),
              [](const WeightedPose& a, const WeightedPose& b) { return a.weight > b.weight; });
}

}  // namespace wobo
