#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

#include "render/terrain_mesh.hpp"
#include "strata/strata.hpp"
#include "test.hpp"

using namespace strata::render;

namespace {

StrataTerrainTile tile(const std::vector<float>& heights, int sx, int sz, float cell = 2.0f) {
    StrataTerrainTile t{};
    t.origin[0] = 10.0f;
    t.origin[1] = 1.0f;
    t.origin[2] = -4.0f;
    t.cell_size = cell;
    t.samples_x = sx;
    t.samples_z = sz;
    t.heights = heights.data();
    t.color[0] = 0.2f;
    t.color[1] = 0.5f;
    t.color[2] = 0.1f;
    t.color[3] = 1.0f;
    return t;
}

glm::vec3 position(const TerrainMesh& m, uint32_t v) {
    const float* p = &m.vertices[v * TerrainMesh::kFloatsPerVertex];
    return {p[0], p[1], p[2]};
}

glm::vec3 normal(const TerrainMesh& m, uint32_t v) {
    const float* p = &m.vertices[v * TerrainMesh::kFloatsPerVertex + 3];
    return {p[0], p[1], p[2]};
}

bool rejects(const StrataTerrainTile& t, const char* text) {
    try {
        validateTerrainTile(t);
    } catch (const strata::Error& e) {
        return e.status() == STRATA_ERROR_INVALID_ARGUMENT && std::strstr(e.what(), text) != nullptr;
    }
    return false;
}

}  // namespace

TEST(terrain_mesh_has_a_vertex_per_sample_and_two_triangles_per_cell) {
    const std::vector<float> h(5 * 3, 0.0f);
    const TerrainMesh m = buildTerrainMesh(tile(h, 5, 3));
    CHECK(m.vertexCount() == 15);
    CHECK(m.indexCount() == 4 * 2 * 6);
    // Sample (x=4, z=2) sits at origin + (4, 0, 2) * cell, raised by its height.
    const glm::vec3 p = position(m, 2 * 5 + 4);
    CHECK_NEAR(p.x, 18.0, 1e-6);
    CHECK_NEAR(p.y, 1.0, 1e-6);
    CHECK_NEAR(p.z, 0.0, 1e-6);
}

TEST(flat_terrain_faces_up_and_winds_counter_clockwise_from_above) {
    const std::vector<float> h(4 * 4, 3.0f);
    const TerrainMesh m = buildTerrainMesh(tile(h, 4, 4));
    for (int v = 0; v < m.vertexCount(); ++v) {
        const glm::vec3 n = normal(m, static_cast<uint32_t>(v));
        CHECK_NEAR(n.y, 1.0, 1e-6);
    }
    for (size_t i = 0; i < m.indices.size(); i += 3) {
        const glm::vec3 a = position(m, m.indices[i]), b = position(m, m.indices[i + 1]), c = position(m, m.indices[i + 2]);
        CHECK(glm::cross(b - a, c - a).y > 0.0f);
    }
}

TEST(terrain_normals_tilt_away_from_the_slope) {
    // Height rises 1 per cell of 2 along +X: slope 0.5, normal (-0.5, 1, 0) normalised.
    std::vector<float> h;
    for (int z = 0; z < 3; ++z)
        for (int x = 0; x < 4; ++x) h.push_back(static_cast<float>(x));
    const TerrainMesh m = buildTerrainMesh(tile(h, 4, 3));
    const double len = std::sqrt(1.25);
    for (uint32_t v : {0u, 1u, 3u, 5u, 11u}) {  // interior and edges agree on a constant slope
        const glm::vec3 n = normal(m, v);
        CHECK_NEAR(n.x, -0.5 / len, 1e-5);
        CHECK_NEAR(n.y, 1.0 / len, 1e-5);
        CHECK_NEAR(n.z, 0.0, 1e-6);
    }
}

TEST(terrain_uses_per_sample_colours_when_given) {
    const std::vector<float> h(2 * 2, 0.0f);
    std::vector<float> colors = {1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1};
    StrataTerrainTile t = tile(h, 2, 2);
    TerrainMesh m = buildTerrainMesh(t);
    CHECK_NEAR(m.vertices[6], 0.2, 1e-6);  // the tile colour
    t.colors = colors.data();
    m = buildTerrainMesh(t);
    CHECK_NEAR(m.vertices[2 * TerrainMesh::kFloatsPerVertex + 8], 1.0, 1e-6);  // sample 2 is blue
    CHECK_NEAR(m.vertices[2 * TerrainMesh::kFloatsPerVertex + 6], 0.0, 1e-6);
}

TEST(terrain_validation_names_the_bad_field) {
    std::vector<float> h(3 * 3, 0.0f);
    StrataTerrainTile t = tile(h, 3, 3);
    validateTerrainTile(t);  // must not throw
    t.cell_size = 0.0f;
    CHECK(rejects(t, "cell_size"));
    t = tile(h, 1, 3);
    CHECK(rejects(t, "samples"));
    t = tile(h, 3, 1026);
    CHECK(rejects(t, "samples"));
    t = tile(h, 3, 3);
    t.heights = nullptr;
    CHECK(rejects(t, "heights is NULL"));
    h[4] = std::numeric_limits<float>::quiet_NaN();
    t = tile(h, 3, 3);
    CHECK(rejects(t, "height 4"));
    h[4] = 0.0f;
    t = tile(h, 3, 3);
    t.origin[1] = std::numeric_limits<float>::infinity();
    CHECK(rejects(t, "origin"));
    t = tile(h, 3, 3);
    t.color[0] = -1.0f;
    CHECK(rejects(t, "color"));
}
