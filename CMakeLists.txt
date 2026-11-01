cmake_minimum_required(VERSION 3.21)

project(WheresWobo
    VERSION 0.1.0
    DESCRIPTION "Where's Wobo? - keep an autonomous robot lost for as long as possible"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ----------------------------------------------------------------------------
# Build options: native (desktop) and web (Emscripten) are mutually exclusive.
# Tests and the headless benchmark only need the simulation library, so they
# build alongside the native game but never under Emscripten.
# ----------------------------------------------------------------------------
option(WOBO_BUILD_NATIVE "Build the native desktop target" ON)
option(WOBO_BUILD_WEB    "Build the Emscripten web target"  OFF)
option(WOBO_BUILD_TESTS  "Build the unit tests and headless benchmark" ON)

if(WOBO_BUILD_NATIVE AND WOBO_BUILD_WEB)
    message(FATAL_ERROR "WOBO_BUILD_NATIVE and WOBO_BUILD_WEB are mutually exclusive; enable only one.")
endif()
if(NOT WOBO_BUILD_NATIVE AND NOT WOBO_BUILD_WEB)
    message(FATAL_ERROR "Enable exactly one of WOBO_BUILD_NATIVE or WOBO_BUILD_WEB.")
endif()
if(WOBO_BUILD_WEB AND NOT EMSCRIPTEN)
    message(FATAL_ERROR "WOBO_BUILD_WEB requires configuring with emcmake (Emscripten toolchain).")
endif()
if(EMSCRIPTEN)
    set(WOBO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
endif()

# CMake 4.x dropped support for cmake_minimum_required < 3.5.
# This allows FetchContent'd projects to still configure.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE INTERNAL "")
endif()

# ----------------------------------------------------------------------------
# raylib (fetched automatically).
# ----------------------------------------------------------------------------
include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
if(WOBO_BUILD_WEB)
    set(PLATFORM Web CACHE STRING "raylib target platform" FORCE)
endif()

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        6.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(raylib)

# Treat raylib's headers as system headers so our warning flags do not fire
# inside raymath.h and friends.
get_target_property(WOBO_RAYLIB_INCLUDES raylib INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(raylib PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${WOBO_RAYLIB_INCLUDES}")

# ----------------------------------------------------------------------------
# Embedded data: everything under data/ (maps, configs, cards) and the UI fonts
# is compiled into the binaries, so native and web read the same bytes and the
# web build needs no preloaded file system. Editing a data file regenerates
# the table on the next build.
# ----------------------------------------------------------------------------
include(cmake/EmbedFiles.cmake)

file(GLOB_RECURSE WOBO_DATA_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/data/*.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/*.cfg")
wobo_embed_files(
    OUTPUT    "${CMAKE_BINARY_DIR}/generated/embedded_data.cpp"
    SYMBOL    wobo_data_files
    BASE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/data"
    FILES     ${WOBO_DATA_FILES})

wobo_embed_files(
    OUTPUT    "${CMAKE_BINARY_DIR}/generated/embedded_fonts.cpp"
    SYMBOL    wobo_font_files
    BASE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts"
    FILES     "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/dejavu_sans_subset.ttf"
              "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/dejavu_sans_bold_subset.ttf")

# Warnings on the game's own sources only -- raylib is fetched and not ours to
# keep clean.
function(wobo_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# ----------------------------------------------------------------------------
# Simulation core: world, sensors, localization, planning and game rules.
# Deliberately does NOT link raylib, so nothing in here can include it and the
# whole robot problem runs headless (tests, benchmark).
# ----------------------------------------------------------------------------
add_library(wobo_core STATIC
    src/core/config.cpp
    src/core/data.cpp
    src/core/rng.cpp
    src/world/map2d.cpp
    src/world/map_loader.cpp
    src/world/raycast.cpp
    src/simulation/dynamics.cpp
    src/simulation/lidar.cpp
    src/simulation/odometry.cpp
    src/simulation/world_simulation.cpp
    src/localization/particle_filter.cpp
    src/localization/histogram_filter.cpp
    src/localization/localizer_registry.cpp
    src/planning/baseline_planners.cpp
    src/planning/goal_navigator.cpp
    src/planning/info_gain_planner.cpp
    src/planning/planner_registry.cpp
    src/robot/robot_agent.cpp
    src/gameplay/actions.cpp
    src/gameplay/cards.cpp
    src/gameplay/energy.cpp
    src/gameplay/referee.cpp
    src/gameplay/run_director.cpp
    src/gameplay/run_stats.cpp
    "${CMAKE_BINARY_DIR}/generated/embedded_data.cpp"
)
target_include_directories(wobo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
wobo_warnings(wobo_core)

# ----------------------------------------------------------------------------
# Game: rendering, UI and the application loop on top of the core.
# ----------------------------------------------------------------------------
add_library(wobo_game STATIC
    src/app/click_capture.cpp
    src/app/game_app.cpp
    src/app/input_router.cpp
    src/app/save.cpp
    src/rendering/camera_rig.cpp
    src/rendering/debug_renderer.cpp
    src/rendering/effects_renderer.cpp
    src/rendering/iso_renderer.cpp
    src/rendering/primitives.cpp
    src/rendering/robot_model.cpp
    src/rendering/toon_shader.cpp
    src/ui/action_icons.cpp
    src/ui/card_screen.cpp
    src/ui/hud.cpp
    src/ui/menus.cpp
    src/ui/poster.cpp
    src/ui/ui_text.cpp
    src/ui/widgets.cpp
    "${CMAKE_BINARY_DIR}/generated/embedded_fonts.cpp"
)
target_link_libraries(wobo_game PUBLIC wobo_core raylib)
if(NOT EMSCRIPTEN)
    # click_capture.cpp chains a GLFW callback; raylib bundles GLFW.
    target_include_directories(wobo_game SYSTEM PRIVATE "${raylib_SOURCE_DIR}/src/external/glfw/include")
endif()
wobo_warnings(wobo_game)

# ----------------------------------------------------------------------------
# Native desktop target.
# ----------------------------------------------------------------------------
if(WOBO_BUILD_NATIVE)
    add_executable(WheresWobo src/main.cpp)
    target_link_libraries(WheresWobo PRIVATE wobo_game)

    if(APPLE)
        target_link_libraries(WheresWobo PRIVATE
            "-framework Cocoa"
            "-framework IOKit"
            "-framework OpenGL")
    elseif(UNIX)
        target_link_libraries(WheresWobo PRIVATE dl pthread)
    endif()
endif()

# ----------------------------------------------------------------------------
# Headless tests and benchmark (simulation core only, no window).
# ----------------------------------------------------------------------------
if(WOBO_BUILD_TESTS)
    enable_testing()

    add_executable(wobo_tests
        tests/test_main.cpp
        tests/map_tests.cpp
        tests/sensor_tests.cpp
        tests/odometry_tests.cpp
        tests/localization_tests.cpp
        tests/gameplay_tests.cpp
    )
    target_link_libraries(wobo_tests PRIVATE wobo_core)
    wobo_warnings(wobo_tests)
    add_test(NAME wobo_tests COMMAND wobo_tests)

    add_executable(wobo_bench tools/bench.cpp)
    target_link_libraries(wobo_bench PRIVATE wobo_core)
    wobo_warnings(wobo_bench)
endif()

# ----------------------------------------------------------------------------
# Web (Emscripten) target -> dist/index.{html,js,wasm}
# ----------------------------------------------------------------------------
if(WOBO_BUILD_WEB)
    add_executable(wobo_web src/web/web_main.cpp)
    target_link_libraries(wobo_web PRIVATE wobo_game)

    set_target_properties(wobo_web PROPERTIES
        OUTPUT_NAME "index"
        SUFFIX ".html"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist")

    target_link_options(wobo_web PRIVATE
        "-sUSE_GLFW=3"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sGL_ENABLE_GET_PROC_ADDRESS=1"
        "-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=$stringToNewUTF8,$UTF8ToString"
        "--shell-file" "${CMAKE_CURRENT_SOURCE_DIR}/web/emscripten_shell.html")

    # Link-preview card referenced by the og:image tags in the shell
    # (rendered with `WheresWobo --poster docs/og-image.png --size 1200x630`).
    add_custom_command(TARGET wobo_web POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/docs/og-image.png"
            "${CMAKE_BINARY_DIR}/dist/og-image.png")
endif()
