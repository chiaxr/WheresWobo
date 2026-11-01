#include "app/click_capture.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>

namespace {

EM_JS(void, wobo_web_install_click_capture, (), {
    var canvas = Module.canvas;
    Module.__woboPresses = 0;
    Module.__woboX = 0;
    Module.__woboY = 0;
    var record = function (e) {
        var rect = canvas.getBoundingClientRect();
        if (!(rect.width > 0)) return;
        Module.__woboX = (e.clientX - rect.left) * canvas.width / rect.width;
        Module.__woboY = (e.clientY - rect.top) * canvas.height / rect.height;
    };
    // Primary pointer only, so a second pinch finger or a right click never
    // turns into a phantom tap.
    canvas.addEventListener('pointerdown', function (e) {
        if (!e.isPrimary || e.button !== 0) return;
        Module.__woboPresses++;
        record(e);
    }, true);
    canvas.addEventListener('pointerup', function (e) {
        if (!e.isPrimary || e.button !== 0) return;
        record(e);
    }, true);
});

EM_JS(int, wobo_web_presses, (), { return Module.__woboPresses === undefined ? -1 : Module.__woboPresses; });
EM_JS(float, wobo_web_x, (), { return Module.__woboX || 0; });
EM_JS(float, wobo_web_y, (), { return Module.__woboY || 0; });

}  // namespace

namespace wobo::click_capture {
void install() { wobo_web_install_click_capture(); }
int presses() { return wobo_web_presses(); }
Vector2 last_position() { return {wobo_web_x(), wobo_web_y()}; }
}  // namespace wobo::click_capture

#else

#include "GLFW/glfw3.h"

namespace {

GLFWmousebuttonfun g_previous = nullptr;
int g_presses = -1;
Vector2 g_last{};

void on_mouse_button(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        double x = 0.0, y = 0.0;
        glfwGetCursorPos(window, &x, &y);
        g_last = {static_cast<float>(x), static_cast<float>(y)};
        if (action == GLFW_PRESS) ++g_presses;
    }
    // raylib's own handler still sees every event.
    if (g_previous) g_previous(window, button, action, mods);
}

}  // namespace

namespace wobo::click_capture {

void install() {
    auto* window = static_cast<GLFWwindow*>(GetWindowHandle());
    if (!window) return;
    g_previous = glfwSetMouseButtonCallback(window, on_mouse_button);
    g_presses = 0;
}

int presses() { return g_presses; }
Vector2 last_position() { return g_last; }

}  // namespace wobo::click_capture

#endif
