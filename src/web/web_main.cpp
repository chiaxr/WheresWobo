// Emscripten web entry point. Mirrors the PANIC/QWAS web drivers: one app,
// stepped by emscripten_set_main_loop, paused while the device is in portrait
// orientation, rendering at real device pixels, and saving whenever the page
// is hidden (the web main loop never returns, so there is no shutdown).

#include <cmath>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include "raylib.h"

#include "app/game_app.h"

namespace {

wobo::GameApp g_app;

// Emscripten sizes the canvas backing store in CSS pixels, and raylib has no
// FLAG_WINDOW_HIGHDPI on web, so on a 3x phone everything would be drawn at a
// third of the resolution and stretched. Render into a framebuffer of device
// pixels instead and tell the game how many make a CSS pixel.
EM_JS(double, wobo_web_pixel_ratio, (), { return window.devicePixelRatio || 1; });

// Emscripten has changed whether mouse coordinates come back in CSS pixels
// or framebuffer pixels; probe which one this runtime uses (see PANIC).
EM_JS(double, wobo_web_mouse_report_scale, (), {
    if (typeof Browser === 'undefined' || !Browser.calculateMouseCoords) return 0;
    var rect = Module.canvas.getBoundingClientRect();
    if (!(rect.width > 0)) return 0;
    var sx = (window.scrollX !== undefined) ? window.scrollX : window.pageXOffset;
    var sy = (window.scrollY !== undefined) ? window.scrollY : window.pageYOffset;
    var probe = Browser.calculateMouseCoords(sx + rect.left + rect.width, sy + rect.top);
    return probe.x / rect.width;
});

EM_JS(void, wobo_web_css_size, (int* out_w, int* out_h), {
    var rect = Module.canvas.getBoundingClientRect();
    HEAP32[out_w >> 2] = Math.round(rect.width);
    HEAP32[out_h >> 2] = Math.round(rect.height);
});

// Resizing the backing store writes an inline CSS size onto the canvas; the
// shell sizes it from its container, so put that back.
EM_JS(void, wobo_web_restore_css_size, (), {
    Module.canvas.style.width = '100%';
    Module.canvas.style.height = '100%';
});

EM_JS(int, wobo_web_is_portrait, (), {
    if (typeof Module !== 'undefined' && Module.__woboIsPortrait) {
        return Module.__woboIsPortrait() ? 1 : 0;
    }
    return (window.innerHeight > window.innerWidth) ? 1 : 0;
});

void sync_framebuffer() {
    int css_w = 0;
    int css_h = 0;
    wobo_web_css_size(&css_w, &css_h);
    if (css_w <= 0 || css_h <= 0) return;

    // Capped: beyond 2x the extra pixels are invisible but the cost is not.
    constexpr double max_ratio = 2.0;
    double ratio = wobo_web_pixel_ratio();
    if (!(ratio > 0.0)) ratio = 1.0;
    if (ratio > max_ratio) ratio = max_ratio;

    const int want_w = static_cast<int>(std::lround(css_w * ratio));
    const int want_h = static_cast<int>(std::lround(css_h * ratio));
    if (want_w != GetScreenWidth() || want_h != GetScreenHeight()) {
        SetWindowSize(want_w, want_h);
        wobo_web_restore_css_size();
    }
    g_app.set_pixel_scale(static_cast<float>(ratio));

    const double reported = wobo_web_mouse_report_scale();
    const float fix = (reported > 0.0) ? static_cast<float>(ratio / reported) : 1.0f;
    SetMouseScale(fix, fix);
}

EM_BOOL on_visibility_change(int, const EmscriptenVisibilityChangeEvent* event, void*) {
    if (event->hidden) g_app.save_progress();
    return EM_FALSE;
}

void frame() {
    sync_framebuffer();
    g_app.set_paused(wobo_web_is_portrait() != 0);
    g_app.frame();
}

}  // namespace

int main() {
    g_app.init();
    emscripten_set_visibilitychange_callback(nullptr, false, on_visibility_change);
    emscripten_set_main_loop(frame, 0, true);
    return 0;
}
