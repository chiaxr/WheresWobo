#include "app/save.h"

#include "raylib.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>

#include <cstdlib>

namespace {
// localStorage can throw (private browsing, blocked site data), so failures
// behave like an empty save.
EM_JS(char*, wobo_web_load_save, (), {
    try {
        var s = localStorage.getItem('wheres_wobo_save');
        return s === null ? 0 : stringToNewUTF8(s);
    } catch (e) {
        return 0;
    }
});

EM_JS(void, wobo_web_store_save, (const char* text), {
    try {
        localStorage.setItem('wheres_wobo_save', UTF8ToString(text));
    } catch (e) {
    }
});
}  // namespace

namespace wobo {

std::string load_save_text() {
    char* text = wobo_web_load_save();
    if (!text) return "";
    std::string result(text);
    free(text);
    return result;
}

void store_save_text(const std::string& text) { wobo_web_store_save(text.c_str()); }

}  // namespace wobo

#else

namespace wobo {

namespace {
const char* save_path() { return TextFormat("%swheres_wobo_save.txt", GetApplicationDirectory()); }
}  // namespace

std::string load_save_text() {
    const char* path = save_path();
    if (!FileExists(path)) return "";
    char* text = LoadFileText(path);
    if (!text) return "";
    std::string result(text);
    UnloadFileText(text);
    return result;
}

void store_save_text(const std::string& text) { SaveFileText(save_path(), text.c_str()); }

}  // namespace wobo

#endif
