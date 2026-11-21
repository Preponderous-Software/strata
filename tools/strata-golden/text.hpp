// printf into a std::string, for report lines handed to a Console.
#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>

namespace strata::golden {

#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
inline std::string sformat(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    const int n = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    std::string out(n > 0 ? static_cast<size_t>(n) : 0u, '\0');
    if (n > 0) std::vsnprintf(&out[0], out.size() + 1, fmt, args);
    va_end(args);
    return out;
}

}  // namespace strata::golden
