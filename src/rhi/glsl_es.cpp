#include "rhi/glsl_es.hpp"

#include <cstdio>
#include <regex>

#include "core/error.hpp"

namespace strata::rhi {
namespace {

const char* kEsPrologue =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "precision highp sampler2D;\n"
    "precision highp sampler2DArray;\n"
    "precision highp sampler2DShadow;\n"
    "precision highp sampler2DArrayShadow;\n"
    "precision highp samplerCubeShadow;\n";

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) {
        s.replace(at, from.size(), to);
    }
}

}  // namespace

GlslEsShader translateToGlslEs(const std::string& glsl450, ShaderStage stage, const GlslEsOptions& options) {
    GlslEsShader out;
    std::string s = glsl450;

    const std::string version = "#version 450 core\n";
    if (s.compare(0, version.size(), version) != 0) {
        fail(STRATA_ERROR_INTERNAL, "GLSL ES rewrite: shader does not start with '#version 450 core'");
    }
    s.replace(0, version.size(), kEsPrologue);

    // Uniform blocks: layout(std140, binding = N) uniform Name
    {
        static const std::regex block(R"(layout\(std140, binding = (\d+)\) uniform (\w+))");
        std::string rewritten;
        auto begin = std::sregex_iterator(s.begin(), s.end(), block);
        size_t last = 0;
        for (auto it = begin; it != std::sregex_iterator(); ++it) {
            const std::smatch& m = *it;
            rewritten.append(s, last, static_cast<size_t>(m.position(0)) - last);
            rewritten += "layout(std140) uniform " + m[2].str();
            out.blocks.push_back({m[2].str(), std::stoi(m[1].str())});
            last = static_cast<size_t>(m.position(0) + m.length(0));
        }
        rewritten.append(s, last, std::string::npos);
        s = rewritten;
    }

    // Samplers: layout(binding = N) uniform <type> <name>;
    {
        static const std::regex sampler(R"(layout\(binding = (\d+)\) uniform (\w+) (\w+);)");
        std::string rewritten;
        std::vector<std::string> cubeArrays;
        size_t last = 0;
        for (auto it = std::sregex_iterator(s.begin(), s.end(), sampler); it != std::sregex_iterator(); ++it) {
            const std::smatch& m = *it;
            rewritten.append(s, last, static_cast<size_t>(m.position(0)) - last);
            const int unit = std::stoi(m[1].str());
            const std::string type = m[2].str(), name = m[3].str();
            if (type == "samplerCubeArrayShadow") {
                if (options.cubeArrayUnits.empty() || options.cubeArrayUnits.front() != unit) {
                    fail(STRATA_ERROR_INTERNAL, "GLSL ES rewrite: %s on unit %d has no cube units to spread over",
                         name.c_str(), unit);
                }
                const size_t cubes = options.cubeArrayUnits.size();
                for (size_t i = 0; i < cubes; ++i) {
                    const std::string cube = name + "_" + std::to_string(i);
                    rewritten += "uniform samplerCubeShadow " + cube + ";\n";
                    out.samplers.push_back({cube, options.cubeArrayUnits[i]});
                }
                // texture(samplerCubeArrayShadow, vec4(dir, layer), ref) for a
                // layer picked at run time: GLSL ES 3.00 only indexes sampler
                // arrays with constants, so branch per cube.
                rewritten += "float strata_" + name + "_texture(vec4 c, float ref) {\n";
                rewritten += "    int layer = int(c.w + 0.5);\n";
                for (size_t i = cubes; i-- > 1;) {
                    rewritten += "    if (layer == " + std::to_string(i) + ") return texture(" + name + "_" +
                                 std::to_string(i) + ", vec4(c.xyz, ref));\n";
                }
                rewritten += "    return texture(" + name + "_0, vec4(c.xyz, ref));\n}";
                cubeArrays.push_back(name);
            } else {
                rewritten += "uniform " + type + " " + name + ";";
                out.samplers.push_back({name, unit});
            }
            last = static_cast<size_t>(m.position(0) + m.length(0));
        }
        rewritten.append(s, last, std::string::npos);
        s = rewritten;
        for (const std::string& name : cubeArrays) {
            replaceAll(s, "texture(" + name + ", ", "strata_" + name + "_texture(");
        }
    }

    if (s.find("binding =") != std::string::npos) {
        fail(STRATA_ERROR_INTERNAL, "GLSL ES rewrite: a binding qualifier was left behind");
    }
    if (s.find("samplerCubeArray") != std::string::npos) {
        fail(STRATA_ERROR_INTERNAL, "GLSL ES rewrite: a cube map array sampler was left behind");
    }

    if (options.emulateDepthClamp) {
        const std::string main = "void main() {";
        const size_t at = s.find(main);
        if (at == std::string::npos || s.find(main, at + 1) != std::string::npos) {
            fail(STRATA_ERROR_INTERNAL, "GLSL ES rewrite: 'void main() {' must occur exactly once");
        }
        s.replace(at, main.size(), "void strata_main() {");
        if (stage == ShaderStage::Vertex) {
            // Keep the primitive from being clipped at the near and far
            // planes, and hand the true clip-space depth to the fragment
            // shader. Clamping z per vertex alone ("pancaking") is not enough:
            // it bends the depth across a large triangle that crosses a plane.
            s +=
                "out highp vec2 strata_vClipZW;\n"
                "void main() {\n"
                "    strata_main();\n"
                "    strata_vClipZW = gl_Position.zw;\n"
                "    if (gl_Position.w > 0.0) gl_Position.z = clamp(gl_Position.z, -gl_Position.w, gl_Position.w);\n"
                "}\n";
        } else {
            if (s.find("gl_FragDepth") != std::string::npos) {
                fail(STRATA_ERROR_INTERNAL, "GLSL ES rewrite: cannot emulate depth clamp in a shader that writes depth");
            }
            // The window-space depth, clamped to the depth range as
            // GL_DEPTH_CLAMP does, plus glPolygonOffset's
            // m * factor + r * units, which writing gl_FragDepth bypasses
            // (r: one unit in the last place of a 32-bit float depth).
            char bias[160];
            std::snprintf(bias, sizeof bias, "    const float strata_kBiasSlope = %.9e;\n    const float strata_kBiasUnits = %.9e;\n",
                          static_cast<double>(options.depthBiasSlope), static_cast<double>(options.depthBiasConstant));
            s += "in highp vec2 strata_vClipZW;\n"
                 "void main() {\n"
                 "    strata_main();\n"
                 "    float d = 0.5 * strata_vClipZW.x / strata_vClipZW.y + 0.5;\n";
            s += bias;
            s +=
                "    float m = max(abs(dFdx(d)), abs(dFdy(d)));\n"
                "    float r = exp2(floor(log2(max(abs(d), 1e-30))) - 23.0);\n"
                "    gl_FragDepth = clamp(d + m * strata_kBiasSlope + r * strata_kBiasUnits, 0.0, 1.0);\n"
                "}\n";
        }
    }

    out.source = std::move(s);
    return out;
}

}  // namespace strata::rhi
