#include "gameplay/referee.h"

#include <cmath>

namespace wobo {

RefereeConfig RefereeConfig::from(const Config& cfg) {
    RefereeConfig c;
    c.position_tolerance_m = cfg.get_float("referee.position_tolerance", c.position_tolerance_m);
    c.heading_tolerance_rad =
        cfg.get_float("referee.heading_tolerance_deg", c.heading_tolerance_rad * kRadToDeg) * kDegToRad;
    c.stability_s = cfg.get_float("referee.stability", c.stability_s);
    c.close_call = cfg.get_float("referee.close_call", c.close_call);
    return c;
}

void LocalizationReferee::reset() {
    held_s_ = 0.0f;
    peak_ = 0.0f;
    pos_err_ = head_err_ = 0.0f;
    within_ = false;
    localized_ = false;
    close_calls_ = 0;
}

bool LocalizationReferee::step(float dt_s, const Pose2D& truth, const PoseEstimate& estimate) {
    if (localized_) return false;
    pos_err_ = distance(truth.position(), estimate.pose.position());
    head_err_ = std::fabs(angle_diff(truth.heading_rad, estimate.pose.heading_rad));
    within_ = estimate.valid && pos_err_ <= config_.position_tolerance_m &&
              head_err_ <= config_.heading_tolerance_rad;
    if (within_) {
        held_s_ += dt_s;
        peak_ = std::fmax(peak_, lock_progress());
        if (held_s_ >= config_.stability_s) {
            localized_ = true;
            return true;
        }
    } else {
        if (peak_ >= config_.close_call) ++close_calls_;
        held_s_ = 0.0f;
        peak_ = 0.0f;
    }
    return false;
}

}  // namespace wobo
