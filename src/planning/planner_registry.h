#pragma once

// Factory for active planners, selected by name (`planner.type`).

#include <memory>
#include <string>
#include <vector>

#include "core/config.h"
#include "planning/planner.h"

namespace wobo {

std::unique_ptr<IActivePlanner> make_planner(const std::string& type, const Config& cfg);
std::vector<std::string> planner_names();

}  // namespace wobo
