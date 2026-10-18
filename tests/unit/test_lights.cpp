#include <cmath>
#include <cstring>
#include <limits>

#include <glm/glm.hpp>

#include "render/lights.hpp"
#include "strata/strata.hpp"
#include "test.hpp"

using namespace strata::render;

namespace {

StrataPointLight goodLight() {
    StrataPointLight l{};
    l.position[1] = 2.0f;
    l.range = 10.0f;
    l.color[0] = 1.0f;
    l.color[1] = 0.6f;
    l.color[2] = 0.3f;
    l.intensity = 4.0f;
    return l;
}

bool rejects(const StrataPointLight& light) {
    try {
        validatePointLight(light, 3);
    } catch (const strata::Error& e) {
        return e.status() == STRATA_ERROR_INVALID_ARGUMENT && std::strstr(e.what(), "point light 3") != nullptr;
    }
    return false;
}

}  // namespace

TEST(point_light_falloff_is_intensity_at_the_light_and_zero_at_range) {
    CHECK_NEAR(pointLightAttenuation(0.0f, 10.0f, 4.0f), 4.0, 1e-6);
    CHECK_NEAR(pointLightAttenuation(10.0f, 10.0f, 4.0f), 0.0, 0.0);
    CHECK_NEAR(pointLightAttenuation(25.0f, 10.0f, 4.0f), 0.0, 0.0);
    // Between: the range-scaled inverse-square core times the squared window.
    const double x = 0.5, window = 1.0 - (x * x) * (x * x);
    CHECK_NEAR(pointLightAttenuation(5.0f, 10.0f, 4.0f), 4.0 * window * window / (1.0 + 16.0 * x * x), 1e-6);
    // The shape depends only on distance / range, not on world units.
    CHECK_NEAR(pointLightAttenuation(2.0f, 8.0f, 1.0f), pointLightAttenuation(20.0f, 80.0f, 1.0f), 1e-6);
    CHECK_NEAR(pointLightAttenuation(3.0f, 10.0f, 0.0f), 0.0, 0.0);
    CHECK_NEAR(pointLightAttenuation(3.0f, 0.0f, 4.0f), 0.0, 0.0);
}

TEST(point_light_falloff_decreases_monotonically_to_range) {
    float previous = pointLightAttenuation(0.0f, 8.0f, 1.0f);
    for (int i = 1; i <= 80; ++i) {
        const float a = pointLightAttenuation(0.1f * static_cast<float>(i), 8.0f, 1.0f);
        CHECK(a <= previous);
        previous = a;
    }
    CHECK(previous == 0.0f);
}

TEST(point_light_validation_accepts_a_good_light_and_names_bad_fields) {
    validatePointLight(goodLight(), 0);  // must not throw
    StrataPointLight l = goodLight();
    l.range = 0.0f;
    CHECK(rejects(l));
    l = goodLight();
    l.range = std::numeric_limits<float>::infinity();
    CHECK(rejects(l));
    l = goodLight();
    l.position[2] = std::numeric_limits<float>::quiet_NaN();
    CHECK(rejects(l));
    l = goodLight();
    l.color[1] = -0.1f;
    CHECK(rejects(l));
    l = goodLight();
    l.intensity = -1.0f;
    CHECK(rejects(l));
    l = goodLight();
    l.flags = 0x2u;  // only STRATA_POINT_LIGHT_CAST_SHADOWS is defined
    CHECK(rejects(l));
    l.flags = STRATA_POINT_LIGHT_CAST_SHADOWS;
    validatePointLight(l, 0);  // must not throw
}

TEST(at_most_four_point_lights_cast_shadows) {
    StrataPointLight lights[8];
    for (auto& l : lights) l = goodLight();
    for (int i = 0; i < STRATA_MAX_SHADOWED_POINT_LIGHTS; ++i) lights[i * 2].flags = STRATA_POINT_LIGHT_CAST_SHADOWS;
    validatePointLights(lights, 8);  // four casters: fine
    lights[7].flags = STRATA_POINT_LIGHT_CAST_SHADOWS;
    bool threw = false;
    try {
        validatePointLights(lights, 8);
    } catch (const strata::Error& e) {
        threw = std::strstr(e.what(), "cast shadows") != nullptr;
    }
    CHECK(threw);
}

TEST(cube_faces_follow_the_gl_face_order_and_cover_their_axis) {
    const glm::vec3 light(3.0f, 2.0f, -1.0f);
    const auto faces = cubeFaceViewProjections(light, 10.0f);
    const glm::vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
        for (int a = 0; a < 6; ++a) {
            const glm::vec4 clip = faces[static_cast<size_t>(f)] * glm::vec4(light + axes[a] * 4.0f, 1.0f);
            const bool inside = clip.w > 0.0f && std::fabs(clip.x) <= clip.w && std::fabs(clip.y) <= clip.w &&
                                std::fabs(clip.z) <= clip.w;
            CHECK(inside == (f == a));
            if (f == a) {
                CHECK_NEAR(clip.x / clip.w, 0.0, 1e-5);  // the axis is the face's centre
                CHECK_NEAR(clip.y / clip.w, 0.0, 1e-5);
            }
        }
    }
    // A point beyond the range falls past the far plane.
    const glm::vec4 far = faces[0] * glm::vec4(light + glm::vec3(10.5f, 0, 0), 1.0f);
    CHECK(far.z > far.w);
    // +X face, GL convention: up is -Y, so a point above the axis lands at negative clip y.
    const glm::vec4 above = faces[0] * glm::vec4(light + glm::vec3(4, 1, 0), 1.0f);
    CHECK(above.y < 0.0f);
}

TEST(point_light_lists_are_checked_whole_and_capped) {
    StrataPointLight lights[STRATA_MAX_POINT_LIGHTS + 1];
    for (auto& l : lights) l = goodLight();
    validatePointLights(lights, STRATA_MAX_POINT_LIGHTS);  // the maximum is fine
    validatePointLights(nullptr, 0);
    bool threw = false;
    try {
        validatePointLights(lights, STRATA_MAX_POINT_LIGHTS + 1);
    } catch (const strata::Error&) {
        threw = true;
    }
    CHECK(threw);
    lights[5].range = -1.0f;
    threw = false;
    try {
        validatePointLights(lights, 8);
    } catch (const strata::Error& e) {
        threw = std::strstr(e.what(), "point light 5") != nullptr;
    }
    CHECK(threw);
}
