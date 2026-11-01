#include "localization/localizer_registry.h"

#include "localization/particle_filter.h"

namespace wobo {

namespace {

struct Entry {
    const char* name;
    std::unique_ptr<ILocalizer> (*make)(const Config&);
};

const Entry kLocalizers[] = {
    {"particle_filter",
     [](const Config& cfg) -> std::unique_ptr<ILocalizer> {
         return std::make_unique<ParticleFilter>(ParticleFilterConfig::from(cfg));
     }},
};

}  // namespace

std::unique_ptr<ILocalizer> make_localizer(const std::string& type, const Config& cfg) {
    for (const Entry& e : kLocalizers) {
        if (type == e.name) return e.make(cfg);
    }
    return nullptr;
}

std::vector<std::string> localizer_names() {
    std::vector<std::string> out;
    for (const Entry& e : kLocalizers) out.emplace_back(e.name);
    return out;
}

}  // namespace wobo
