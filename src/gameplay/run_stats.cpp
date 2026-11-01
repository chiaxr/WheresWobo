#include "gameplay/run_stats.h"

#include <cstdlib>

namespace wobo {

void RunStats::load(const Config& cfg) {
    values_.clear();
    for (const auto& [key, value] : cfg.values()) {
        if (key.rfind("action.", 0) != 0 && key.rfind("energy.", 0) != 0 &&
            key.rfind("passive.", 0) != 0) {
            continue;
        }
        char* end = nullptr;
        const float v = std::strtof(value.c_str(), &end);
        if (end != value.c_str()) values_[key] = v;
    }
}

float RunStats::get(const std::string& key, float fallback) const {
    const auto it = values_.find(key);
    return it == values_.end() ? fallback : it->second;
}

bool RunStats::apply(const std::string& key, const std::string& op_value) {
    if (op_value.size() < 2) return false;
    const char op = op_value[0];
    char* end = nullptr;
    const char* num = op_value.c_str() + 1;
    const float v = std::strtof(num, &end);
    if (end == num) return false;
    switch (op) {
        case '+': add(key, v); return true;
        case '*': set(key, get(key) * v); return true;
        case '=': set(key, v); return true;
        default: return false;
    }
}

}  // namespace wobo
