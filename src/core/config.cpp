#include "core/config.h"

#include <cstdlib>

namespace wobo {

std::string_view trim(std::string_view s) {
    const auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return s;
}

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = s.find(sep, start);
        out.emplace_back(trim(s.substr(start, pos == std::string_view::npos ? pos : pos - start)));
        if (pos == std::string_view::npos) break;
        start = pos + 1;
    }
    return out;
}

Config Config::parse(std::string_view text) {
    Config cfg;
    std::string prefix;
    int line_no = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        start = end + 1;
        ++line_no;

        // Comments run to end of line. A '#' inside a value would need
        // quoting, which nothing in the game's data uses.
        if (const auto hash = line.find('#'); hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        line = trim(line);
        if (line.empty()) {
            if (end == text.size()) break;
            continue;
        }

        if (line.front() == '[') {
            if (line.back() != ']') {
                cfg.errors_.push_back("line " + std::to_string(line_no) + ": unterminated section");
                continue;
            }
            const std::string name(trim(line.substr(1, line.size() - 2)));
            prefix = name.empty() ? "" : name + ".";
            cfg.sections_.push_back(name);
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string_view::npos) {
            cfg.errors_.push_back("line " + std::to_string(line_no) + ": expected key = value");
            continue;
        }
        const std::string key = prefix + std::string(trim(line.substr(0, eq)));
        const std::string value(trim(line.substr(eq + 1)));
        cfg.values_[key] = value;
        cfg.repeated_.emplace(key, value);
        if (end == text.size()) break;
    }
    return cfg;
}

std::string Config::get_string(const std::string& key, const std::string& fallback) const {
    const auto it = values_.find(key);
    return it == values_.end() ? fallback : it->second;
}

float Config::get_float(const std::string& key, float fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end() || it->second.empty()) return fallback;
    char* end = nullptr;
    const float v = std::strtof(it->second.c_str(), &end);
    return end == it->second.c_str() ? fallback : v;
}

int Config::get_int(const std::string& key, int fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end() || it->second.empty()) return fallback;
    char* end = nullptr;
    const long v = std::strtol(it->second.c_str(), &end, 10);
    return end == it->second.c_str() ? fallback : static_cast<int>(v);
}

bool Config::get_bool(const std::string& key, bool fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    const std::string& v = it->second;
    if (v == "true" || v == "yes" || v == "on" || v == "1") return true;
    if (v == "false" || v == "no" || v == "off" || v == "0") return false;
    return fallback;
}

std::vector<std::pair<std::string, std::string>> Config::section(const std::string& prefix) const {
    std::vector<std::pair<std::string, std::string>> out;
    const std::string p = prefix + ".";
    for (auto it = values_.lower_bound(p); it != values_.end(); ++it) {
        if (it->first.compare(0, p.size(), p) != 0) break;
        out.emplace_back(it->first.substr(p.size()), it->second);
    }
    return out;
}

std::vector<std::string> Config::get_all(const std::string& key) const {
    std::vector<std::string> out;
    const auto [lo, hi] = repeated_.equal_range(key);
    for (auto it = lo; it != hi; ++it) out.push_back(it->second);
    return out;
}

}  // namespace wobo
