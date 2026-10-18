// T1: one static heightmapped terrain tile, turned into a mesh at upload.
//
// Samples are a samples_x by samples_z grid, x fastest, spaced cell_size apart
// from the origin along +X and +Z; each height is added to origin.y. Normals
// come from central differences (one-sided at the edges). Triangles wind
// counter-clockwise seen from above, two per cell.
// Pure arithmetic: no GL, so it is unit-tested directly.
#pragma once

#include <cstdint>
#include <vector>

#include "strata/strata.h"

namespace strata::render {

constexpr int kTerrainMinSamples = 2;
constexpr int kTerrainMaxSamples = 1025;

struct TerrainMesh {
    static constexpr int kFloatsPerVertex = 9;  // position xyz, normal xyz, colour rgb
    std::vector<float> vertices;
    std::vector<uint32_t> indices;

    int vertexCount() const { return static_cast<int>(vertices.size() / kFloatsPerVertex); }
    int indexCount() const { return static_cast<int>(indices.size()); }
};

// Throws strata::Error(STRATA_ERROR_INVALID_ARGUMENT) naming the bad field:
// non-finite origin or colour, cell_size <= 0, sample counts outside
// 2..1025, a NULL heights array, a non-finite height, or a negative colour.
void validateTerrainTile(const StrataTerrainTile& tile);

// Validates, then builds the mesh.
TerrainMesh buildTerrainMesh(const StrataTerrainTile& tile);

}  // namespace strata::render
