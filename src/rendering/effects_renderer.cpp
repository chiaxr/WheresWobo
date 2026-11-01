#include "rendering/effects_renderer.h"

#include <algorithm>
#include <cmath>

#include "rendering/primitives.h"
#include "rlgl.h"
#include "ui/ui_text.h"

namespace wobo {

void draw_road_block(const RoadBlock& block, const Map2D& map, float alpha, float grow, bool ghost, Color tint) {
    if (block.cells.empty()) return;
    const Color orange{255, 128, 32, 255};
    const Color white{240, 240, 236, 255};
    const auto col = [&](Color c) {
        Color out = ghost ? prim::mix(c, tint, 0.6f) : c;
        return prim::with_alpha(out, alpha);
    };
    const float cs = map.cell_size();
    const float len = cs * static_cast<float>(block.cells.size());
    const float g = 1.0f - std::pow(1.0f - clampf(grow, 0.0f, 1.0f), 3.0f);
    // In a frame where the barrier runs along local x.
    rlPushMatrix();
    rlTranslatef(block.centre.x, 0.0f, block.centre.z);
    if (!block.along_x) rlRotatef(90.0f, 0, 1, 0);
    rlScalef(1.0f, std::max(0.02f, g), 1.0f);
    const auto part = [&](Vector3 c, Vector3 s, Color fill) {
        if (!ghost) {
            // Inverted-hull outline.
            rlDrawRenderBatchActive();
            rlSetCullFace(RL_CULL_FACE_FRONT);
            prim::box(c, {s.x + 0.03f, s.y + 0.03f, s.z + 0.03f}, prim::with_alpha(Color{30, 28, 40, 255}, alpha));
            rlDrawRenderBatchActive();
            rlSetCullFace(RL_CULL_FACE_BACK);
        }
        prim::box(c, s, col(fill));
    };
    // Base: alternating orange and white segments filling the cells.
    constexpr int kSegments = 6;
    const float seg = len / kSegments;
    for (int i = 0; i < kSegments; ++i) {
        const float x = -len * 0.5f + seg * (i + 0.5f);
        part({x, 0.14f, 0.0f}, {seg, 0.28f, cs * 0.84f}, i % 2 ? white : orange);
    }
    // Posts and a striped board on top.
    for (float x : {-len * 0.36f, len * 0.36f}) part({x, 0.38f, 0.0f}, {0.05f, 0.22f, 0.05f}, white);
    for (int i = 0; i < kSegments; ++i) {
        const float x = -len * 0.45f + len * 0.9f / kSegments * (i + 0.5f);
        part({x, 0.5f, 0.0f}, {len * 0.9f / kSegments, 0.1f, 0.04f}, i % 2 ? orange : white);
    }
    // Amber lamps on the ends of the board.
    if (!ghost) {
        for (float x : {-len * 0.45f, len * 0.45f}) {
            prim::sphere({x, 0.59f, 0.0f}, 0.04f, prim::with_alpha(Color{255, 196, 60, 255}, alpha), 5, 8);
        }
    }
    rlPopMatrix();
}

void EffectsRenderer::poof(Vec2 at, Color colour) { bursts_.push_back({at, colour, 0.0f, 0.9f}); }

void EffectsRenderer::float_text(Vec2 at, std::string text, Color colour) {
    texts_.push_back({at, std::move(text), colour, 0.0f, 1.6f});
}

void EffectsRenderer::shatter(Vec2 at, float radius) {
    constexpr int kShards = 18;
    for (int i = 0; i < kShards; ++i) {
        const float a = kTwoPi * i / kShards;
        shards_.push_back({{at.x + std::cos(a) * radius, at.z + std::sin(a) * radius}, a,
                           (i % 2 ? 1.0f : -1.0f) * (6.0f + i), 0.0f, 0.85f + 0.03f * (i % 5)});
    }
    bursts_.push_back({at, Color{255, 90, 95, 255}, 0.0f, 0.6f});
}

void EffectsRenderer::update(float dt_s) {
    for (auto& sh : shards_) sh.t += dt_s;
    shards_.erase(std::remove_if(shards_.begin(), shards_.end(), [](const Shard& s) { return s.t >= s.life; }),
                  shards_.end());
    for (auto& b : bursts_) b.t += dt_s;
    for (auto& t : texts_) t.t += dt_s;
    bursts_.erase(std::remove_if(bursts_.begin(), bursts_.end(), [](const Burst& b) { return b.t >= b.life; }),
                  bursts_.end());
    texts_.erase(std::remove_if(texts_.begin(), texts_.end(), [](const FloatText& t) { return t.t >= t.life; }),
                 texts_.end());
}

void EffectsRenderer::draw_bursts() const {
    // Ring fragments fly outward, hop up and tumble down.
    for (const Shard& sh : shards_) {
        const float k = sh.t / sh.life;
        const float out = 0.9f * sh.t;
        const float y = 0.05f + 1.6f * sh.t - 3.5f * sh.t * sh.t;
        if (y < 0.0f) continue;
        rlPushMatrix();
        rlTranslatef(sh.at.x + std::cos(sh.angle) * out, y, sh.at.z + std::sin(sh.angle) * out);
        rlRotatef(-sh.angle * kRadToDeg, 0, 1, 0);
        rlRotatef(sh.t * sh.spin * 60.0f, 1, 0, 0);
        prim::box({0, 0, 0}, {0.05f, 0.05f, 0.17f * (1.0f - 0.5f * k)}, Color{255, 90, 95, 255});
        rlPopMatrix();
    }
    for (const Burst& b : bursts_) {
        const float k = b.t / b.life;
        const float ease = 1.0f - (1.0f - k) * (1.0f - k);
        rlDisableDepthMask();
        prim::ring({b.at.x, 0.03f, b.at.z}, 0.1f + ease * 0.7f, 0.16f + ease * 0.75f,
                   prim::with_alpha(b.colour, 1.0f - k), 32);
        rlDrawRenderBatchActive();
        rlEnableDepthMask();
        // Puffs that rise and shrink.
        for (int i = 0; i < 7; ++i) {
            const float a = kTwoPi * i / 7.0f + 0.4f;
            const float r = 0.15f + ease * 0.35f;
            const float size = 0.11f * (1.0f - k);
            if (size <= 0.005f) continue;
            prim::sphere({b.at.x + std::cos(a) * r, 0.1f + ease * 0.5f + 0.05f * (i % 3), b.at.z + std::sin(a) * r},
                         size, prim::mix(Color{255, 255, 255, 255}, b.colour, 0.35f), 5, 8);
        }
    }
}

void EffectsRenderer::draw_active(const RunDirector& run, const Pose2D& w, float t) const {
    const PlayerActionSystem& a = run.actions();
    if (!a.active(ActionId::Range)) return;

    // Smoke Bomb: a cloud of puffs billows up round Wobo, follows it while
    // the effect lasts, then rises and thins out as it ends.
    const ActionState& st = a.state(ActionId::Range);
    const float total = std::max(st.active_total, 0.01f);
    const float elapsed = total - st.active_left;
    const float grow = 1.0f - std::pow(1.0f - clampf(elapsed / 0.5f, 0.0f, 1.0f), 3.0f);
    const float fade = clampf(st.active_left / std::min(1.5f, total * 0.4f), 0.0f, 1.0f);
    const auto frac = [](float x) { return x - std::floor(x); };
    rlDisableDepthMask();
    constexpr int kPuffs = 18;
    for (int i = 0; i < kPuffs; ++i) {
        const float drift = (i % 2 ? 1.0f : -1.0f) * t * 0.25f;
        const float ang = i * 2.39996f + drift;  // golden angle spreads them out
        const float r = (0.3f + 0.45f * frac(i * 0.618f)) * (0.6f + 0.4f * grow);
        const float y = 0.12f + 0.45f * frac(i * 0.371f) + (1.0f - fade) * 0.6f;
        const float size = (0.18f + 0.12f * frac(i * 0.77f)) * grow * (0.55f + 0.45f * fade) *
                           (1.0f + 0.08f * std::sin(t * 2.0f + i));
        if (size <= 0.01f) continue;
        const Color c = prim::with_alpha(Color{214, 214, 226, 255}, 0.55f * fade);
        prim::sphere({w.x + std::cos(ang) * r, y, w.z + std::sin(ang) * r}, size, c, 6, 10);
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

// --- Crane ----------------------------------------------------------------

namespace {

// Timeline, in seconds of real time.
constexpr float kDescend = 0.55f;
constexpr float kGrip = 0.25f;
constexpr float kLift = 0.5f;
constexpr float kCarry = 0.9f;
constexpr float kLower = 0.5f;
constexpr float kRelease = 0.25f;
constexpr float kRetract = 0.45f;
constexpr float kGripEnd = kDescend + kGrip;
constexpr float kLiftEnd = kGripEnd + kLift;
constexpr float kCarryEnd = kLiftEnd + kCarry;
constexpr float kLowerEnd = kCarryEnd + kLower;
constexpr float kReleaseEnd = kLowerEnd + kRelease;
constexpr float kTotal = kReleaseEnd + kRetract;

constexpr float kSkyY = 5.0f;    // where the hook comes from
constexpr float kGripY = 0.62f;  // hook height when holding Wobo on the ground
constexpr float kCarryY = 2.2f;  // hook height while carrying

float ease(float k) {
    k = clampf(k, 0.0f, 1.0f);
    return k * k * (3.0f - 2.0f * k);
}

}  // namespace

void CraneAnimation::start(const Pose2D& from, const Pose2D& to) {
    from_ = from;
    to_ = to;
    t_ = 0.0f;
    landed_ = false;
    active_ = true;
}

bool CraneAnimation::update(float dt_s) {
    if (!active_) return false;
    t_ += dt_s;
    bool touched_down = false;
    if (!landed_ && t_ >= kLowerEnd) {
        landed_ = true;
        touched_down = true;
    }
    if (t_ >= kTotal) active_ = false;
    return touched_down;
}

bool CraneAnimation::holding() const { return active_ && t_ < kReleaseEnd; }

CraneAnimation::Frame CraneAnimation::frame() const {
    Frame f{from_.position(), kSkyY, 1.0f, from_, 0.0f, false};
    const float t = t_;
    if (t < kDescend) {
        f.hook_y = lerpf(kSkyY, kGripY, ease(t / kDescend));
    } else if (t < kGripEnd) {
        f.hook_y = kGripY;
        f.open = 1.0f - ease((t - kDescend) / kGrip);
    } else if (t < kCarryEnd) {
        f.open = 0.0f;
        f.carrying = true;
        if (t < kLiftEnd) {
            f.hook_y = lerpf(kGripY, kCarryY, ease((t - kGripEnd) / kLift));
        } else {
            const float k = ease((t - kLiftEnd) / kCarry);
            f.hook_y = kCarryY + 0.15f * std::sin(k * kPi);
            f.hook = {lerpf(from_.x, to_.x, k), lerpf(from_.z, to_.z, k)};
            f.wobo.heading_rad = wrap_angle(from_.heading_rad + angle_diff(to_.heading_rad, from_.heading_rad) * k);
        }
    } else {
        f.hook = to_.position();
        f.wobo = to_;
        if (t < kLowerEnd) {
            f.open = 0.0f;
            f.carrying = true;
            f.hook_y = lerpf(kCarryY, kGripY, ease((t - kCarryEnd) / kLower));
        } else if (t < kReleaseEnd) {
            f.hook_y = kGripY;
            f.open = ease((t - kLowerEnd) / kRelease);
        } else {
            f.hook_y = lerpf(kGripY, kSkyY, ease((t - kReleaseEnd) / kRetract));
        }
    }
    if (f.carrying) {
        f.wobo.x = f.hook.x;
        f.wobo.z = f.hook.z;
        f.wobo_lift = f.hook_y - kGripY;
    }
    return f;
}

void CraneAnimation::draw() const {
    if (!active_) return;
    const Frame f = frame();
    const Color cable{50, 48, 64, 255};
    const Color steel{120, 124, 142, 255};
    const Color yellow{255, 200, 50, 255};
    const float y = f.hook_y;
    // Cable up into the sky.
    prim::cylinder({f.hook.x, y + 0.2f, f.hook.z}, {f.hook.x, kSkyY + 3.0f, f.hook.z}, 0.016f, cable, 6);
    // Hook block, hazard-striped.
    prim::box({f.hook.x, y + 0.13f, f.hook.z}, {0.22f, 0.14f, 0.22f}, yellow);
    prim::box({f.hook.x, y + 0.13f, f.hook.z}, {0.225f, 0.035f, 0.225f}, cable);
    prim::cylinder({f.hook.x, y + 0.06f, f.hook.z}, {f.hook.x, y + 0.2f, f.hook.z}, 0.03f, steel, 8);
    // Three claws, splayed open or curled shut round Wobo's head.
    const float spread = lerpf(10.0f, 38.0f, f.open) * kDegToRad;
    for (int i = 0; i < 3; ++i) {
        const float a = kTwoPi * i / 3.0f + 0.3f;
        const float cx = std::cos(a), cz = std::sin(a);
        const float r0 = 0.07f;
        const Vector3 root{f.hook.x + cx * r0, y + 0.06f, f.hook.z + cz * r0};
        const float reach = 0.24f;
        const Vector3 knee{root.x + cx * reach * std::sin(spread + 0.35f), y + 0.06f - reach * std::cos(spread + 0.35f),
                           root.z + cz * reach * std::sin(spread + 0.35f)};
        const float inward = lerpf(0.12f, 0.02f, f.open);
        const Vector3 tip{knee.x - cx * inward, knee.y - 0.08f, knee.z - cz * inward};
        prim::cylinder(root, knee, 0.02f, steel, 6);
        prim::cylinder(knee, tip, 0.017f, steel, 6);
        prim::sphere(tip, 0.022f, cable, 4, 6);
    }
    // A shadow on the floor so the height reads.
    rlDisableDepthMask();
    const float shade = clampf(1.0f - y / kSkyY, 0.0f, 1.0f);
    prim::disc({f.hook.x, 0.012f, f.hook.z}, 0.12f + 0.2f * shade, prim::with_alpha(Color{20, 18, 32, 255}, 0.35f * shade), 20);
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void EffectsRenderer::draw_texts(const Camera3D& camera, float s) const {
    for (const FloatText& f : texts_) {
        const float k = f.t / f.life;
        const Vector2 p = GetWorldToScreen({f.at.x, 0.9f + k * 0.5f, f.at.z}, camera);
        const float size = 22.0f * s;
        const float alpha = k < 0.75f ? 1.0f : 1.0f - (k - 0.75f) / 0.25f;
        const float w = ui::measure(f.text.c_str(), size, ui::Weight::Bold);
        ui::text(f.text.c_str(), p.x - w * 0.5f + 2.0f * s, p.y + 2.0f * s, size,
                 prim::with_alpha(Color{30, 28, 40, 255}, alpha), ui::Weight::Bold);
        ui::text(f.text.c_str(), p.x - w * 0.5f, p.y, size, prim::with_alpha(f.colour, alpha), ui::Weight::Bold);
    }
}

}  // namespace wobo
