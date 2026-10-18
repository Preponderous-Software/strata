// The primitives must be BN's, tessellation and winding included, or image
// parity with BN's renderer is impossible.
#include <glm/glm.hpp>

#include "render/primitive_mesh.hpp"
#include "strata/strata.h"
#include "test.hpp"

using strata::render::PrimitiveMesh;

namespace {

glm::vec3 vertex(const PrimitiveMesh& m, uint32_t i) {
    const float* v = &m.vertices[i * PrimitiveMesh::kFloatsPerVertex];
    return {v[0], v[1], v[2]};
}

glm::vec3 normal(const PrimitiveMesh& m, uint32_t i) {
    const float* v = &m.vertices[i * PrimitiveMesh::kFloatsPerVertex + 3];
    return {v[0], v[1], v[2]};
}

// Every non-degenerate triangle of a convex solid centred on the origin must
// wind counter-clockwise seen from outside: its face normal points away from
// the centre.
int inwardTriangles(const PrimitiveMesh& m) {
    int inward = 0;
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const glm::vec3 a = vertex(m, m.indices[t]), b = vertex(m, m.indices[t + 1]), c = vertex(m, m.indices[t + 2]);
        const glm::vec3 n = glm::cross(b - a, c - a);
        if (glm::length(n) < 1e-7f) continue;  // the poles' degenerate rows
        if (glm::dot(n, (a + b + c) / 3.0f) <= 0.0f) ++inward;
    }
    return inward;
}

void checkBounds(const PrimitiveMesh& m, glm::vec3 expectedMin, glm::vec3 expectedMax) {
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (int i = 0; i < m.vertexCount(); ++i) {
        lo = glm::min(lo, vertex(m, static_cast<uint32_t>(i)));
        hi = glm::max(hi, vertex(m, static_cast<uint32_t>(i)));
    }
    for (int k = 0; k < 3; ++k) {
        CHECK_NEAR(lo[k], expectedMin[k], 1e-5);
        CHECK_NEAR(hi[k], expectedMax[k], 1e-5);
    }
}

void checkUnitNormalsAndIndices(const PrimitiveMesh& m) {
    for (int i = 0; i < m.vertexCount(); ++i) CHECK_NEAR(glm::length(normal(m, static_cast<uint32_t>(i))), 1.0, 1e-5);
    for (uint32_t index : m.indices) CHECK(index < static_cast<uint32_t>(m.vertexCount()));
}

}  // namespace

TEST(cube_matches_bn) {
    const PrimitiveMesh m = strata::render::createPrimitive(STRATA_PRIMITIVE_CUBE);
    CHECK(m.vertexCount() == 24);
    CHECK(m.indexCount() == 36);
    CHECK(inwardTriangles(m) == 0);
    checkBounds(m, glm::vec3(-0.5f), glm::vec3(0.5f));
    checkUnitNormalsAndIndices(m);
}

TEST(sphere_matches_bn) {
    const PrimitiveMesh m = strata::render::createPrimitive(STRATA_PRIMITIVE_SPHERE);
    CHECK(m.vertexCount() == 17 * 24);
    CHECK(m.indexCount() == 16 * 24 * 6);
    CHECK(inwardTriangles(m) == 0);
    checkBounds(m, glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(0.5f, 0.5f, 0.5f));
    checkUnitNormalsAndIndices(m);
}

TEST(cylinder_matches_bn) {
    const PrimitiveMesh m = strata::render::createPrimitive(STRATA_PRIMITIVE_CYLINDER);
    CHECK(m.vertexCount() == 24 * 4 + 2);
    CHECK(m.indexCount() == 24 * 12);
    CHECK(inwardTriangles(m) == 0);
    checkBounds(m, glm::vec3(-0.5f, -1.0f, -0.5f), glm::vec3(0.5f, 1.0f, 0.5f));
    checkUnitNormalsAndIndices(m);
}

TEST(capsule_matches_bn) {
    const PrimitiveMesh m = strata::render::createPrimitive(STRATA_PRIMITIVE_CAPSULE);
    CHECK(m.vertexCount() == 18 * 24);
    CHECK(m.indexCount() == 17 * 24 * 6);
    CHECK(inwardTriangles(m) == 0);
    checkBounds(m, glm::vec3(-0.5f, -1.0f, -0.5f), glm::vec3(0.5f, 1.0f, 0.5f));
    checkUnitNormalsAndIndices(m);
}

TEST(odd_capsule_stacks_round_up_like_bn) {
    CHECK(strata::render::createCapsule(15, 8).vertexCount() == strata::render::createCapsule(16, 8).vertexCount());
}
