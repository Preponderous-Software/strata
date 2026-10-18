// strata/sim/fixed_step.hpp -- a fixed-timestep accumulator.
//
// The simulation never reads a clock. The host measures real elapsed time
// (or decides there is none, in a headless run), passes it to advance(), and
// runs the number of fixed ticks it returns. The result of a run therefore
// depends only on the seed and the number of ticks, never on how fast the
// machine is or how the frames happened to be paced.
//
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#pragma once

#include <cstdint>

namespace strata::sim {

class FixedStep {
public:
    // step: seconds per tick (> 0). maxTicksPerAdvance caps catch-up after a
    // stall, so a long pause does not trigger a spiral of ticks.
    explicit FixedStep(double stepSeconds = 1.0 / 60.0, int maxTicksPerAdvance = 8)
        : step_(stepSeconds > 0.0 ? stepSeconds : 1.0 / 60.0),
          maxTicks_(maxTicksPerAdvance > 0 ? maxTicksPerAdvance : 1) {}

    // Adds real elapsed seconds and returns how many ticks to run now. Time
    // beyond the catch-up cap is dropped.
    int advance(double elapsedSeconds) {
        if (elapsedSeconds > 0.0) accumulator_ += elapsedSeconds * scale_;
        int ticks = 0;
        while (accumulator_ >= step_ && ticks < maxTicks_) {
            accumulator_ -= step_;
            ++ticks;
        }
        if (ticks == maxTicks_ && accumulator_ >= step_) accumulator_ = 0.0;
        total_ += static_cast<uint64_t>(ticks);
        return ticks;
    }

    // Fraction of a tick left in the accumulator, for interpolating a frame.
    double alpha() const { return accumulator_ / step_; }

    // Speeds the simulation up or slows it down (0 pauses it).
    void setTimeScale(double scale) { scale_ = scale > 0.0 ? scale : 0.0; }
    double timeScale() const { return scale_; }

    double stepSeconds() const { return step_; }
    uint64_t ticks() const { return total_; }

private:
    double step_;
    int maxTicks_;
    double accumulator_ = 0.0;
    double scale_ = 1.0;
    uint64_t total_ = 0;
};

}  // namespace strata::sim
