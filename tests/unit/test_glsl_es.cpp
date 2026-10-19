// The GLSL 4.50 -> GLSL ES 3.00 rewrite used by the GLES 3 / WebGL 2 device,
// checked on Strata's own shaders with no GL context. That the rewritten
// shaders compile and draw the same pixels is checked by running the golden
// images through the GLES device (STRATA_GL_API=gles3) and in a browser.
#include <string>

#include "core/error.hpp"
#include "render/shaders.hpp"
#include "rhi/glsl_es.hpp"
#include "test.hpp"

using namespace strata::rhi;
namespace shaders = strata::render::shaders;

namespace {

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

int unitOf(const std::vector<GlslEsBinding>& bindings, const std::string& name) {
    for (const auto& b : bindings) {
        if (b.name == name) return b.unit;
    }
    return -1;
}

GlslEsOptions cubeOptions() {
    GlslEsOptions o;
    o.cubeArrayUnits = {2, 5, 6, 7};
    return o;
}

}  // namespace

TEST(glsl_es_version_and_precision) {
    const GlslEsShader v = translateToGlslEs(shaders::kPrimitiveVertex, ShaderStage::Vertex, {});
    CHECK(v.source.rfind("#version 300 es\n", 0) == 0);
    CHECK(contains(v.source, "precision highp float;"));
    CHECK(!contains(v.source, "450"));
}

TEST(glsl_es_uniform_block_bindings_move_to_the_api) {
    const GlslEsShader v = translateToGlslEs(shaders::kMeshVertex, ShaderStage::Vertex, {});
    CHECK(!contains(v.source, "binding"));
    CHECK(contains(v.source, "layout(std140) uniform Frame {"));
    CHECK(contains(v.source, "layout(std140) uniform Mesh {"));
    CHECK(unitOf(v.blocks, "Frame") == 0);
    CHECK(unitOf(v.blocks, "Mesh") == 3);
    CHECK(v.samplers.empty());
}

TEST(glsl_es_sampler_bindings_move_to_the_api) {
    const GlslEsShader f = translateToGlslEs(shaders::kTextFragment, ShaderStage::Fragment, {});
    CHECK(contains(f.source, "uniform sampler2D uFont;"));
    CHECK(unitOf(f.samplers, "uFont") == 3);
}

TEST(glsl_es_cube_map_array_becomes_one_cube_per_unit) {
    const GlslEsShader f = translateToGlslEs(shaders::kSunFragment, ShaderStage::Fragment, cubeOptions());
    CHECK(!contains(f.source, "samplerCubeArray"));
    CHECK(!contains(f.source, "texture(uPointShadowMaps,"));
    CHECK(contains(f.source, "strata_uPointShadowMaps_texture(vec4("));
    CHECK(unitOf(f.samplers, "uShadowMap") == 1);
    CHECK(unitOf(f.samplers, "uPointShadowMaps_0") == 2);
    CHECK(unitOf(f.samplers, "uPointShadowMaps_1") == 5);
    CHECK(unitOf(f.samplers, "uPointShadowMaps_3") == 7);
    CHECK(unitOf(f.blocks, "PointLights") == 2);
}

TEST(glsl_es_cube_map_array_needs_units) {
    bool threw = false;
    try {
        translateToGlslEs(shaders::kSunFragment, ShaderStage::Fragment, {});
    } catch (const strata::Error& e) {
        threw = e.status() == STRATA_ERROR_INTERNAL;
    }
    CHECK(threw);
}

TEST(glsl_es_depth_clamp_emulation_writes_fragment_depth) {
    GlslEsOptions o;
    o.emulateDepthClamp = true;
    o.depthBiasSlope = 2.0f;
    o.depthBiasConstant = 2.0f;
    const GlslEsShader v = translateToGlslEs(shaders::kShadowVertex, ShaderStage::Vertex, o);
    CHECK(contains(v.source, "void strata_main() {"));
    CHECK(contains(v.source, "out highp vec2 strata_vClipZW;"));
    const GlslEsShader f = translateToGlslEs(shaders::kShadowFragment, ShaderStage::Fragment, o);
    CHECK(contains(f.source, "gl_FragDepth = clamp("));
    // GLSL ES has no implicit int -> float: the bias must be a float literal.
    CHECK(contains(f.source, "strata_kBiasSlope = 2.000000000e+00;"));

    // Without the option nothing is wrapped.
    const GlslEsShader plain = translateToGlslEs(shaders::kShadowVertex, ShaderStage::Vertex, {});
    CHECK(!contains(plain.source, "strata_main"));
}

TEST(glsl_es_mesh_albedo_variant_keeps_its_sampler) {
    // renderer.cpp splices `layout(binding = 4) uniform sampler2D uAlbedo;`
    // into the lit shaders; the same pattern must be rewritten.
    std::string lit = shaders::kLegacyFragment;
    lit.replace(lit.find("in vec4 vColor;\n"), 16,
                "in vec4 vColorIn;\nin vec2 vUv;\nlayout(binding = 4) uniform sampler2D uAlbedo;\nvec4 vColor;\n");
    const GlslEsShader f = translateToGlslEs(lit, ShaderStage::Fragment, {});
    CHECK(unitOf(f.samplers, "uAlbedo") == 4);
    CHECK(!contains(f.source, "binding"));
}
