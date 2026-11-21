#include "core/drive/adapters/steady_clock.hpp"

#include <chrono>

namespace strata::drive {

double SteadyClock::now() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace strata::drive
