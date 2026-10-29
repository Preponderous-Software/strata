// The web kit's point-light cut (web/kit/strata_kit.h, kit 1.2): the tier
// keeps the highest-priority lights, and a dropped fill light's ambient lift
// replaces it.
#include "strata_kit.h"
#include "test.hpp"

using strata_kit::Light;

namespace {

Light at(float x, int priority, strata_kit::Rgb dropLift = {0.0f, 0.0f, 0.0f}) {
    Light l{};
    l.light.position[0] = x;
    l.light.range = 5.0f;
    l.light.intensity = 1.0f;
    l.priority = priority;
    l.dropLift = dropLift;
    return l;
}

}  // namespace

TEST(kit_cut_keeps_highest_priority_lights_in_order_and_lifts_nothing_without_fills) {
    std::vector<Light> lights = {at(0, 0), at(1, 2), at(2, 0), at(3, 1), at(4, 2), at(5, 0)};
    StrataPointLight kept[STRATA_MAX_POINT_LIGHTS];
    float lift[3] = {0.0f, 0.0f, 0.0f};
    CHECK(strata_kit::cutLights(lights, 4, kept, lift) == 4);
    const float order[4] = {1, 4, 3, 0};  // priority 2, 2, 1, then the first 0
    for (int i = 0; i < 4; ++i) CHECK(kept[i].position[0] == order[i]);
    CHECK(lift[0] == 0.0f && lift[1] == 0.0f && lift[2] == 0.0f);
}

TEST(kit_cut_turns_dropped_fill_lights_into_ambient_lift) {
    std::vector<Light> lights = {at(0, 5), at(1, 5), at(2, 0, {0.05f, 0.04f, 0.03f}), at(3, 0, {0.01f, 0.02f, 0.03f})};
    StrataPointLight kept[STRATA_MAX_POINT_LIGHTS];
    float lift[3] = {0.1f, 0.0f, 0.0f};  // the host's own lift comes first
    CHECK(strata_kit::cutLights(lights, 3, kept, lift) == 3);
    CHECK_NEAR(lift[0], 0.11, 1e-6);  // only the last fill was dropped
    CHECK_NEAR(lift[1], 0.02, 1e-6);
    CHECK_NEAR(lift[2], 0.03, 1e-6);
    float none[3] = {0.0f, 0.0f, 0.0f};
    CHECK(strata_kit::cutLights(lights, 16, kept, none) == 4);  // a higher tier keeps every fill
    CHECK(none[0] == 0.0f && none[1] == 0.0f && none[2] == 0.0f);
}
