#include "render/lights.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "core/error.hpp"

namespace strata::render {

float pointLightAttenuation(float distance, float range, float intensity) {
    if (!(range > 0.0f)) return 0.0f;
    const float ratio = distance / range;
    const float r2 = ratio * ratio;
    const float window = std::clamp(1.0f - r2 * r2, 0.0f, 1.0f);
    return intensity * window * window / (1.0f + 16.0f * r2);
}

void validatePointLight(const StrataPointLight& l, size_t index) {
    const auto i = static_cast<unsigned long>(index);
    for (float v : l.position) {
        if (!std::isfinite(v)) fail(STRATA_ERROR_INVALID_ARGUMENT, "point light %lu: position is not finite", i);
    }
    if (!(l.range > 0.0f) || !std::isfinite(l.range)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "point light %lu: range must be positive and finite (got %g)", i,
             static_cast<double>(l.range));
    }
    for (float v : l.color) {
        if (!(v >= 0.0f) || !std::isfinite(v)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "point light %lu: colour components must be finite and >= 0", i);
        }
    }
    if (!(l.intensity >= 0.0f) || !std::isfinite(l.intensity)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "point light %lu: intensity must be finite and >= 0 (got %g)", i,
             static_cast<double>(l.intensity));
    }
    if ((l.flags & ~STRATA_POINT_LIGHT_CAST_SHADOWS) != 0u) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "point light %lu: unknown flags 0x%x", i,
             l.flags & ~STRATA_POINT_LIGHT_CAST_SHADOWS);
    }
}

void validatePointLights(const StrataPointLight* lights, size_t count, size_t otherCasters, size_t maxCount) {
    if (count > maxCount) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "at most %lu point lights (got %lu)%s", static_cast<unsigned long>(maxCount),
             static_cast<unsigned long>(count),
             maxCount < STRATA_MAX_CLUSTERED_LIGHTS ? "; STRATA_LIGHT_CULLING_CLUSTERED (ABI 1.21) allows more" : "");
    }
    size_t casters = 0;
    for (size_t i = 0; i < count; ++i) {
        validatePointLight(lights[i], i);
        if (lights[i].flags & STRATA_POINT_LIGHT_CAST_SHADOWS) ++casters;
    }
    if (casters > STRATA_MAX_SHADOWED_POINT_LIGHTS) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "at most %d point lights may cast shadows (got %lu)",
             STRATA_MAX_SHADOWED_POINT_LIGHTS, static_cast<unsigned long>(casters));
    }
    if (casters + otherCasters > STRATA_MAX_SHADOWED_POINT_LIGHTS) {
        fail(STRATA_ERROR_INVALID_ARGUMENT,
             "at most %d point and spot lights together may cast shadows (got %lu point and %lu spot)",
             STRATA_MAX_SHADOWED_POINT_LIGHTS, static_cast<unsigned long>(casters),
             static_cast<unsigned long>(otherCasters));
    }
}

float spotConeFactor(float cosAngle, float cosInner, float cosOuter) {
    const float t = std::clamp((cosAngle - cosOuter) / std::max(cosInner - cosOuter, 1e-4f), 0.0f, 1.0f);
    return t * t;
}

void validateSpotLight(const StrataSpotLight& l, size_t index) {
    const auto i = static_cast<unsigned long>(index);
    for (float v : l.position) {
        if (!std::isfinite(v)) fail(STRATA_ERROR_INVALID_ARGUMENT, "spot light %lu: position is not finite", i);
    }
    if (!(l.range > 0.0f) || !std::isfinite(l.range)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "spot light %lu: range must be positive and finite (got %g)", i,
             static_cast<double>(l.range));
    }
    const glm::vec3 d(l.direction[0], l.direction[1], l.direction[2]);
    if (!std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z) || !(glm::length(d) > 1e-6f)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "spot light %lu: direction must be finite and non-zero", i);
    }
    for (float v : l.color) {
        if (!(v >= 0.0f) || !std::isfinite(v)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "spot light %lu: colour components must be finite and >= 0", i);
        }
    }
    if (!(l.intensity >= 0.0f) || !std::isfinite(l.intensity)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "spot light %lu: intensity must be finite and >= 0 (got %g)", i,
             static_cast<double>(l.intensity));
    }
    if (!(l.outer_cone_degrees > 0.0f && l.outer_cone_degrees <= 90.0f) ||
        !(l.inner_cone_degrees >= 0.0f && l.inner_cone_degrees <= l.outer_cone_degrees)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT,
             "spot light %lu: cones must satisfy 0 <= inner <= outer, 0 < outer <= 90 degrees (got %g, %g)", i,
             static_cast<double>(l.inner_cone_degrees), static_cast<double>(l.outer_cone_degrees));
    }
    if ((l.flags & ~STRATA_SPOT_LIGHT_CAST_SHADOWS) != 0u) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "spot light %lu: unknown flags 0x%x", i,
             l.flags & ~STRATA_SPOT_LIGHT_CAST_SHADOWS);
    }
}

void validateSpotLights(const StrataSpotLight* lights, size_t count, size_t otherCasters, size_t maxCount) {
    if (count > maxCount) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "at most %lu spot lights (got %lu)%s", static_cast<unsigned long>(maxCount),
             static_cast<unsigned long>(count),
             maxCount < STRATA_MAX_CLUSTERED_LIGHTS ? "; STRATA_LIGHT_CULLING_CLUSTERED (ABI 1.21) allows more" : "");
    }
    for (size_t i = 0; i < count; ++i) validateSpotLight(lights[i], i);
    const size_t casters = countShadowCasters(lights, count);
    if (casters + otherCasters > STRATA_MAX_SHADOWED_POINT_LIGHTS) {
        fail(STRATA_ERROR_INVALID_ARGUMENT,
             "at most %d point and spot lights together may cast shadows (got %lu point and %lu spot)",
             STRATA_MAX_SHADOWED_POINT_LIGHTS, static_cast<unsigned long>(otherCasters),
             static_cast<unsigned long>(casters));
    }
}

size_t countShadowCasters(const StrataPointLight* lights, size_t count) {
    size_t n = 0;
    for (size_t i = 0; i < count; ++i) n += (lights[i].flags & STRATA_POINT_LIGHT_CAST_SHADOWS) ? 1 : 0;
    return n;
}

size_t countShadowCasters(const StrataSpotLight* lights, size_t count) {
    size_t n = 0;
    for (size_t i = 0; i < count; ++i) n += (lights[i].flags & STRATA_SPOT_LIGHT_CAST_SHADOWS) ? 1 : 0;
    return n;
}

std::array<glm::mat4, 6> cubeFaceViewProjections(const glm::vec3& p, float range) {
    // The GL cube map face conventions (direction, up), as every cube shadow
    // map renderer uses them.
    static const glm::vec3 kDirections[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const glm::vec3 kUps[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
    const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, kPointShadowNear, range);
    std::array<glm::mat4, 6> faces{};
    for (size_t i = 0; i < 6; ++i) faces[i] = projection * glm::lookAt(p, p + kDirections[i], kUps[i]);
    return faces;
}

}  // namespace strata::render
