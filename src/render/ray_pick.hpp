// Ray picking against static meshes (ABI 1.8): the maths behind
// strata_raycast_meshes, kept free of GL so it is unit-tested directly.
//
// Each mesh keeps a CPU copy of its model-space positions and indices and a
// model-space bounding box. A world-space ray is moved into the mesh's model
// space with the inverse of its model matrix, tested against the box, then
// against every triangle with Moller-Trumbore. Because the model matrix is
// affine, the ray parameter is the same in both spaces: with a unit world
// direction, the model-space t of a hit is its world distance, so
// max_distance needs no conversion.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "strata/strata.h"

namespace strata::render {

struct Aabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

// What the renderer keeps per mesh for picking: model-space xyz per vertex,
// the triangle indices and their bounding box (padded slightly, so a hit on
// the box's face is not lost to rounding in the slab test).
struct PickGeometry {
    std::vector<float> positions;
    std::vector<uint32_t> indices;
    Aabb bounds;
};

// Copies a mesh's positions and indices. The mesh must already be valid
// (validateStaticMesh).
PickGeometry buildPickGeometry(const StrataMesh& mesh);

// The bounding box of `vertexCount` xyz positions, padded as above.
Aabb computeBounds(const float* positions, size_t vertexCount);

// Slab test: true when origin + t * dir meets the box for some t in
// [0, maxT]. dir need not be normalised; a zero component is allowed.
bool rayHitsAabb(const glm::vec3& origin, const glm::vec3& dir, const Aabb& box, float maxT);

// Moller-Trumbore, both faces. True with the ray parameter in *t when the
// ray meets triangle (a, b, c) at some t in [0, maxT]. Edges and corners
// count; a degenerate triangle never does.
bool rayHitsTriangle(const glm::vec3& origin, const glm::vec3& dir, const glm::vec3& a, const glm::vec3& b,
                     const glm::vec3& c, float maxT, float* t);

struct PickHit {
    bool hit = false;
    int32_t triangle = -1;      // index in the index list / 3
    float distance = 0.0f;      // world distance along the unit direction
    glm::vec3 point{0.0f};      // world space
    glm::vec3 normal{0.0f};     // unit, world space, facing the ray's origin
};

// The nearest triangle of one mesh along a world-space ray. worldDir must be
// unit length; maxDistance > 0 (may be infinite). model is the mesh's model
// matrix; its bottom row is taken as (0, 0, 0, 1) and its upper 3 x 3 must be
// invertible. Equally near triangles go to the first in the index list.
PickHit raycastMesh(const PickGeometry& mesh, const glm::mat4& model, const glm::vec3& worldOrigin,
                    const glm::vec3& worldDir, float maxDistance);

}  // namespace strata::render
