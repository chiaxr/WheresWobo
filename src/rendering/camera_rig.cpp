#include "rendering/camera_rig.h"

#include <algorithm>
#include <cmath>

#include "raymath.h"

namespace wobo {

namespace {

constexpr float kYaw = 45.0f * kDegToRad;
constexpr float kPitch = 40.0f * kDegToRad;
constexpr float kDistance = 60.0f;

Vector3 view_offset(bool top_down) {
    if (top_down) return {0.0f, 1.0f, 0.0f};
    return {std::cos(kPitch) * std::sin(kYaw), std::sin(kPitch), std::cos(kPitch) * std::cos(kYaw)};
}

Vector3 view_up(bool top_down) { return top_down ? Vector3{0.0f, 0.0f, -1.0f} : Vector3{0.0f, 1.0f, 0.0f}; }

}  // namespace

Vector3 CameraRig::screen_right() const {
    const Vector3 forward = Vector3Negate(view_offset(top_down_));
    return Vector3Normalize(Vector3CrossProduct(forward, view_up(top_down_)));
}

// The ground direction that moves things up the screen, scaled so that one
// unit along it moves one world unit up the screen.
Vector3 CameraRig::screen_up_on_ground() const {
    const Vector3 forward = Vector3Negate(view_offset(top_down_));
    const Vector3 up = Vector3CrossProduct(screen_right(), forward);
    const Vector3 g{up.x, 0.0f, up.z};
    const float len2 = g.x * g.x + g.z * g.z;
    return len2 > 1e-6f ? Vector3Scale(g, 1.0f / len2) : Vector3{0, 0, -1};
}

void CameraRig::fit(float map_width, float map_depth, int screen_w, int screen_h) {
    map_w_ = map_width;
    map_d_ = map_depth;
    const Vector3 forward = Vector3Negate(view_offset(top_down_));
    const Vector3 r = screen_right();
    const Vector3 s = Vector3CrossProduct(r, forward);
    float rmin = 1e9f, rmax = -1e9f, smin = 1e9f, smax = -1e9f;
    for (float x : {0.0f, map_width}) {
        for (float z : {0.0f, map_depth}) {
            for (float y : {0.0f, 1.2f}) {
                const Vector3 p{x, y, z};
                rmin = std::min(rmin, Vector3DotProduct(p, r));
                rmax = std::max(rmax, Vector3DotProduct(p, r));
                smin = std::min(smin, Vector3DotProduct(p, s));
                smax = std::max(smax, Vector3DotProduct(p, s));
            }
        }
    }
    const float aspect = static_cast<float>(screen_w) / static_cast<float>(std::max(1, screen_h));
    const float usable = std::max(0.3f, 1.0f - inset_top_ - inset_bottom_);
    fit_height_ = std::max((smax - smin) / usable, (rmax - rmin) / aspect) * 1.06f;

    // Centre of the framed box, then nudge so it sits in the usable band.
    Vec2 centre{map_width * 0.5f, map_depth * 0.5f};
    const float s_mid = 0.5f * (smin + smax);
    const float s_centre = Vector3DotProduct(Vector3{centre.x, 0.0f, centre.z}, s);
    // A top inset (HUD) pushes the content down, so the camera centre sits
    // above the content centre.
    const float shift = (s_mid - s_centre) + (inset_top_ - inset_bottom_) * 0.5f * fit_height_;
    const Vector3 up_g = screen_up_on_ground();
    centre.x += up_g.x * shift;
    centre.z += up_g.z * shift;
    target_ = target_goal_ = centre;
    zoom_ = zoom_goal_ = 1.0f;
    rebuild();
}

void CameraRig::update(float dt_s) {
    const float k = 1.0f - std::exp(-dt_s * 8.0f);
    target_.x = lerpf(target_.x, target_goal_.x, k);
    target_.z = lerpf(target_.z, target_goal_.z, k);
    zoom_ = lerpf(zoom_, zoom_goal_, k);
    rebuild();
}

namespace {

// Ground point under a screen position for a given camera, using raylib's own
// unprojection so pan and zoom stay glued to the cursor.
bool ground_under(const Camera3D& cam, Vector2 screen, Vec2* out) {
    const Ray ray = GetScreenToWorldRay(screen, cam);
    if (std::fabs(ray.direction.y) < 1e-6f) return false;
    const float t = -ray.position.y / ray.direction.y;
    out->x = ray.position.x + ray.direction.x * t;
    out->z = ray.position.z + ray.direction.z * t;
    return true;
}

}  // namespace

void CameraRig::pan_pixels(Vector2 delta, int screen_h) {
    (void)screen_h;
    const Vector2 centre{GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f};
    Vec2 a, b;
    if (!ground_under(camera_, centre, &a) || !ground_under(camera_, {centre.x - delta.x, centre.y - delta.y}, &b)) {
        return;
    }
    target_goal_.x = clampf(target_goal_.x + (b.x - a.x), -2.0f, map_w_ + 2.0f);
    target_goal_.z = clampf(target_goal_.z + (b.z - a.z), -2.0f, map_d_ + 2.0f);
    target_ = target_goal_;
    rebuild();
}

void CameraRig::zoom_at(float factor, Vector2 screen_point, int screen_w, int screen_h) {
    (void)screen_w;
    (void)screen_h;
    Vec2 before, after;
    const bool had = ground_under(camera_, screen_point, &before);
    // In as far as a quarter of the whole map, or a view about 2.5 m tall on
    // big maps, whichever is closer.
    const float closest = std::min(0.25f, 2.5f / std::max(fit_height_, 1e-3f));
    zoom_ = zoom_goal_ = clampf(zoom_goal_ * factor, closest, 1.5f);
    target_ = target_goal_;
    rebuild();
    // Keep the point under the cursor fixed.
    if (had && ground_under(camera_, screen_point, &after)) {
        target_goal_.x += before.x - after.x;
        target_goal_.z += before.z - after.z;
        target_ = target_goal_;
        rebuild();
    }
}

void CameraRig::focus(Vec2 p) { target_goal_ = p; }

void CameraRig::frame_on(Vec2 p, float view_height) {
    target_ = target_goal_ = p;
    zoom_ = zoom_goal_ = view_height / std::max(fit_height_, 1e-3f);
    rebuild();
}

void CameraRig::rebuild() {
    const Vector3 t{target_.x, 0.0f, target_.z};
    camera_.target = t;
    camera_.position = Vector3Add(t, Vector3Scale(view_offset(top_down_), kDistance));
    camera_.up = view_up(top_down_);
    camera_.fovy = fit_height_ * zoom_;
    camera_.projection = CAMERA_ORTHOGRAPHIC;
}

}  // namespace wobo
