// Point lights: validation and the falloff, shared by the renderer and the
// unit tests. The shader evaluates the same formula (shaders.hpp).
#pragma once

#include <cstddef>

#include "strata/strata.h"

namespace strata::render {

// With x = distance / range:
//   intensity * window^2 / (1 + 16 x^2),  window = clamp(1 - x^4, 0, 1).
// An inverse-square-like core scaled to the range (so the shape of a pool of
// light does not depend on world units), windowed to exactly 0 at and beyond
// `range` and smooth all the way there.
float pointLightAttenuation(float distance, float range, float intensity);

// Throws strata::Error(STRATA_ERROR_INVALID_ARGUMENT) naming the light and the
// bad field: non-finite values, range <= 0, a negative colour or intensity,
// or unknown flag bits.
void validatePointLight(const StrataPointLight& light, size_t index);

// Validates every light (and the count) before the caller changes anything.
void validatePointLights(const StrataPointLight* lights, size_t count);

}  // namespace strata::render
