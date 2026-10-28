#include <string>

#include "render/renderer.hpp"
#include "strata/strata.hpp"
#include "test.hpp"

using namespace strata::render;

namespace {

StrataLighting goodSun() {
    StrataLighting l{};
    l.mode = STRATA_LIGHTING_SUN;
    l.time_of_day = 0.4f;
    l.latitude_degrees = 40.0f;
    l.cascade_count = 4;
    l.shadow_map_size = 2048;
    l.shadow_distance = 120.0f;
    l.split_lambda = 0.6f;
    l.fog = 1;
    return l;
}

StrataLighting goodLegacy() {
    StrataLighting l{};
    l.mode = STRATA_LIGHTING_LEGACY;
    l.legacy_direction[0] = 0.3f;
    l.legacy_direction[1] = -1.0f;
    l.legacy_ambient = 0.35f;
    return l;
}

// The error strata_last_error() would carry, or "" if the lighting is accepted.
std::string rejection(const StrataLighting& lighting) {
    try {
        Renderer::validateLighting(lighting);
    } catch (const strata::Error& e) {
        if (e.status() != STRATA_ERROR_INVALID_ARGUMENT) return "wrong status";
        return e.what();
    }
    return "";
}

bool mentions(const std::string& message, const char* field, const char* value) {
    return message.find(field) != std::string::npos && message.find(value) != std::string::npos;
}

}  // namespace

TEST(lighting_validation_accepts_good_sun_legacy_and_space_lighting) {
    CHECK(rejection(goodSun()).empty());
    CHECK(rejection(goodLegacy()).empty());
    StrataLighting space{};
    space.mode = STRATA_LIGHTING_SPACE;
    space.legacy_ambient = 0.03f;
    CHECK(rejection(space).empty());
}

TEST(lighting_validation_errors_name_the_rejected_value) {
    StrataLighting l = goodSun();
    l.split_lambda = 1.5f;
    CHECK(mentions(rejection(l), "split_lambda", "(got 1.5)"));

    l = goodSun();
    l.shadow_distance = -3.0f;
    CHECK(mentions(rejection(l), "shadow_distance", "(got -3)"));

    l = goodSun();
    l.latitude_degrees = 95.0f;
    CHECK(mentions(rejection(l), "time_of_day 0.4", "latitude 95"));

    l = goodLegacy();
    l.legacy_ambient = 2.0f;
    CHECK(mentions(rejection(l), "legacy ambient", "(got 2)"));

    StrataLighting space{};
    space.mode = STRATA_LIGHTING_SPACE;
    space.legacy_ambient = -0.5f;
    CHECK(mentions(rejection(space), "space ambient", "(got -0.5)"));

    l = goodSun();
    l.cascade_count = 9;
    CHECK(mentions(rejection(l), "cascade_count", "(got 9)"));
}
