// GLSL 4.50 -> GLSL ES 3.00 for the GLES 3 / WebGL 2 device.
//
// Strata's shaders (render/shaders.hpp) are written once, for GL 4.5. The
// GLES 3 device rewrites each one when its pipeline is created, so there is
// still one copy of every shader. The rewrite is mechanical and covers only
// what those shaders use:
//
//   * `#version 450 core` becomes `#version 300 es` plus default precisions.
//   * `layout(binding = N)` on uniform blocks and samplers is removed (GLSL ES
//     3.00 has no binding qualifier); the bindings are returned so the device
//     can set them through the API after linking.
//   * A `samplerCubeArrayShadow` (GLES 3.2 only) becomes one
//     `samplerCubeShadow` per cube, selected by layer in a helper function.
//     The device binds each cube of the array to its own texture unit.
//   * Optionally, depth clamping for a pipeline that asks for it, on a
//     context without EXT_depth_clamp: the vertex shader keeps primitives
//     from being clipped at the near and far planes, and the fragment shader
//     writes the clamped depth (plus the pipeline's polygon offset, which a
//     depth write turns off) to gl_FragDepth.
#pragma once

#include <string>
#include <vector>

namespace strata::rhi {

enum class ShaderStage { Vertex, Fragment };

struct GlslEsBinding {
    std::string name;
    int unit = 0;  // uniform block binding, or texture unit for a sampler
};

struct GlslEsOptions {
    bool emulateDepthClamp = false;
    // With emulateDepthClamp: the pipeline's polygon offset (factor, units).
    float depthBiasSlope = 0.0f;
    float depthBiasConstant = 0.0f;
    // Texture units for the cubes of a samplerCubeArrayShadow, in layer order.
    // The first is the unit the GL 4.5 shader names.
    std::vector<int> cubeArrayUnits;
};

struct GlslEsShader {
    std::string source;
    std::vector<GlslEsBinding> blocks;
    std::vector<GlslEsBinding> samplers;
};

// Throws strata::Error (STRATA_ERROR_INTERNAL) on a shader it cannot rewrite.
GlslEsShader translateToGlslEs(const std::string& glsl450, ShaderStage stage, const GlslEsOptions& options);

}  // namespace strata::rhi
