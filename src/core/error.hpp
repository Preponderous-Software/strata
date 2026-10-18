// Internal failures are exceptions; the C ABI shim turns them into a status
// code plus strata_last_error() text and never lets one cross the boundary.
#pragma once

#include <stdexcept>
#include <string>

#include "strata/strata.hpp"

namespace strata {

[[noreturn]] void fail(StrataStatus status, const char* format, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

}  // namespace strata
