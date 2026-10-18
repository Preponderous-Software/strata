#include "render/lights.hpp"

#include <algorithm>
#include <cmath>

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
    if (l.flags != 0u) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "point light %lu: unknown flags 0x%x", i, l.flags);
    }
}

void validatePointLights(const StrataPointLight* lights, size_t count) {
    if (count > STRATA_MAX_POINT_LIGHTS) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "at most %d point lights (got %lu)", STRATA_MAX_POINT_LIGHTS,
             static_cast<unsigned long>(count));
    }
    for (size_t i = 0; i < count; ++i) validatePointLight(lights[i], i);
}

}  // namespace strata::render
