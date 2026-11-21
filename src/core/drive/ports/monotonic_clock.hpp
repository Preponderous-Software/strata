#pragma once

namespace strata::drive {

// A clock that never goes back, for measuring the time between frames.
//
// Contract: now() is seconds since an arbitrary fixed origin; successive calls
// never decrease. It never blocks and never throws. Main thread only.
class MonotonicClock {
public:
    virtual ~MonotonicClock() = default;
    virtual double now() const = 0;
};

}  // namespace strata::drive
