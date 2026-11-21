#pragma once

#include "core/drive/ports/monotonic_clock.hpp"

namespace strata::drive {

// MonotonicClock on std::chrono::steady_clock.
class SteadyClock final : public MonotonicClock {
public:
    double now() const override;
};

}  // namespace strata::drive
