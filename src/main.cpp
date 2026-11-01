// Native desktop entry point.
//
//   WheresWobo                      play
//   WheresWobo --profile [SECONDS]  time every map uncapped, print frame stats, quit
//   WheresWobo --poster PATH.png    render the README / social-preview poster, quit
//   options: --size WxH             window (or poster) size (default 1280x720)

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "raylib.h"

#include "app/game_app.h"

int main(int argc, char** argv) {
    wobo::LaunchOptions opt;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const bool has_value = i + 1 < argc && argv[i + 1][0] != '-';
        if (std::strcmp(a, "--profile") == 0) {
            opt.profile_s = has_value ? static_cast<float>(std::atof(argv[++i])) : 8.0f;
        } else if (std::strcmp(a, "--poster") == 0 && has_value) {
            opt.poster_path = argv[++i];
        } else if (std::strcmp(a, "--size") == 0 && has_value) {
            std::sscanf(argv[++i], "%dx%d", &opt.width, &opt.height);
        } else {
            std::fprintf(stderr,
                         "usage: WheresWobo [--profile [SECONDS]] [--poster PATH]"
                         " [--size WxH]\n");
            return 2;
        }
    }

    wobo::GameApp app;
    app.init(opt);

    while (!WindowShouldClose() && !app.finished()) {
        app.frame();
    }

    app.shutdown();
    return 0;
}
