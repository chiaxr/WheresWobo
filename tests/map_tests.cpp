#include "core/config.h"
#include "core/rng.h"
#include "test.h"
#include "world/map_loader.h"
#include "world/raycast.h"

using namespace wobo;

namespace {

const char* kPalette = R"(
[material.wall]
rgb = 100, 100, 100
solid = true
[material.red]
rgb = 255, 0, 0
solid = true
[material.floor]
rgb = 200, 200, 200
)";

const char* kMap = R"(
name = Test
cell_size = 0.5
[legend]
hash = wall
R = red
. = floor
[grid]
######
#....R
#....#
######
)";

Map2D test_map() {
    std::string err;
    auto pal = parse_materials(kPalette, &err);
    auto map = parse_map(kMap, pal, &err);
    if (!map) std::printf("map error: %s\n", err.c_str());
    return *map;
}

}  // namespace

TEST("map: parses grid, legend and bounds") {
    const Map2D map = test_map();
    CHECK(map.width() == 6);
    CHECK(map.height() == 4);
    CHECK(map.solid(0, 0));
    CHECK(!map.solid(1, 1));
    CHECK(map.solid(5, 1));
    CHECK(map.material_at(5, 1).id == "red");
    CHECK(map.solid(-1, 2));   // outside is solid
    CHECK(map.solid(6, 2));
    CHECK(map.free_cells().size() == 8);
}

TEST("map: circle collision") {
    const Map2D map = test_map();
    CHECK(map.circle_free(1.25f, 1.0f, 0.2f));
    CHECK(!map.circle_free(0.6f, 0.75f, 0.2f));  // overlaps the left wall
    CHECK(map.point_free(1.0f, 0.75f));
    CHECK(!map.point_free(0.25f, 0.25f));
}

TEST("map: sampled poses are always valid") {
    const Map2D map = test_map();
    Rng rng(42);
    for (int i = 0; i < 500; ++i) {
        const Pose2D p = map.sample_valid_pose(rng, 0.2f);
        CHECK(map.valid_pose(p, 0.2f));
        CHECK(p.heading_rad >= -kPi && p.heading_rad <= kPi);
    }
}

TEST("map: every embedded map loads") {
    const auto maps = list_maps();
    CHECK(maps.size() >= 5);
    for (const MapInfo& info : maps) {
        std::string err;
        const auto map = load_map(info.id, &err);
        if (!map) std::printf("    %s: %s\n", info.id.c_str(), err.c_str());
        CHECK(map.has_value());
    }
}

TEST("raycast: hits the expected surface and colour") {
    const Map2D map = test_map();
    // From the middle of row 1, looking +x: red wall at x = 2.5.
    const RayHit hit = cast_ray(map, 1.0f, 0.75f, 0.0f, 10.0f);
    CHECK(hit.hit);
    CHECK_NEAR(hit.range, 1.5f, 1e-4);
    CHECK(map.material(hit.material).id == "red");
    // Looking -x: grey wall at x = 0.5.
    const RayHit back = cast_ray(map, 1.0f, 0.75f, kPi, 10.0f);
    CHECK(back.hit);
    CHECK_NEAR(back.range, 0.5f, 1e-4);
    CHECK(map.material(back.material).id == "wall");
    // Looking +z from z = 0.75: wall row 3 starts at z = 1.5.
    const RayHit down = cast_ray(map, 1.0f, 0.75f, kPi / 2, 10.0f);
    CHECK_NEAR(down.range, 0.75f, 1e-4);
}

TEST("raycast: no return beyond max range") {
    const Map2D map = test_map();
    const RayHit hit = cast_ray(map, 1.0f, 0.75f, 0.0f, 1.0f);
    CHECK(!hit.hit);
    CHECK_NEAR(hit.range, 1.0f, 1e-6);
}

TEST("raycast: diagonal ray") {
    const Map2D map = test_map();
    const RayHit hit = cast_ray(map, 1.0f, 1.0f, kPi / 4, 10.0f);
    CHECK(hit.hit);
    // z hits 1.5 after travelling 0.5 in z -> 0.5*sqrt(2).
    CHECK_NEAR(hit.range, 0.70710678f, 1e-4);
}

TEST("config: sections, repeated keys and types") {
    const Config cfg = Config::parse(R"(
top = 1
[a.b]
x = 2.5   # comment
flag = yes
e = one
e = two
)");
    CHECK(cfg.get_int("top") == 1);
    CHECK_NEAR(cfg.get_float("a.b.x"), 2.5, 1e-6);
    CHECK(cfg.get_bool("a.b.flag"));
    CHECK(cfg.get_all("a.b.e").size() == 2);
    CHECK(cfg.sections().size() == 1);
    CHECK(cfg.errors().empty());
}

TEST("rng: fixed seed is repeatable, streams differ") {
    Rng a(derive_seed(7, "sensor"));
    Rng b(derive_seed(7, "sensor"));
    Rng c(derive_seed(7, "cards"));
    bool differs = false;
    for (int i = 0; i < 100; ++i) {
        const auto x = a.next_u32();
        CHECK(x == b.next_u32());
        differs |= x != c.next_u32();
    }
    CHECK(differs);
    Rng n(3);
    double sum = 0.0, sum2 = 0.0;
    const int count = 20000;
    for (int i = 0; i < count; ++i) {
        const double v = n.normal();
        sum += v;
        sum2 += v * v;
    }
    CHECK_NEAR(sum / count, 0.0, 0.03);
    CHECK_NEAR(sum2 / count, 1.0, 0.05);
}
