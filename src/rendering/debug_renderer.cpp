#include "rendering/debug_renderer.h"

#include <algorithm>
#include <cmath>

#include "rendering/primitives.h"
#include "rlgl.h"

namespace wobo::overlay {

namespace {

constexpr Color kBelief{236, 72, 153, 255};   // particles / hypotheses
constexpr Color kIntent{60, 52, 80, 255};

// A flat strip from a to b at height y.
void strip(Vector3 a, Vector3 b, float width, Color c) {
    const float dx = b.x - a.x, dz = b.z - a.z;
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-5f) return;
    const float nx = -dz / len * width * 0.5f, nz = dx / len * width * 0.5f;
    // (nx, nz) points to the left of a->b; this order winds counter-clockwise
    // seen from above.
    prim::quad_up({a.x + nx, a.y, a.z + nz}, {b.x + nx, b.y, b.z + nz}, {b.x - nx, b.y, b.z - nz},
                  {a.x - nx, a.y, a.z - nz}, c);
}

}  // namespace

void particles(const LocalizationDiagnostics& diag) {
    const auto n = static_cast<float>(diag.particles.size());
    if (n <= 0.0f) return;
    rlDisableDepthMask();
    for (const WeightedPose& p : diag.particles) {
        const float rel = clampf(p.weight * n, 0.0f, 6.0f) / 6.0f;
        const Color c = prim::with_alpha(prim::mix(kBelief, Color{120, 40, 200, 255}, 1.0f - rel), 0.45f + 0.5f * rel);
        prim::arrow({p.pose.x, 0.012f, p.pose.z}, p.pose.heading_rad, 0.15f, 0.09f, c);
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void hypotheses(const LocalizationDiagnostics& diag, float time_s) {
    rlDisableDepthMask();
    int shown = 0;
    for (const WeightedPose& h : diag.hypotheses) {
        if (h.weight < 0.04f || shown >= 4) continue;
        ++shown;
        const float a = clampf(0.35f + h.weight, 0.0f, 0.95f);
        const Vector3 c{h.pose.x, 0.02f, h.pose.z};
        const float pulse = 0.02f * std::sin(time_s * 4.0f + shown);
        prim::ring(c, 0.24f + pulse, 0.28f + pulse, prim::with_alpha(kBelief, a), 28);
        prim::arrow({c.x + std::cos(h.pose.heading_rad) * 0.36f, 0.02f, c.z + std::sin(h.pose.heading_rad) * 0.36f},
                    h.pose.heading_rad, 0.16f, 0.14f, prim::with_alpha(kBelief, a));
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void scan(const RgbLidarScan& s, const Pose2D& from, float alpha) {
    if (!s.valid) return;
    const float y = 0.3f;
    const Vector3 o{from.x, y, from.z};
    rlDisableDepthMask();
    for (const LidarReturn& r : s.returns) {
        const float a = from.heading_rad + r.angle_rad;
        const Vector3 end{from.x + std::cos(a) * r.range_m, y, from.z + std::sin(a) * r.range_m};
        strip(o, end, 0.022f, prim::with_alpha(Color{255, 255, 255, 255}, (r.hit ? 0.55f : 0.22f) * alpha));
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    for (const LidarReturn& r : s.returns) {
        if (!r.hit) continue;
        const float a = from.heading_rad + r.angle_rad;
        const Vector3 end{from.x + std::cos(a) * r.range_m, y, from.z + std::sin(a) * r.range_m};
        prim::box(end, {0.075f, 0.075f, 0.075f}, prim::with_alpha(Color{30, 28, 40, 255}, alpha));
        prim::box(end, {0.055f, 0.08f, 0.055f}, prim::with_alpha(prim::to_color(r.rgb), alpha));
    }
}

void range_ring(Vec2 centre, float range_m, bool interfered, float time_s) {
    rlDisableDepthMask();
    const Color c = interfered ? Color{255, 90, 95, 255} : Color{255, 255, 255, 255};
    prim::dashed_ring({centre.x, 0.015f, centre.z}, range_m, interfered ? 0.05f : 0.03f, 36, time_s * 0.3f,
                      prim::with_alpha(c, interfered ? 0.85f : 0.4f));
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void intent(const Pose2D& from, const RelativeMotion& remaining, float time_s) {
    if (std::fabs(remaining.forward_m) < 0.05f && std::fabs(remaining.delta_heading_rad) < 0.05f) return;
    // Remaining motion is "turn, then drive": recover the turn and distance.
    const float dist = std::sqrt(remaining.forward_m * remaining.forward_m + remaining.lateral_m * remaining.lateral_m);
    const float dir = from.heading_rad + std::atan2(remaining.lateral_m, remaining.forward_m);
    rlDisableDepthMask();
    if (dist > 0.05f) {
        const int dots = std::max(2, static_cast<int>(dist / 0.12f));
        const float phase = std::fmod(time_s * 1.5f, 1.0f);
        for (int i = 1; i <= dots; ++i) {
            const float t = (static_cast<float>(i) - phase) / dots * dist;
            if (t < 0.2f) continue;
            prim::disc({from.x + std::cos(dir) * t, 0.02f, from.z + std::sin(dir) * t}, 0.025f,
                       prim::with_alpha(kIntent, 0.7f), 8);
        }
        const Vector3 end{from.x + std::cos(dir) * dist, 0.02f, from.z + std::sin(dir) * dist};
        prim::ring(end, 0.07f, 0.1f, prim::with_alpha(kIntent, 0.8f), 16);
    } else {
        // Turning on the spot: an arc showing which way.
        const float sweep = clampf(remaining.delta_heading_rad, -kPi, kPi);
        const float a0 = std::min(from.heading_rad, from.heading_rad + sweep);
        prim::arc_ring({from.x, 0.02f, from.z}, 0.3f, 0.34f, a0, a0 + std::fabs(sweep), prim::with_alpha(kIntent, 0.6f), 12);
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void candidates(const Pose2D& from, const std::vector<PlanCandidate>& cands) {
    if (cands.empty()) return;
    float lo = 1e9f, hi = -1e9f;
    for (const auto& c : cands) {
        lo = std::min(lo, c.score);
        hi = std::max(hi, c.score);
    }
    rlDisableDepthMask();
    for (const auto& c : cands) {
        const float t = hi > lo ? (c.score - lo) / (hi - lo) : 1.0f;
        const float a = from.heading_rad + c.turn_rad;
        const Vector3 o{from.x, 0.03f, from.z};
        const Vector3 e{from.x + std::cos(a) * c.forward_m, 0.03f, from.z + std::sin(a) * c.forward_m};
        const Color col = c.chosen ? Color{40, 200, 120, 230} : prim::with_alpha(prim::mix(Color{90, 90, 255, 255}, Color{255, 200, 40, 255}, t), 0.5f);
        strip(o, e, c.chosen ? 0.05f : 0.025f, col);
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void target_marker(const Pose2D& target, bool valid, float time_s) {
    const Color c = valid ? Color{6, 214, 160, 255} : Color{255, 90, 95, 255};
    const float pulse = 0.03f * std::sin(time_s * 6.0f);
    rlDisableDepthMask();
    prim::ring({target.x, 0.02f, target.z}, 0.22f + pulse, 0.28f + pulse, prim::with_alpha(c, 0.9f), 28);
    prim::arrow({target.x + std::cos(target.heading_rad) * 0.42f, 0.02f, target.z + std::sin(target.heading_rad) * 0.42f},
                target.heading_rad, 0.22f, 0.2f, prim::with_alpha(c, 0.9f));
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void min_distance_ring(Vec2 centre, float radius, float time_s) {
    rlDisableDepthMask();
    prim::dashed_ring({centre.x, 0.018f, centre.z}, radius, 0.04f, 30, -time_s * 0.4f,
                      prim::with_alpha(Color{255, 90, 95, 255}, 0.6f));
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void tether(const Pose2D& wobo, const Pose2D& guess, float time_s) {
    const float dx = guess.x - wobo.x, dz = guess.z - wobo.z;
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len < 0.05f) return;
    const float ux = dx / len, uz = dz / len;
    const Color c{255, 90, 95, 255};
    rlDisableDepthMask();
    // Dashes start just outside Wobo and flow from the guess towards it.
    const float dash = 0.16f;
    const float phase = std::fmod(time_s * 0.6f, 2.0f * dash);
    for (float t = 0.3f - phase; t < len - 0.15f; t += 2.0f * dash) {
        const float a = std::max(t, 0.3f);
        const float b = std::min(t + dash, len - 0.15f);
        if (b <= a) continue;
        strip({wobo.x + ux * a, 0.025f, wobo.z + uz * a}, {wobo.x + ux * b, 0.025f, wobo.z + uz * b}, 0.05f,
              prim::with_alpha(c, 0.8f));
    }
    // A small marker where the guess is.
    prim::ring({guess.x, 0.025f, guess.z}, 0.08f, 0.13f, prim::with_alpha(c, 0.85f), 16);
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void capture_ring(Vec2 centre, float radius, float progress, float alarm, float time_s) {
    const Vector3 c{centre.x, 0.022f, centre.z};
    const Color red{255, 90, 95, 255};
    // In the isometric view the top of the screen is world angle -3pi/4, and
    // increasing angle runs clockwise on screen.
    const float top = -0.75f * kPi;
    const Color ink{30, 28, 40, 255};
    rlDisableDepthMask();
    if (progress <= 0.002f) {
        prim::dashed_ring(c, radius, 0.035f, 18, time_s * 0.5f, prim::with_alpha(red, 0.55f));
    } else {
        const float beat = alarm * (0.5f + 0.5f * std::sin(time_s * 12.0f));
        const float w = 0.045f + 0.02f * beat;
        // Dark track underneath so the ring reads over the pink particles.
        prim::ring(c, radius - w - 0.02f, radius + w + 0.02f, prim::with_alpha(ink, 0.75f), 40);
        prim::ring(c, radius - w * 0.5f, radius + w * 0.5f, prim::with_alpha(red, 0.35f), 40);
        prim::arc_ring(c, radius - w, radius + w, top, top + kTwoPi * clampf(progress, 0.0f, 1.0f),
                       prim::with_alpha(red, 0.85f + 0.15f * beat), 40);
        // A soft glow that breathes with the pulse.
        prim::ring(c, radius + w, radius + w + 0.06f + 0.05f * beat, prim::with_alpha(red, 0.18f + 0.15f * beat),
                   40);
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

}  // namespace wobo::overlay
