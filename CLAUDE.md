# CLAUDE.md

Guidance for Claude Code when working in this repository. The original design document is the source of truth for intent; README.md summarises how it was realised.

## Build, test, run

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON   # add -DFETCHCONTENT_SOURCE_DIR_RAYLIB=<raylib 6.0 src> to skip the download
cmake --build build -j
./build/wobo_tests [name-substring]     # tiny harness in tests/test.h; ctest runs the same binary
./build/wobo_bench --seeds 8 [--map ID] [--kidnap] [--player [weak|medium|strong]] [--limit S] [--set key=value]...
./build/wobo_bench --map office --seed 3 --seeds 1 --trace   # one run, pose/estimate/intent every 2 s
./build/wobo_bench --localizer histogram_filter --planner landmark_tour
./build/WheresWobo
./build/WheresWobo --profile 6 [--size WxH]   # uncapped frame times per scene, then quit
./build/WheresWobo --set run.hours=1 --set run.hour_length=10   # game.cfg overrides; on the web, ?set=key=value
./build/WheresWobo --poster docs/og-image.png --size 1200x630   # link-preview card; also docs/poster.png at 1600x800
./build/WheresWobo --shots DIR --size 1600x900   # <map>.png and <map>_close.png for every map, after 20 s of play
python3 tools/make_maps.py data/maps    # regenerate data/maps/<id>/map.txt
python3 tools/subset_fonts.py <font dir>   # after adding writing (blackboards, signs) to a map

source ../emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web -DWOBO_BUILD_NATIVE=OFF -DWOBO_BUILD_WEB=ON
cmake --build build-web -j              # -> build-web/dist/index.{html,js,wasm}
```

`WOBO_BUILD_NATIVE` and `WOBO_BUILD_WEB` are mutually exclusive (as in QWAS/PANIC). Tests and the bench build only with the native target. On macOS use Apple Clang.

## Ground rules

- **`wobo_core` must not include raylib.** Everything under `src/{core,world,simulation,localization,planning,robot,gameplay}` is the headless robotics problem; CMake does not give it raylib's include path, so a stray `#include "raylib.h"` fails to build. Rendering, UI and the app loop live in `wobo_game`.
- **The robot never sees ground truth.** `RobotAgent` receives `OdometryDelta`, `RgbLidarScan` and a bump flag only. `LocalizationReferee` is the only gameplay code that compares the estimate with `WorldSimulation::true_pose()`.
- **Player actions change measurement paths, not physics** (`SensorModifiers`). The deliberate exceptions, all invisible to the robot's software: Kidnap (`WorldSimulation::teleport` moves the true pose and emits no odometry or event) and Road Block (`RunDirector` keeps the known map that Wobo, the renderer and the planners use apart from the physical `world()` the simulation runs in; the barrier is painted only into the world, and `map_traits` checks it never disconnects the floor). So is Magnet's drag (`RunDirector` passes `WorldSimulation::step` an `ExternalPull` towards the magnet while Wobo is within `action.magnet.range`; the drag comes after the odometry is measured, in a straight line, and stops against the first obstacle, `slide_until_blocked`). Overload touches no sensor: while it lasts, `RunDirector::compute_share`/`compute_update_hz` cap the budget it hands Wobo's localizer (`[action.overload]`), the same channel as the hourly warm-up. Gyro Jam is heading-only and Gummy Wheels distance-only; keep them apart.
- **Map-specific cards:** `RunDirector::suits_map` drops Colour Blind where under `action.colour.min_colour_share` of visible wall faces are colourful, Road Block where no placement keeps the floor connected, and the Repainting Works upgrade (a card's `needs = repaint`) where too few places have a same-shape look-alike that differs in colour (`gameplay/look_alikes.h`). `wobo_bench --study [--only KEY,...]` measures actions against the localizer.
- **Colour Blind** swaps the scanner's colours round and blinds it dead ahead (`SensorModifiers::blind_cone_rad`). Repainting Works gives each use a chance (`action.colour.repaint_chance`) to make the scanner report, for that use, the colours of a shape-alike place elsewhere (`SensorModifiers::repaint`, a material remap chosen from the true pose). Both are measurement-path only.
- **Determinism:** the simulation advances only in fixed steps (`FixedStepClock`, 30 Hz) and every random consumer has its own stream from `derive_seed(seed, "name")`. Do not use `<random>` distributions (implementation-defined), wall-clock time, or frame time inside the simulation.
- **Algorithms are swappable** through `localizer_registry.cpp` / `planner_registry.cpp` (`localizer.type`, `planner.type`). Localizers: `particle_filter` (default, augmented MCL with scan-guided injection) and `histogram_filter` (grid Markov localization). Planners: `info_gain` (default), `landmark_tour`, `random_walk`. `GoalNavigator` is shared: when the greedy search has nothing to gain, keeps standing still, or the belief stops narrowing, `info_gain` hands over to it, and it heads where the estimate and its look-alikes ("twins", found by matching precomputed 360-degree views) would see different things. That is what stops a confidently wrong Wobo stalling on symmetric maps; check changes with `wobo_bench` on `office`, `dining` and `barn`.
- **Tuning lives in `data/configs/game.cfg`** (sensor, odometry, calibration, localizer, planner, referee, energy, actions, passives). Cards are in `data/cards/cards.cfg` and act through generic effects (`grant`, `stat key op value`, `passive`, `oneshot`) on `RunStats`. Balance changes should be checked with `wobo_bench` (`--player weak|medium|strong` runs a scripted saboteur and reports the hour reached; `--trace` shows its energy and active effects), not by feel.
- **The difficulty curve** is Wobo's side only; the player's costs, cooldowns and regen change only through cards. `[compute]`: Wobo's budget starts at `share` and grows each hour towards `final_share` (3), via `RunDirector::apply_compute_budget` -> `RobotAgent::set_budget` (update rate) -> `ILocalizer::set_budget`; the particle filter spends a budget up to 1 on particles and beyond 1 on beams per scan (`beam_stride` divided by the budget), the lever that actually counters late-game interference. `[calibration]`: extra sensor noise that fades. Cooldowns start when an effect ends (`PlayerActionSystem::step`), so duration upgrades cannot make an effect permanent. Wobo never starts within `run.start_unique_radius` of a place nothing else on the map looks like (`LookAlikes::unique_nearby`; the Office's hallway ends), where it found itself in about a minute. Outcomes are bimodal (a run ends in hours 3-9 or the player outgrows Wobo); the day ends after `run.hours` (24), which is the player's win (`RunScore::survived`, `GameEvent::Type::DayEnded`). Check changes with all three `wobo_bench --player` profiles and `--limit 1500` over 16+ seeds per map; 32 when adding or changing a card, since that reshuffles every draft (compare against a `--data` copy without it). To value a card, give it at the start of a run with `--set` (an ability's `owned`, the stats it changes, or `passive.<id>=1`) and compare whole-day wins and runs over by hour 4; the strong player drafts by those values (`kPriority` in `tools/bench.cpp`), so re-rank it when cards change. A player who keeps everything running is short of energy, so energy cards outrank most abilities and an action's price matters more than its `--study` strength. `--study` measures against a full-strength Wobo (final compute, no calibration noise).
- **Maps** are generated by `tools/make_maps.py`; edit it and regenerate rather than hand-editing `data/maps/<id>/map.txt`. Each map is a folder (room for its own assets); `map.txt` has a config header (`focus` is where `--shots` looks closely), optional `[material.<id>]` sections that add to or override the shared palette `data/maps/materials.cfg`, a `[legend]`, the `[grid]`, and `[props]` / `[movers]` lines (`kind x z key=value`, metres, `yaw` in degrees). `#` and `=` cannot be legend keys in the config syntax, so the legend spells `#` as `hash` and planks use `+`.
- **Adding a map or an action** touches several places (generator, prop/mover kinds and models, `ActionId`/`ActionInfo`, `game.cfg`, cards, HUD colours and icons, the bench's study list and scripted players); README's "Contributing maps and actions" lists them all. Keep that section in step when the wiring changes.
- **Props** (`world/props.h`) are part of the known map. Solid kinds have exact `Shape` footprints (discs, turned boxes) that the ray caster, collision and clearance test exactly; decorations (blackboards, doors, paintings, signs, bridges) have none. Their looks are procedural, in `rendering/prop_models.cpp`; writing is drawn by `text_decals.cpp` in the bundled chalk font, which holds only the characters the maps use.
- **Movers** (`simulation/movers.h`: the donkey, the guard, the walker) are dynamic obstacles that are never in Wobo's known map. `RunDirector::step` sets their discs into the physical world before each simulation step, so the LiDAR sees them and Wobo bumps them; they give way to Wobo. Kidnap, Road Block and Magnet can't land on them. Floor traits (`label_floor`, `stays_connected`) take Wobo's radius, since props narrow gaps.

## Rendering notes

- The isometric camera is orthographic (yaw 45 deg, pitch 40 deg); `CameraRig` pans and zooms by unprojecting the ground point under the cursor.
- `ToonShader` has GLSL 330 and GLSL 100 (WebGL) variants chosen from `rlGetVersion()`. Geometry is in world space; every lit face needs a normal. raylib's own cylinders and capsules set none, so use `rendering/primitives.h`. Front faces wind counter-clockwise; flat overlays drawn with the wrong winding are silently culled.
- Wobo's outline is an inverted hull (front-face culling); `draw_xray` draws a silhouette with the depth test off, before the normal draw, so Wobo stays visible behind walls.
- The look is "Night Shift": a flat dark background and, on the title screen only, a spotlight on Wobo. `GameApp::draw_world` ends 3D mode after the board, draws `ui::spotlight` in 2D, then resumes 3D for the overlays and Wobo; 2D drawing leaves the depth buffer alone, so walls still hide what they should. UI colours are in `ui/widgets.h`; the action and card colours there are deliberately shared and kept.
- Map meshes are built once per map (`IsoRenderer::build`) with baked shadows; the raylib `::Material` type clashes with `wobo::Material`, so qualify it.

## Input gotcha

raylib samples buttons once per frame, so clicks and key taps that start and end between frames are lost. `click_capture` (a chained GLFW callback on desktop, a pointer listener on the web) and the `GetKeyPressed` queue in `InputRouter` recover them. Use `PointerFrame::tap` and `PointerFrame::key()`, not `IsMouseButtonPressed`/`IsKeyPressed` directly.
