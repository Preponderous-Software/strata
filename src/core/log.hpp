// Engine logging. One process-wide sink: the host's callback if it set one
// (strata_set_log_callback), otherwise stderr.
#pragma once

#include <cstdarg>
#include <cstdint>

#include "strata/strata.h"

namespace strata {

enum class LogLevel : int32_t { Debug = 0, Info = 1, Warning = 2, Error = 3 };

void setLogSink(StrataLogFn fn, void* user);

#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
void logf(LogLevel level, const char* format, ...);

}  // namespace strata
