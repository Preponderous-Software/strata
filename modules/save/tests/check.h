// Minimal assertion harness for the save-core tests: CHECK records a failure
// and continues, so one run reports every broken guarantee.
#pragma once

#include <cstdio>
#include <string>

namespace check {
inline int& failures() {
    static int n = 0;
    return n;
}
}  // namespace check

#define CHECK_MSG(cond, msg)                                                                              \
    do {                                                                                                  \
        if (!(cond)) {                                                                                    \
            std::printf("  FAIL %s:%d: %s -- %s\n", __FILE__, __LINE__, #cond, std::string(msg).c_str()); \
            std::fflush(stdout);                                                                          \
            ++check::failures();                                                                          \
        }                                                                                                 \
    } while (0)
#define CHECK(cond) CHECK_MSG(cond, "")
#define SECTION(name)                    \
    do {                                 \
        std::printf("- %s\n", name);     \
        std::fflush(stdout);             \
    } while (0)
