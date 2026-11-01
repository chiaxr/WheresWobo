#include "simulation/movers.h"

#include <algorithm>
#include <cstdlib>
#include <queue>

#include "core/config.h"

namespace wobo {

namespace {

constexpr float kTurnRate = 3.0f;      // rad/s
constexpr float kGiveWay = 0.12f;      // gap kept from Wobo (m)
constexpr float kGiveUpWaiting = 3.0f; // s before going somewhere else

std::vector<Vec2> parse_points(const std::string& s) {
    std::vector<Vec2> out;
    for (const std::string& pt : split(s, ';')) {
        const auto xz = split(pt, ',');
        if (xz.size() == 2) out.push_back({std::strtof(xz[0].c_str(), nullptr), std::strtof(xz[1].c_str(), nullptr)});
    }
    return out;
}

std::string param(const MoverSpec& s, const std::string& key) {
    const auto it = s.params.find(key);
    return it == s.params.end() ? "" : it->second;
}

// A straight walk from a to b keeps a disc of radius r clear of the world.
bool line_free(const Map2D& world, Vec2 a, Vec2 b, float r) {
    const float len = distance(a, b);
    const int n = std::max(1, static_cast<int>(len / 0.1f));
    for (int i = 1; i <= n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        if (!world.circle_free(lerpf(a.x, b.x, t), lerpf(a.z, b.z, t), r)) return false;
    }
    return true;
}

}  // namespace

Rgb mover_colour(const std::string& kind) {
    if (kind == "donkey") return Rgb{128, 112, 104};
    if (kind == "guard") return Rgb{44, 58, 108};
    return Rgb{170, 70, 60};  // walker
}

float mover_radius(const std::string& kind) {
    return kind == "donkey" ? 0.3f : 0.18f;
}

void MoverSystem::reset(const std::vector<MoverSpec>& specs, const std::vector<std::uint8_t>& materials,
                        std::uint64_t seed) {
    rng_.reseed(derive_seed(seed, "movers"));
    movers_.clear();
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const MoverSpec& s = specs[i];
        Mover m;
        m.kind = s.kind;
        m.pose = m.prev_pose = {s.position.x, s.position.z, rng_.uniform(-kPi, kPi)};
        m.radius = mover_radius(s.kind);
        m.speed_mps = s.kind == "donkey" ? 0.3f : 0.38f;
        m.material = i < materials.size() ? materials[i] : 0;
        m.route = parse_points(param(s, "route"));
        m.hay = parse_points(param(s, "hay"));
        m.wander = parse_points(param(s, "wander"));
        const auto rest = parse_points(param(s, "rest"));
        if (!rest.empty()) m.rest = rest[0];
        const std::string land = param(s, "land");
        m.land = land.empty() ? 0 : land[0] - 'A';
        for (const std::string& b : split(param(s, "bridges"), ';')) {
            const auto f = split(b, ',');
            if (f.size() != 6 || f[0].empty() || f[1].empty()) continue;
            m.bridges.push_back({f[0][0] - 'A', f[1][0] - 'A',
                                 {std::strtof(f[2].c_str(), nullptr), std::strtof(f[3].c_str(), nullptr)},
                                 {std::strtof(f[4].c_str(), nullptr), std::strtof(f[5].c_str(), nullptr)}});
        }
        m.crossed.assign(m.bridges.size(), 0);
        m.wait_s = rng_.uniform(0.5f, 2.0f);
        movers_.push_back(std::move(m));
    }
}

std::vector<Shape> MoverSystem::discs() const {
    std::vector<Shape> out;
    for (const Mover& m : movers_) out.push_back(Shape::disc(m.pose.position(), m.radius, m.material, 1.0f));
    return out;
}

bool MoverSystem::overlaps(Vec2 p, float r) const {
    for (const Mover& m : movers_) {
        if (distance(p, m.pose.position()) < r + m.radius) return true;
    }
    return false;
}

void MoverSystem::set_mood(Mover& m, MoverMood mood, float duration) {
    m.mood = mood;
    m.mood_s = 0.0f;
    m.wait_s = duration;
}

bool MoverSystem::plan(Mover& m, const Map2D& world, Vec2 goal) {
    // A* over cells whose centre leaves the mover room, 8-connected (no
    // corner cutting), then string-pulled into straight runs.
    const int w = world.width();
    const int h = world.height();
    const auto open = [&](int cx, int cz) {
        return world.in_bounds(cx, cz) && !world.solid(cx, cz) && world.cell_clearance(world.index(cx, cz)) >= m.radius;
    };
    const int start = world.index(std::clamp(world.cell_x(m.pose.x), 0, w - 1),
                                  std::clamp(world.cell_z(m.pose.z), 0, h - 1));
    // The goal cell, or the nearest open one to it.
    int target = -1;
    float best = 1e9f;
    const int gx = world.cell_x(goal.x), gz = world.cell_z(goal.z);
    for (int dz = -3; dz <= 3; ++dz) {
        for (int dx = -3; dx <= 3; ++dx) {
            if (!open(gx + dx, gz + dz)) continue;
            const float d = static_cast<float>(dx * dx + dz * dz);
            if (d < best) { best = d; target = world.index(gx + dx, gz + dz); }
        }
    }
    if (target < 0) return false;

    std::vector<float> g(static_cast<std::size_t>(w * h), 1e9f);
    std::vector<int> from(static_cast<std::size_t>(w * h), -1);
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open_set;
    const auto heuristic = [&](int i) {
        const float dx = std::fabs(static_cast<float>(i % w - target % w));
        const float dz = std::fabs(static_cast<float>(i / w - target / w));
        return std::max(dx, dz) + 0.4142f * std::min(dx, dz);
    };
    g[start] = 0.0f;
    open_set.push({heuristic(start), start});
    bool found = false;
    while (!open_set.empty()) {
        const auto [f, i] = open_set.top();
        open_set.pop();
        if (i == target) { found = true; break; }
        if (f > g[i] + heuristic(i) + 1e-4f) continue;
        const int cx = i % w, cz = i / w;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) continue;
                if (!open(cx + dx, cz + dz)) continue;
                if (dx != 0 && dz != 0 && (!open(cx + dx, cz) || !open(cx, cz + dz))) continue;
                const int j = world.index(cx + dx, cz + dz);
                const float cost = g[i] + (dx != 0 && dz != 0 ? 1.4142f : 1.0f);
                if (cost >= g[j]) continue;
                g[j] = cost;
                from[j] = i;
                open_set.push({cost + heuristic(j), j});
            }
        }
    }
    if (!found) return false;

    std::vector<Vec2> cells;
    for (int i = target; i != start && i >= 0; i = from[i]) cells.push_back(world.cell_center(i % w, i / w));
    std::reverse(cells.begin(), cells.end());
    if (world.circle_free(goal.x, goal.z, m.radius) && world.cell_x(goal.x) == target % w &&
        world.cell_z(goal.z) == target / w) {
        if (!cells.empty()) cells.back() = goal;
        else cells.push_back(goal);
    }
    m.path.clear();
    Vec2 at = m.pose.position();
    std::size_t k = 0;
    while (k < cells.size()) {
        // Furthest waypoint still in a straight, clear line.
        std::size_t far = k;
        for (std::size_t j = k + 1; j < cells.size() && j < k + 12; ++j) {
            if (line_free(world, at, cells[j], m.radius * 0.95f)) far = j;
        }
        m.path.push_back(cells[far]);
        at = cells[far];
        k = far + 1;
    }
    m.goal = goal;
    m.has_goal = true;
    set_mood(m, MoverMood::Walking, 0.0f);
    return true;
}

void MoverSystem::think(Mover& m, const Map2D& world) {
    bool ok = false;
    if (m.kind == "guard" && !m.route.empty()) {
        if (m.has_goal) m.stop = (m.stop + 1) % static_cast<int>(m.route.size());
        ok = plan(m, world, m.route[static_cast<std::size_t>(m.stop)]);
    } else if (m.kind == "walker" && !m.bridges.empty()) {
        if (m.bridge < 0) {
            std::vector<int> options;
            for (std::size_t b = 0; b < m.bridges.size(); ++b) {
                const Mover::Bridge& br = m.bridges[b];
                if (!m.crossed[b] && (br.a == m.land || br.b == m.land)) options.push_back(static_cast<int>(b));
            }
            if (options.empty()) {
                // Every bridge from here is used up. Start again.
                std::fill(m.crossed.begin(), m.crossed.end(), 0);
                set_mood(m, MoverMood::Shrugging, 3.0f);
                return;
            }
            m.bridge = options[rng_.below(static_cast<std::uint32_t>(options.size()))];
            m.crossing = false;
        }
        const Mover::Bridge& br = m.bridges[static_cast<std::size_t>(m.bridge)];
        const bool from_a = br.a == m.land;
        const Vec2 near = from_a ? br.end_a : br.end_b;
        const Vec2 far = from_a ? br.end_b : br.end_a;
        ok = plan(m, world, m.crossing ? far : near);
    } else if (m.kind == "donkey") {
        if (m.stage == 2 && m.hay.size() >= 2) {
            m.pile = static_cast<int>(rng_.below(2));
            m.stage = 3;
            ok = plan(m, world, m.hay[static_cast<std::size_t>(m.pile)]);
        } else if (m.stage == 0 && rng_.chance(0.45f) && m.hay.size() >= 2) {
            m.stage = 1;
            ok = plan(m, world, m.rest);
        } else if (!m.wander.empty()) {
            m.stage = 0;
            ok = plan(m, world, m.wander[rng_.below(static_cast<std::uint32_t>(m.wander.size()))]);
        }
    }
    if (!ok) {
        m.has_goal = false;
        if (m.kind == "walker") m.bridge = -1;
        if (m.kind == "donkey") m.stage = 0;
        set_mood(m, MoverMood::Pausing, 1.0f);
    }
}

void MoverSystem::walk(Mover& m, float dt, const Map2D& world, const Pose2D& wobo, float wobo_radius) {
    if (m.path.empty()) {
        // Arrived.
        if (m.kind == "guard") {
            set_mood(m, MoverMood::Pausing, rng_.uniform(1.5f, 3.5f));
        } else if (m.kind == "walker") {
            if (!m.crossing) {
                m.crossing = true;
                think(m, world);
                return;
            }
            const Mover::Bridge& br = m.bridges[static_cast<std::size_t>(m.bridge)];
            m.crossed[static_cast<std::size_t>(m.bridge)] = 1;
            m.land = br.a == m.land ? br.b : br.a;
            m.bridge = -1;
            m.crossing = false;
            const int n = static_cast<int>(std::count(m.crossed.begin(), m.crossed.end(), 1));
            m.best_crossings = std::max(m.best_crossings, n);
            set_mood(m, MoverMood::Pausing, rng_.uniform(0.5f, 1.5f));
        } else if (m.kind == "donkey") {
            if (m.stage == 1) {
                // Square between the piles: one on each side.
                const Vec2 d = m.hay[1] - m.hay[0];
                m.pose.heading_rad = wrap_angle(std::atan2(d.z, d.x) + 0.5f * kPi);
                m.stage = 2;
                set_mood(m, MoverMood::Torn, rng_.uniform(5.0f, 9.0f));
            } else if (m.stage == 3) {
                m.stage = 0;
                set_mood(m, MoverMood::Eating, rng_.uniform(6.0f, 10.0f));
            } else {
                set_mood(m, MoverMood::Pausing, rng_.uniform(2.0f, 4.0f));
            }
        } else {
            set_mood(m, MoverMood::Pausing, 2.0f);
        }
        m.has_goal = m.kind == "guard";  // the guard's next stop follows on
        return;
    }

    const Vec2 at = m.pose.position();
    const Vec2 to = m.path.front();
    const Vec2 d = to - at;
    const float dist = d.length();
    if (dist < 0.06f) {
        m.path.erase(m.path.begin());
        return;
    }
    const float want = std::atan2(d.z, d.x);
    const float err = angle_diff(want, m.pose.heading_rad);
    const float turn = clampf(err, -kTurnRate * dt, kTurnRate * dt);
    m.pose.heading_rad = wrap_angle(m.pose.heading_rad + turn);
    const float speed = m.speed_mps * std::max(0.0f, std::cos(err));
    const float step = std::min(dist, speed * dt);
    const Vec2 next = at + Vec2{std::cos(m.pose.heading_rad), std::sin(m.pose.heading_rad)} * step;

    // Give way to Wobo: never step closer once near.
    const float room = m.radius + wobo_radius + kGiveWay;
    const float now_gap = distance(at, wobo.position());
    if (distance(next, wobo.position()) < room && distance(next, wobo.position()) < now_gap) {
        if (m.mood != MoverMood::Waiting) set_mood(m, MoverMood::Waiting, kGiveUpWaiting);
        return;
    }
    if (!world.circle_free(next.x, next.z, m.radius * 0.9f)) {
        // Something new in the way (a Road Block): find another way.
        if (!m.has_goal || !plan(m, world, m.goal)) {
            m.path.clear();
            m.has_goal = false;
            set_mood(m, MoverMood::Pausing, 1.0f);
        }
        return;
    }
    if (m.mood != MoverMood::Walking) set_mood(m, MoverMood::Walking, 0.0f);
    m.pose.x = next.x;
    m.pose.z = next.z;
    m.stride_m += step;
}

void MoverSystem::step(float dt, const Map2D& world, const Pose2D& wobo, float wobo_radius) {
    for (Mover& m : movers_) {
        m.prev_pose = m.pose;
        m.mood_s += dt;
        switch (m.mood) {
            case MoverMood::Walking:
                walk(m, dt, world, wobo, wobo_radius);
                break;
            case MoverMood::Waiting:
                if (m.mood_s >= m.wait_s) {
                    // Wobo will not move: go somewhere else.
                    // (The walker sticks with its bridge.)
                    m.path.clear();
                    if (m.kind == "donkey") m.stage = 0;
                    set_mood(m, MoverMood::Pausing, 0.5f);
                } else {
                    walk(m, dt, world, wobo, wobo_radius);
                }
                break;
            default:
                if (m.mood_s >= m.wait_s) think(m, world);
                break;
        }
    }
}

}  // namespace wobo
