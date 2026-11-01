#include "planning/goal_navigator.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>

#include "world/raycast.h"

namespace wobo {

namespace {
constexpr int kViewBeams = 32;
constexpr int kRangeBins = 5;
constexpr float kInf = std::numeric_limits<float>::max();
}  // namespace

float scan_clearance(const RgbLidarScan* scan, float direction, float radius) {
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

GoalNavigatorConfig GoalNavigatorConfig::from(const Config& cfg) {
    GoalNavigatorConfig c;
    c.landmark_weight = cfg.get_float("planner.landmark_weight", c.landmark_weight);
    c.path_cost = cfg.get_float("planner.goal_path_cost", c.path_cost);
    c.novelty_radius_m = cfg.get_float("planner.goal_novelty_radius", c.novelty_radius_m);
    c.reach_m = cfg.get_float("planner.goal_reach", c.reach_m);
    c.timeout_s = cfg.get_float("planner.goal_timeout", c.timeout_s);
    c.safety_margin_m = cfg.get_float("planner.safety_margin", c.safety_margin_m);
    return c;
}

void GoalNavigator::reset() {
    trail_.clear();
    has_goal_ = false;
    goal_cell_ = -1;
    goals_chosen_ = 0;
    blocked_ = 0;
    failed_.clear();
    built_for_ = nullptr;
}

void GoalNavigator::record(const PlannerContext& ctx) {
    const Vec2 p = ctx.odom_pose.position();
    if (!trail_.empty() && distance(trail_.back(), p) < 0.25f) return;
    trail_.push_back(p);
    if (trail_.size() > 240) trail_.pop_front();
}

const std::vector<float>& GoalNavigator::landmarks(const Map2D& map, float radius, float max_range) {
    if (built_for_ != &map) build(map, radius, max_range);
    return landmark_;
}

void GoalNavigator::build(const Map2D& map, float radius, float max_range) {
    built_for_ = &map;
    const int w = map.width();
    const int h = map.height();
    const int n = w * h;
    traversable_.assign(static_cast<std::size_t>(n), 0);
    landmark_.assign(static_cast<std::size_t>(n), 0.0f);
    slot_.assign(static_cast<std::size_t>(n), -1);
    ranges_.clear();
    hit_material_.clear();
    desc_.clear();

    // Per traversable cell: the 360-degree view (range and material per
    // direction), and a rotation-invariant descriptor of it -- a histogram of
    // ranges (plus "nothing in range") and one of wall materials.
    const int n_mat = static_cast<int>(map.materials().size());
    dims_ = kRangeBins + 1 + n_mat;
    std::vector<int> cells;
    for (int i : map.free_cells()) {
        const Vec2 c = map.cell_center(i % w, i / w);
        if (!map.circle_free(c.x, c.z, radius)) continue;
        traversable_[i] = 1;
        slot_[i] = static_cast<int>(cells.size());
        cells.push_back(i);
        desc_.resize(desc_.size() + static_cast<std::size_t>(dims_), 0.0f);
        float* d = &desc_[desc_.size() - static_cast<std::size_t>(dims_)];
        for (int k = 0; k < kViewBeams; ++k) {
            const RayHit e = cast_ray(map, c.x, c.z, kTwoPi * k / kViewBeams, max_range);
            ranges_.push_back(e.range);
            hit_material_.push_back(e.hit ? static_cast<int>(e.material) : -1);
            if (!e.hit) {
                d[kRangeBins] += 1.0f;
                continue;
            }
            d[std::min(kRangeBins - 1, static_cast<int>(e.range / max_range * kRangeBins))] += 1.0f;
            d[kRangeBins + 1 + e.material] += 1.0f;
        }
        for (int j = 0; j < dims_; ++j) d[j] /= kViewBeams;
    }

    // A cell's distinctiveness is how far its view is from the most similar
    // view anywhere else (cells closer than this share most of what they see).
    const float apart2 = 1.5f * 1.5f;
    desc_scale_ = 0.0f;
    for (std::size_t a = 0; a < cells.size(); ++a) {
        const Vec2 pa = map.cell_center(cells[a] % w, cells[a] / w);
        float nearest = kInf;
        for (std::size_t b = 0; b < cells.size(); ++b) {
            const Vec2 pb = map.cell_center(cells[b] % w, cells[b] / w);
            const Vec2 d = pa - pb;
            if (d.x * d.x + d.z * d.z < apart2) continue;
            float l1 = 0.0f;
            for (int j = 0; j < dims_ && l1 < nearest; ++j) {
                l1 += std::fabs(desc_[a * dims_ + j] - desc_[b * dims_ + j]);
            }
            nearest = std::min(nearest, l1);
        }
        if (nearest == kInf) nearest = 0.0f;
        landmark_[cells[a]] = nearest;
        desc_scale_ = std::max(desc_scale_, nearest);
    }
    if (desc_scale_ > 0.0f) {
        for (float& v : landmark_) v /= desc_scale_;
    } else {
        desc_scale_ = 1.0f;
    }
}

float GoalNavigator::view_difference(const Map2D& map, int cell, Vec2 other) const {
    const int ox = map.cell_x(other.x);
    const int oz = map.cell_z(other.z);
    if (!map.in_bounds(ox, oz)) return 1.0f;
    const int a = slot_[cell];
    const int b = slot_[oz * map.width() + ox];
    if (a < 0 || b < 0) return 1.0f;  // the other would be in (or against) a wall
    float l1 = 0.0f;
    for (int j = 0; j < dims_; ++j) l1 += std::fabs(desc_[a * dims_ + j] - desc_[b * dims_ + j]);
    return std::min(1.0f, l1 / desc_scale_);
}

void GoalNavigator::find_twins(const Map2D& map, const Pose2D& estimate, int cell, float max_range,
                               std::vector<Pose2D>* twins) const {
    // Poses elsewhere whose view matches the estimate's under some rotation:
    // where Wobo could equally be. Compared at cell centres, so the exact
    // twins of a periodic or symmetric map match almost perfectly.
    const int w = map.width();
    const int se = slot_[cell];
    if (se < 0) return;
    const Vec2 ce = map.cell_center(cell % w, cell / w);
    struct Match {
        float cost;
        int cell;
        int shift;
    };
    std::vector<Match> matches;
    for (int i : map.free_cells()) {
        const int si = slot_[i];
        if (si < 0 || distance(map.cell_center(i % w, i / w), ce) < 1.5f) continue;
        float best = kInf;
        int best_shift = 0;
        for (int sh = 0; sh < kViewBeams; ++sh) {
            float cost = 0.0f;
            for (int m = 0; m < kViewBeams && cost < best; ++m) {
                const int a = se * kViewBeams + m;
                const int b = si * kViewBeams + (m + sh) % kViewBeams;
                cost += std::fabs(ranges_[a] - ranges_[b]) / max_range;
                if (hit_material_[a] >= 0 && hit_material_[b] >= 0 && hit_material_[a] != hit_material_[b]) {
                    cost += 0.5f;
                }
            }
            if (cost < best) {
                best = cost;
                best_shift = sh;
            }
        }
        if (best / kViewBeams < 0.06f) matches.push_back({best, i, best_shift});
    }
    std::sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) { return a.cost < b.cost; });
    for (const Match& m : matches) {
        const Vec2 c = map.cell_center(m.cell % w, m.cell / w);
        const float rot = kTwoPi * m.shift / kViewBeams;
        const Vec2 off = estimate.position() - ce;
        const Pose2D t{c.x + std::cos(rot) * off.x - std::sin(rot) * off.z,
                       c.z + std::sin(rot) * off.x + std::cos(rot) * off.z,
                       wrap_angle(estimate.heading_rad + rot)};
        bool close = false;
        for (const Pose2D& o : *twins) close = close || distance(o.position(), t.position()) < 1.0f;
        if (!close) twins->push_back(t);
        if (twins->size() >= 8) break;
    }
}

std::vector<Pose2D> GoalNavigator::look_alikes(const Map2D& map, const Pose2D& estimate, float radius,
                                               float max_range) {
    landmarks(map, radius, max_range);
    std::vector<Pose2D> out;
    const int cell = nearest_traversable(map, estimate.position());
    if (cell >= 0) find_twins(map, estimate, cell, max_range, &out);
    return out;
}

void GoalNavigator::distance_field(const Map2D& map, int from) {
    const int w = map.width();
    const int h = map.height();
    const float cs = map.cell_size();
    dist_.assign(static_cast<std::size_t>(w * h), kInf);
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    dist_[from] = 0.0f;
    open.push({0.0f, from});
    while (!open.empty()) {
        const auto [d, i] = open.top();
        open.pop();
        if (d > dist_[i]) continue;
        const int cx = i % w;
        const int cz = i / w;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) continue;
                const int x = cx + dx;
                const int z = cz + dz;
                if (x < 0 || z < 0 || x >= w || z >= h || !traversable_[z * w + x]) continue;
                // No corner cutting.
                if (dx != 0 && dz != 0 && (!traversable_[cz * w + x] || !traversable_[z * w + cx])) continue;
                const float nd = d + (dx != 0 && dz != 0 ? cs * 1.41421356f : cs);
                if (nd < dist_[z * w + x]) {
                    dist_[z * w + x] = nd;
                    open.push({nd, z * w + x});
                }
            }
        }
    }
}

int GoalNavigator::nearest_traversable(const Map2D& map, Vec2 p) const {
    const int w = map.width();
    const int h = map.height();
    const int cx = map.cell_x(p.x);
    const int cz = map.cell_z(p.z);
    int best = -1;
    float best_d = kInf;
    for (int r = 0; r <= 3 && best < 0; ++r) {
        for (int z = cz - r; z <= cz + r; ++z) {
            for (int x = cx - r; x <= cx + r; ++x) {
                if (x < 0 || z < 0 || x >= w || z >= h || !traversable_[z * w + x]) continue;
                const float d = distance(map.cell_center(x, z), p);
                if (d < best_d) {
                    best_d = d;
                    best = z * w + x;
                }
            }
        }
    }
    return best;
}

bool GoalNavigator::segment_free(const Map2D& map, Vec2 a, Vec2 b, float radius) const {
    const float len = distance(a, b);
    const int steps = std::max(1, static_cast<int>(std::ceil(len / 0.1f)));
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / steps;
        if (!map.circle_free(lerpf(a.x, b.x, t), lerpf(a.z, b.z, t), radius)) return false;
    }
    return true;
}

bool GoalNavigator::step(const Map2D& map, const Pose2D& estimate, const LocalizationDiagnostics* diagnostics,
                         const PlannerContext& ctx, MotionPlan* out) {
    const float radius = ctx.robot_radius_m;
    landmarks(map, radius, ctx.nominal_max_range_m);
    const int w = map.width();
    const int start = nearest_traversable(map, estimate.position());
    if (start < 0) return false;

    // The estimate jumping (relocalization) invalidates a goal chosen in the
    // old frame: compare it with where odometry says it should have gone.
    const Pose2D expected = compose(last_estimate_, between(last_odom_, ctx.odom_pose));
    const bool jumped = distance(expected.position(), estimate.position()) > 1.0f ||
                        std::fabs(angle_diff(expected.heading_rad, estimate.heading_rad)) > 0.8f;
    last_estimate_ = estimate;
    last_odom_ = ctx.odom_pose;

    if (has_goal_) {
        const Vec2 g = map.cell_center(goal_cell_ % w, goal_cell_ / w);
        if (jumped || distance(g, estimate.position()) < config_.reach_m ||
            ctx.time_s - goal_since_s_ > config_.timeout_s) {
            has_goal_ = false;
        }
    }

    if (!has_goal_) {
        // Trail into the map frame, through the current estimate.
        std::vector<Vec2> seen;
        seen.reserve(trail_.size());
        for (const Vec2& t : trail_) {
            seen.push_back(compose(estimate, between(ctx.odom_pose, Pose2D{t.x, t.z, 0.0f})).position());
        }
        // Rivals: the localizer's other hypotheses, and the look-alikes of
        // the estimate it may have dropped. The goal is a place where the
        // estimate and its rivals expect to see different things.
        std::vector<Pose2D> rivals;
        if (diagnostics) {
            for (std::size_t k = 1; k < diagnostics->hypotheses.size(); ++k) {
                const WeightedPose& hyp = diagnostics->hypotheses[k];
                if (hyp.weight > 0.05f && distance(hyp.pose.position(), estimate.position()) > 1.0f) {
                    rivals.push_back(hyp.pose);
                }
            }
        }
        find_twins(map, estimate, start, ctx.nominal_max_range_m, &rivals);
        twins_ = static_cast<int>(rivals.size());

        distance_field(map, start);
        float best = -kInf;
        std::erase_if(failed_, [&](const auto& f) { return f.second < ctx.time_s; });
        for (int i : map.free_cells()) {
            if (!traversable_[i] || dist_[i] == kInf || dist_[i] < config_.reach_m) continue;
            if (std::any_of(failed_.begin(), failed_.end(), [&](const auto& f) {
                    const int fx = f.first % w - i % w;
                    const int fz = f.first / w - i / w;
                    return fx * fx + fz * fz <= 4;
                })) {
                continue;
            }
            const Vec2 c = map.cell_center(i % w, i / w);
            float nearest = config_.novelty_radius_m;
            for (const Vec2& s : seen) nearest = std::min(nearest, distance(s, c));
            const float novelty = nearest / config_.novelty_radius_m;
            float telling = landmark_[i];
            if (!rivals.empty()) {
                telling = 1.0f;
                for (const Pose2D& r : rivals) {
                    const Vec2 there = compose(r, between(estimate, Pose2D{c.x, c.z, 0.0f})).position();
                    telling = std::min(telling, view_difference(map, i, there));
                }
            }
            const float score =
                novelty * (1.0f + config_.landmark_weight * telling) - config_.path_cost * dist_[i];
            if (score > best) {
                best = score;
                goal_cell_ = i;
                goal_telling_ = telling;
            }
        }
        if (best == -kInf) return false;
        has_goal_ = true;
        ++goals_chosen_;
        goal_since_s_ = ctx.time_s;
    }

    // Walk the path down the goal's distance field, and aim at the furthest
    // point of it in a straight, clear line.
    distance_field(map, goal_cell_);
    if (dist_[start] == kInf) {
        has_goal_ = false;
        return false;
    }
    const Vec2 here = estimate.position();
    Vec2 aim = map.cell_center(goal_cell_ % w, goal_cell_ / w);
    Vec2 first = aim;
    int cur = start;
    for (int guard = 0; guard < 64 && cur != goal_cell_; ++guard) {
        int next = cur;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = cur % w + dx;
                const int z = cur / w + dz;
                if (x < 0 || z < 0 || x >= w || z >= map.height() || !traversable_[z * w + x]) continue;
                if (dx != 0 && dz != 0 && (!traversable_[cur / w * w + x] || !traversable_[z * w + cur % w])) {
                    continue;  // the same no-corner-cutting rule as the distance field
                }
                if (dist_[z * w + x] < dist_[next]) next = z * w + x;
            }
        }
        if (next == cur) break;
        const Vec2 c = map.cell_center(next % w, next / w);
        if (guard == 0) first = aim = c;
        if (distance(here, c) > config_.step_m || !segment_free(map, here, c, radius)) break;
        aim = c;
        cur = next;
    }

    // Try the long leg; if the scan disagrees with the map about it (a
    // diagonal out of a doorway shaves the frame), the next cell of the path.
    out->kind = MotionPlan::Kind::Verify;
    out->forward_m = 0.0f;
    for (const Vec2 target : {aim, first}) {
        const Vec2 d = target - here;
        const float turn = angle_diff(std::atan2(d.z, d.x), estimate.heading_rad);
        float clear = scan_clearance(ctx.last_scan, turn, radius) - radius - config_.safety_margin_m;
        if (clear < 0.25f) {
            // Doorways are barely wider than Wobo: creep through on a tighter
            // corridor (the agent's front stop and bump reflex still guard it).
            clear = std::min(0.5f, scan_clearance(ctx.last_scan, turn, radius - 0.03f) - radius);
        }
        out->turn_rad = turn;
        out->forward_m = std::min({d.length(), config_.step_m, clear});
        if (out->forward_m >= 0.15f) break;
    }
    if (out->forward_m < 0.15f) {
        out->forward_m = 0.0f;
        // The way the estimate says is open is not: give the goal up for a
        // while rather than stare at the obstacle.
        if (++blocked_ >= 3) {
            failed_.push_back({goal_cell_, ctx.time_s + 2.0 * config_.timeout_s});
            has_goal_ = false;
            blocked_ = 0;
        }
    } else {
        blocked_ = 0;
    }
    out->expected_gain = 0.0f;
    out->reason = goal_telling_ > 0.5f ? "Checking a landmark" : "Heading somewhere new";
    return true;
}

}  // namespace wobo
