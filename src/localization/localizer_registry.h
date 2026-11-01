#pragma once

// Factory for localizers, selected by name from configuration
// (`localizer.type`). To add an algorithm: implement ILocalizer, then add one
// entry to the table in localizer_registry.cpp.

#include <memory>
#include <string>
#include <vector>

#include "core/config.h"
#include "localization/localizer.h"

namespace wobo {

std::unique_ptr<ILocalizer> make_localizer(const std::string& type, const Config& cfg);
std::vector<std::string> localizer_names();

}  // namespace wobo
