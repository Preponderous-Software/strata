#include "render/pass_list.hpp"
#include "test.hpp"

using namespace strata::render;

TEST(engine_pass_list_is_valid) {
    CHECK(validatePassOrder(framePasses(), frameInputs()).empty());
    CHECK(framePasses().front().name == std::string("shadow"));
    CHECK(describePasses(framePasses()).find("opaque:") != std::string::npos);
    CHECK(describePasses(framePasses()).find("point_lights") != std::string::npos);
    CHECK(describePasses(framePasses()).find("point_shadow:") != std::string::npos);
    CHECK(describePasses(framePasses()).find("terrain") != std::string::npos);
}

TEST(reading_before_writing_is_reported) {
    const std::vector<PassInfo> passes = {
        {"sky", {"depth"}, {"color"}},
        {"opaque", {"instances"}, {"color", "depth"}},
    };
    const auto problems = validatePassOrder(passes, {"instances"});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].find("sky") != std::string::npos);
}

TEST(duplicate_passes_are_reported) {
    const std::vector<PassInfo> passes = {{"opaque", {}, {"color"}}, {"opaque", {}, {"color"}}};
    CHECK(validatePassOrder(passes, {}).size() == 1);
}
