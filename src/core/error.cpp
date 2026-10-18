#include "core/error.hpp"

#include <cstdarg>
#include <cstdio>

namespace strata {

void fail(StrataStatus status, const char* format, ...) {
    char message[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof message, format, args);
    va_end(args);
    throw Error(status, message);
}

}  // namespace strata
