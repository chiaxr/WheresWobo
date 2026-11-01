#pragma once

// Access to the game's data files (data/ in the source tree): maps, configs
// and cards. They are compiled in, so this works identically on desktop, on
// the web and in headless tools. For quick iteration a native build can point
// at a directory on disk instead (set_override_dir), which is read first.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wobo::data {

// Read a file by its path relative to data/, e.g. "maps/atrium.txt".
std::optional<std::string> read(std::string_view path);

// Paths of every embedded file under `prefix` (e.g. "maps/"), sorted.
std::vector<std::string> list(std::string_view prefix);

// Read from this directory before the embedded copy. Empty disables it.
void set_override_dir(std::string dir);

}  // namespace wobo::data
