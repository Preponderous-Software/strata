// Depth readback (ABI 1.9): window depth back to planar view distance for
// both depth conventions and orthographic projections, and the RGBA8 packing
// the GPU pass uses, all without GL.
#include <cmath>
#include <cstring>
#include <limits>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "render/depth_readback.hpp"
#include "test.hpp"

using namespace strata::render;

namespace {

// What the GPU stores for a view-space point `distance` in front of the
// camera: NDC z mapped to window depth by glDepthRange(0, 1).
float windowDepthOf(const glm::mat4& projection, glm::vec3 viewPoint) {
    const glm::vec4 clip = projection * glm::vec4(viewPoint, 1.0f);
    return (clip.z / clip.w + 1.0f) * 0.5f;
}

}  // namespace

TEST(depth_readback_planar_for_a_minus_one_to_one_perspective) {
    const glm::mat4 p = glm::perspectiveRH_NO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 50.0f);
    for (float distance : {0.1f, 0.5f, 1.0f, 4.0f, 12.5f, 49.0f}) {
        const float d = windowDepthOf(p, {0.0f, 0.0f, -distance});
        CHECK_NEAR(planarDepth(glm::value_ptr(p), d), distance, distance * 1e-4f);
        // Planar, not radial: a point off the axis at the same view z reads the same.
        const float off = windowDepthOf(p, {distance * 0.4f, -distance * 0.3f, -distance});
        CHECK_NEAR(planarDepth(glm::value_ptr(p), off), distance, distance * 1e-4f);
    }
}

TEST(depth_readback_planar_for_a_zero_to_one_perspective) {
    // A host may pass a D3D-style projection (strata.h: either depth convention).
    const glm::mat4 p = glm::perspectiveRH_ZO(glm::radians(45.0f), 4.0f / 3.0f, 0.25f, 100.0f);
    for (float distance : {0.3f, 2.0f, 30.0f, 99.0f}) {
        CHECK_NEAR(planarDepth(glm::value_ptr(p), windowDepthOf(p, {0.0f, 0.0f, -distance})), distance,
                   distance * 1e-4f);
    }
}

TEST(depth_readback_planar_for_an_orthographic_projection) {
    const glm::mat4 p = glm::orthoRH_NO(-5.0f, 5.0f, -3.0f, 3.0f, 1.0f, 21.0f);
    for (float distance : {1.0f, 7.5f, 20.0f}) {
        CHECK_NEAR(planarDepth(glm::value_ptr(p), windowDepthOf(p, {1.0f, 2.0f, -distance})), distance, 1e-4f);
    }
}

TEST(depth_readback_cleared_depth_is_infinity) {
    const glm::mat4 p = glm::perspectiveRH_NO(glm::radians(60.0f), 1.0f, 0.1f, 50.0f);
    CHECK(std::isinf(planarDepth(glm::value_ptr(p), 1.0f)));
    CHECK(planarDepth(glm::value_ptr(p), 1.0f) > 0.0f);
    CHECK(std::isinf(planarDepth(glm::value_ptr(p), std::numeric_limits<float>::quiet_NaN())));
}

TEST(depth_readback_unpacks_the_float_bits_low_byte_first) {
    for (float value : {0.0f, 0.5f, 0.999999f, 1.0f, 0.123456789f}) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof bits);
        const uint8_t texel[4] = {static_cast<uint8_t>(bits & 0xffu), static_cast<uint8_t>((bits >> 8) & 0xffu),
                                  static_cast<uint8_t>((bits >> 16) & 0xffu), static_cast<uint8_t>(bits >> 24)};
        CHECK(unpackDepthTexel(texel) == value);
    }
}
