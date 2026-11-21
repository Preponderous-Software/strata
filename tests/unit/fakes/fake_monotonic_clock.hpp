#pragma once

#include "core/drive/ports/monotonic_clock.hpp"

namespace strata::drive {

// MonotonicClock for tests: time stands still until the test moves `seconds`.
// The pointer form lets a test keep advancing a clock the Drive owns.
class FakeMonotonicClock final : public MonotonicClock {
public:
    explicit FakeMonotonicClock(const double* seconds) : seconds_(seconds) {}
    double now() const override { return *seconds_; }

private:
    const double* seconds_;
};

}  // namespace strata::drive
