// Point lights: validation and the falloff, shared by the renderer and the
// unit tests. The shader evaluates the same formula (shaders.hpp).
#pragma once

#include <array>
#include <cstddef>

#include <glm/glm.hpp>

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

// Validates every light, the count (at most maxCount: STRATA_MAX_POINT_LIGHTS,
// or STRATA_MAX_CLUSTERED_LIGHTS under clustered culling, ABI 1.21) and the
// number of shadow casters (with otherCasters, the spot lights', ABI 1.20)
// before the caller changes anything.
void validatePointLights(const StrataPointLight* lights, size_t count, size_t otherCasters = 0,
                         size_t maxCount = STRATA_MAX_POINT_LIGHTS);

// Spot lights (ABI 1.20) ----------------------------------------------------

// The cone factor: with t = (cos(angle) - cos(outer)) / (cos(inner) - cos(outer))
// clamped to 0..1, t * t (1 inside the inner cone, 0 outside the outer).
// cosInner == cosOuter gives a hard edge. The shader evaluates the same.
float spotConeFactor(float cosAngle, float cosInner, float cosOuter);

// Throws strata::Error(STRATA_ERROR_INVALID_ARGUMENT) naming the light and the
// bad field: non-finite values, range <= 0, a zero direction, a negative
// colour or intensity, cones outside 0 <= inner <= outer, 0 < outer <= 90,
// or unknown flag bits.
void validateSpotLight(const StrataSpotLight& light, size_t index);

// Validates every light and the count (at most maxCount, as for point
// lights). otherCasters is the number of shadow casters in the point-light
// list; the two together may not exceed STRATA_MAX_SHADOWED_POINT_LIGHTS.
void validateSpotLights(const StrataSpotLight* lights, size_t count, size_t otherCasters,
                        size_t maxCount = STRATA_MAX_SPOT_LIGHTS);

// Shadow-casting lights in a list.
size_t countShadowCasters(const StrataPointLight* lights, size_t count);
size_t countShadowCasters(const StrataSpotLight* lights, size_t count);

// Cube shadow maps (L4) ------------------------------------------------------

constexpr int kPointShadowMapSize = 512;  // texels per cube face side
constexpr float kPointShadowNear = 0.05f;

// View-projection for each cube face in GL order (+X, -X, +Y, -Y, +Z, -Z),
// a 90-degree perspective from the light out to `range`. Rendering face i into
// layer (6 * slot + i) of a cube map array makes texture(cubeArray, vec4(dir,
// slot)) look up the texel that direction `dir` from the light hit.
std::array<glm::mat4, 6> cubeFaceViewProjections(const glm::vec3& lightPosition, float range);

}  // namespace strata::render
