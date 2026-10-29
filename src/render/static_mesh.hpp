// Static triangle meshes (ABI 1.4), turned into one vertex format (position,
// normal, colour, texture coordinate) at upload. ABI 1.7 added the texture
// coordinate and a model matrix, which the renderer applies when drawing.
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
    static constexpr int kFloatsPerVertex = 11;  // position xyz, normal xyz, colour rgb, uv
    std::vector<float> vertices;
    std::vector<uint32_t> indices;

    int vertexCount() const { return static_cast<int>(vertices.size() / kFloatsPerVertex); }
    int indexCount() const { return static_cast<int>(indices.size()); }
};

// Throws strata::Error(STRATA_ERROR_INVALID_ARGUMENT) naming the bad field:
// a NULL positions or indices array, vertex_count outside 3..16777216,
// index_count not a positive multiple of 3, an index out of range, a
// non-finite position or normal, a zero-length normal, or a non-finite or
// negative colour. colorComponents is 3 (mesh.colors is RGB triples) or 4
// (RGBA quadruples, STRATA_MESH_COLORS_RGBA, ABI 1.15; the alpha is ignored).
void validateStaticMesh(const StrataMesh& mesh, int colorComponents = 3);

// Validates, then builds the mesh. uvs is NULL (every vertex at 0, 0) or
// vertex_count finite (u, v) pairs.
StaticMesh buildStaticMesh(const StrataMesh& mesh, const float* uvs = nullptr, int colorComponents = 3);

}  // namespace strata::render
