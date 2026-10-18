#include <cstring>

#include "test.hpp"

namespace strata::test {

namespace {
int g_failures = 0;
}

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

void recordFailure(const char* file, int line, const std::string& what) {
    ++g_failures;
    std::printf("    %s:%d: %s\n", file, line, what.c_str());
}

}  // namespace strata::test

int main(int argc, char** argv) {
    using namespace strata::test;
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failedCases = 0;
    for (const Case& c : registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        const int before = g_failures;
        try {
            c.fn();
        } catch (const RequireFailed&) {
        } catch (const std::exception& e) {
            recordFailure(c.name, 0, std::string("unexpected exception: ") + e.what());
        }
        ++run;
        const bool failed = g_failures != before;
        failedCases += failed ? 1 : 0;
        std::printf("%s %s\n", failed ? "FAIL" : "ok  ", c.name);
    }
    std::printf("%d test(s), %d failed\n", run, failedCases);
    return failedCases == 0 && run > 0 ? 0 : 1;
}
