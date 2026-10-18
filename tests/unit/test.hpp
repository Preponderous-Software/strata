// A deliberately tiny test harness: TEST registers a function, CHECK records a
// failure and carries on, REQUIRE records one and leaves the test.
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace strata::test {

struct Case {
    const char* name;
    std::function<void()> fn;
};

std::vector<Case>& registry();
void recordFailure(const char* file, int line, const std::string& what);

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct RequireFailed {};

}  // namespace strata::test

#define STRATA_CONCAT2(a, b) a##b
#define STRATA_CONCAT(a, b) STRATA_CONCAT2(a, b)
#define TEST(name)                                                                              \
    static void STRATA_CONCAT(test_, name)();                                                   \
    static ::strata::test::Registrar STRATA_CONCAT(registrar_, name)(#name, STRATA_CONCAT(test_, name)); \
    static void STRATA_CONCAT(test_, name)()

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) ::strata::test::recordFailure(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define REQUIRE(cond)                                                                    \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            ::strata::test::recordFailure(__FILE__, __LINE__, "REQUIRE(" #cond ")");     \
            throw ::strata::test::RequireFailed{};                                       \
        }                                                                                \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                              \
    do {                                                                                                   \
        const double strata_a_ = (a), strata_b_ = (b);                                                     \
        if (!(std::fabs(strata_a_ - strata_b_) <= (eps)))                                                  \
            ::strata::test::recordFailure(__FILE__, __LINE__,                                              \
                                          "CHECK_NEAR(" #a ", " #b ") " + std::to_string(strata_a_) + " vs " + \
                                              std::to_string(strata_b_));                                  \
    } while (0)
