// Static triangle meshes (ABI 1.4), already in world space, turned into the
// terrain's vertex format (position, normal, colour) at upload.
//
// When the host gives no normals they are computed per vertex as the
// area-weighted sum of the normals of the triangles that use it, each normal
// following the counter-clockwise winding. Pure arithmetic: no GL, so it is
// unit-tested directly.
#pragma once

#include <cstdint>
#include <vector>

#include "strata/strata.h"

namespace strata::render {

constexpr int kMeshMinVertices = 3;
constexpr int kMeshMaxVertices = STRATA_MAX_MESH_VERTICES;

struct StaticMesh {
    static constexpr int kFloatsPerVertex = 9;  // position xyz, normal xyz, colour rgb (as TerrainMesh)
    std::vector<float> vertices;
    std::vector<uint32_t> indices;

    int vertexCount() const { return static_cast<int>(vertices.size() / kFloatsPerVertex); }
    int indexCount() const { return static_cast<int>(indices.size()); }
};

// Throws strata::Error(STRATA_ERROR_INVALID_ARGUMENT) naming the bad field:
// a NULL positions or indices array, vertex_count outside 3..16777216,
// index_count not a positive multiple of 3, an index out of range, a
// non-finite position or normal, a zero-length normal, or a non-finite or
// negative colour.
void validateStaticMesh(const StrataMesh& mesh);

// Validates, then builds the mesh.
StaticMesh buildStaticMesh(const StrataMesh& mesh);

}  // namespace strata::render
