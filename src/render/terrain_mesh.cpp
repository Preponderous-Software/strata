#include "render/terrain_mesh.hpp"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "core/error.hpp"

namespace strata::render {

void validateTerrainTile(const StrataTerrainTile& t) {
    for (float v : t.origin) {
        if (!std::isfinite(v)) fail(STRATA_ERROR_INVALID_ARGUMENT, "terrain: origin is not finite");
    }
    if (!(t.cell_size > 0.0f) || !std::isfinite(t.cell_size)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "terrain: cell_size must be positive and finite (got %g)",
             static_cast<double>(t.cell_size));
    }
    if (t.samples_x < kTerrainMinSamples || t.samples_x > kTerrainMaxSamples || t.samples_z < kTerrainMinSamples ||
        t.samples_z > kTerrainMaxSamples) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "terrain: samples must be %d..%d on each side (got %d x %d)",
             kTerrainMinSamples, kTerrainMaxSamples, t.samples_x, t.samples_z);
    }
    if (!t.heights) fail(STRATA_ERROR_INVALID_ARGUMENT, "terrain: heights is NULL");
    const size_t count = static_cast<size_t>(t.samples_x) * static_cast<size_t>(t.samples_z);
    for (size_t i = 0; i < count; ++i) {
        if (!std::isfinite(t.heights[i])) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "terrain: height %lu is not finite", static_cast<unsigned long>(i));
        }
    }
    if (t.colors) {
        for (size_t i = 0; i < count * 3; ++i) {
            if (!(t.colors[i] >= 0.0f) || !std::isfinite(t.colors[i])) {
                fail(STRATA_ERROR_INVALID_ARGUMENT, "terrain: colour of sample %lu must be finite and >= 0",
                     static_cast<unsigned long>(i / 3));
            }
        }
    } else {
        for (float v : t.color) {
            if (!(v >= 0.0f) || !std::isfinite(v)) {
                fail(STRATA_ERROR_INVALID_ARGUMENT, "terrain: color must be finite and >= 0");
            }
        }
    }
}

TerrainMesh buildTerrainMesh(const StrataTerrainTile& t) {
    validateTerrainTile(t);
    const int sx = t.samples_x, sz = t.samples_z;
    const float c = t.cell_size;
    auto height = [&](int x, int z) {
        x = std::clamp(x, 0, sx - 1);
        z = std::clamp(z, 0, sz - 1);
        return t.heights[static_cast<size_t>(z) * static_cast<size_t>(sx) + static_cast<size_t>(x)];
    };
    TerrainMesh mesh;
    mesh.vertices.reserve(static_cast<size_t>(sx) * static_cast<size_t>(sz) * TerrainMesh::kFloatsPerVertex);
    for (int z = 0; z < sz; ++z) {
        for (int x = 0; x < sx; ++x) {
            // Central differences; at an edge the step is one-sided, so halve the span.
            const float dx = static_cast<float>(std::min(x + 1, sx - 1) - std::max(x - 1, 0)) * c;
            const float dz = static_cast<float>(std::min(z + 1, sz - 1) - std::max(z - 1, 0)) * c;
            const float slopeX = (height(x + 1, z) - height(x - 1, z)) / dx;
            const float slopeZ = (height(x, z + 1) - height(x, z - 1)) / dz;
            const glm::vec3 n = glm::normalize(glm::vec3(-slopeX, 1.0f, -slopeZ));
            const size_t i = static_cast<size_t>(z) * static_cast<size_t>(sx) + static_cast<size_t>(x);
            const float* rgb = t.colors ? t.colors + i * 3 : t.color;
            const float v[TerrainMesh::kFloatsPerVertex] = {t.origin[0] + static_cast<float>(x) * c,
                                                            t.origin[1] + height(x, z),
                                                            t.origin[2] + static_cast<float>(z) * c,
                                                            n.x, n.y, n.z, rgb[0], rgb[1], rgb[2]};
            mesh.vertices.insert(mesh.vertices.end(), v, v + TerrainMesh::kFloatsPerVertex);
        }
    }
    mesh.indices.reserve(static_cast<size_t>(sx - 1) * static_cast<size_t>(sz - 1) * 6);
    for (int z = 0; z + 1 < sz; ++z) {
        for (int x = 0; x + 1 < sx; ++x) {
            const auto v00 = static_cast<uint32_t>(z * sx + x);
            const uint32_t v10 = v00 + 1, v01 = v00 + static_cast<uint32_t>(sx), v11 = v01 + 1;
            // Counter-clockwise seen from +Y.
            const uint32_t quad[6] = {v00, v01, v10, v10, v01, v11};
            mesh.indices.insert(mesh.indices.end(), quad, quad + 6);
        }
    }
    return mesh;
}

}  // namespace strata::render
