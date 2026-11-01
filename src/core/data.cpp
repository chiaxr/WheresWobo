#include "core/data.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "core/embedded.h"

extern const wobo::EmbeddedFile wobo_data_files[];
extern const std::size_t wobo_data_files_count;

namespace wobo::data {

namespace {
std::string g_override_dir;
}  // namespace

void set_override_dir(std::string dir) { g_override_dir = std::move(dir); }

std::optional<std::string> read(std::string_view path) {
    if (!g_override_dir.empty()) {
        std::ifstream in(g_override_dir + "/" + std::string(path), std::ios::binary);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }
    }
    for (std::size_t i = 0; i < wobo_data_files_count; ++i) {
        const EmbeddedFile& f = wobo_data_files[i];
        if (path == f.path) {
            return std::string(reinterpret_cast<const char*>(f.data), f.size);
        }
    }
    return std::nullopt;
}

std::vector<std::string> list(std::string_view prefix) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < wobo_data_files_count; ++i) {
        const std::string_view p = wobo_data_files[i].path;
        if (p.substr(0, prefix.size()) == prefix) out.emplace_back(p);
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace wobo::data
