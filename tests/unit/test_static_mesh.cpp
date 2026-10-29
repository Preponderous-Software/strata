#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

#include "render/static_mesh.hpp"
#include "strata/strata.hpp"
#include "test.hpp"

using namespace strata::render;

namespace {

// A unit quad in the XZ plane at y = 2, two triangles counter-clockwise seen
// from above (+Y).
const std::vector<float> kQuad = {0, 2, 0, 0, 2, 1, 1, 2, 0, 1, 2, 1};
const std::vector<uint32_t> kQuadIndices = {0, 1, 2, 2, 1, 3};

StrataMesh mesh(const std::vector<float>& positions, const std::vector<uint32_t>& indices) {
    StrataMesh m{};
    m.positions = positions.data();
    m.indices = indices.data();
    m.vertex_count = static_cast<int32_t>(positions.size() / 3);
    m.index_count = static_cast<int32_t>(indices.size());
    m.color[0] = 0.6f;
    m.color[1] = 0.3f;
    m.color[2] = 0.2f;
    m.color[3] = 1.0f;
    return m;
}

glm::vec3 normal(const StaticMesh& m, int v) {
    const float* p = &m.vertices[static_cast<size_t>(v) * StaticMesh::kFloatsPerVertex + 3];
    return {p[0], p[1], p[2]};
}

bool near(glm::vec3 a, glm::vec3 b) { return glm::length(a - b) < 1e-5f; }

bool rejects(const StrataMesh& m, const char* text) {
    try {
        validateStaticMesh(m);
    } catch (const strata::Error& e) {
        return e.status() == STRATA_ERROR_INVALID_ARGUMENT && std::strstr(e.what(), text) != nullptr;
    }
    return false;
}

}  // namespace

TEST(static_mesh_copies_positions_indices_and_the_mesh_colour) {
    const StaticMesh m = buildStaticMesh(mesh(kQuad, kQuadIndices));
    CHECK(m.vertexCount() == 4);
    CHECK(m.indexCount() == 6);
    CHECK(m.indices == kQuadIndices);
    for (int v = 0; v < 4; ++v) {
        const float* p = &m.vertices[static_cast<size_t>(v) * StaticMesh::kFloatsPerVertex];
        CHECK(p[0] == kQuad[static_cast<size_t>(v) * 3] && p[1] == 2.0f);
        CHECK(p[6] == 0.6f && p[7] == 0.3f && p[8] == 0.2f);
    }
}

TEST(static_mesh_computes_normals_from_counter_clockwise_winding) {
    const StaticMesh up = buildStaticMesh(mesh(kQuad, kQuadIndices));
    for (int v = 0; v < 4; ++v) CHECK(near(normal(up, v), {0, 1, 0}));
    // Reversed winding faces down.
    const std::vector<uint32_t> reversed = {0, 2, 1, 2, 3, 1};
    const StaticMesh down = buildStaticMesh(mesh(kQuad, reversed));
    for (int v = 0; v < 4; ++v) CHECK(near(normal(down, v), {0, -1, 0}));
}

TEST(static_mesh_weights_shared_normals_by_area) {
    // Vertex 0 is shared by a big triangle facing +Y and a small one facing +X.
    const std::vector<float> p = {0, 0, 0, 0, 0, 4, 4, 0, 0, 0, 1, 0, 0, 0, -1};
    const std::vector<uint32_t> i = {0, 1, 2, 0, 4, 3};
    const StaticMesh m = buildStaticMesh(mesh(p, i));
    const glm::vec3 n = normal(m, 0);
    CHECK(near(n, glm::normalize(glm::vec3(1.0f, 16.0f, 0.0f))));  // areas 8 and 0.5
    CHECK(near(normal(m, 1), {0, 1, 0}));
    CHECK(near(normal(m, 3), {1, 0, 0}));
}

TEST(static_mesh_uses_host_normals_and_colours) {
    std::vector<float> normals(12, 0.0f), colors(12, 0.0f);
    for (int v = 0; v < 4; ++v) {
        normals[static_cast<size_t>(v) * 3 + 2] = 3.0f;  // normalised by the engine
        colors[static_cast<size_t>(v) * 3] = 0.1f * static_cast<float>(v);
    }
    StrataMesh in = mesh(kQuad, kQuadIndices);
    in.normals = normals.data();
    in.colors = colors.data();
    const StaticMesh m = buildStaticMesh(in);
    for (int v = 0; v < 4; ++v) {
        CHECK(near(normal(m, v), {0, 0, 1}));
        CHECK(m.vertices[static_cast<size_t>(v) * StaticMesh::kFloatsPerVertex + 6] == 0.1f * static_cast<float>(v));
    }
}

TEST(static_mesh_reads_rgba_vertex_colours_with_four_components) {
    // ABI 1.15 (STRATA_MESH_COLORS_RGBA): RGBA quadruples give each vertex the
    // same colour as the RGB triples; the alpha is skipped.
    std::vector<float> rgb(12), rgba(16);
    for (int v = 0; v < 4; ++v) {
        for (int c = 0; c < 3; ++c) {
            const float x = 0.1f * static_cast<float>(v * 3 + c);
            rgb[static_cast<size_t>(v * 3 + c)] = x;
            rgba[static_cast<size_t>(v * 4 + c)] = x;
        }
        rgba[static_cast<size_t>(v * 4 + 3)] = 0.5f;
    }
    StrataMesh a = mesh(kQuad, kQuadIndices), b = mesh(kQuad, kQuadIndices);
    a.colors = rgb.data();
    b.colors = rgba.data();
    const StaticMesh fromRgb = buildStaticMesh(a);
    const StaticMesh fromRgba = buildStaticMesh(b, nullptr, 4);
    CHECK(fromRgb.vertices == fromRgba.vertices);
    CHECK(fromRgba.vertices[StaticMesh::kFloatsPerVertex + 6] == rgb[3]);  // vertex 1 starts at rgba[4], not rgba[3]

    // The alpha is validated like the other components, naming the vertex.
    rgba[2 * 4 + 3] = -1.0f;
    try {
        validateStaticMesh(b, 4);
        CHECK(false);
    } catch (const strata::Error& e) {
        CHECK(std::strstr(e.what(), "colour of vertex 2") != nullptr);
    }
    try {
        validateStaticMesh(a, 5);
        CHECK(false);
    } catch (const strata::Error& e) {
        CHECK(std::strstr(e.what(), "3 or 4") != nullptr);
    }
}

TEST(static_mesh_rejects_bad_meshes_naming_the_field) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    StrataMesh m = mesh(kQuad, kQuadIndices);
    CHECK(!rejects(m, ""));

    StrataMesh bad = m;
    bad.positions = nullptr;
    CHECK(rejects(bad, "positions is NULL"));
    bad = m;
    bad.indices = nullptr;
    CHECK(rejects(bad, "indices is NULL"));
    bad = m;
    bad.vertex_count = 2;
    CHECK(rejects(bad, "vertex_count"));
    bad = m;
    bad.vertex_count = STRATA_MAX_MESH_VERTICES + 1;
    CHECK(rejects(bad, "vertex_count"));
    bad = m;
    bad.index_count = 5;
    CHECK(rejects(bad, "index_count"));
    bad = m;
    bad.index_count = 0;
    CHECK(rejects(bad, "index_count"));

    const std::vector<uint32_t> outOfRange = {0, 1, 4};
    CHECK(rejects(mesh(kQuad, outOfRange), "index 2 is 4"));

    std::vector<float> positions = kQuad;
    positions[4] = nan;
    CHECK(rejects(mesh(positions, kQuadIndices), "position of vertex 1"));

    std::vector<float> normals(12, 0.0f);
    bad = m;
    bad.normals = normals.data();
    CHECK(rejects(bad, "zero length"));
    normals.assign(12, 1.0f);
    normals[9] = std::numeric_limits<float>::infinity();
    CHECK(rejects(bad, "normal of vertex 3 is not finite"));

    std::vector<float> colors(12, 0.5f);
    colors[3] = -0.1f;
    bad = m;
    bad.colors = colors.data();
    CHECK(rejects(bad, "colour of vertex 1"));
    bad = m;
    bad.color[1] = nan;
    CHECK(rejects(bad, "color must be finite"));
}

// ABI 1.7: texture coordinates ride along in the last two floats.
TEST(static_mesh_carries_uvs) {
    const std::vector<float> uvs = {0, 0, 0, 1, 1, 0, 1, 1};
    const StaticMesh m = buildStaticMesh(mesh(kQuad, kQuadIndices), uvs.data());
    CHECK(StaticMesh::kFloatsPerVertex == 11);
    for (int v = 0; v < 4; ++v) {
        const float* p = &m.vertices[static_cast<size_t>(v) * StaticMesh::kFloatsPerVertex + 9];
        CHECK(p[0] == uvs[static_cast<size_t>(v) * 2]);
        CHECK(p[1] == uvs[static_cast<size_t>(v) * 2 + 1]);
    }
    const StaticMesh none = buildStaticMesh(mesh(kQuad, kQuadIndices));
    CHECK(none.vertices[9] == 0.0f && none.vertices[10] == 0.0f);
}

TEST(static_mesh_rejects_non_finite_uvs) {
    std::vector<float> uvs = {0, 0, 0, 1, 1, 0, 1, 1};
    uvs[5] = std::numeric_limits<float>::quiet_NaN();
    bool threw = false;
    try {
        buildStaticMesh(mesh(kQuad, kQuadIndices), uvs.data());
    } catch (const strata::Error& e) {
        threw = e.status() == STRATA_ERROR_INVALID_ARGUMENT && std::strstr(e.what(), "uv of vertex 2") != nullptr;
    }
    CHECK(threw);
}
