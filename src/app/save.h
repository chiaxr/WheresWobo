#pragma once

// Persistent key=value text: browser localStorage on the web, a file next to
// the executable on desktop. Holds personal bests per map.

#include <string>

namespace wobo {

std::string load_save_text();
void store_save_text(const std::string& text);

}  // namespace wobo
