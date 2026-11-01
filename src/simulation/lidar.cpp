#include "simulation/lidar.h"

#include <algorithm>
#include <cmath>

#include "world/raycast.h"

namespace wobo {

LidarConfig LidarConfig::from(const Config& cfg) {
    LidarConfig c;
    c.beams = std::max(4, cfg.get_int("sensor.beams", c.beams));
    c.max_range_m = cfg.get_float("sensor.max_range", c.max_range_m);
    c.scan_hz = cfg.get_float("sensor.scan_hz", c.scan_hz);
    c.range_noise_m = cfg.get_float("sensor.range_noise", c.range_noise_m);
    c.colour_noise = cfg.get_float("sensor.colour_noise", c.colour_noise);
    c.colour_levels = std::max(2, cfg.get_int("sensor.colour_levels", c.colour_levels));
    c.dropout = cfg.get_float("sensor.dropout", c.dropout);
    return c;
}

Rgb RgbLidarSimulator::measure_colour(Rgb surface, Rng& rng, const SensorModifiers& mods) const {
    float c[3] = {static_cast<float>(surface.r), static_cast<float>(surface.g),
                  static_cast<float>(surface.b)};

    if (mods.colour_hue_shift > 0.0f) {
        // Blend towards the channel-rotated colour: R<-G, G<-B, B<-R.
        const float t = clampf(mods.colour_hue_shift, 0.0f, 1.0f);
        const float r = c[0], g = c[1], b = c[2];
        c[0] = lerpf(r, g, t);
        c[1] = lerpf(g, b, t);
        c[2] = lerpf(b, r, t);
    }
    if (mods.colour_desaturate > 0.0f) {
        const float grey = 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2];
        const float t = clampf(mods.colour_desaturate, 0.0f, 1.0f);
        for (float& ch : c) ch = lerpf(ch, grey, t);
    }

    const float noise = config_.colour_noise + mods.colour_noise_add;
    const float step = 255.0f / static_cast<float>(config_.colour_levels - 1);
    Rgb out;
    std::uint8_t* dst[3] = {&out.r, &out.g, &out.b};
    for (int i = 0; i < 3; ++i) {
        float v = c[i] + (noise > 0.0f ? rng.normal(0.0f, noise) : 0.0f);
        v = clampf(v, 0.0f, 255.0f);
        v = std::round(v / step) * step;
        *dst[i] = static_cast<std::uint8_t>(clampf(v, 0.0f, 255.0f));
    }
    return out;
}

RgbLidarScan RgbLidarSimulator::scan(const Map2D& map, const Pose2D& true_pose, double time_s,
                                     Rng& rng, const SensorModifiers& mods) const {
    RgbLidarScan scan;
    scan.timestamp_s = time_s;
    scan.max_range_m = config_.max_range_m * clampf(mods.range_scale, 0.05f, 4.0f);
    scan.returns.resize(static_cast<std::size_t>(config_.beams));

    if (mods.blackout) {
        scan.valid = false;
        for (int i = 0; i < config_.beams; ++i) {
            scan.returns[i] = {beam_angle(i, config_.beams), scan.max_range_m, {}, false};
        }
        return scan;
    }

    const Pose2D ghost = compose(true_pose, mods.ghost_offset);
    const float range_noise = config_.range_noise_m + mods.range_noise_add;
    const float dropout = clampf(config_.dropout + mods.dropout_add, 0.0f, 1.0f);

    for (int i = 0; i < config_.beams; ++i) {
        LidarReturn& ret = scan.returns[i];
        ret.angle_rad = beam_angle(i, config_.beams);

        const bool from_ghost = mods.ghost_fraction > 0.0f && rng.chance(mods.ghost_fraction);
        const Pose2D& origin = from_ghost ? ghost : true_pose;
        const RayHit hit = cast_ray(map, origin.x, origin.z, origin.heading_rad + ret.angle_rad,
                                    scan.max_range_m);

        if (!hit.hit || rng.chance(dropout)) {
            ret.hit = false;
            ret.range_m = scan.max_range_m;
            continue;
        }
        const float measured = std::max(0.0f, hit.range + rng.normal(0.0f, range_noise));
        if (measured >= scan.max_range_m) {
            ret.hit = false;
            ret.range_m = scan.max_range_m;
            continue;
        }
        ret.hit = true;
        ret.range_m = measured;
        ret.rgb = measure_colour(map.material(hit.material).rgb, rng, mods);
    }
    return scan;
}

}  // namespace wobo
