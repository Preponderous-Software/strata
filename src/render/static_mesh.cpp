#include "render/static_mesh.hpp"

#include <cmath>

#include <glm/glm.hpp>

#include "core/error.hpp"

namespace strata::render {

void validateStaticMesh(const StrataMesh& m, int colorComponents) {
    if (colorComponents != 3 && colorComponents != 4) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: colour components must be 3 or 4 (got %d)", colorComponents);
    }
    if (!m.positions) fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: positions is NULL");
    if (!m.indices) fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: indices is NULL");
    if (m.vertex_count < kMeshMinVertices || m.vertex_count > kMeshMaxVertices) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: vertex_count must be %d..%d (got %d)", kMeshMinVertices,
             kMeshMaxVertices, m.vertex_count);
    }
    if (m.index_count <= 0 || m.index_count % 3 != 0) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: index_count must be a positive multiple of 3 (got %d)",
             m.index_count);
    }
    const size_t vertices = static_cast<size_t>(m.vertex_count);
    for (size_t i = 0; i < vertices * 3; ++i) {
        if (!std::isfinite(m.positions[i])) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: position of vertex %lu is not finite",
                 static_cast<unsigned long>(i / 3));
        }
    }
    if (m.normals) {
        for (size_t v = 0; v < vertices; ++v) {
            const float* n = m.normals + v * 3;
            if (!std::isfinite(n[0]) || !std::isfinite(n[1]) || !std::isfinite(n[2])) {
                fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: normal of vertex %lu is not finite",
                     static_cast<unsigned long>(v));
            }
            if (n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < 1e-12f) {
                fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: normal of vertex %lu has zero length",
                     static_cast<unsigned long>(v));
            }
        }
    }
    if (m.colors) {
        const size_t stride = static_cast<size_t>(colorComponents);
        for (size_t i = 0; i < vertices * stride; ++i) {
            if (!(m.colors[i] >= 0.0f) || !std::isfinite(m.colors[i])) {
                fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: colour of vertex %lu must be finite and >= 0",
                     static_cast<unsigned long>(i / stride));
            }
        }
    } else {
        for (float v : m.color) {
            if (!(v >= 0.0f) || !std::isfinite(v)) fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: color must be finite and >= 0");
        }
    }
    for (int32_t i = 0; i < m.index_count; ++i) {
        if (m.indices[i] >= static_cast<uint32_t>(m.vertex_count)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: index %d is %u, past vertex_count %d", i, m.indices[i],
                 m.vertex_count);
        }
    }
}

StaticMesh buildStaticMesh(const StrataMesh& m, const float* uvs, int colorComponents) {
    validateStaticMesh(m, colorComponents);
    const size_t count = static_cast<size_t>(m.vertex_count);
    if (uvs) {
        for (size_t i = 0; i < count * 2; ++i) {
            if (!std::isfinite(uvs[i])) {
                fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: uv of vertex %lu is not finite",
                     static_cast<unsigned long>(i / 2));
            }
        }
    }
    auto position = [&](uint32_t v) { return glm::vec3(m.positions[v * 3], m.positions[v * 3 + 1], m.positions[v * 3 + 2]); };

    std::vector<glm::vec3> normals(count, glm::vec3(0.0f));
    if (m.normals) {
        for (size_t v = 0; v < count; ++v) {
            normals[v] = glm::normalize(glm::vec3(m.normals[v * 3], m.normals[v * 3 + 1], m.normals[v * 3 + 2]));
        }
    } else {
        // The cross product's length is twice the triangle's area, so summing
        // unnormalised face normals weights each face by its area.
        for (int32_t i = 0; i < m.index_count; i += 3) {
            const uint32_t a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
            const glm::vec3 n = glm::cross(position(b) - position(a), position(c) - position(a));
            normals[a] += n;
            normals[b] += n;
            normals[c] += n;
        }
        for (auto& n : normals) {
            const float length = glm::length(n);
            n = length > 0.0f ? n / length : glm::vec3(0.0f, 1.0f, 0.0f);  // unused or degenerate: up
        }
    }

    StaticMesh mesh;
    mesh.vertices.reserve(count * StaticMesh::kFloatsPerVertex);
    for (size_t v = 0; v < count; ++v) {
        const float* rgb = m.colors ? m.colors + v * static_cast<size_t>(colorComponents) : m.color;
        const float out[StaticMesh::kFloatsPerVertex] = {m.positions[v * 3], m.positions[v * 3 + 1], m.positions[v * 3 + 2],
                                                         normals[v].x,       normals[v].y,           normals[v].z,
                                                         rgb[0],             rgb[1],                 rgb[2],
                                                         uvs ? uvs[v * 2] : 0.0f, uvs ? uvs[v * 2 + 1] : 0.0f};
        mesh.vertices.insert(mesh.vertices.end(), out, out + StaticMesh::kFloatsPerVertex);
    }
    mesh.indices.assign(m.indices, m.indices + m.index_count);
    return mesh;
}

}  // namespace strata::render
