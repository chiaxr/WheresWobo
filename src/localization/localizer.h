#pragma once

// The replaceable localization interface. A localizer sees the known map,
// its prior, noisy odometry and RGB LiDAR scans -- never ground truth, player
// actions or the renderer. New algorithms implement this and register in
// localizer_registry.cpp; nothing else in the game needs to change.

#include <cstdint>
#include <string_view>

#include "core/messages.h"
#include "localization/localization_types.h"
#include "world/map2d.h"

namespace wobo {

class ILocalizer {
public:
    virtual ~ILocalizer() = default;

    virtual std::string_view name() const = 0;

    // `known_map` outlives the localizer's use of it.
    virtual void reset(const Map2D& known_map, const InitialBelief& prior, std::uint64_t seed) = 0;

    virtual void predict(const OdometryDelta& odometry) = 0;
    virtual void observe(const RgbLidarScan& scan) = 0;

    // Its compute, as a multiple of the configured budget (1), e.g. of its
    // particles. Wobo's computer warms up as a run goes on, from a fraction
    // to more than 1; methods with nothing to scale may ignore it.
    virtual void set_budget(float share) { (void)share; }
    // Late in the day Wobo learns to re-seed a lost belief from where its
    // scan fits the map's precomputed views; `temperature` is how sharply it
    // favours the best fits (0: not yet). Methods without such a step may
    // ignore it.
    virtual void set_view_injection(float temperature) { (void)temperature; }

    virtual PoseEstimate estimate() const = 0;
    // nullptr when the method has no diagnostics to offer.
    virtual const LocalizationDiagnostics* diagnostics() const = 0;
};

}  // namespace wobo
