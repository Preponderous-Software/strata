// Cascaded shadow map setup: where the cascades split, and the light-space
// projection that covers each one.
//
// - Splits use the practical scheme: a blend (lambda) of logarithmic and
//   uniform splits between the camera's near plane and the shadow distance.
// - Each cascade's projection fits a bounding sphere of its frustum slice, so
//   its size does not change as the camera turns.
// - The sphere's centre is snapped to whole shadow texels in light space, so
//   shadows do not shimmer as the camera moves.
// Pure arithmetic: no GL, so it is unit-tested directly.
#pragma once

#include <array>

#include <glm/glm.hpp>

namespace strata::render {

constexpr int kMaxCascades = 4;

struct CascadeInput {
    glm::mat4 view{1.0f};       // the camera's view matrix
    float tanHalfFovY = 0.577f; // from the projection
    float aspect = 1.0f;        // width / height
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    float shadowDistance = 100.0f;  // cascades stop here (clamped to the far plane)
    int count = 4;
    float lambda = 0.6f;
    int mapSize = 2048;
    glm::vec3 toLight{0, 1, 0};  // unit vector towards the light
};

struct Cascades {
    int count = 0;
    // splitFar[i]: view-space distance where cascade i ends.
    std::array<float, kMaxCascades> splitFar{};
    std::array<glm::mat4, kMaxCascades> viewProj{};
    // World-space size of one shadow texel in each cascade (for normal offset).
    std::array<float, kMaxCascades> texelWorld{};
    std::array<float, kMaxCascades> radius{};
    std::array<glm::vec3, kMaxCascades> centre{};
};

// splits[0] = near, splits[count] = far.
std::array<float, kMaxCascades + 1> practicalSplits(float nearPlane, float farPlane, int count, float lambda);

// Bounding sphere of the view-frustum slice [sliceNear, sliceFar], as the
// view-space distance of its centre along the view axis plus its radius.
void sliceBoundingSphere(float tanHalfFovY, float aspect, float sliceNear, float sliceFar, float& centreDistance,
                         float& radius);

Cascades computeCascades(const CascadeInput& input);

// The rotation-only light view used by every cascade (looks along -toLight).
glm::mat4 lightRotation(const glm::vec3& toLight);

}  // namespace strata::render
