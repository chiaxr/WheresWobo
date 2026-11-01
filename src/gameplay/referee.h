#pragma once

// The game's judge. Compares Wobo's estimate with ground truth -- which the
// robot never sees -- and declares Wobo found once the estimate has stayed
// within tolerance for the stability window. Confidence alone never wins:
// a confidently wrong robot is still lost.

#include "core/config.h"
#include "core/types.h"
#include "localization/localization_types.h"

namespace wobo {

struct RefereeConfig {
    float position_tolerance_m = 0.4f;
    float heading_tolerance_rad = 15.0f * kDegToRad;
    float stability_s = 3.0f;
    float close_call = 0.5f;

    static RefereeConfig from(const Config& cfg);
};

class LocalizationReferee {
public:
    explicit LocalizationReferee(const RefereeConfig& config = {}) : config_(config) {}

    void reset();
    // Returns true on the step Wobo is declared localized.
    bool step(float dt_s, const Pose2D& truth, const PoseEstimate& estimate);

    bool localized() const { return localized_; }
    bool within_tolerance() const { return within_; }
    float position_error() const { return pos_err_; }
    float heading_error() const { return head_err_; }
    // 0..1 progress through the stability window.
    float lock_progress() const { return held_s_ / config_.stability_s; }
    int close_calls() const { return close_calls_; }
    const RefereeConfig& config() const { return config_; }

private:
    RefereeConfig config_;
    float held_s_ = 0.0f;
    float peak_ = 0.0f;
    float pos_err_ = 0.0f;
    float head_err_ = 0.0f;
    bool within_ = false;
    bool localized_ = false;
    int close_calls_ = 0;
};

}  // namespace wobo
