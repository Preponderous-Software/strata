// Unit tests for the glTF module: loading the synthetic assets
// (make_fixtures.py) and fitting models into Strata's limits. CPU only.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "atlas.hpp"
#include "strata/gltf/model.hpp"
#include "strata/gltf/plan.hpp"
#include "test.hpp"

using namespace strata::gltf;

namespace {

std::string asset(const char* name) { return std::string(STRATA_GLTF_ASSETS) + "/" + name; }

const Primitive* findPrimitive(const Model& m, const std::string& node, size_t nth = 0) {
    for (const Primitive& p : m.primitives) {
        if (p.node == node && nth-- == 0) return &p;
    }
    return nullptr;
}

void centroid(const Primitive& p, float out[3]) {
    out[0] = out[1] = out[2] = 0;
    for (size_t v = 0; v < p.positions.size(); v += 3) {
        for (size_t k = 0; k < 3; ++k) out[k] += p.positions[v + k];
    }
    for (size_t k = 0; k < 3; ++k) out[k] /= static_cast<float>(p.vertexCount());
}

// Unnormalised normal of triangle t from positions and winding.
void faceNormal(const std::vector<float>& pos, const std::vector<uint32_t>& idx, size_t t, float n[3]) {
    const float* a = &pos[idx[t * 3] * 3];
    const float* b = &pos[idx[t * 3 + 1] * 3];
    const float* c = &pos[idx[t * 3 + 2] * 3];
    const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    n[0] = e1[1] * e2[2] - e1[2] * e2[1];
    n[1] = e1[2] * e2[0] - e1[0] * e2[2];
    n[2] = e1[0] * e2[1] - e1[1] * e2[0];
}

// A model of `count` unit quads in a row, each with its own solid-colour
// 8x8 image (uvs 0..1), or all sharing material 0 when shared is true.
Model quads(int count, bool shared) {
    Model m;
    m.source = "<test>";
    for (int i = 0; i < count; ++i) {
        if (!shared || i == 0) {
            Image im;
            im.name = "solid " + std::to_string(i);
            im.width = im.height = 8;
            im.rgba.resize(8 * 8 * 4);
            for (size_t t = 0; t < 64; ++t) {
                im.rgba[t * 4] = static_cast<uint8_t>(i % 256);
                im.rgba[t * 4 + 1] = static_cast<uint8_t>((i * 7) % 256);
                im.rgba[t * 4 + 2] = static_cast<uint8_t>(255 - i % 256);
                im.rgba[t * 4 + 3] = 255;
            }
            m.images.push_back(im);
            Texture tex;
            tex.image = static_cast<int>(m.images.size()) - 1;
            m.textures.push_back(tex);
            Material mat;
            mat.name = "m" + std::to_string(i);
            mat.baseColorTexture = static_cast<int>(m.textures.size()) - 1;
            m.materials.push_back(mat);
        }
        Primitive p;
        p.node = "quad " + std::to_string(i);
        p.mesh = "quad";
        p.material = shared ? 0 : i;
        const float x = static_cast<float>(i) * 1.5f;
        p.positions = {x, 0, 0, x + 1, 0, 0, x + 1, 1, 0, x, 1, 0};
        p.normals = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
        p.uvs = {0, 1, 1, 1, 1, 0, 0, 0};
        p.indices = {0, 1, 2, 0, 2, 3};
        m.primitives.push_back(p);
    }
    return m;
}

const uint8_t* texel(const PlannedTexture& t, float u, float v) {
    const int x = std::clamp(static_cast<int>(u * static_cast<float>(t.width)), 0, t.width - 1);
    const int y = std::clamp(static_cast<int>(v * static_cast<float>(t.height)), 0, t.height - 1);
    return &t.rgba[(static_cast<size_t>(y) * static_cast<size_t>(t.width) + static_cast<size_t>(x)) * 4];
}

}  // namespace

// ---------------------------------------------------------------- loading

TEST(gltf_cube_loads_with_its_material) {
    const Model m = loadFile(asset("cube.gltf"));
    REQUIRE(m.primitives.size() == 1);
    const Primitive& p = m.primitives[0];
    CHECK(p.vertexCount() == 24);
    CHECK(p.indices.size() == 36);
    CHECK(p.normals.size() == 72);
    CHECK(p.colors.empty());
    CHECK(p.uvs.empty());  // the material has no texture, so uvs are not read
    REQUIRE(m.materials.size() == 1);
    CHECK_NEAR(m.materials[0].baseColorFactor[0], 0.8, 1e-6);
    CHECK(m.materials[0].alphaMode == AlphaMode::Opaque);
    for (int k = 0; k < 3; ++k) {
        CHECK_NEAR(m.boundsMin[k], -0.5, 1e-6);
        CHECK_NEAR(m.boundsMax[k], 0.5, 1e-6);
    }
    // Every triangle winds counter-clockwise seen from outside: its face
    // normal agrees with the vertex normals.
    for (size_t t = 0; t < p.triangleCount(); ++t) {
        float n[3];
        faceNormal(p.positions, p.indices, t, n);
        const float* vn = &p.normals[p.indices[t * 3] * 3];
        CHECK(n[0] * vn[0] + n[1] * vn[1] + n[2] * vn[2] > 0);
    }
    CHECK(m.warnings.empty());
}

TEST(gltf_glb_with_embedded_images_and_samplers) {
    const Model m = loadFile(asset("textured_quad.glb"));
    REQUIRE(m.primitives.size() == 2);
    REQUIRE(m.images.size() == 2);
    CHECK(m.images[0].width == 16 && m.images[0].rgba.size() == 16 * 16 * 4);
    CHECK(m.images[1].width == 64);
    // quadrants: top-left red, with a white marker at the very corner.
    CHECK(m.images[0].rgba[0] == 255 && m.images[0].rgba[1] == 255);
    const uint8_t* red = &m.images[0].rgba[(4 * 16 + 4) * 4];
    CHECK(red[0] == 220 && red[1] == 40 && red[2] == 40);
    REQUIRE(m.textures.size() == 2);
    CHECK(m.textures[0].sampler.nearest && m.textures[0].sampler.clampU && m.textures[0].sampler.clampV);
    CHECK(!m.textures[0].sampler.mipmaps);
    CHECK(!m.textures[1].sampler.nearest && !m.textures[1].sampler.clampU && m.textures[1].sampler.mipmaps);
    const Primitive* ground = findPrimitive(m, "ground");
    REQUIRE(ground);
    CHECK(ground->uvs.size() == 8);
    CHECK_NEAR(*std::max_element(ground->uvs.begin(), ground->uvs.end()), 4.0, 1e-6);
}

TEST(gltf_hierarchy_applies_node_transforms) {
    const Model m = loadFile(asset("hierarchy.gltf"));
    CHECK(m.warnings.empty());
    CHECK(m.primitives.size() == 7);  // root block, child block, post x2, wedge x2, ground
    CHECK(m.nodesVisited == 6);
    // External image with a %20 in its URI.
    REQUIRE(m.images.size() == 1);
    CHECK(m.images[0].width == 32);

    float c[3];
    const Primitive* root = findPrimitive(m, "root");
    REQUIRE(root);
    centroid(*root, c);
    CHECK_NEAR(c[0], -0.8, 1e-5);
    CHECK_NEAR(c[1], 0.5, 1e-5);
    const Primitive* top = findPrimitive(m, "block on top");
    REQUIRE(top);
    centroid(*top, c);  // root translation + child translation
    CHECK_NEAR(c[0], -0.8, 1e-5);
    CHECK_NEAR(c[1], 1.5, 1e-5);
    CHECK_NEAR(c[2], 0.0, 1e-5);
    // Its scale (0.6) shows in the extent, the 45 degree turn in a corner.
    float maxY = -1e9f;
    for (size_t v = 1; v < top->positions.size(); v += 3) maxY = std::max(maxY, top->positions[v]);
    CHECK_NEAR(maxY, 1.8, 1e-5);
    // The matrix node: post base centred at (1.6, -0.5, 0.4) relative to the root.
    const Primitive* base = findPrimitive(m, "post (matrix)", 0);
    REQUIRE(base);
    centroid(*base, c);
    CHECK_NEAR(c[0], -0.8 + 1.6, 1e-5);
    CHECK_NEAR(c[1], 0.5 - 0.5 + 0.125, 1e-5);
    CHECK_NEAR(c[2], 0.4, 1e-5);
    // Normals stay unit length through the scaled node.
    for (size_t v = 0; v < top->normals.size(); v += 3) {
        const float* n = &top->normals[v];
        CHECK_NEAR(n[0] * n[0] + n[1] * n[1] + n[2] * n[2], 1.0, 1e-5);
    }
}

TEST(gltf_mirrored_node_keeps_counter_clockwise_winding) {
    const Model m = loadFile(asset("hierarchy.gltf"));
    const Primitive* plain = findPrimitive(m, "wedge");
    const Primitive* mirrored = findPrimitive(m, "wedge mirrored");
    REQUIRE(plain && mirrored);
    // No normals in the file: flat normals, one vertex per corner.
    CHECK(mirrored->vertexCount() == mirrored->indices.size());
    for (const Primitive* p : {plain, mirrored}) {
        for (size_t t = 0; t < p->triangleCount(); ++t) {
            float n[3];
            faceNormal(p->positions, p->indices, t, n);
            const float* vn = &p->normals[p->indices[t * 3] * 3];
            CHECK(n[0] * vn[0] + n[1] * vn[1] + n[2] * vn[2] > 0);
        }
    }
    // The slope faces +x on the plain wedge and -x on the mirrored one.
    float plainX = 0, mirroredX = 0;
    for (size_t v = 0; v < plain->normals.size(); v += 3) plainX = std::max(plainX, plain->normals[v]);
    for (size_t v = 0; v < mirrored->normals.size(); v += 3) mirroredX = std::min(mirroredX, mirrored->normals[v]);
    CHECK(plainX > 0.5f);
    CHECK(mirroredX < -0.5f);
}

TEST(gltf_vertex_colours_strips_and_fans) {
    const Model m = loadFile(asset("vertex_colors.glb"));
    REQUIRE(m.primitives.size() == 4);
    const Primitive& pyramid = m.primitives[0];
    const Primitive& strip = m.primitives[1];
    const Primitive& fan = m.primitives[2];
    CHECK(pyramid.triangleCount() == 4);
    CHECK(strip.triangleCount() == 14);
    CHECK(fan.triangleCount() == 16);
    for (const Primitive* p : {&pyramid, &strip, &fan}) {
        CHECK(p->colors.size() == p->vertexCount() * 4);
        CHECK(p->vertexCount() == p->indices.size());  // flat: no normals in the file
    }
    // Float RGB: the first corner is red, alpha filled in as 1.
    CHECK_NEAR(pyramid.colors[0], 1.0, 1e-6);
    CHECK_NEAR(pyramid.colors[1], 0.1, 1e-6);
    CHECK_NEAR(pyramid.colors[3], 1.0, 1e-6);
    // Normalised uint8: the strip's first vertex is (255, 0, 200, 255).
    CHECK_NEAR(strip.colors[0], 1.0, 1e-6);
    CHECK_NEAR(strip.colors[1], 0.0, 1e-6);
    CHECK_NEAR(strip.colors[2], 200.0 / 255.0, 1e-6);
    // Normalised uint16: the fan's centre is white.
    CHECK_NEAR(fan.colors[0], 1.0, 1e-6);
    // Strip and fan triangles face up/forward (+z or +y), none flipped.
    for (size_t t = 0; t < fan.triangleCount(); ++t) {
        float n[3];
        faceNormal(fan.positions, fan.indices, t, n);
        CHECK(n[1] > 0);
    }
    for (size_t t = 0; t < strip.triangleCount(); ++t) {
        float n[3];
        faceNormal(strip.positions, strip.indices, t, n);
        CHECK(n[2] > -1e-3f);
    }
}

TEST(gltf_alpha_modes_unlit_and_texture_transform) {
    const Model m = loadFile(asset("alpha_modes.gltf"));
    REQUIRE(m.materials.size() == 7);
    CHECK(!m.materials[0].isEmissive());
    CHECK(m.materials[6].isEmissive());
    CHECK_NEAR(m.materials[6].emissive[0], 0.5, 1e-6);  // factor 1.0 x strength 0.5
    CHECK_NEAR(m.materials[6].emissive[1], 0.3, 1e-6);
    CHECK(m.materials[0].alphaMode == AlphaMode::Opaque);
    CHECK(m.materials[1].alphaMode == AlphaMode::Mask);
    CHECK_NEAR(m.materials[1].alphaCutoff, 0.5, 1e-6);
    CHECK(m.materials[2].alphaMode == AlphaMode::Blend);
    CHECK_NEAR(m.materials[2].baseColorFactor[3], 0.6, 1e-6);
    CHECK(m.materials[3].doubleSided);
    CHECK(m.materials[4].unlit);
    const Primitive* panel = findPrimitive(m, "panel");
    REQUIRE(panel);
    // KHR_texture_transform: offset (0.5, 0), scale 2: uv (0, 0) -> (0.5, 0), (1, 1) -> (2.5, 2).
    CHECK_NEAR(panel->uvs[0], 0.5, 1e-6);
    CHECK_NEAR(panel->uvs[1], 0.0, 1e-6);
    CHECK_NEAR(panel->uvs[4], 2.5, 1e-6);
    CHECK_NEAR(panel->uvs[5], 2.0, 1e-6);
}

TEST(gltf_load_errors_name_the_problem) {
    try {
        loadFile(asset("missing.glb"));
        CHECK(false);
    } catch (const Error& e) {
        CHECK(e.kind() == Error::Kind::Io);
    }
    const char junk[] = "this is not a glTF file at all";
    try {
        loadMemory(junk, sizeof junk, "");
        CHECK(false);
    } catch (const Error& e) {
        CHECK(e.kind() == Error::Kind::Invalid);
    }
    LoadOptions options;
    options.scene = 3;
    try {
        loadFile(asset("cube.gltf"), options);
        CHECK(false);
    } catch (const Error& e) {
        CHECK(e.kind() == Error::Kind::Invalid);
        CHECK(std::string(e.what()).find("scene 3") != std::string::npos);
    }
}

TEST(gltf_load_from_memory) {
    std::ifstream in(asset("textured_quad.glb"), std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(!bytes.empty());
    const Model m = loadMemory(bytes.data(), bytes.size(), "");
    CHECK(m.primitives.size() == 2);
    CHECK(m.images[0].width == 16);
}

// ---------------------------------------------------------------- planning

TEST(gltf_plan_one_mesh_per_primitive_when_it_fits) {
    const Plan p = plan(loadFile(asset("textured_quad.glb")));
    CHECK(!p.report.merged && !p.report.atlased);
    REQUIRE(p.meshes.size() == 2);
    REQUIRE(p.textures.size() == 2);
    CHECK(p.textures[0].flags == (0x2u | 0x4u | 0x8u));  // clamp u, clamp v, nearest
    CHECK(p.textures[1].flags == 0x1u);                  // mipmaps, repeat
    for (const PlannedMesh& m : p.meshes) {
        CHECK(m.flags == 0x4u);  // STRATA_MESH_COLORS_RGBA
        CHECK(m.colors.size() == m.vertexCount() * 4);
        CHECK(m.uvs.size() == m.vertexCount() * 2);
        CHECK(m.texture >= 0);
    }
    CHECK(p.report.meshesOut == 2 && p.report.texturesOut == 2 && p.report.triangles == 4);
}

TEST(gltf_plan_merges_when_primitives_exceed_the_mesh_budget) {
    const Model m = quads(300, true);
    const Plan p = plan(m);
    CHECK(p.report.merged);
    CHECK(!p.report.atlased);
    REQUIRE(p.meshes.size() == 1);
    CHECK(p.meshes[0].primitives == 300);
    CHECK(p.meshes[0].triangleCount() == 600);
    CHECK(p.meshes[0].material == 0);
}

TEST(gltf_plan_atlases_when_textures_exceed_the_budget) {
    const Model m = quads(300, false);
    const Plan p = plan(m);
    CHECK(p.report.atlased);
    CHECK(p.report.atlasPages == 1);
    CHECK_NEAR(p.report.atlasScale, 1.0, 1e-6);
    REQUIRE(p.textures.size() == 1);
    REQUIRE(p.meshes.size() == 1);
    const PlannedMesh& mesh = p.meshes[0];
    CHECK(mesh.material == -2);
    CHECK(mesh.materials.size() == 300);
    // Each quad's uv centre samples its own colour from the page.
    for (int q = 0; q < 300; q += 37) {
        float u = 0, v = 0;
        for (size_t k = 0; k < 4; ++k) {
            u += mesh.uvs[(static_cast<size_t>(q) * 4 + k) * 2] * 0.25f;
            v += mesh.uvs[(static_cast<size_t>(q) * 4 + k) * 2 + 1] * 0.25f;
        }
        const uint8_t* t = texel(p.textures[0], u, v);
        CHECK(t[0] == static_cast<uint8_t>(q % 256));
        CHECK(t[1] == static_cast<uint8_t>((q * 7) % 256));
    }
}

TEST(gltf_plan_atlas_scales_down_to_fit_the_page_budget) {
    const Model m = quads(40, false);
    BudgetOptions o;
    o.maxTextures = 2;
    o.atlasSize = 64;  // 16 cells of 8 x 8 plus gutter a page at full size: 3 pages, so it must shrink
    const Plan p = plan(m, o);
    CHECK(p.report.atlased);
    CHECK(p.textures.size() == 2);
    CHECK(p.report.atlasScale < 1.0f);
    CHECK(p.meshes.size() == p.textures.size());
}

TEST(gltf_plan_drops_repeating_textures_past_the_budget) {
    const Model model = loadFile(asset("hierarchy.gltf"));
    BudgetOptions o;
    o.maxMeshes = 1;
    const Plan p = plan(model, o);
    CHECK(p.report.atlased);
    CHECK(p.report.repeatingDropped == 1);
    CHECK(p.report.primitivesUntextured == 3);
    CHECK(p.textures.empty());
    REQUIRE(p.meshes.size() == 1);
    CHECK(p.meshes[0].texture == -1);
    CHECK(p.meshes[0].primitives == 7);
}

TEST(gltf_plan_merges_by_material_on_request) {
    const Model model = loadFile(asset("hierarchy.gltf"));
    BudgetOptions o;
    o.alwaysMerge = true;
    const Plan p = plan(model, o);
    CHECK(p.report.merged && !p.report.atlased);
    CHECK(p.meshes.size() == 2);  // the textured grid, the untextured blue + gold
    CHECK(p.textures.size() == 1);
    size_t triangles = 0;
    for (const PlannedMesh& m : p.meshes) triangles += m.triangleCount();
    CHECK(triangles == model.triangleCount());
}

TEST(gltf_plan_splits_meshes_at_the_vertex_limit) {
    const Model model = loadFile(asset("cube.gltf"));
    BudgetOptions o;
    o.maxMeshVertices = 7;
    o.alwaysMerge = true;
    const Plan p = plan(model, o);
    CHECK(p.meshes.size() > 1);
    size_t triangles = 0;
    for (const PlannedMesh& m : p.meshes) {
        CHECK(m.vertexCount() <= 7);
        triangles += m.triangleCount();
        for (uint32_t i : m.indices) CHECK(i < m.vertexCount());
    }
    CHECK(triangles == 12);
    CHECK(p.report.meshesSplit == static_cast<int>(p.meshes.size()) - 1);
}

TEST(gltf_plan_colour_policies) {
    const Model model = loadFile(asset("cube.gltf"));  // baseColorFactor (0.8, 0.12, 0.1)
    BudgetOptions o;
    o.colors = ColorPolicy::AsStored;
    CHECK_NEAR(plan(model, o).meshes[0].colors[1], 0.12, 1e-6);
    o.colors = ColorPolicy::Display;
    CHECK_NEAR(plan(model, o).meshes[0].colors[1], linearToSrgb(0.12f), 1e-6);
    CHECK_NEAR(linearToSrgb(0.5f), 0.7354, 1e-3);
    CHECK_NEAR(srgbToLinear(linearToSrgb(0.3f)), 0.3, 1e-5);

    const Model tex = loadFile(asset("textured_quad.glb"));
    o.colors = ColorPolicy::Linear;
    const Plan lin = plan(tex, o);
    o.colors = ColorPolicy::Display;
    const Plan disp = plan(tex, o);
    // quadrants red (220, 40, 40) decodes to linear (182.5, 5.4, 5.4).
    const size_t red = (4 * 16 + 4) * 4;
    CHECK(disp.textures[0].rgba[red] == 220);
    CHECK(lin.textures[0].rgba[red] == 182 || lin.textures[0].rgba[red] == 183);
    CHECK(lin.textures[0].rgba[red + 1] == 5);
}

TEST(gltf_plan_alpha_modes) {
    const Model model = loadFile(asset("alpha_modes.gltf"));
    const Plan p = plan(model);
    REQUIRE(p.meshes.size() == 7);
    const PlannedMesh& opaque = p.meshes[0];
    const PlannedMesh& mask = p.meshes[1];
    const PlannedMesh& blend = p.meshes[2];
    const PlannedMesh& leaf = p.meshes[3];
    const PlannedMesh& panel = p.meshes[4];
    CHECK((opaque.flags & 0x2u) == 0);
    CHECK((mask.flags & 0x2u) != 0);
    CHECK(mask.alphaMode == AlphaMode::Mask);
    CHECK_NEAR(mask.alphaCutoff, 0.5, 1e-6);
    CHECK_NEAR(mask.color[3], 1.0, 1e-6);
    CHECK((blend.flags & 0x2u) != 0);
    CHECK_NEAR(blend.color[3], 0.6, 1e-6);
    CHECK(blend.alphaMode == AlphaMode::Blend);
    // MASK's texture has the cutoff baked in: only 0 and 255 alpha.
    REQUIRE(mask.texture >= 0 && mask.texture != opaque.texture);
    bool binary = true;
    const std::vector<uint8_t>& texels = p.textures[static_cast<size_t>(mask.texture)].rgba;
    for (size_t i = 3; i < texels.size(); i += 4) binary = binary && (texels[i] == 0 || texels[i] == 255);
    CHECK(binary);
    // doubleSided: each triangle gets a reversed twin.
    CHECK(leaf.doubleSided);
    CHECK(leaf.triangleCount() == 4);
    CHECK(p.report.backFacesAdded == 2);
    CHECK((panel.flags & 0x1u) != 0);  // unlit
    CHECK(p.report.maskAsBlend == 1);
    // The emissive lamp: unlit, base colour plus emissive, emissive recorded.
    const PlannedMesh& lamp = p.meshes[5];
    CHECK((lamp.flags & 0x1u) != 0);
    CHECK_NEAR(lamp.emissive[0], 0.5, 1e-6);
    CHECK_NEAR(lamp.colors[0], linearToSrgb(0.2f) + linearToSrgb(0.5f), 1e-5);
    CHECK(p.report.emissiveUnlit == 1);

    BudgetOptions o;
    o.mask = MaskPolicy::Opaque;
    o.backFaces = false;
    o.emissive = EmissivePolicy::Ignore;
    const Plan q = plan(model, o);
    CHECK((q.meshes[5].flags & 0x1u) == 0);
    CHECK_NEAR(q.meshes[5].emissive[0], 0.5, 1e-6);  // still recorded
    CHECK((q.meshes[1].flags & 0x2u) == 0);
    CHECK(q.meshes[1].texture == q.meshes[0].texture);  // the same texture as the opaque disc
    CHECK(q.meshes[3].triangleCount() == 2);
    CHECK(q.report.maskAsOpaque == 1);
}

TEST(gltf_plan_reports_an_impossible_budget) {
    const Model model = loadFile(asset("alpha_modes.gltf"));
    BudgetOptions o;
    o.maxMeshes = 1;  // opaque and translucent can never share a mesh
    try {
        plan(model, o);
        CHECK(false);
    } catch (const Error& e) {
        CHECK(e.kind() == Error::Kind::Unsupported);
    }
    o.maxMeshes = 0;
    try {
        plan(model, o);
        CHECK(false);
    } catch (const Error& e) {
        CHECK(e.kind() == Error::Kind::Invalid);
    }
}

TEST(gltf_plan_report_text_and_json) {
    const Plan p = plan(loadFile(asset("alpha_modes.gltf")));
    const std::string text = p.report.text();
    CHECK(text.find("meshes out:           7") != std::string::npos);
    const std::string json = p.report.json();
    CHECK(json.front() == '{' && json.back() == '}');
    CHECK(json.find("\"meshes_out\": 7") != std::string::npos);
    CHECK(json.find("\"notes\": [\"") != std::string::npos);  // the MASK note
}

// ---------------------------------------------------------------- atlas

TEST(gltf_atlas_resize_averages) {
    AtlasImage im;
    im.width = 2;
    im.height = 2;
    im.rgba = {0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255};
    const AtlasImage one = resizeImage(im, 1, 1);
    REQUIRE(one.rgba.size() == 4);
    CHECK(one.rgba[0] == 128);
    CHECK(one.rgba[3] == 255);
}

TEST(gltf_atlas_gutter_repeats_edges) {
    AtlasImage im;
    im.width = im.height = 4;
    im.rgba.assign(4 * 4 * 4, 200);
    const std::vector<AtlasInput> inputs = {{&im}, {&im}};
    const AtlasResult r = packAtlas(inputs, 64, 1);
    REQUIRE(r.pages.size() == 1);
    REQUIRE(r.placements.size() == 2);
    const AtlasPlacement& a = r.placements[0];
    CHECK(a.x == kAtlasGutter && a.y == kAtlasGutter);
    CHECK(r.placements[1].x == a.x + a.width + 2 * kAtlasGutter);
    const AtlasImage& page = r.pages[0];
    CHECK(page.width == 32 && page.height == 16);  // cropped to powers of two
    CHECK(page.rgba[0] == 200);                    // the gutter corner repeats the image corner
}

TEST(gltf_plan_untextured_primitives_join_the_atlas_on_a_white_texel) {
    Model m = quads(3, false);
    Primitive plain = m.primitives[0];
    plain.node = "plain";
    plain.material = -1;  // the default material: untextured white
    plain.uvs.clear();
    m.primitives.push_back(plain);
    BudgetOptions o;
    o.maxMeshes = 1;
    const Plan p = plan(m, o);
    CHECK(p.report.atlased);
    REQUIRE(p.meshes.size() == 1);
    REQUIRE(p.textures.size() == 1);
    const PlannedMesh& mesh = p.meshes[0];
    CHECK(mesh.primitives == 4);
    // The untextured quad's uvs all sample white.
    const size_t first = 3 * 4;
    for (size_t k = 0; k < 4; ++k) {
        const uint8_t* t = texel(p.textures[0], mesh.uvs[(first + k) * 2], mesh.uvs[(first + k) * 2 + 1]);
        CHECK(t[0] == 255 && t[1] == 255 && t[2] == 255);
    }
}
