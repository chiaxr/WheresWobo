#pragma once

// Plain-text configuration: `key = value` lines, `#` comments, and `[section]`
// headers that prefix the keys that follow (`[sensor]` + `beams = 32` is
// `sensor.beams`). Used for game tuning, maps' metadata and card definitions.

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace wobo {

class Config {
public:
    // Parses `text`. Malformed lines are recorded in errors() and skipped.
    static Config parse(std::string_view text);

    bool has(const std::string& key) const { return values_.count(key) != 0; }
    std::string get_string(const std::string& key, const std::string& fallback = "") const;
    float get_float(const std::string& key, float fallback = 0.0f) const;
    int get_int(const std::string& key, int fallback = 0) const;
    bool get_bool(const std::string& key, bool fallback = false) const;

    void set(const std::string& key, const std::string& value) { values_[key] = value; }

    // Section names in file order (without brackets), e.g. "card.range_up".
    const std::vector<std::string>& sections() const { return sections_; }
    // All keys that start with `prefix` + "." (prefix stripped), in key order.
    std::vector<std::pair<std::string, std::string>> section(const std::string& prefix) const;
    // Every value given for a repeated key, in file order (e.g. `effect = ...`
    // listed several times in one card).
    std::vector<std::string> get_all(const std::string& key) const;

    const std::map<std::string, std::string>& values() const { return values_; }
    const std::vector<std::string>& errors() const { return errors_; }

private:
    std::map<std::string, std::string> values_;
    std::multimap<std::string, std::string> repeated_;
    std::vector<std::string> sections_;
    std::vector<std::string> errors_;
};

std::string_view trim(std::string_view s);
std::vector<std::string> split(std::string_view s, char sep);

}  // namespace wobo
