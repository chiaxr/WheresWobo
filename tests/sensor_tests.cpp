#include <cmath>

#include "simulation/lidar.h"
#include "test.h"
#include "world/map_loader.h"

using namespace wobo;

namespace {

Map2D room() {
    // 10 x 10 cells of 0.5 m: a 4 m x 4 m interior. West wall red, east blue.
    const char* palette = R"(
[material.wall]
rgb = 100, 100, 100
solid = true
[material.red]
rgb = 255, 0, 0
solid = true
[material.blue]
rgb = 0, 0, 255
solid = true
[material.floor]
rgb = 200, 200, 200
)";
    const char* text = R"(
cell_size = 0.5
[legend]
hash = wall
R = red
B = blue
. = floor
[grid]
##########
R........B
R........B
R........B
R........B
R........B
R........B
R........B
R........B
##########
)";
    std::string err;
    return *parse_map(text, parse_materials(palette, &err), &err);
}

LidarConfig clean_config() {
    LidarConfig c;
    c.beams = 8;
    c.max_range_m = 10.0f;
    c.range_noise_m = 0.0f;
    c.colour_noise = 0.0f;
    c.colour_levels = 256;
    c.dropout = 0.0f;
    return c;
}

}  // namespace

TEST("lidar: noiseless scan returns true ranges and material colours") {
    const Map2D map = room();
    RgbLidarSimulator lidar(clean_config());
    Rng rng(1);
    // Centre of the room (x = 2.5, z = 2.5), facing +x (towards blue).
    const RgbLidarScan scan = lidar.scan(map, {2.5f, 2.5f, 0.0f}, 0.0, rng, {});
    REQUIRE(scan.returns.size() == 8);
    CHECK(scan.valid);
    const LidarReturn& front = scan.returns[0];
    CHECK(front.hit);
    CHECK_NEAR(front.angle_rad, 0.0f, 1e-6);
    CHECK_NEAR(front.range_m, 2.0f, 1e-4);  // blue wall at x = 4.5
    CHECK(front.rgb == (Rgb{0, 0, 255}));
    const LidarReturn& back = scan.returns[4];
    CHECK_NEAR(back.range_m, 2.0f, 1e-4);   // red wall at x = 0.5
    CHECK(back.rgb == (Rgb{255, 0, 0}));
    const LidarReturn& side = scan.returns[2];  // +90 deg: +z, grey wall at 4.5
    CHECK_NEAR(side.range_m, 2.0f, 1e-4);
    CHECK(side.rgb == (Rgb{100, 100, 100}));
}

TEST("lidar: beams are relative to the robot heading") {
    const Map2D map = room();
    RgbLidarSimulator lidar(clean_config());
    Rng rng(1);
    // Facing -x: beam 0 now sees the red wall.
    const RgbLidarScan scan = lidar.scan(map, {2.5f, 2.5f, kPi}, 0.0, rng, {});
    CHECK(scan.returns[0].rgb == (Rgb{255, 0, 0}));
}

TEST("lidar: no-return beams are reported at max range") {
    const Map2D map = room();
    LidarConfig c = clean_config();
    c.max_range_m = 1.0f;
    RgbLidarSimulator lidar(c);
    Rng rng(1);
    const RgbLidarScan scan = lidar.scan(map, {2.5f, 2.5f, 0.0f}, 0.0, rng, {});
    for (const LidarReturn& r : scan.returns) {
        CHECK(!r.hit);
        CHECK_NEAR(r.range_m, 1.0f, 1e-6);
    }
}

TEST("lidar: range interference scales max range only") {
    const Map2D map = room();
    RgbLidarSimulator lidar(clean_config());
    Rng rng(1);
    SensorModifiers mods;
    mods.range_scale = 0.15f;  // 10 m -> 1.5 m
    const RgbLidarScan scan = lidar.scan(map, {1.0f, 2.5f, 0.0f}, 0.0, rng, mods);
    CHECK_NEAR(scan.max_range_m, 1.5f, 1e-5);
    CHECK(!scan.returns[0].hit);                 // blue wall 3.5 m away
    CHECK(scan.returns[4].hit);                  // red wall 0.5 m away
    CHECK_NEAR(scan.returns[4].range_m, 0.5f, 1e-4);
}

TEST("lidar: blackout invalidates the scan") {
    const Map2D map = room();
    RgbLidarSimulator lidar(clean_config());
    Rng rng(1);
    SensorModifiers mods;
    mods.blackout = true;
    const RgbLidarScan scan = lidar.scan(map, {2.5f, 2.5f, 0.0f}, 0.0, rng, mods);
    CHECK(!scan.valid);
    for (const LidarReturn& r : scan.returns) CHECK(!r.hit);
}

TEST("lidar: range noise is zero-mean with the configured spread") {
    const Map2D map = room();
    LidarConfig c = clean_config();
    c.range_noise_m = 0.05f;
    RgbLidarSimulator lidar(c);
    Rng rng(9);
    double sum = 0.0, sum2 = 0.0;
    const int n = 4000;
    for (int i = 0; i < n; ++i) {
        const auto scan = lidar.scan(map, {2.5f, 2.5f, 0.0f}, 0.0, rng, {});
        const double e = scan.returns[0].range_m - 2.0;
        sum += e;
        sum2 += e * e;
    }
    CHECK_NEAR(sum / n, 0.0, 0.004);
    CHECK_NEAR(std::sqrt(sum2 / n), 0.05, 0.005);
    // The map itself is untouched by sensing.
    CHECK(map.material_at(9, 3).rgb == (Rgb{0, 0, 255}));
}

TEST("lidar: dropout rate matches configuration") {
    const Map2D map = room();
    LidarConfig c = clean_config();
    c.dropout = 0.2f;
    RgbLidarSimulator lidar(c);
    Rng rng(4);
    int dropped = 0, total = 0;
    for (int i = 0; i < 1000; ++i) {
        for (const auto& r : lidar.scan(map, {2.5f, 2.5f, 0.0f}, 0.0, rng, {}).returns) {
            dropped += r.hit ? 0 : 1;
            ++total;
        }
    }
    CHECK_NEAR(static_cast<double>(dropped) / total, 0.2, 0.02);
}

TEST("lidar: colour quantisation and desaturation") {
    LidarConfig c = clean_config();
    c.colour_levels = 4;  // 0, 85, 170, 255
    RgbLidarSimulator lidar(c);
    Rng rng(1);
    const Rgb q = lidar.measure_colour({100, 200, 30}, rng, {});
    CHECK(q == (Rgb{85, 170, 0}));
    SensorModifiers grey;
    grey.colour_desaturate = 1.0f;
    const Rgb g = lidar.measure_colour({255, 0, 0}, rng, grey);
    CHECK(g.r == g.g);
    CHECK(g.g == g.b);
}
