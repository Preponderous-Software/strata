// Ray picking (ABI 1.8): ray-box, ray-triangle and the nearest hit over a
// mesh with a model matrix, all without GL.
#include <cmath>
#include <limits>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "render/ray_pick.hpp"
#include "strata/strata.hpp"
#include "test.hpp"

using namespace strata::render;

namespace {

const float kInf = std::numeric_limits<float>::infinity();

bool near(glm::vec3 a, glm::vec3 b, float eps = 1e-4f) { return glm::length(a - b) < eps; }

// A unit cube centred on the origin, 12 triangles wound counter-clockwise
// seen from outside. Faces in order: -X, +X, -Y, +Y, -Z, +Z (two each).
const std::vector<float> kCube = {
    -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, 0.5f, -0.5f,  -0.5f, 0.5f, -0.5f,
    -0.5f, -0.5f, 0.5f,   0.5f, -0.5f, 0.5f,   0.5f, 0.5f, 0.5f,   -0.5f, 0.5f, 0.5f};
const std::vector<uint32_t> kCubeIndices = {
    0, 4, 7, 0, 7, 3,  // -X
    1, 2, 6, 1, 6, 5,  // +X
    0, 1, 5, 0, 5, 4,  // -Y
    3, 7, 6, 3, 6, 2,  // +Y
    0, 3, 2, 0, 2, 1,  // -Z
    4, 5, 6, 4, 6, 7}; // +Z

PickGeometry cube() {
    StrataMesh m{};
    m.positions = kCube.data();
    m.indices = kCubeIndices.data();
    m.vertex_count = 8;
    m.index_count = static_cast<int32_t>(kCubeIndices.size());
    return buildPickGeometry(m);
}

}  // namespace

TEST(ray_pick_bounds_cover_the_vertices) {
    const PickGeometry g = cube();
    CHECK(g.positions.size() == kCube.size());
    CHECK(g.indices.size() == kCubeIndices.size());
    for (int axis = 0; axis < 3; ++axis) {
        CHECK(g.bounds.min[axis] <= -0.5f && g.bounds.min[axis] > -0.501f);
        CHECK(g.bounds.max[axis] >= 0.5f && g.bounds.max[axis] < 0.501f);
    }
}

TEST(ray_pick_aabb_hits_and_misses) {
    Aabb box;
    box.min = glm::vec3(-1.0f);
    box.max = glm::vec3(1.0f);
    CHECK(rayHitsAabb({-5, 0, 0}, {1, 0, 0}, box, kInf));
    CHECK(!rayHitsAabb({-5, 2, 0}, {1, 0, 0}, box, kInf));  // passes above
    CHECK(!rayHitsAabb({-5, 0, 0}, {-1, 0, 0}, box, kInf)); // points away
    CHECK(!rayHitsAabb({-5, 0, 0}, {1, 0, 0}, box, 3.9f));  // stops short
    CHECK(rayHitsAabb({-5, 0, 0}, {1, 0, 0}, box, 4.1f));
    CHECK(rayHitsAabb({0, 0, 0}, {0, 1, 0}, box, 0.01f));   // starts inside
    CHECK(rayHitsAabb({-5, -5, 0}, {1, 1, 0}, box, kInf));  // diagonal, zero z component
    CHECK(!rayHitsAabb({-5, -5, 3}, {1, 1, 0}, box, kInf)); // zero z component, outside the z slab
}

TEST(ray_pick_triangle_both_faces_and_edges) {
    const glm::vec3 a(0, 0, 0), b(1, 0, 0), c(0, 1, 0);  // counter-clockwise seen from +Z
    float t = 0.0f;
    CHECK(rayHitsTriangle({0.25f, 0.25f, 5}, {0, 0, -1}, a, b, c, kInf, &t));
    CHECK_NEAR(t, 5.0, 1e-5);
    // The back face counts too.
    CHECK(rayHitsTriangle({0.25f, 0.25f, -3}, {0, 0, 1}, a, b, c, kInf, &t));
    CHECK_NEAR(t, 3.0, 1e-5);
    // Outside the triangle, though inside its bounding square.
    CHECK(!rayHitsTriangle({0.75f, 0.75f, 5}, {0, 0, -1}, a, b, c, kInf, &t));
    // On an edge and on a corner.
    CHECK(rayHitsTriangle({0.5f, 0.5f, 5}, {0, 0, -1}, a, b, c, kInf, &t));
    CHECK(rayHitsTriangle({0.0f, 0.0f, 5}, {0, 0, -1}, a, b, c, kInf, &t));
    // Behind the origin, beyond maxT, parallel to the plane, degenerate.
    CHECK(!rayHitsTriangle({0.25f, 0.25f, 5}, {0, 0, 1}, a, b, c, kInf, &t));
    CHECK(!rayHitsTriangle({0.25f, 0.25f, 5}, {0, 0, -1}, a, b, c, 4.99f, &t));
    CHECK(!rayHitsTriangle({-1, 0.25f, 0}, {1, 0, 0}, a, b, c, kInf, &t));
    CHECK(!rayHitsTriangle({0.25f, 0.25f, 5}, {0, 0, -1}, a, b, b, kInf, &t));
    // The direction need not be unit length: t scales with it.
    CHECK(rayHitsTriangle({0.25f, 0.25f, 5}, {0, 0, -2}, a, b, c, kInf, &t));
    CHECK_NEAR(t, 2.5, 1e-5);
}

TEST(ray_pick_mesh_hit_from_outside) {
    const PickGeometry g = cube();
    const PickHit hit = raycastMesh(g, glm::mat4(1.0f), {0.1f, 0.2f, 10.0f}, {0, 0, -1}, kInf);
    REQUIRE(hit.hit);
    CHECK_NEAR(hit.distance, 9.5, 1e-4);
    CHECK(near(hit.point, {0.1f, 0.2f, 0.5f}));
    CHECK(near(hit.normal, {0, 0, 1}));
    CHECK(hit.triangle == 10 || hit.triangle == 11);  // the +Z face
}

TEST(ray_pick_mesh_miss) {
    const PickGeometry g = cube();
    CHECK(!raycastMesh(g, glm::mat4(1.0f), {2.0f, 0.0f, 10.0f}, {0, 0, -1}, kInf).hit);   // beside it
    CHECK(!raycastMesh(g, glm::mat4(1.0f), {0.0f, 0.0f, 10.0f}, {0, 0, 1}, kInf).hit);    // away from it
    const PickHit none = raycastMesh(g, glm::mat4(1.0f), {0.0f, 0.0f, 10.0f}, {0, 1, 0}, kInf);
    CHECK(!none.hit);
    CHECK(none.triangle == -1);
}

TEST(ray_pick_mesh_max_distance_cuts_off) {
    const PickGeometry g = cube();
    CHECK(!raycastMesh(g, glm::mat4(1.0f), {0, 0, 10}, {0, 0, -1}, 9.4f).hit);
    CHECK(raycastMesh(g, glm::mat4(1.0f), {0, 0, 10}, {0, 0, -1}, 9.6f).hit);
}

TEST(ray_pick_mesh_ray_from_inside_hits_the_inside) {
    const PickGeometry g = cube();
    const PickHit hit = raycastMesh(g, glm::mat4(1.0f), {0, 0, 0}, {1, 0, 0}, kInf);
    REQUIRE(hit.hit);
    CHECK_NEAR(hit.distance, 0.5, 1e-5);
    CHECK(near(hit.point, {0.5f, 0, 0}));
    // The +X face's stored normal points out (+X); the reported one faces the
    // ray's origin, inside the cube.
    CHECK(near(hit.normal, {-1, 0, 0}));
    CHECK(hit.triangle == 2 || hit.triangle == 3);
}

TEST(ray_pick_mesh_nearest_of_its_triangles) {
    // A ray along -X through the cube crosses +X then -X: +X is nearer.
    const PickGeometry g = cube();
    const PickHit hit = raycastMesh(g, glm::mat4(1.0f), {5, 0.1f, 0.1f}, {-1, 0, 0}, kInf);
    REQUIRE(hit.hit);
    CHECK_NEAR(hit.distance, 4.5, 1e-5);
    CHECK(hit.triangle == 2 || hit.triangle == 3);
    CHECK(near(hit.normal, {1, 0, 0}));
}

TEST(ray_pick_transformed_mesh) {
    // Scale the cube by 4 about its centre, turn it 90 degrees about +Y and
    // put it at (10, 0, 0): its faces sit at x = 8 and x = 12.
    const PickGeometry g = cube();
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(10, 0, 0));
    model = glm::rotate(model, glm::radians(90.0f), glm::vec3(0, 1, 0));
    model = glm::scale(model, glm::vec3(4.0f));
    const PickHit hit = raycastMesh(g, model, {0, 0.5f, 0.5f}, {1, 0, 0}, kInf);
    REQUIRE(hit.hit);
    CHECK_NEAR(hit.distance, 8.0, 1e-4);
    CHECK(near(hit.point, {8, 0.5f, 0.5f}));
    CHECK(near(hit.normal, {-1, 0, 0}));
    // The rotation turned the model's -Z face to world -X.
    CHECK(hit.triangle == 8 || hit.triangle == 9);
    // Untransformed, a ray from inside meets the +X face half a unit away.
    CHECK_NEAR(raycastMesh(g, glm::mat4(1.0f), {0, 0.25f, 0.25f}, {1, 0, 0}, kInf).distance, 0.5, 1e-5);
    // A non-uniform scale still gives a unit normal at right angles to the face.
    const glm::mat4 squashed = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 0.1f, 3.0f));
    const PickHit top = raycastMesh(g, squashed, {0.2f, 5, 0.2f}, {0, -1, 0}, kInf);
    REQUIRE(top.hit);
    CHECK_NEAR(top.distance, 4.95, 1e-4);
    CHECK(near(top.normal, {0, 1, 0}));
}

TEST(ray_pick_nearest_of_several_meshes) {
    // What the renderer does: keep the nearest hit over meshes in order.
    const PickGeometry g = cube();
    const glm::mat4 distant = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -10));
    const glm::mat4 nearer = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -4));
    const glm::mat4 aside = glm::translate(glm::mat4(1.0f), glm::vec3(5, 0, -2));
    const std::vector<glm::mat4> models = {distant, aside, nearer};
    int best = -1;
    PickHit bestHit;
    for (size_t i = 0; i < models.size(); ++i) {
        const PickHit hit = raycastMesh(g, models[i], {0, 0, 0}, {0, 0, -1}, kInf);
        if (hit.hit && (best < 0 || hit.distance < bestHit.distance)) {
            best = static_cast<int>(i);
            bestHit = hit;
        }
    }
    CHECK(best == 2);
    CHECK_NEAR(bestHit.distance, 3.5, 1e-5);
}
