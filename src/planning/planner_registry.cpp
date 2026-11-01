#include "planning/planner_registry.h"

#include "planning/info_gain_planner.h"

namespace wobo {

namespace {

struct Entry {
    const char* name;
    std::unique_ptr<IActivePlanner> (*make)(const Config&);
};

const Entry kPlanners[] = {
    {"info_gain",
     [](const Config& cfg) -> std::unique_ptr<IActivePlanner> {
         return std::make_unique<InfoGainPlanner>(InfoGainPlannerConfig::from(cfg));
     }},
};

}  // namespace

std::unique_ptr<IActivePlanner> make_planner(const std::string& type, const Config& cfg) {
    for (const Entry& e : kPlanners) {
        if (type == e.name) return e.make(cfg);
    }
    return nullptr;
}

std::vector<std::string> planner_names() {
    std::vector<std::string> out;
    for (const Entry& e : kPlanners) out.emplace_back(e.name);
    return out;
}

}  // namespace wobo
