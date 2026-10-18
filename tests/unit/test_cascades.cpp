#include <glm/gtc/matrix_transform.hpp>

#include "render/cascades.hpp"
#include "test.hpp"

using namespace strata::render;

namespace {

CascadeInput baseInput() {
    CascadeInput in;
    in.view = glm::lookAt(glm::vec3(10, 20, 30), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    in.tanHalfFovY = std::tan(glm::radians(30.0f));
    in.aspect = 16.0f / 9.0f;
    in.nearPlane = 0.5f;
    in.farPlane = 400.0f;
    in.shadowDistance = 150.0f;
    in.count = 4;
    in.lambda = 0.6f;
    in.mapSize = 2048;
    in.toLight = glm::normalize(glm::vec3(0.4f, 0.8f, 0.3f));
    return in;
}

}  // namespace

TEST(splits_are_ordered_and_span_near_to_far) {
    const auto s = practicalSplits(0.5f, 150.0f, 4, 0.6f);
    CHECK_NEAR(s[0], 0.5, 1e-6);
    CHECK_NEAR(s[4], 150.0, 1e-4);
    for (int i = 0; i < 4; ++i) CHECK(s[i] < s[i + 1]);
}

TEST(lambda_zero_is_uniform_and_one_is_logarithmic) {
    const auto u = practicalSplits(1.0f, 101.0f, 4, 0.0f);
    CHECK_NEAR(u[1], 26.0, 1e-4);
    CHECK_NEAR(u[2], 51.0, 1e-4);
    const auto l = practicalSplits(1.0f, 10000.0f, 4, 1.0f);
    CHECK_NEAR(l[1], 10.0, 1e-3);
    CHECK_NEAR(l[2], 100.0, 1e-2);
}

TEST(bounding_sphere_contains_the_slice_corners) {
    const float tanHalf = 0.6f, aspect = 1.5f, a = 2.0f, b = 40.0f;
    float c = 0, r = 0;
    sliceBoundingSphere(tanHalf, aspect, a, b, c, r);
    for (float z : {a, b}) {
        for (float sx : {-1.0f, 1.0f}) {
            for (float sy : {-1.0f, 1.0f}) {
                const glm::vec3 corner(sx * z * tanHalf * aspect, sy * z * tanHalf, z);
                CHECK(glm::length(corner - glm::vec3(0, 0, c)) <= r + 1e-3f);
            }
        }
    }
}

TEST(cascade_size_does_not_change_when_the_camera_turns) {
    CascadeInput in = baseInput();
    const Cascades a = computeCascades(in);
    in.view = glm::lookAt(glm::vec3(10, 20, 30), glm::vec3(50, -5, -20), glm::vec3(0, 1, 0));
    const Cascades b = computeCascades(in);
    REQUIRE(a.count == 4 && b.count == 4);
    for (int i = 0; i < 4; ++i) {
        CHECK_NEAR(a.radius[i], b.radius[i], 1e-6);
        CHECK_NEAR(a.texelWorld[i], b.texelWorld[i], 1e-9);
    }
}

TEST(snapping_holds_the_projection_still_for_sub_texel_moves) {
    CascadeInput in = baseInput();
    const Cascades a = computeCascades(in);
    // Move the camera by a tiny fraction of the finest texel: every cascade
    // either keeps its matrix exactly or jumps by a whole texel, never less.
    in.view = glm::translate(in.view, glm::vec3(a.texelWorld[0] * 0.01f, 0, 0));
    const Cascades b = computeCascades(in);
    for (int i = 0; i < 4; ++i) {
        const glm::vec4 originA = a.viewProj[i] * glm::vec4(0, 0, 0, 1);
        const glm::vec4 originB = b.viewProj[i] * glm::vec4(0, 0, 0, 1);
        const float texelsMoved = (originB.x - originA.x) * 2048.0f / 2.0f;
        const float fraction = std::fabs(texelsMoved - std::round(texelsMoved));
        CHECK(fraction < 1e-2f);
    }
}

TEST(each_cascade_covers_its_slice) {
    const CascadeInput in = baseInput();
    const Cascades cascades = computeCascades(in);
    const glm::mat4 inverseView = glm::inverse(in.view);
    float sliceNear = in.nearPlane;
    for (int i = 0; i < cascades.count; ++i) {
        const float sliceFar = cascades.splitFar[i];
        for (float z : {sliceNear, sliceFar}) {
            for (float sx : {-1.0f, 1.0f}) {
                for (float sy : {-1.0f, 1.0f}) {
                    const glm::vec4 viewCorner(sx * z * in.tanHalfFovY * in.aspect, sy * z * in.tanHalfFovY, -z, 1);
                    const glm::vec4 clip = cascades.viewProj[i] * (inverseView * viewCorner);
                    CHECK(std::fabs(clip.x) <= 1.0f + 1e-3f);
                    CHECK(std::fabs(clip.y) <= 1.0f + 1e-3f);
                    CHECK(clip.z >= -1.0f - 1e-3f && clip.z <= 1.0f + 1e-3f);
                }
            }
        }
        sliceNear = sliceFar;
    }
    CHECK_NEAR(cascades.splitFar[3], 150.0, 1e-3);
}

TEST(shadow_distance_is_clamped_to_the_far_plane) {
    CascadeInput in = baseInput();
    in.farPlane = 60.0f;
    const Cascades c = computeCascades(in);
    CHECK_NEAR(c.splitFar[c.count - 1], 60.0, 1e-4);
}
