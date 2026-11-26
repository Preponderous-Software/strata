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
    "    vec4 uOutput;         // x exposure, y STRATA_OUTPUT_* flags (ABI 1.20)\n" \
    "};\n"

// ABI 1.20: the colour output of strata_set_color_output, after the Frame
// block. With the defaults (flags 0, exposure 1) it returns its input exactly.
#define STRATA_OUTPUT_FUNCTIONS                                                    \
    "vec3 strataSrgbToLinear(vec3 c) {\n"                                          \
    "    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));\n" \
    "}\n"                                                                          \
    "bool strataOutputSrgb() { return (int(uOutput.y) & 1) != 0; }\n"             \
    "vec3 strataOutput(vec3 c) {\n"                                                \
    "    int flags = int(uOutput.y);\n"                                            \
    "    if (flags == 0 && uOutput.x == 1.0) return c;\n"                          \
    "    c *= uOutput.x;\n"                                                        \
    "    if ((flags & 2) != 0) {\n"                                                \
    "        c = c / (1.0 + max(c, vec3(0.0)));\n"                                 \
    "    } else if ((flags & 4) != 0) {\n"                                         \
    "        c = max(c, vec3(0.0));\n"                                             \
    "        c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);\n" \
    "    }\n"                                                                      \
    "    if ((flags & 1) != 0) {\n"                                                \
    "        c = clamp(c, 0.0, 1.0);\n"                                            \
    "        c = mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));\n" \
    "    }\n"                                                                      \
    "    return c;\n"                                                              \
    "}\n"

// std140 block of point lights (binding 2); mirrored by PointLightUniforms in
// renderer.cpp. Colour is premultiplied by intensity.
#define STRATA_POINT_LIGHT_BLOCK                                                   \
    "layout(std140, binding = 2) uniform PointLights {\n"                          \
    "    ivec4 uPointInfo;              // x light count, y cube shadow map size, z spot count\n" \
    "    vec4 uPointPositionRange[32];  // xyz position, w range\n"                 \
    "    vec4 uPointColor[32];          // rgb colour * intensity, w shadow slot or -1\n" \
    "    vec4 uSpotPositionRange[32];   // ABI 1.20 spot lights: xyz position, w range\n" \
    "    vec4 uSpotColor[32];           // rgb colour * intensity, w shadow slot or -1\n" \
    "    vec4 uSpotDirection[32];       // xyz the way it shines (unit), w cos(outer cone)\n" \
    "    vec4 uSpotCone[32];            // x cos(inner cone)\n"                     \
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

// Terrain (T1): one static mesh, already in world space, with a colour per
// vertex. Writes the same outputs as kPrimitiveVertex, so both opaque fragment
// shaders serve it unchanged.
inline const char* kTerrainVertex =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPosition;\n"
    "layout(location = 1) in vec3 aNormal;\n"
    "layout(location = 5) in vec3 aColor;\n" STRATA_FRAME_BLOCK
    "out vec3 vNormal;\n"
    "out vec4 vColor;\n"
    "out vec3 vWorld;\n"
    "out float vViewDepth;\n"
    "void main() {\n"
    "    vec4 world = vec4(aPosition, 1.0);\n"
    "    vNormal = aNormal;\n"
    "    vColor = vec4(aColor, 1.0);\n"
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
    "in float vViewDepth;\n" STRATA_FRAME_BLOCK STRATA_OUTPUT_FUNCTIONS
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec3 normal = normalize(vNormal);\n"
    "    float diffuse = max(dot(normal, -uLight.xyz), 0.0);\n"
    "    float light = uLight.w + (1.0 - uLight.w) * diffuse;\n"
    "    fragColor = vec4(strataOutput(vColor.rgb * light), vColor.a);\n"
    "}\n";

// SUN lighting: key light (sun or moon) with cascaded shadows, hemisphere
// ambient and distance fog.
inline const char* kSunFragment =
    "#version 450 core\n"
    "in vec3 vNormal;\n"
    "in vec4 vColor;\n"
    "in vec3 vWorld;\n"
    "in float vViewDepth;\n" STRATA_FRAME_BLOCK STRATA_OUTPUT_FUNCTIONS
    "layout(binding = 1) uniform sampler2DArrayShadow uShadowMap;\n"
    "layout(binding = 2) uniform samplerCubeArrayShadow uPointShadowMaps;\n" STRATA_POINT_LIGHT_BLOCK
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
    "// Cube shadow map lookup (L4): the map stores distance / range. A normal\n"
    "// offset that grows as the light grazes the surface, plus a distance bias,\n"
    "// keep surfaces from shadowing themselves; nine taps soften the edge.\n"
    "float pointShadow(int slot, vec3 world, vec3 normal, float ndl, vec3 light, float range) {\n"
    "    float size = float(uPointInfo.y);\n"
    "    float texel = 2.0 * length(world - light) / size;\n"
    "    vec3 v = world + normal * (texel * (1.0 + 3.0 * (1.0 - ndl))) - light;\n"
    "    float dist = length(v);\n"
    "    vec3 n = v / max(dist, 1e-4);\n"
    "    float ref = (dist - 1.5 * texel) / range;\n"
    "    vec3 t = normalize(cross(n, abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));\n"
    "    vec3 b = cross(n, t);\n"
    "    float spread = 3.0 / size;\n"
    "    float sum = 0.0;\n"
    "    for (int x = -1; x <= 1; ++x)\n"
    "        for (int y = -1; y <= 1; ++y)\n"
    "            sum += texture(uPointShadowMaps, vec4(n + (t * float(x) + b * float(y)) * spread, float(slot)), ref);\n"
    "    return sum / 9.0;\n"
    "}\n"
    "// Point lights: the falloff of render/lights.cpp, Lambert diffuse.\n"
    "vec3 pointLights(vec3 world, vec3 normal) {\n"
    "    vec3 sum = vec3(0.0);\n"
    "    for (int i = 0; i < uPointInfo.x; ++i) {\n"
    "        vec3 toPoint = uPointPositionRange[i].xyz - world;\n"
    "        float d = length(toPoint);\n"
    "        float r = d / uPointPositionRange[i].w;\n"
    "        float r2 = r * r;\n"
    "        float window = clamp(1.0 - r2 * r2, 0.0, 1.0);\n"
    "        float falloff = window * window / (1.0 + 16.0 * r2);\n"
    "        float ndl = max(dot(normal, toPoint / max(d, 1e-4)), 0.0);\n"
    "        int slot = int(uPointColor[i].w);\n"
    "        if (slot >= 0 && falloff * ndl > 0.0)\n"
    "            ndl *= pointShadow(slot, world, normal, ndl, uPointPositionRange[i].xyz, uPointPositionRange[i].w);\n"
    "        sum += uPointColor[i].rgb * (falloff * ndl);\n"
    "    }\n"
    "    // Spot lights (ABI 1.20): the same falloff times the cone factor of\n"
    "    // render/lights.cpp; shadowed ones share the cube shadow maps.\n"
    "    for (int i = 0; i < uPointInfo.z; ++i) {\n"
    "        vec3 toPoint = uSpotPositionRange[i].xyz - world;\n"
    "        float d = length(toPoint);\n"
    "        vec3 l = toPoint / max(d, 1e-4);\n"
    "        float r = d / uSpotPositionRange[i].w;\n"
    "        float r2 = r * r;\n"
    "        float window = clamp(1.0 - r2 * r2, 0.0, 1.0);\n"
    "        float cone = clamp((dot(-l, uSpotDirection[i].xyz) - uSpotDirection[i].w) /\n"
    "                           max(uSpotCone[i].x - uSpotDirection[i].w, 1e-4), 0.0, 1.0);\n"
    "        float falloff = window * window / (1.0 + 16.0 * r2) * cone * cone;\n"
    "        float ndl = max(dot(normal, l), 0.0);\n"
    "        int slot = int(uSpotColor[i].w);\n"
    "        if (slot >= 0 && falloff * ndl > 0.0)\n"
    "            ndl *= pointShadow(slot, world, normal, ndl, uSpotPositionRange[i].xyz, uSpotPositionRange[i].w);\n"
    "        sum += uSpotColor[i].rgb * (falloff * ndl);\n"
    "    }\n"
    "    return sum;\n"
    "}\n"
    "void main() {\n"
    "    vec3 normal = normalize(vNormal);\n"
    "    vec3 toLight = uLight.xyz;\n"
    "    float ndl = max(dot(normal, toLight), 0.0);\n"
    "    float shadow = ndl > 0.0 ? mix(1.0, shadowFactor(vWorld, normal), uLight.w) : 1.0;\n"
    "    vec3 ambient = mix(uGroundAmbient.rgb, uSkyAmbient.rgb, normal.y * 0.5 + 0.5);\n"
    "    vec3 color = vColor.rgb * (ambient + uLightColor.rgb * ndl * shadow + pointLights(vWorld, normal));\n"
    "    if (uSkyAmbient.w > 0.5) {\n"
    "        float d = length(vWorld - uEye.xyz);\n"
    "        float f = clamp((d - uFogColor.w) / max(uZenith.w - uFogColor.w, 1e-3), 0.0, 1.0);\n"
    "        vec3 fogColor = strataOutputSrgb() ? strataSrgbToLinear(uFogColor.rgb) : uFogColor.rgb;\n"
    "        color = mix(color, fogColor, f * f * 0.85);\n"
    "    }\n"
    "    fragColor = vec4(strataOutput(color), vColor.a);\n"
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

inline const char* kTerrainShadowVertex =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPosition;\n"
    "layout(std140, binding = 1) uniform ShadowPass { mat4 uLightViewProjection; };\n"
    "void main() {\n"
    "    gl_Position = uLightViewProjection * vec4(aPosition, 1.0);\n"
    "}\n";

inline const char* kShadowFragment =
    "#version 450 core\n"
    "void main() {}\n";

// Point shadow pass (L4): one cube face per draw. Depth is distance / range,
// so lookups compare distances rather than projected depth.
#define STRATA_POINT_SHADOW_BLOCK \
    "layout(std140, binding = 1) uniform PointShadowPass { mat4 uFaceViewProjection; vec4 uLightPositionRange; };\n"

inline const char* kPointShadowVertex =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPosition;\n"
    "layout(location = 2) in vec3 iPosition;\n"
    "layout(location = 3) in vec3 iScale;\n" STRATA_POINT_SHADOW_BLOCK
    "out vec3 vWorld;\n"
    "void main() {\n"
    "    vec4 world = vec4(aPosition * iScale + iPosition, 1.0);\n"
    "    vWorld = world.xyz;\n"
    "    gl_Position = uFaceViewProjection * world;\n"
    "}\n";

inline const char* kTerrainPointShadowVertex =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPosition;\n" STRATA_POINT_SHADOW_BLOCK
    "out vec3 vWorld;\n"
    "void main() {\n"
    "    vWorld = aPosition;\n"
    "    gl_Position = uFaceViewProjection * vec4(aPosition, 1.0);\n"
    "}\n";

inline const char* kPointShadowFragment =
    "#version 450 core\n"
    "in vec3 vWorld;\n" STRATA_POINT_SHADOW_BLOCK
    "void main() {\n"
    "    gl_FragDepth = length(vWorld - uLightPositionRange.xyz) / uLightPositionRange.w;\n"
    "}\n";

// Static meshes (ABI 1.4, transformed and textured since 1.7): the terrain's
// attributes plus a texture coordinate, a model matrix and the inverse
// transpose of its upper 3x3 for normals (binding 3, one range per mesh).
// ABI 1.20 added the material: uMeshMaterial is x alpha cutoff (0 off), y
// normal-map scale, z 1 for STRATA_MATERIAL_DOUBLE_SIDED, w 1 when a normal
// map is bound (unit 9); uMeshEmissive is rgb emissive * strength, w 1 when an
// emissive texture is bound (unit 8).
#define STRATA_MESH_BLOCK \
    "layout(std140, binding = 3) uniform Mesh { mat4 uModel; mat4 uNormalMatrix; vec4 uMeshParams;\n" \
    "    vec4 uMeshMaterial; vec4 uMeshEmissive; };\n"

// The Mesh block for fragment shaders that read uMeshParams (x: the alpha of a
// STRATA_MESH_TRANSLUCENT mesh, ABI 1.11) or the material (ABI 1.20).
inline const char* kMeshBlock = STRATA_MESH_BLOCK;

// ABI 1.20: what renderer.cpp splices into the lit fragment shaders to make
// the mesh variants (after the Frame block, so uOutput is declared): the
// material's samplers, its emission and its normal (two-sided faces turned
// to the viewer, then the normal map in a tangent frame from screen-space
// derivatives, after Schueler's "Normal Mapping without Precomputed
// Tangents"; the map's green points towards the image's first row, v = 0,
// hence -b).
inline const char* kMeshMaterialFunctions =
    "layout(binding = 8) uniform sampler2D uEmissive;\n"
    "layout(binding = 9) uniform sampler2D uNormalMap;\n"
    "vec3 strataMeshEmission() {\n"
    "    vec3 e = uMeshEmissive.rgb;\n"
    "    if (uMeshEmissive.w > 0.5) e *= texture(uEmissive, vUv).rgb;\n"
    "    return e;\n"
    "}\n"
    "vec3 strataMeshNormal() {\n"
    "    vec3 n = normalize(vNormal);\n"
    "    if (uMeshMaterial.z > 0.5 && !gl_FrontFacing) n = -n;\n"
    "    if (uMeshMaterial.w > 0.5) {\n"
    "        vec3 m = texture(uNormalMap, vUv).xyz * 2.0 - 1.0;\n"
    "        m.xy *= uMeshMaterial.y;\n"
    "        vec3 dp1 = dFdx(vWorld), dp2 = dFdy(vWorld);\n"
    "        vec2 duv1 = dFdx(vUv), duv2 = dFdy(vUv);\n"
    "        vec3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);\n"
    "        vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;\n"
    "        vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;\n"
    "        float k = inversesqrt(max(max(dot(t, t), dot(b, b)), 1e-30));\n"
    "        vec3 mapped = mat3(t * k, -b * k, n) * m;\n"
    "        if (dot(mapped, mapped) > 1e-12) n = normalize(mapped);\n"
    "    }\n"
    "    return n;\n"
    "}\n";

inline const char* kMeshVertex =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPosition;\n"
    "layout(location = 1) in vec3 aNormal;\n"
    "layout(location = 5) in vec3 aColor;\n"
    "layout(location = 6) in vec2 aUv;\n" STRATA_FRAME_BLOCK STRATA_MESH_BLOCK
    "out vec3 vNormal;\n"
    "out vec4 vColorIn;\n"
    "out vec2 vUv;\n"
    "out vec3 vWorld;\n"
    "out float vViewDepth;\n"
    "void main() {\n"
    "    vec4 world = uModel * vec4(aPosition, 1.0);\n"
    "    vNormal = mat3(uNormalMatrix) * aNormal;\n"
    "    vColorIn = vec4(aColor, 1.0);\n"
    "    vUv = aUv;\n"
    "    vWorld = world.xyz;\n"
    "    vViewDepth = -(uView * world).z;\n"
    "    gl_Position = uProjection * uView * world;\n"
    "}\n";

// The line renderer.cpp replaces with the alpha-cutout discard in the cutout
// variants of the mesh shaders (ABI 1.20), and with nothing otherwise.
inline const char* kCutoutMarker = "    // strata:cutout\n";
inline const char* kCutoutDiscard = "    if (texel.a < uMeshMaterial.x) discard;\n";

// STRATA_MESH_UNLIT: colour times texture, no lighting, no fog; plus the
// emission (ABI 1.20).
inline const char* kMeshUnlitFragment =
    "#version 450 core\n"
    "in vec4 vColorIn;\n"
    "in vec2 vUv;\n"
    "layout(binding = 4) uniform sampler2D uAlbedo;\n"
    "layout(binding = 8) uniform sampler2D uEmissive;\n" STRATA_FRAME_BLOCK STRATA_OUTPUT_FUNCTIONS STRATA_MESH_BLOCK
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec4 texel = texture(uAlbedo, vUv);\n"
    "    // strata:cutout\n"
    "    vec3 e = uMeshEmissive.rgb;\n"
    "    if (uMeshEmissive.w > 0.5) e *= texture(uEmissive, vUv).rgb;\n"
    "    fragColor = vec4(strataOutput(vColorIn.rgb * texel.rgb + e), 1.0);\n"
    "}\n";

// ABI 1.11: STRATA_MESH_UNLIT | STRATA_MESH_TRANSLUCENT: colour times texture,
// with the texel's alpha times the mesh's alpha.
inline const char* kMeshUnlitTranslucentFragment =
    "#version 450 core\n"
    "in vec4 vColorIn;\n"
    "in vec2 vUv;\n"
    "layout(binding = 4) uniform sampler2D uAlbedo;\n"
    "layout(binding = 8) uniform sampler2D uEmissive;\n" STRATA_FRAME_BLOCK STRATA_OUTPUT_FUNCTIONS STRATA_MESH_BLOCK
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec4 texel = texture(uAlbedo, vUv);\n"
    "    // strata:cutout\n"
    "    vec3 e = uMeshEmissive.rgb;\n"
    "    if (uMeshEmissive.w > 0.5) e *= texture(uEmissive, vUv).rgb;\n"
    "    fragColor = vec4(strataOutput(vColorIn.rgb * texel.rgb + e), texel.a * uMeshParams.x);\n"
    "}\n";

inline const char* kMeshShadowVertex =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPosition;\n"
    "layout(std140, binding = 1) uniform ShadowPass { mat4 uLightViewProjection; };\n" STRATA_MESH_BLOCK
    "void main() {\n"
    "    gl_Position = uLightViewProjection * (uModel * vec4(aPosition, 1.0));\n"
    "}\n";

inline const char* kMeshPointShadowVertex =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPosition;\n" STRATA_POINT_SHADOW_BLOCK STRATA_MESH_BLOCK
    "out vec3 vWorld;\n"
    "void main() {\n"
    "    vec4 world = uModel * vec4(aPosition, 1.0);\n"
    "    vWorld = world.xyz;\n"
    "    gl_Position = uFaceViewProjection * world;\n"
    "}\n";

// ABI 1.20: alpha-cutout meshes in the shadow passes carry their uvs and
// discard where the albedo texel's alpha is below the cutoff, so light falls
// through the holes.
inline const char* kMeshShadowCutoutVertex =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPosition;\n"
    "layout(location = 6) in vec2 aUv;\n"
    "layout(std140, binding = 1) uniform ShadowPass { mat4 uLightViewProjection; };\n" STRATA_MESH_BLOCK
    "out vec2 vUv;\n"
    "void main() {\n"
    "    vUv = aUv;\n"
    "    gl_Position = uLightViewProjection * (uModel * vec4(aPosition, 1.0));\n"
    "}\n";

inline const char* kMeshShadowCutoutFragment =
    "#version 450 core\n"
    "in vec2 vUv;\n"
    "layout(binding = 4) uniform sampler2D uAlbedo;\n" STRATA_MESH_BLOCK
    "void main() {\n"
    "    if (texture(uAlbedo, vUv).a < uMeshMaterial.x) discard;\n"
    "}\n";

inline const char* kMeshPointShadowCutoutVertex =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPosition;\n"
    "layout(location = 6) in vec2 aUv;\n" STRATA_POINT_SHADOW_BLOCK STRATA_MESH_BLOCK
    "out vec3 vWorld;\n"
    "out vec2 vUv;\n"
    "void main() {\n"
    "    vec4 world = uModel * vec4(aPosition, 1.0);\n"
    "    vWorld = world.xyz;\n"
    "    vUv = aUv;\n"
    "    gl_Position = uFaceViewProjection * world;\n"
    "}\n";

inline const char* kMeshPointShadowCutoutFragment =
    "#version 450 core\n"
    "in vec3 vWorld;\n"
    "in vec2 vUv;\n"
    "layout(binding = 4) uniform sampler2D uAlbedo;\n" STRATA_POINT_SHADOW_BLOCK STRATA_MESH_BLOCK
    "void main() {\n"
    "    if (texture(uAlbedo, vUv).a < uMeshMaterial.x) discard;\n"
    "    gl_FragDepth = length(vWorld - uLightPositionRange.xyz) / uLightPositionRange.w;\n"
    "}\n";

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
    "in vec2 vNdc;\n" STRATA_FRAME_BLOCK STRATA_OUTPUT_FUNCTIONS
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
    "    if (strataOutputSrgb()) color = strataSrgbToLinear(color);  // ABI 1.20: the sky keeps its look\n"
    "    fragColor = vec4(strataOutput(color), 1.0);\n"
    "}\n";

// Overlays (ABI 1.5). Viewport size for pixel maths, binding 3.
#define STRATA_OVERLAY_BLOCK \
    "layout(std140, binding = 3) uniform OverlayViewport { vec4 uViewport; };  // w, h, 1/w, 1/h\n"

inline const char* kLineVertex =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPosition;\n"
    "layout(location = 1) in vec4 aColor;\n" STRATA_FRAME_BLOCK
    "out vec4 vColor;\n"
    "void main() {\n"
    "    vColor = aColor;\n"
    "    gl_Position = uProjection * uView * vec4(aPosition, 1.0);\n"
    "}\n";

inline const char* kLineFragment =
    "#version 450 core\n"
    "in vec4 vColor;\n"
    "out vec4 fragColor;\n"
    "void main() { fragColor = vColor; }\n";

// Points: one camera-facing quad per instance, six vertices from gl_VertexID.
// The diameter is max(size_pixels, projected world_diameter); the quad is one
// pixel larger all round so the edge can be antialiased.
inline const char* kPointVertex =
    "#version 450 core\n"
    "layout(location = 0) in vec3 iPosition;\n"
    "layout(location = 1) in vec2 iSize;  // size_pixels, world_diameter\n"
    "layout(location = 2) in vec4 iColor;\n" STRATA_FRAME_BLOCK STRATA_OVERLAY_BLOCK
    "out vec4 vColor;\n"
    "out vec2 vCorner;\n"
    "out float vRadius;\n"
    "const vec2 kCorners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));\n"
    "void main() {\n"
    "    vec4 clip = uProjection * uView * vec4(iPosition, 1.0);\n"
    "    vec2 corner = kCorners[gl_VertexID];\n"
    "    vCorner = corner;\n"
    "    vColor = iColor;\n"
    "    if (clip.w <= 0.0) {  // behind the eye\n"
    "        vRadius = 0.0;\n"
    "        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    float projected = iSize.y * uProjection[1][1] * 0.5 * uViewport.y / clip.w;\n"
    "    float diameter = min(max(iSize.x, projected), 4.0 * max(uViewport.x, uViewport.y));\n"
    "    vRadius = 0.5 * diameter;\n"
    "    vec2 offset = corner * (vRadius + 1.0) * 2.0 * uViewport.zw;\n"
    "    gl_Position = clip + vec4(offset * clip.w, 0.0, 0.0);\n"
    "}\n";

inline const char* kSolidPointFragment =
    "#version 450 core\n"
    "in vec4 vColor;\n"
    "in vec2 vCorner;\n"
    "in float vRadius;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    float r = length(vCorner) * (vRadius + 1.0);\n"
    "    float a = clamp(vRadius + 0.5 - r, 0.0, 1.0);\n"
    "    if (a <= 0.0) discard;\n"
    "    fragColor = vec4(vColor.rgb, vColor.a * a);\n"
    "}\n";

inline const char* kSoftPointFragment =
    "#version 450 core\n"
    "in vec4 vColor;\n"
    "in vec2 vCorner;\n"
    "in float vRadius;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    float t = clamp(length(vCorner) * (vRadius + 1.0) / max(vRadius, 0.5), 0.0, 1.0);\n"
    "    float a = (1.0 - t) * (1.0 - t);\n"
    "    if (a <= 0.0) discard;\n"
    "    fragColor = vec4(vColor.rgb, vColor.a * a);\n"
    "}\n";

// Wide lines (ABI 1.13): one camera-facing quad per segment, six vertices
// from gl_VertexID, each instance two StrataWideLineVertex ends. An end behind
// the eye is first moved along the segment to just in front of it. The quad
// is the segment's width plus one pixel all round and, for a round cap, half
// the width longer at each end; the fragment shader works out coverage from
// the window-space ends, so the edge is antialiased like a point's.
inline const char* kWideLineVertex =
    "#version 450 core\n"
    "layout(location = 0) in vec3 iP0;\n"
    "layout(location = 1) in vec4 iC0;\n"
    "layout(location = 2) in vec2 iW0;  // width_pixels, world_width\n"
    "layout(location = 3) in vec3 iP1;\n"
    "layout(location = 4) in vec4 iC1;\n"
    "layout(location = 5) in vec2 iW1;\n" STRATA_FRAME_BLOCK STRATA_OVERLAY_BLOCK
    "flat out vec4 vEnds;  // window-space ends, pixels\n"
    "flat out vec2 vHalf;  // half width at each end, pixels\n"
    "flat out vec4 vC0;\n"
    "flat out vec4 vC1;\n"
    "const vec2 kCorners[6] = vec2[](vec2(0, -1), vec2(1, -1), vec2(1, 1), vec2(0, -1), vec2(1, 1), vec2(0, 1));\n"
    "const float kMinW = 1e-3;\n"
    "float halfWidth(vec2 w, float clipW) {\n"
    "    float projected = w.y * uProjection[1][1] * 0.5 * uViewport.y / clipW;\n"
    "    return 0.5 * min(max(w.x, projected), 4.0 * max(uViewport.x, uViewport.y));\n"
    "}\n"
    "void main() {\n"
    "    mat4 viewProjection = uProjection * uView;\n"
    "    vec4 a = viewProjection * vec4(iP0, 1.0);\n"
    "    vec4 b = viewProjection * vec4(iP1, 1.0);\n"
    "    vec4 ca = iC0, cb = iC1;\n"
    "    vec2 wa = iW0, wb = iW1;\n"
    "    vEnds = vec4(0.0);\n"
    "    vHalf = vec2(0.0);\n"
    "    vC0 = ca;\n"
    "    vC1 = cb;\n"
    "    if (a.w < kMinW && b.w < kMinW) {  // wholly behind the eye\n"
    "        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    if (a.w < kMinW) {\n"
    "        float t = (kMinW - a.w) / (b.w - a.w);\n"
    "        a = mix(a, b, t); ca = mix(ca, cb, t); wa = mix(wa, wb, t);\n"
    "    } else if (b.w < kMinW) {\n"
    "        float t = (kMinW - b.w) / (a.w - b.w);\n"
    "        b = mix(b, a, t); cb = mix(cb, ca, t); wb = mix(wb, wa, t);\n"
    "    }\n"
    "    vec2 sa = (a.xy / a.w * 0.5 + 0.5) * uViewport.xy;\n"
    "    vec2 sb = (b.xy / b.w * 0.5 + 0.5) * uViewport.xy;\n"
    "    float ha = halfWidth(wa, a.w), hb = halfWidth(wb, b.w);\n"
    "    vec2 d = sb - sa;\n"
    "    float len = length(d);\n"
    "    vec2 dir = len > 1e-4 ? d / len : vec2(1.0, 0.0);\n"
    "    vec2 normal = vec2(-dir.y, dir.x);\n"
    "    vec2 corner = kCorners[gl_VertexID];\n"
    "    bool atB = corner.x > 0.5;\n"
    "    vec4 clip = atB ? b : a;\n"
    "    float h = atB ? hb : ha;\n"
    "    vec2 offset = normal * corner.y * (h + 1.0) + dir * (atB ? 1.0 : -1.0) * (h + 1.0);\n"
    "    vEnds = vec4(sa, sb);\n"
    "    vHalf = vec2(ha, hb);\n"
    "    vC0 = ca;\n"
    "    vC1 = cb;\n"
    "    gl_Position = clip + vec4(offset * 2.0 * uViewport.zw * clip.w, 0.0, 0.0);\n"
    "}\n";

#define STRATA_WIDE_LINE_FRAGMENT_HEAD                                                      \
    "#version 450 core\n"                                                                   \
    "flat in vec4 vEnds;\n"                                                                 \
    "flat in vec2 vHalf;\n"                                                                 \
    "flat in vec4 vC0;\n"                                                                   \
    "flat in vec4 vC1;\n"                                                                   \
    "out vec4 fragColor;\n"                                                                 \
    "void main() {\n"                                                                       \
    "    vec2 rel = gl_FragCoord.xy - vEnds.xy;\n"                                          \
    "    vec2 d = vEnds.zw - vEnds.xy;\n"                                                   \
    "    float len2 = dot(d, d);\n"                                                         \
    "    float t = len2 > 1e-8 ? clamp(dot(rel, d) / len2, 0.0, 1.0) : 0.0;\n"              \
    "    float h = mix(vHalf.x, vHalf.y, t);\n"

#define STRATA_WIDE_LINE_FRAGMENT_TAIL                                                      \
    "    if (a <= 0.0) discard;\n"                                                          \
    "    vec4 c = mix(vC0, vC1, t);\n"                                                      \
    "    fragColor = vec4(c.rgb, c.a * a);\n"                                               \
    "}\n"

// Square ends: coverage across the segment times coverage along it.
inline const char* kWideLineFragment =
    STRATA_WIDE_LINE_FRAGMENT_HEAD
    "    float len = sqrt(len2);\n"
    "    vec2 dir = len > 1e-4 ? d / len : vec2(1.0, 0.0);\n"
    "    float across = abs(rel.x * dir.y - rel.y * dir.x);\n"
    "    float along = dot(rel, dir);\n"
    "    float a = clamp(h + 0.5 - across, 0.0, 1.0) * clamp(0.5 + min(along, len - along), 0.0, 1.0);\n"
    STRATA_WIDE_LINE_FRAGMENT_TAIL;

// Round caps: distance to the nearest point of the segment.
inline const char* kWideLineRoundFragment =
    STRATA_WIDE_LINE_FRAGMENT_HEAD
    "    float a = clamp(h + 0.5 - length(rel - d * t), 0.0, 1.0);\n"
    STRATA_WIDE_LINE_FRAGMENT_TAIL;

#undef STRATA_WIDE_LINE_FRAGMENT_HEAD
#undef STRATA_WIDE_LINE_FRAGMENT_TAIL

// Text: one quad per glyph, in pixels with the origin at the top left. The
// font atlas is read texel by texel, so whole-number scales stay sharp.
inline const char* kTextVertex =
    "#version 450 core\n"
    "layout(location = 0) in vec4 iRect;  // x, y, size, glyph\n"
    "layout(location = 1) in vec4 iColor;\n" STRATA_OVERLAY_BLOCK
    "out vec4 vColor;\n"
    "out vec2 vLocal;\n"
    "flat out int vGlyph;\n"
    "const vec2 kCorners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));\n"
    "void main() {\n"
    "    vec2 corner = kCorners[gl_VertexID];\n"
    "    vec2 pixel = iRect.xy + corner * iRect.z;\n"
    "    vColor = iColor;\n"
    "    vLocal = corner * 8.0;\n"
    "    vGlyph = int(iRect.w + 0.5);\n"
    "    gl_Position = vec4(pixel.x * 2.0 * uViewport.z - 1.0, 1.0 - pixel.y * 2.0 * uViewport.w, 0.0, 1.0);\n"
    "}\n";

inline const char* kTextFragment =
    "#version 450 core\n"
    "in vec4 vColor;\n"
    "in vec2 vLocal;\n"
    "flat in int vGlyph;\n"
    "layout(binding = 3) uniform sampler2D uFont;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    ivec2 texel = ivec2(vGlyph * 8 + min(int(vLocal.x), 7), min(int(vLocal.y), 7));\n"
    "    if (texelFetch(uFont, texel, 0).r < 0.5) discard;\n"
    "    fragColor = vColor;\n"
    "}\n";

// Filled rectangles (ABI 1.11): one quad per instance, in pixels with the
// origin at the top left, drawn in the text pass before the glyphs.
inline const char* kRectVertex =
    "#version 450 core\n"
    "layout(location = 0) in vec4 iRect;  // x, y, width, height\n"
    "layout(location = 1) in vec4 iColor;\n" STRATA_OVERLAY_BLOCK
    "out vec4 vColor;\n"
    "const vec2 kCorners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));\n"
    "void main() {\n"
    "    vec2 pixel = iRect.xy + kCorners[gl_VertexID] * iRect.zw;\n"
    "    vColor = iColor;\n"
    "    gl_Position = vec4(pixel.x * 2.0 * uViewport.z - 1.0, 1.0 - pixel.y * 2.0 * uViewport.w, 0.0, 1.0);\n"
    "}\n";

inline const char* kRectFragment =
    "#version 450 core\n"
    "in vec4 vColor;\n"
    "out vec4 fragColor;\n"
    "void main() { fragColor = vColor; }\n";

// Depth readback (ABI 1.9): a fullscreen triangle that copies the depth
// attachment's float bits into RGBA8, low byte in red, so the value survives
// an RGBA8 readback exactly on every device.
inline const char* kDepthPackVertex =
    "#version 450 core\n"
    "void main() {\n"
    "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;\n"
    "    gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";

inline const char* kDepthPackFragment =
    "#version 450 core\n"
    "layout(binding = 0) uniform sampler2D uDepth;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    uint bits = floatBitsToUint(texelFetch(uDepth, ivec2(gl_FragCoord.xy), 0).r);\n"
    "    fragColor = vec4(uvec4(bits & 255u, (bits >> 8) & 255u, (bits >> 16) & 255u, bits >> 24)) / 255.0;\n"
    "}\n";

#undef STRATA_FRAME_BLOCK
#undef STRATA_OUTPUT_FUNCTIONS
#undef STRATA_POINT_LIGHT_BLOCK
#undef STRATA_POINT_SHADOW_BLOCK
#undef STRATA_MESH_BLOCK
#undef STRATA_OVERLAY_BLOCK

}  // namespace strata::render::shaders
