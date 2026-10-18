// GLSL for the fixed pass list. Inline, because there are only a few dozen
// lines of it and no asset pipeline yet to load it from.
#pragma once

namespace strata::render::shaders {

// std140 block shared by every pass; mirrored by FrameUniforms in renderer.cpp.
#define STRATA_FRAME_BLOCK                                                         \
    "layout(std140, binding = 0) uniform Frame {\n"                                \
    "    mat4 uView;\n"                                                            \
    "    mat4 uProjection;\n"                                                      \
    "    mat4 uInverseViewProjection;\n"                                           \
    "    mat4 uCascadeViewProjection[4];\n"                                        \
    "    vec4 uEye;            // xyz eye, w far plane\n"                          \
    "    vec4 uLight;          // legacy: travel direction + ambient; sun: to-light + shadow strength\n" \
    "    vec4 uLightColor;     // rgb, w unused\n"                                 \
    "    vec4 uSkyAmbient;     // rgb, w fog on\n"                                 \
    "    vec4 uGroundAmbient;\n"                                                   \
    "    vec4 uFogColor;       // rgb, w fog start distance\n"                     \
    "    vec4 uZenith;         // rgb, w fog end distance\n"                       \
    "    vec4 uHorizon;\n"                                                         \
    "    vec4 uToSun;          // xyz, w sine of the sun's elevation\n"            \
    "    vec4 uCascadeSplits;  // view-space far distance of each cascade\n"       \
    "    vec4 uCascadeTexel;   // world size of one shadow texel per cascade\n"    \
    "    ivec4 uShadowInfo;    // x cascades (0 = off), y map size\n"              \
    "};\n"

// Instanced primitives. The vertex maths is Beyond Nations'
// InstancedPrimitiveShader term for term (scale then translate, normal
// divided by the squared scale, projection * view * world) so the legacy
// path reproduces BN's pixels.
inline const char* kPrimitiveVertex =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPosition;\n"
    "layout(location = 1) in vec3 aNormal;\n"
    "layout(location = 2) in vec3 iPosition;\n"
    "layout(location = 3) in vec3 iScale;\n"
    "layout(location = 4) in vec4 iColor;\n" STRATA_FRAME_BLOCK
    "out vec3 vNormal;\n"
    "out vec4 vColor;\n"
    "out vec3 vWorld;\n"
    "out float vViewDepth;\n"
    "void main() {\n"
    "    vec4 world = vec4(aPosition * iScale + iPosition, 1.0);\n"
    "    vec3 squaredScale = iScale * iScale;\n"
    "    vNormal = normalize(iScale * (aNormal / max(squaredScale, vec3(1e-8))));\n"
    "    vColor = iColor;\n"
    "    vWorld = world.xyz;\n"
    "    vViewDepth = -(uView * world).z;\n"
    "    gl_Position = uProjection * uView * world;\n"
    "}\n";

// LEGACY lighting: BN's PrimitiveRenderer fragment shader, unchanged.
inline const char* kLegacyFragment =
    "#version 450 core\n"
    "in vec3 vNormal;\n"
    "in vec4 vColor;\n"
    "in vec3 vWorld;\n"
    "in float vViewDepth;\n" STRATA_FRAME_BLOCK
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec3 normal = normalize(vNormal);\n"
    "    float diffuse = max(dot(normal, -uLight.xyz), 0.0);\n"
    "    float light = uLight.w + (1.0 - uLight.w) * diffuse;\n"
    "    fragColor = vec4(vColor.rgb * light, vColor.a);\n"
    "}\n";

// SUN lighting: key light (sun or moon) with cascaded shadows, hemisphere
// ambient and distance fog.
inline const char* kSunFragment =
    "#version 450 core\n"
    "in vec3 vNormal;\n"
    "in vec4 vColor;\n"
    "in vec3 vWorld;\n"
    "in float vViewDepth;\n" STRATA_FRAME_BLOCK
    "layout(binding = 1) uniform sampler2DArrayShadow uShadowMap;\n"
    "out vec4 fragColor;\n"
    "float sampleCascade(int cascade, vec3 world, vec3 normal) {\n"
    "    // Normal offset: push the lookup off the surface by a texel or so.\n"
    "    vec3 p = world + normal * (uCascadeTexel[cascade] * 1.5);\n"
    "    vec3 uvz = (uCascadeViewProjection[cascade] * vec4(p, 1.0)).xyz * 0.5 + 0.5;\n"
    "    if (any(lessThan(uvz.xy, vec2(0.0))) || any(greaterThan(uvz.xy, vec2(1.0))) || uvz.z > 1.0) return 1.0;\n"
    "    float texel = 1.0 / float(uShadowInfo.y);\n"
    "    float sum = 0.0;\n"
    "    for (int x = -1; x <= 1; ++x)\n"
    "        for (int y = -1; y <= 1; ++y)\n"
    "            sum += texture(uShadowMap, vec4(uvz.xy + vec2(x, y) * texel, float(cascade), uvz.z));\n"
    "    return sum / 9.0;\n"
    "}\n"
    "float shadowFactor(vec3 world, vec3 normal) {\n"
    "    int count = uShadowInfo.x;\n"
    "    if (count == 0) return 1.0;\n"
    "    int cascade = count;\n"
    "    for (int i = 0; i < count; ++i) {\n"
    "        if (vViewDepth <= uCascadeSplits[i]) { cascade = i; break; }\n"
    "    }\n"
    "    if (cascade >= count) return 1.0;\n"
    "    float s = sampleCascade(cascade, world, normal);\n"
    "    // Blend into the next cascade (or out to unshadowed after the last)\n"
    "    // across the final 10% of this one, so no seam shows.\n"
    "    float start = cascade == 0 ? 0.0 : uCascadeSplits[cascade - 1];\n"
    "    float band = 0.1 * (uCascadeSplits[cascade] - start);\n"
    "    float t = (vViewDepth - (uCascadeSplits[cascade] - band)) / band;\n"
    "    if (t > 0.0) {\n"
    "        float next = cascade + 1 < count ? sampleCascade(cascade + 1, world, normal) : 1.0;\n"
    "        s = mix(s, next, clamp(t, 0.0, 1.0));\n"
    "    }\n"
    "    return s;\n"
    "}\n"
    "void main() {\n"
    "    vec3 normal = normalize(vNormal);\n"
    "    vec3 toLight = uLight.xyz;\n"
    "    float ndl = max(dot(normal, toLight), 0.0);\n"
    "    float shadow = ndl > 0.0 ? mix(1.0, shadowFactor(vWorld, normal), uLight.w) : 1.0;\n"
    "    vec3 ambient = mix(uGroundAmbient.rgb, uSkyAmbient.rgb, normal.y * 0.5 + 0.5);\n"
    "    vec3 color = vColor.rgb * (ambient + uLightColor.rgb * ndl * shadow);\n"
    "    if (uSkyAmbient.w > 0.5) {\n"
    "        float d = length(vWorld - uEye.xyz);\n"
    "        float f = clamp((d - uFogColor.w) / max(uZenith.w - uFogColor.w, 1e-3), 0.0, 1.0);\n"
    "        color = mix(color, uFogColor.rgb, f * f * 0.85);\n"
    "    }\n"
    "    fragColor = vec4(color, vColor.a);\n"
    "}\n";

// Shadow pass: depth only, one cascade per draw, matrix from binding 1.
inline const char* kShadowVertex =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPosition;\n"
    "layout(location = 2) in vec3 iPosition;\n"
    "layout(location = 3) in vec3 iScale;\n"
    "layout(std140, binding = 1) uniform ShadowPass { mat4 uLightViewProjection; };\n"
    "void main() {\n"
    "    gl_Position = uLightViewProjection * vec4(aPosition * iScale + iPosition, 1.0);\n"
    "}\n";

inline const char* kShadowFragment =
    "#version 450 core\n"
    "void main() {}\n";

// Sky: a fullscreen triangle at the far plane, coloured by view direction.
inline const char* kSkyVertex =
    "#version 450 core\n"
    "out vec2 vNdc;\n"
    "void main() {\n"
    "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;\n"
    "    vNdc = p;\n"
    "    gl_Position = vec4(p, 1.0, 1.0);\n"
    "}\n";

inline const char* kSkyFragment =
    "#version 450 core\n"
    "in vec2 vNdc;\n" STRATA_FRAME_BLOCK
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec4 a = uInverseViewProjection * vec4(vNdc, 0.5, 1.0);\n"
    "    vec4 b = uInverseViewProjection * vec4(vNdc, 1.0, 1.0);\n"
    "    vec3 dir = normalize(b.xyz / b.w - a.xyz / a.w);\n"
    "    vec3 color;\n"
    "    if (dir.y >= 0.0) {\n"
    "        color = mix(uHorizon.rgb, uZenith.rgb, pow(dir.y, 0.55));\n"
    "    } else {\n"
    "        color = mix(uHorizon.rgb, uGroundAmbient.rgb, clamp(-dir.y * 3.0, 0.0, 1.0));\n"
    "    }\n"
    "    float towardsSun = max(dot(dir, uToSun.xyz), 0.0);\n"
    "    float sunUp = smoothstep(-0.05, 0.02, uToSun.w);\n"
    "    color += vec3(1.0, 0.85, 0.6) * pow(towardsSun, 48.0) * 0.35 * sunUp;\n"
    "    color += vec3(1.0, 0.97, 0.90) * smoothstep(0.9990, 0.9994, towardsSun) * sunUp;\n"
    "    fragColor = vec4(color, 1.0);\n"
    "}\n";

#undef STRATA_FRAME_BLOCK

}  // namespace strata::render::shaders
