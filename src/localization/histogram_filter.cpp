#include "localization/histogram_filter.h"

#include <algorithm>
#include <cmath>

#include "world/raycast.h"

namespace wobo {

namespace {
constexpr unsigned char kNoHit = 255;
constexpr int kRangeBins = 64;
constexpr float kBinM = 0.05f;
}  // namespace

HistogramFilterConfig HistogramFilterConfig::from(const Config& cfg) {
    HistogramFilterConfig c;
    c.cell_m = std::max(0.1f, cfg.get_float("localizer.grid_cell", c.cell_m));
    c.headings = std::clamp(cfg.get_int("localizer.grid_headings", c.headings), 8, 128);
    c.robot_radius = cfg.get_float("robot.radius", c.robot_radius);
    c.beam_stride = std::max(1, cfg.get_int("localizer.grid_beam_stride", c.beam_stride));
    c.sigma_range = cfg.get_float("localizer.sigma_range", c.sigma_range);
    c.sigma_colour = cfg.get_float("localizer.sigma_colour", c.sigma_colour);
    c.colour_floor = cfg.get_float("localizer.colour_floor", c.colour_floor);
    c.p_random = cfg.get_float("localizer.p_random", c.p_random);
    c.p_unexpected_miss = cfg.get_float("localizer.p_unexpected_miss", c.p_unexpected_miss);
    c.temperature = cfg.get_float("localizer.temperature", c.temperature);
    c.forward_var_per_m = cfg.get_float("localizer.forward_var_per_m", c.forward_var_per_m);
    c.turn_var_per_rad = cfg.get_float("localizer.turn_var_per_rad", c.turn_var_per_rad);
    c.turn_var_per_m = cfg.get_float("localizer.turn_var_per_m", c.turn_var_per_m);
    c.blur = std::clamp(cfg.get_float("localizer.grid_blur", c.blur), 0.0f, 0.25f);
    c.alpha_slow = cfg.get_float("localizer.alpha_slow", c.alpha_slow);
    c.alpha_fast = cfg.get_float("localizer.alpha_fast", c.alpha_fast);
    c.max_mix = cfg.get_float("localizer.max_injection", c.max_mix);
    c.cluster_radius_m = cfg.get_float("localizer.cluster_radius", c.cluster_radius_m);
    c.cluster_heading_rad =
        cfg.get_float("localizer.cluster_heading_deg", c.cluster_heading_rad * kRadToDeg) * kDegToRad;
    c.export_samples = std::max(50, cfg.get_int("localizer.grid_samples", c.export_samples));
    c.max_range = cfg.get_float("sensor.max_range", c.max_range);
    return c;
}

Vec2 HistogramFilter::position_of(int cell, int h) const {
    const float cx = (static_cast<float>(cell % gw_) + 0.5f) * config_.cell_m;
    const float cz = (static_cast<float>(cell / gw_) + 0.5f) * config_.cell_m;
    return Vec2{cx, cz} + layer_rem_[h];
}

void HistogramFilter::build(const Map2D& map) {
    built_for_ = &map;
    gw_ = static_cast<int>(std::ceil(map.world_width() / config_.cell_m));
    gh_ = static_cast<int>(std::ceil(map.world_depth() / config_.cell_m));
    const int cells = gw_ * gh_;
    const int nh = config_.headings;
    valid_.assign(static_cast<std::size_t>(cells), 0);
    exp_range_.assign(static_cast<std::size_t>(cells * nh), 0.0f);
    exp_mat_.assign(static_cast<std::size_t>(cells * nh), kNoHit);
    // A cell holds Wobo if a robot at its centre would (nearly) fit: the
    // robot's centre can be anywhere in the cell, up to half a cell closer.
    const float fit = std::max(0.0f, config_.robot_radius - 0.5f * config_.cell_m);
    for (int c = 0; c < cells; ++c) {
        const float x = (static_cast<float>(c % gw_) + 0.5f) * config_.cell_m;
        const float z = (static_cast<float>(c / gw_) + 0.5f) * config_.cell_m;
        if (!map.point_free(x, z) || !map.circle_free(x, z, fit)) continue;
        valid_[c] = 1;
        for (int d = 0; d < nh; ++d) {
            const RayHit e = cast_ray(map, x, z, kTwoPi * d / nh, config_.max_range);
            exp_range_[c * nh + d] = e.range;
            exp_mat_[c * nh + d] = e.hit ? e.material : kNoHit;
        }
    }
}

void HistogramFilter::reset(const Map2D& known_map, const InitialBelief& prior, std::uint64_t /*seed*/) {
    map_ = &known_map;
    if (built_for_ != &known_map) build(known_map);
    const int nh = config_.headings;
    const int cells = gw_ * gh_;
    belief_.assign(static_cast<std::size_t>(cells * nh), 0.0f);
    ll_.assign(belief_.size(), 0.0f);
    claimed_.assign(belief_.size(), 0);
    layer_rem_.assign(static_cast<std::size_t>(nh), Vec2{});
    heading_rem_ = 0.0f;
    pending_dist_ = pending_rot_ = 0.0f;
    beam_offset_ = 0;
    have_averages_ = false;
    w_slow_ = w_fast_ = 0.0f;
    for (int c = 0; c < cells; ++c) {
        if (!valid_[c]) continue;
        for (int h = 0; h < nh; ++h) {
            float p = 1.0f;
            if (prior.kind == InitialBelief::Kind::Gaussian) {
                const Vec2 d = position_of(c, h) - prior.mean.position();
                const float dh = angle_diff(heading_of(h), prior.mean.heading_rad);
                const float sp2 = prior.sigma_position_m * prior.sigma_position_m;
                const float sh2 = prior.sigma_heading_rad * prior.sigma_heading_rad;
                p = std::exp(-0.5f * ((d.x * d.x + d.z * d.z) / sp2 + dh * dh / sh2)) + 1e-9f;
            }
            belief_[state(c, h)] = p;
        }
    }
    normalise();
    diag_ = LocalizationDiagnostics{};
    estimate_ = PoseEstimate{};
    update_estimate();
}

void HistogramFilter::shift_layer(int h, int dx, int dz) {
    const int nh = config_.headings;
    scratch_.assign(static_cast<std::size_t>(gw_ * gh_), 0.0f);
    for (int z = 0; z < gh_; ++z) {
        const int tz = z + dz;
        if (tz < 0 || tz >= gh_) continue;
        for (int x = 0; x < gw_; ++x) {
            const int tx = x + dx;
            if (tx < 0 || tx >= gw_) continue;
            const int to = tz * gw_ + tx;
            // Belief that would move into a wall is impossible: it is dropped.
            if (valid_[to]) scratch_[to] = belief_[(z * gw_ + x) * nh + h];
        }
    }
    for (int c = 0; c < gw_ * gh_; ++c) belief_[c * nh + h] = scratch_[c];
}

void HistogramFilter::rotate_layers(int dh) {
    const int nh = config_.headings;
    const int cells = gw_ * gh_;
    scratch_.resize(static_cast<std::size_t>(nh));
    for (int c = 0; c < cells; ++c) {
        if (!valid_[c]) continue;
        float* row = &belief_[c * nh];
        for (int h = 0; h < nh; ++h) scratch_[((h + dh) % nh + nh) % nh] = row[h];
        std::copy(scratch_.begin(), scratch_.begin() + nh, row);
    }
    std::vector<Vec2> rem(layer_rem_.size());
    for (int h = 0; h < nh; ++h) rem[((h + dh) % nh + nh) % nh] = layer_rem_[h];
    layer_rem_.swap(rem);
}

void HistogramFilter::predict(const OdometryDelta& odom) {
    if (!map_) return;
    const float dist = std::fabs(odom.forward_m) + std::fabs(odom.lateral_m);
    const float rot = std::fabs(odom.delta_heading_rad);
    if (dist < 1e-6f && rot < 1e-6f) return;
    pending_dist_ += dist;
    pending_rot_ += rot;

    const int nh = config_.headings;
    const float half = 0.5f * config_.cell_m;
    bool moved = false;
    if (dist > 0.0f) {
        for (int h = 0; h < nh; ++h) {
            const float th = heading_of(h) + 0.5f * odom.delta_heading_rad;
            const float c = std::cos(th);
            const float s = std::sin(th);
            Vec2& r = layer_rem_[h];
            r.x += c * odom.forward_m - s * odom.lateral_m;
            r.z += s * odom.forward_m + c * odom.lateral_m;
            int dx = 0;
            int dz = 0;
            while (r.x >= half) { r.x -= config_.cell_m; ++dx; }
            while (r.x < -half) { r.x += config_.cell_m; --dx; }
            while (r.z >= half) { r.z -= config_.cell_m; ++dz; }
            while (r.z < -half) { r.z += config_.cell_m; --dz; }
            if (dx != 0 || dz != 0) {
                shift_layer(h, dx, dz);
                moved = true;
            }
        }
    }
    const float step = kTwoPi / nh;
    heading_rem_ += odom.delta_heading_rad;
    int dh = 0;
    while (heading_rem_ >= 0.5f * step) { heading_rem_ -= step; ++dh; }
    while (heading_rem_ < -0.5f * step) { heading_rem_ += step; --dh; }
    if (dh != 0) rotate_layers(dh);
    if (moved) normalise();

    // Keep the estimate moving with the robot between scans.
    estimate_.pose = compose(estimate_.pose, odom.motion());
    for (auto& h : diag_.hypotheses) h.pose = compose(h.pose, odom.motion());
}

void HistogramFilter::blur(float a, float b) {
    const int nh = config_.headings;
    const int cells = gw_ * gh_;
    if (a > 0.0f) {
        // Separable [a, 1-2a, a] along x then z, per heading layer. Mass that
        // spreads into walls is lost (and renormalised away).
        for (int pass = 0; pass < 2; ++pass) {
            const int stride = pass == 0 ? 1 : gw_;
            scratch_.assign(belief_.size(), 0.0f);
            for (int c = 0; c < cells; ++c) {
                if (!valid_[c]) continue;
                const int x = c % gw_;
                const int z = c / gw_;
                const bool has_prev = pass == 0 ? x > 0 : z > 0;
                const bool has_next = pass == 0 ? x + 1 < gw_ : z + 1 < gh_;
                const float* v = &belief_[c * nh];
                float* o = &scratch_[c * nh];
                for (int h = 0; h < nh; ++h) o[h] += (1.0f - 2.0f * a) * v[h];
                if (has_prev && valid_[c - stride]) {
                    float* p = &scratch_[(c - stride) * nh];
                    for (int h = 0; h < nh; ++h) p[h] += a * v[h];
                }
                if (has_next && valid_[c + stride]) {
                    float* p = &scratch_[(c + stride) * nh];
                    for (int h = 0; h < nh; ++h) p[h] += a * v[h];
                }
            }
            belief_.swap(scratch_);
        }
    }
    if (b > 0.0f) {
        std::vector<float> row(static_cast<std::size_t>(nh));
        for (int c = 0; c < cells; ++c) {
            if (!valid_[c]) continue;
            float* v = &belief_[c * nh];
            for (int h = 0; h < nh; ++h) {
                row[h] = (1.0f - 2.0f * b) * v[h] + b * (v[(h + nh - 1) % nh] + v[(h + 1) % nh]);
            }
            std::copy(row.begin(), row.end(), v);
        }
    }
    normalise();
}

void HistogramFilter::normalise() {
    double sum = 0.0;
    for (float p : belief_) sum += p;
    if (sum <= 0.0 || !std::isfinite(sum)) {
        // Everything ruled out (or lost to walls): start again from scratch.
        const int nh = config_.headings;
        for (int c = 0; c < gw_ * gh_; ++c) {
            for (int h = 0; h < nh; ++h) belief_[state(c, h)] = valid_[c] ? 1.0f : 0.0f;
        }
        for (float p : belief_) sum += p;
        if (sum <= 0.0) return;
    }
    const float inv = static_cast<float>(1.0 / sum);
    for (float& p : belief_) p *= inv;
}

void HistogramFilter::observe(const RgbLidarScan& scan) {
    if (!map_ || !scan.valid || scan.returns.empty()) return;
    const int nh = config_.headings;
    const float step = kTwoPi / nh;
    const float eps = config_.p_random;
    const float max_range = scan.max_range_m;
    const float inv_sr2 = 1.0f / (config_.sigma_range * config_.sigma_range);
    const float inv_sc2 = 1.0f / (config_.sigma_colour * config_.sigma_colour);
    const int n_mat = static_cast<int>(map_->materials().size());

    // Per beam: the grid direction offset, and log-likelihood tables over
    // (expected material, range difference) so the update is lookups.
    struct Beam {
        int offset;
        bool hit;
        float range;
        float ll_expect_miss;  // beam hit where the map expects nothing in range
    };
    std::vector<Beam> beams;
    std::vector<float> table;  // [beam][material][bin]
    const int n_returns = static_cast<int>(scan.returns.size());
    for (int i = beam_offset_ % config_.beam_stride; i < n_returns; i += config_.beam_stride) {
        const LidarReturn& r = scan.returns[i];
        Beam b{static_cast<int>(std::lround(r.angle_rad / step)), r.hit, r.range_m, 0.0f};
        b.offset = (b.offset % nh + nh) % nh;
        if (r.hit) {
            const float dr = r.range_m - max_range;
            b.ll_expect_miss = std::log((1.0f - eps) * std::exp(-0.5f * dr * dr * inv_sr2) + eps);
            for (int m = 0; m < n_mat; ++m) {
                const float dc = colour_distance(r.rgb, map_->material(static_cast<std::uint8_t>(m)).rgb);
                const float pc =
                    config_.colour_floor + (1.0f - config_.colour_floor) * std::exp(-0.5f * dc * dc * inv_sc2);
                for (int k = 0; k < kRangeBins; ++k) {
                    const float d = (static_cast<float>(k) + 0.5f) * kBinM;
                    table.push_back(std::log((1.0f - eps) * std::exp(-0.5f * d * d * inv_sr2) * pc + eps));
                }
            }
        } else {
            table.resize(table.size() + static_cast<std::size_t>(n_mat * kRangeBins), 0.0f);
        }
        beams.push_back(b);
    }
    beam_offset_ = (beam_offset_ + 1) % config_.beam_stride;
    if (beams.empty()) return;
    const float ll_unexpected_miss = std::log(config_.p_unexpected_miss);
    const float n_beams = static_cast<float>(beams.size());

    const int cells = gw_ * gh_;
    float max_ll = -1e30f;
    double w_avg = 0.0;
    for (int c = 0; c < cells; ++c) {
        if (!valid_[c]) continue;
        const float* er = &exp_range_[c * nh];
        const unsigned char* em = &exp_mat_[c * nh];
        for (int h = 0; h < nh; ++h) {
            float ll = 0.0f;
            for (std::size_t k = 0; k < beams.size(); ++k) {
                const Beam& b = beams[k];
                const int d = (h + b.offset) % nh;
                const bool expect_hit = em[d] != kNoHit && er[d] < max_range;
                if (!b.hit) {
                    if (expect_hit) ll += ll_unexpected_miss;
                } else if (!expect_hit) {
                    ll += b.ll_expect_miss;
                } else {
                    const int bin = std::min(kRangeBins - 1, static_cast<int>(std::fabs(b.range - er[d]) / kBinM));
                    ll += table[(k * n_mat + em[d]) * kRangeBins + bin];
                }
            }
            const int s = state(c, h);
            ll_[s] = ll;
            max_ll = std::max(max_ll, ll);
            w_avg += belief_[s] * std::exp(ll / n_beams);
        }
    }

    // Augmented-MCL style recovery signal.
    if (!have_averages_) {
        w_slow_ = w_fast_ = static_cast<float>(w_avg);
        have_averages_ = true;
    } else {
        w_slow_ += config_.alpha_slow * (static_cast<float>(w_avg) - w_slow_);
        w_fast_ += config_.alpha_fast * (static_cast<float>(w_avg) - w_fast_);
    }
    const float ratio = w_slow_ > 1e-9f ? w_fast_ / w_slow_ : 1.0f;
    const float mix = clampf(1.0f - ratio, 0.0f, config_.max_mix);
    estimate_.consistency = clampf(ratio, 0.0f, 1.0f);

    // Bayes update (tempered); when recent scans fit badly, mix in the scan's
    // own likelihood so places that look right regain belief.
    double sum_l = 0.0;
    for (int c = 0; c < cells; ++c) {
        if (!valid_[c]) continue;
        for (int h = 0; h < nh; ++h) {
            const int s = state(c, h);
            ll_[s] = std::exp(config_.temperature * (ll_[s] - max_ll));
            belief_[s] *= ll_[s];
            sum_l += ll_[s];
        }
    }
    normalise();
    if (mix > 0.005f && sum_l > 0.0) {
        const float k = static_cast<float>(mix / sum_l);
        for (int c = 0; c < cells; ++c) {
            if (!valid_[c]) continue;
            for (int h = 0; h < nh; ++h) {
                const int s = state(c, h);
                belief_[s] = (1.0f - mix) * belief_[s] + k * ll_[s];
            }
        }
    }
    diag_.injected_fraction = mix;

    // Odometry noise since the last scan, plus the discretisation floor.
    if (pending_dist_ > 0.0f || pending_rot_ > 0.0f) {
        const float cs2 = config_.cell_m * config_.cell_m;
        const float a = config_.forward_var_per_m * pending_dist_ / (2.0f * cs2) + config_.blur;
        const float b = (config_.turn_var_per_rad * pending_rot_ + config_.turn_var_per_m * pending_dist_) /
                            (2.0f * step * step) +
                        config_.blur;
        blur(std::min(a, 0.25f), std::min(b, 0.25f));
        pending_dist_ = pending_rot_ = 0.0f;
    }
    update_estimate();
}

void HistogramFilter::update_estimate() {
    const int nh = config_.headings;
    const int cells = gw_ * gh_;
    std::fill(claimed_.begin(), claimed_.end(), 0);
    diag_.hypotheses.clear();
    const int r_cells = static_cast<int>(std::ceil(config_.cluster_radius_m / config_.cell_m));
    const int r_heads = std::max(1, static_cast<int>(std::lround(config_.cluster_heading_rad / (kTwoPi / nh))));
    const float r2 = config_.cluster_radius_m * config_.cluster_radius_m;

    float remaining = 1.0f;
    for (int round = 0; round < 4 && remaining > 0.03f; ++round) {
        int best = -1;
        float best_p = 0.0f;
        for (int s = 0; s < cells * nh; ++s) {
            if (!claimed_[s] && belief_[s] > best_p) {
                best_p = belief_[s];
                best = s;
            }
        }
        if (best < 0) break;
        const int bc = best / nh;
        const int bh = best % nh;
        const Vec2 centre = position_of(bc, bh);

        float mass = 0.0f, sx = 0.0f, sz = 0.0f, sc = 0.0f, ss = 0.0f;
        const auto visit = [&](auto&& fn) {
            for (int dz = -r_cells; dz <= r_cells; ++dz) {
                for (int dx = -r_cells; dx <= r_cells; ++dx) {
                    const int x = bc % gw_ + dx;
                    const int z = bc / gw_ + dz;
                    if (x < 0 || z < 0 || x >= gw_ || z >= gh_ || !valid_[z * gw_ + x]) continue;
                    for (int dh = -r_heads; dh <= r_heads; ++dh) {
                        const int h = ((bh + dh) % nh + nh) % nh;
                        const int s = state(z * gw_ + x, h);
                        if (claimed_[s]) continue;
                        const Vec2 d = position_of(z * gw_ + x, h) - centre;
                        if (d.x * d.x + d.z * d.z > r2) continue;
                        fn(s, z * gw_ + x, h);
                    }
                }
            }
        };
        visit([&](int s, int c, int h) {
            const float p = belief_[s];
            const Vec2 pos = position_of(c, h);
            mass += p;
            sx += p * pos.x;
            sz += p * pos.z;
            sc += p * std::cos(heading_of(h));
            ss += p * std::sin(heading_of(h));
        });
        if (mass <= 0.0f) break;
        const Pose2D mean{sx / mass, sz / mass, std::atan2(ss, sc)};
        float vxx = 0.0f, vzz = 0.0f, vxz = 0.0f, vhh = 0.0f;
        visit([&](int s, int c, int h) {
            const float p = belief_[s];
            const Vec2 d = position_of(c, h) - mean.position();
            const float dh = angle_diff(heading_of(h), mean.heading_rad);
            vxx += p * d.x * d.x;
            vzz += p * d.z * d.z;
            vxz += p * d.x * d.z;
            vhh += p * dh * dh;
            claimed_[s] = 1;
        });
        diag_.hypotheses.push_back({mean, mass});
        remaining -= mass;
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

    // A systematic sample of the grid stands in for particles in the UI and
    // the planner, which only need poses with weights.
    const int k = config_.export_samples;
    diag_.particles.resize(static_cast<std::size_t>(k));
    const float inc = 1.0f / static_cast<float>(k);
    float u = 0.5f * inc;
    float acc = 0.0f;
    int i = 0;
    for (int s = 0; s < cells * nh && i < k; ++s) {
        acc += belief_[s];
        while (u < acc && i < k) {
            diag_.particles[i++] = {Pose2D{position_of(s / nh, s % nh).x, position_of(s / nh, s % nh).z,
                                           heading_of(s % nh)},
                                    inc};
            u += inc;
        }
    }
    diag_.particles.resize(static_cast<std::size_t>(i));
    diag_.effective_sample_size = 0.0f;
}

}  // namespace wobo
