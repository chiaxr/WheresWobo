// Native desktop entry point.

#include "raylib.h"

#include "app/game_app.h"

int main() {
    wobo::GameApp app;
    app.init();

    while (!WindowShouldClose()) {
        app.frame();
    }

    app.shutdown();
    return 0;
}
