#pragma once

// Fixed-step simulation clock. Real frame time goes in (scaled by the speed
// setting); a whole number of fixed simulation steps comes out, and the
// leftover fraction is the interpolation factor for rendering. Simulation
// results therefore never depend on frame rate.

namespace wobo {

class FixedStepClock {
public:
    explicit FixedStepClock(double step_s = 1.0 / 30.0) : step_s_(step_s) {}

    double step_s() const { return step_s_; }

    void set_speed(double speed) { speed_ = speed; }
    double speed() const { return speed_; }
    void set_paused(bool paused) { paused_ = paused; }
    bool paused() const { return paused_; }

    // Feed real elapsed time; returns how many steps to run now. Large frame
    // hitches are capped so the game slows down rather than spiralling.
    int advance(double real_dt_s, int max_steps = 12) {
        if (paused_) return 0;
        if (real_dt_s > 0.1) real_dt_s = 0.1;
        accumulator_ += real_dt_s * speed_;
        int steps = static_cast<int>(accumulator_ / step_s_);
        if (steps > max_steps) {
            steps = max_steps;
            accumulator_ = 0.0;
        } else {
            accumulator_ -= steps * step_s_;
        }
        return steps;
    }

    // Fraction of a step since the last one, for render interpolation.
    float alpha() const { return static_cast<float>(accumulator_ / step_s_); }

    void reset() { accumulator_ = 0.0; }

private:
    double step_s_;
    double accumulator_ = 0.0;
    double speed_ = 1.0;
    bool paused_ = false;
};

}  // namespace wobo
