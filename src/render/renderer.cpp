#include "render/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <glm/gtc/type_ptr.hpp>

#include "core/error.hpp"
#include "core/log.hpp"
#include "render/clusters.hpp"
#include "render/depth_readback.hpp"
#include "render/lights.hpp"
#include "render/pass_list.hpp"
#include "render/primitive_mesh.hpp"
#include "render/shaders.hpp"
#include "render/sun.hpp"
#include "render/static_mesh.hpp"
#include "render/terrain_mesh.hpp"

namespace strata::render {
namespace {

using Clock = std::chrono::steady_clock;

float msSince(Clock::time_point start) {
    return std::chrono::duration<float, std::milli>(Clock::now() - start).count();
}

// std140 mirror of the Frame block in shaders.hpp.
struct FrameUniforms {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 inverseViewProjection;
    glm::mat4 cascadeViewProjection[kMaxCascades];
    glm::vec4 eye;
    glm::vec4 light;
    glm::vec4 lightColor;
    glm::vec4 skyAmbient;
    glm::vec4 groundAmbient;
    glm::vec4 fogColor;
    glm::vec4 zenith;
    glm::vec4 horizon;
    glm::vec4 toSun;
    glm::vec4 cascadeSplits;
    glm::vec4 cascadeTexel;
    glm::ivec4 shadowInfo;
    glm::vec4 output;  // ABI 1.20: x exposure, y STRATA_OUTPUT_* flags
};
static_assert(sizeof(FrameUniforms) == 656, "FrameUniforms must match the std140 Frame block");

// std140 mirror of the PointLights block in shaders.hpp.
struct PointLightUniforms {
    glm::ivec4 info;  // x point lights, y cube map size, z spot lights (ABI 1.20)
    glm::vec4 positionRange[STRATA_MAX_POINT_LIGHTS];
    glm::vec4 color[STRATA_MAX_POINT_LIGHTS];
    glm::vec4 spotPositionRange[STRATA_MAX_SPOT_LIGHTS];
    glm::vec4 spotColor[STRATA_MAX_SPOT_LIGHTS];
    glm::vec4 spotDirection[STRATA_MAX_SPOT_LIGHTS];  // w: cos(outer)
    glm::vec4 spotCone[STRATA_MAX_SPOT_LIGHTS];       // x: cos(inner)
    glm::ivec4 clusterGrid;      // ABI 1.21: xy tiles, z slices, w 1 under clustered culling
    glm::ivec4 clusterLayout;    // x first header texel, y first index texel
    glm::vec4 clusterDepth;      // x near, y slice scale, z 1 if orthographic
    glm::vec4 clusterViewport;   // xy 1 / viewport size
};
static_assert(STRATA_MAX_POINT_LIGHTS == 32 && STRATA_MAX_SPOT_LIGHTS == 32,
              "the PointLights block in shaders.hpp has arrays of 32");
static_assert(sizeof(PointLightUniforms) == 16 + 2 * 16 * STRATA_MAX_POINT_LIGHTS + 4 * 16 * STRATA_MAX_SPOT_LIGHTS + 4 * 16,
              "PointLightUniforms must match the std140 PointLights block");

constexpr uint32_t kInstanceStride = sizeof(StrataRenderItem);

// std140 mirror of the Mesh block in shaders.hpp.
struct MeshUniforms {
    glm::mat4 model;
    glm::mat4 normalMatrix;  // inverse transpose of model's upper 3x3, in a mat4
    glm::vec4 params;        // x: alpha of a STRATA_MESH_TRANSLUCENT mesh (ABI 1.11)
    glm::vec4 material;      // ABI 1.20: x cutoff, y normal scale, z double-sided, w normal map bound
    glm::vec4 emissive;      // ABI 1.20: rgb emissive * strength, w emissive texture bound
};
static_assert(kInstanceStride == 44, "StrataRenderItem must stay 44 bytes (Beyond Nations' RenderItem)");

bool finite(float v) { return std::isfinite(v); }

void replaceOnce(std::string& s, const std::string& from, const std::string& to) {
    const auto at = s.find(from);
    if (at == std::string::npos || s.find(from, at + 1) != std::string::npos) {
        fail(STRATA_ERROR_INTERNAL, "mesh shader: '%s' must occur exactly once", from.c_str());
    }
    s.replace(at, from.size(), to);
}

// A lit fragment shader for static meshes: the same shader with its vColor
// input renamed and replaced by the vertex colour times the mesh's texture
// (unit 4), so lighting, shadows and fog stay one piece of GLSL. ABI 1.20
// splices in the material: the normal (two-sided faces, normal map), the
// emission (added before fog) and, in the cutout variant, the discard.
std::string withAlbedo(const char* fragment, bool translucent, bool cutout) {
    std::string s = fragment;
    replaceOnce(s, "in vec4 vColor;\n",
                std::string("in vec4 vColorIn;\nin vec2 vUv;\nlayout(binding = 4) uniform sampler2D uAlbedo;\n") +
                    shaders::kMeshBlock + "vec4 vColor;\n");
    replaceOnce(s, "out vec4 fragColor;\n", std::string(shaders::kMeshMaterialFunctions) + "out vec4 fragColor;\n");
    // Opaque meshes ignore alpha. Translucent ones (ABI 1.11) carry the
    // texel's alpha times the mesh's, which the lit shaders pass through.
    std::string prologue = "void main() {\n    vec4 texel = texture(uAlbedo, vUv);\n";
    if (cutout) prologue += shaders::kCutoutDiscard;
    prologue += translucent ? "    vColor = vColorIn * texel * vec4(1.0, 1.0, 1.0, uMeshParams.x);\n"
                            : "    vColor = vColorIn * vec4(texel.rgb, 1.0);\n";
    replaceOnce(s, "void main() {\n", prologue);
    replaceOnce(s, "    vec3 normal = normalize(vNormal);\n", "    vec3 normal = strataMeshNormal();\n");
    if (s.find("    if (uSkyAmbient.w > 0.5) {\n") != std::string::npos) {
        replaceOnce(s, "    if (uSkyAmbient.w > 0.5) {\n", "    color += strataMeshEmission();\n    if (uSkyAmbient.w > 0.5) {\n");
    } else {
        replaceOnce(s, "strataOutput(vColor.rgb * light)", "strataOutput(vColor.rgb * light + strataMeshEmission())");
    }
    return s;
}

// The unlit mesh shaders with or without the cutout discard (ABI 1.20).
std::string unlitFragment(const char* fragment, bool cutout) {
    std::string s = fragment;
    replaceOnce(s, shaders::kCutoutMarker, cutout ? shaders::kCutoutDiscard : "");
    return s;
}

bool allFinite(const float* values, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (!finite(values[i])) return false;
    }
    return true;
}

}  // namespace

Renderer::Renderer(rhi::GpuDevice& device, platform::Surface& surface) : device_(device), surface_(surface) {
    const auto problems = validatePassOrder(framePasses(), frameInputs());
    if (!problems.empty()) {
        fail(STRATA_ERROR_INTERNAL, "invalid pass list: %s", problems.front().c_str());
    }
    logf(LogLevel::Debug, "pass list:\n%s", describePasses(framePasses()).c_str());

    buildMeshes();
    buildPipelines();
    overlay_ = std::make_unique<Overlay>(device_);

    frameUniforms_ = device_.createBuffer(sizeof(FrameUniforms), nullptr, true, "strata.frame-uniforms");
    const size_t alignment = device_.uniformBufferAlignment();
    shadowUniformStride_ = ((sizeof(glm::mat4) + alignment - 1) / alignment) * alignment;
    shadowUniforms_ = device_.createBuffer(shadowUniformStride_ * kMaxCascades, nullptr, true, "strata.shadow-uniforms");
    pointLightUniforms_ =
        device_.createBuffer(sizeof(PointLightUniforms), nullptr, true, "strata.point-light-uniforms");
    pointShadowUniformStride_ = ((sizeof(glm::mat4) + sizeof(glm::vec4) + alignment - 1) / alignment) * alignment;
    pointShadowUniforms_ = device_.createBuffer(pointShadowUniformStride_ * 6 * STRATA_MAX_SHADOWED_POINT_LIGHTS,
                                                nullptr, true, "strata.point-shadow-uniforms");
    instances_ = device_.createBuffer(1024 * kInstanceStride, nullptr, true, "strata.instances");
    meshUniformStride_ = ((sizeof(MeshUniforms) + alignment - 1) / alignment) * alignment;
    meshUniforms_ = device_.createBuffer(meshUniformStride_ * STRATA_MAX_MESHES, nullptr, true, "strata.mesh-uniforms");
    {
        const uint8_t white[4] = {255, 255, 255, 255};
        rhi::TextureDesc desc;
        desc.width = 1;
        desc.height = 1;
        desc.initialData = white;
        desc.debugName = "strata.white";
        whiteTexture_ = device_.createTexture(desc);
    }
    for (size_t i = 0; i < kClusterRing; ++i) ensureClusterData(i, 1);  // bound by every lit pass, even with culling off

    // BN's constants until the host says otherwise.
    lighting_.mode = STRATA_LIGHTING_LEGACY;
    const glm::vec3 legacy = glm::normalize(glm::vec3(-0.35f, -0.85f, -0.4f));
    lighting_.legacy_direction[0] = legacy.x;
    lighting_.legacy_direction[1] = legacy.y;
    lighting_.legacy_direction[2] = legacy.z;
    lighting_.legacy_ambient = 0.35f;
    lighting_.time_of_day = 0.4f;
    lighting_.latitude_degrees = 35.0f;
    lighting_.cascade_count = 4;
    lighting_.shadow_map_size = 2048;
    lighting_.shadow_distance = 150.0f;
    lighting_.split_lambda = 0.6f;
    lighting_.fog = 1;

    const glm::mat4 identity(1.0f);
    std::memcpy(camera_.view, glm::value_ptr(identity), sizeof camera_.view);
    std::memcpy(camera_.projection, glm::value_ptr(identity), sizeof camera_.projection);
    camera_.near_plane = 0.1f;
    camera_.far_plane = 100.0f;

    for (auto& list : perKind_) list.reserve(1024);
}

Renderer::~Renderer() {
    overlay_.reset();
    device_.destroy(meshVertices_);
    device_.destroy(meshIndices_);
    device_.destroy(instances_);
    device_.destroy(frameUniforms_);
    device_.destroy(shadowUniforms_);
    device_.destroy(pointLightUniforms_);
    device_.destroy(pointShadowUniforms_);
    for (auto texture : clusterData_) device_.destroy(texture);
    for (auto target : pointShadowTargets_) device_.destroy(target);
    device_.destroy(pointShadowMaps_);
    device_.destroy(pointShadowPipeline_);
    clearMeshes();
    for (auto& t : textures_) device_.destroy(t.handle);
    textures_.clear();
    device_.destroy(whiteTexture_);
    device_.destroy(meshUniforms_);
    device_.destroy(terrainVertices_);
    device_.destroy(terrainIndices_);
    device_.destroy(terrainLegacyPipeline_);
    device_.destroy(terrainSunPipeline_);
    device_.destroy(terrainShadowPipeline_);
    device_.destroy(terrainPointShadowPipeline_);
    for (auto& entry : meshPipelines_) device_.destroy(entry.second.handle);
    meshPipelines_.clear();
    for (auto target : shadowTargets_) device_.destroy(target);
    device_.destroy(shadowMaps_);
    device_.destroy(offscreenTarget_);
    device_.destroy(offscreenColor_);
    device_.destroy(offscreenDepth_);
    device_.destroy(depthPackTarget_);
    device_.destroy(depthPackColor_);
    device_.destroy(depthPackPipeline_);
    device_.destroy(legacyPipeline_);
    device_.destroy(sunPipeline_);
    device_.destroy(shadowPipeline_);
    device_.destroy(skyPipeline_);
}

void Renderer::buildMeshes() {
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
    for (int kind = 0; kind < STRATA_PRIMITIVE_KIND_COUNT; ++kind) {
        const PrimitiveMesh mesh = createPrimitive(kind);
        KindRange& range = kinds_[kind];
        range.firstIndex = static_cast<uint32_t>(indices.size());
        range.indexCount = static_cast<uint32_t>(mesh.indices.size());
        range.baseVertex = static_cast<int32_t>(vertices.size() / PrimitiveMesh::kFloatsPerVertex);
        vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
        indices.insert(indices.end(), mesh.indices.begin(), mesh.indices.end());
    }
    meshVertices_ = device_.createBuffer(vertices.size() * sizeof(float), vertices.data(), false, "strata.primitive-vertices");
    meshIndices_ = device_.createIndexBuffer(indices.size() * sizeof(uint32_t), indices.data(), "strata.primitive-indices");
}

void Renderer::buildPipelines() {
    using namespace rhi;
    const std::vector<VertexBufferLayout> buffers = {
        {0, PrimitiveMesh::kFloatsPerVertex * sizeof(float), false},
        {1, kInstanceStride, true},
    };
    // Instance attributes read StrataRenderItem in place: the kind at offset
    // 0 is skipped, then position, scale and colour.
    const std::vector<VertexAttribute> attributes = {
        {0, 0, 3, AttribType::Float, 0},
        {1, 0, 3, AttribType::Float, 3 * sizeof(float)},
        {2, 1, 3, AttribType::Float, offsetof(StrataRenderItem, position)},
        {3, 1, 3, AttribType::Float, offsetof(StrataRenderItem, scale)},
        {4, 1, 4, AttribType::Float, offsetof(StrataRenderItem, color)},
    };

    PipelineDesc legacy;
    legacy.name = "opaque.legacy";
    legacy.vertexSource = shaders::kPrimitiveVertex;
    legacy.fragmentSource = shaders::kLegacyFragment;
    legacy.buffers = buffers;
    legacy.attributes = attributes;
    legacy.depthTest = DepthTest::Less;  // BN: glDepthFunc(GL_LESS)
    legacy.cull = CullMode::Back;
    legacyPipeline_ = device_.createPipeline(legacy);

    PipelineDesc sun = legacy;
    sun.name = "opaque.sun";
    sun.fragmentSource = shaders::kSunFragment;
    sunPipeline_ = device_.createPipeline(sun);

    PipelineDesc shadow;
    shadow.name = "shadow";
    shadow.vertexSource = shaders::kShadowVertex;
    shadow.fragmentSource = shaders::kShadowFragment;
    shadow.buffers = buffers;
    shadow.attributes = {attributes[0], attributes[2], attributes[3]};
    shadow.depthTest = DepthTest::Less;
    shadow.colorWrite = false;
    // Every primitive is closed, so drawing back faces into the map moves the
    // self-shadowing boundary to the far side of each solid.
    shadow.cull = CullMode::None;
    shadow.depthBiasConstant = 2.0f;
    shadow.depthBiasSlope = 2.0f;
    shadow.depthClamp = true;
    shadowPipeline_ = device_.createPipeline(shadow);

    // Point shadows store distance / range from the fragment shader, which
    // turns polygon offset off; the lookup biases by distance instead.
    PipelineDesc pointShadow = shadow;
    pointShadow.name = "point_shadow";
    pointShadow.vertexSource = shaders::kPointShadowVertex;
    pointShadow.fragmentSource = shaders::kPointShadowFragment;
    pointShadow.depthBiasConstant = 0.0f;
    pointShadow.depthBiasSlope = 0.0f;
    pointShadow.depthClamp = false;
    pointShadowPipeline_ = device_.createPipeline(pointShadow);

    // Terrain: position, normal and colour per vertex, no instances.
    const std::vector<VertexBufferLayout> terrainBuffers = {
        {0, TerrainMesh::kFloatsPerVertex * sizeof(float), false}};
    const std::vector<VertexAttribute> terrainAttributes = {
        {0, 0, 3, AttribType::Float, 0},
        {1, 0, 3, AttribType::Float, 3 * sizeof(float)},
        {5, 0, 3, AttribType::Float, 6 * sizeof(float)},
    };
    PipelineDesc terrain = legacy;
    terrain.name = "opaque.terrain.legacy";
    terrain.vertexSource = shaders::kTerrainVertex;
    terrain.buffers = terrainBuffers;
    terrain.attributes = terrainAttributes;
    terrainLegacyPipeline_ = device_.createPipeline(terrain);
    terrain.name = "opaque.terrain.sun";
    terrain.fragmentSource = shaders::kSunFragment;
    terrainSunPipeline_ = device_.createPipeline(terrain);

    // Static meshes: made on first use by meshPipeline(), one per variant
    // (pass, shading, two-sided, cutout; ABI 1.20). The variants every
    // scene with meshes needs are made now, as they always were.
    for (int pass = 0; pass < 4; ++pass) {
        for (int shading = 0; shading < 3; ++shading) {
            if (pass >= 2 && shading > 0) break;  // shadow passes have one shading
            MeshVariant v;
            v.pass = static_cast<uint8_t>(pass);
            v.shading = static_cast<uint8_t>(shading);
            meshPipeline(v);
        }
    }

    PipelineDesc terrainShadow = shadow;
    terrainShadow.name = "shadow.terrain";
    terrainShadow.vertexSource = shaders::kTerrainShadowVertex;
    terrainShadow.buffers = terrainBuffers;
    terrainShadow.attributes = {terrainAttributes[0]};
    terrainShadowPipeline_ = device_.createPipeline(terrainShadow);

    PipelineDesc terrainPointShadow = pointShadow;
    terrainPointShadow.name = "point_shadow.terrain";
    terrainPointShadow.vertexSource = shaders::kTerrainPointShadowVertex;
    terrainPointShadow.buffers = terrainBuffers;
    terrainPointShadow.attributes = {terrainAttributes[0]};
    terrainPointShadowPipeline_ = device_.createPipeline(terrainPointShadow);

    PipelineDesc sky;
    sky.name = "sky";
    sky.vertexSource = shaders::kSkyVertex;
    sky.fragmentSource = shaders::kSkyFragment;
    sky.depthTest = DepthTest::LessEqual;
    sky.depthWrite = false;
    sky.cull = CullMode::None;
    skyPipeline_ = device_.createPipeline(sky);

    PipelineDesc depthPack;
    depthPack.name = "depth_pack";
    depthPack.vertexSource = shaders::kDepthPackVertex;
    depthPack.fragmentSource = shaders::kDepthPackFragment;
    depthPack.depthTest = DepthTest::Off;
    depthPack.depthWrite = false;
    depthPack.cull = CullMode::None;
    depthPackPipeline_ = device_.createPipeline(depthPack);
}

rhi::PipelineDesc Renderer::meshPipelineDesc(const MeshVariant& v, std::string& vertex, std::string& fragment) const {
    using namespace rhi;
    const std::vector<VertexBufferLayout> meshBuffers = {{0, StaticMesh::kFloatsPerVertex * sizeof(float), false}};
    const VertexAttribute position{0, 0, 3, AttribType::Float, 0};
    const VertexAttribute normal{1, 0, 3, AttribType::Float, 3 * sizeof(float)};
    const VertexAttribute color{5, 0, 3, AttribType::Float, 6 * sizeof(float)};
    const VertexAttribute uv{6, 0, 2, AttribType::Float, 9 * sizeof(float)};
    PipelineDesc d;
    d.buffers = meshBuffers;
    d.depthTest = DepthTest::Less;
    const bool cutout = v.cutout != 0;
    if (v.pass == kMeshOpaque || v.pass == kMeshTranslucent) {
        const bool translucent = v.pass == kMeshTranslucent;
        d.attributes = {position, normal, color, uv};
        vertex = shaders::kMeshVertex;
        if (v.shading == kShadingUnlit) {
            fragment = unlitFragment(translucent ? shaders::kMeshUnlitTranslucentFragment : shaders::kMeshUnlitFragment,
                                     cutout);
        } else {
            fragment = withAlbedo(v.shading == kShadingLegacy ? shaders::kLegacyFragment : shaders::kSunFragment,
                                  translucent, cutout);
        }
        static const char* const kNames[2][3] = {{"opaque.mesh.legacy", "opaque.mesh.sun", "opaque.mesh.unlit"},
                                                 {"translucent.mesh.legacy", "translucent.mesh.sun",
                                                  "translucent.mesh.unlit"}};
        d.name = kNames[translucent ? 1 : 0][v.shading];
        // Back faces culled, unless two-sided (ABI 1.20). ABI 1.11:
        // translucent meshes are alpha blended over the opaque scene and the
        // sky, depth-tested without writing depth, both faces drawn.
        d.cull = (translucent || v.doubleSided) ? CullMode::None : CullMode::Back;
        if (translucent) {
            d.depthWrite = false;
            d.blend = BlendMode::Alpha;
        }
    } else {
        // Depth only; both faces, as for every caster. The cascades use
        // polygon offset and depth clamp; the cube maps store distance /
        // range from the fragment shader, which turns polygon offset off.
        const bool cube = v.pass == kMeshPointShadow;
        d.name = cube ? (cutout ? "point_shadow.mesh.cutout" : "point_shadow.mesh")
                      : (cutout ? "shadow.mesh.cutout" : "shadow.mesh");
        d.attributes = cutout ? std::vector<VertexAttribute>{position, uv} : std::vector<VertexAttribute>{position};
        if (cube) {
            vertex = cutout ? shaders::kMeshPointShadowCutoutVertex : shaders::kMeshPointShadowVertex;
            fragment = cutout ? shaders::kMeshPointShadowCutoutFragment : shaders::kPointShadowFragment;
        } else {
            vertex = cutout ? shaders::kMeshShadowCutoutVertex : shaders::kMeshShadowVertex;
            fragment = cutout ? shaders::kMeshShadowCutoutFragment : shaders::kShadowFragment;
            d.depthBiasConstant = 2.0f;
            d.depthBiasSlope = 2.0f;
            d.depthClamp = true;
        }
        d.colorWrite = false;
        d.cull = CullMode::None;
    }
    d.vertexSource = vertex.c_str();
    d.fragmentSource = fragment.c_str();
    return d;
}

rhi::PipelineHandle Renderer::meshPipeline(const MeshVariant& v) {
    const uint32_t key = v.key();
    const auto found = meshPipelines_.find(key);
    if (found != meshPipelines_.end()) return found->second.handle;
    CachedPipeline& cached = meshPipelines_[key];
    rhi::PipelineDesc desc = meshPipelineDesc(v, cached.vertex, cached.fragment);
    try {
        cached.handle = device_.createPipeline(desc);
    } catch (...) {
        meshPipelines_.erase(key);
        throw;
    }
    return cached.handle;
}

void Renderer::validateCamera(const StrataCamera& c) {
    if (!allFinite(c.view, 16) || !allFinite(c.projection, 16) || !allFinite(c.eye, 3)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "camera has a non-finite value");
    }
    if (!(c.near_plane > 0.0f) || !(c.far_plane > c.near_plane) || !finite(c.far_plane)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "camera planes must satisfy 0 < near < far (got %g, %g)",
             static_cast<double>(c.near_plane), static_cast<double>(c.far_plane));
    }
}

void Renderer::validateLighting(const StrataLighting& l) {
    if (l.mode != STRATA_LIGHTING_LEGACY && l.mode != STRATA_LIGHTING_SUN && l.mode != STRATA_LIGHTING_SPACE) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "unknown lighting mode %d", l.mode);
    }
    if (l.mode == STRATA_LIGHTING_SPACE) {
        if (!(l.legacy_ambient >= 0.0f && l.legacy_ambient <= 1.0f)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "space ambient (legacy_ambient) must be in [0, 1] (got %g)",
                 static_cast<double>(l.legacy_ambient));
        }
        return;
    }
    if (l.mode == STRATA_LIGHTING_LEGACY) {
        const glm::vec3 d(l.legacy_direction[0], l.legacy_direction[1], l.legacy_direction[2]);
        if (!allFinite(l.legacy_direction, 3) || glm::length(d) < 1e-6f) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "legacy light direction must be a finite, non-zero vector");
        }
        if (!(l.legacy_ambient >= 0.0f && l.legacy_ambient <= 1.0f)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "legacy ambient must be in [0, 1] (got %g)",
                 static_cast<double>(l.legacy_ambient));
        }
        return;
    }
    if (!finite(l.time_of_day) || !finite(l.latitude_degrees) || std::fabs(l.latitude_degrees) > 90.0f) {
        fail(STRATA_ERROR_INVALID_ARGUMENT,
             "time of day must be finite and latitude within [-90, 90] (got time_of_day %g, latitude %g)",
             static_cast<double>(l.time_of_day), static_cast<double>(l.latitude_degrees));
    }
    if (l.cascade_count < 0 || l.cascade_count > kMaxCascades) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "cascade_count must be 0..%d (got %d)", kMaxCascades, l.cascade_count);
    }
    if (l.cascade_count > 0) {
        if (l.shadow_map_size < 256 || l.shadow_map_size > 4096) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "shadow_map_size must be 256..4096 (got %d)", l.shadow_map_size);
        }
        if (!(l.shadow_distance > 0.0f) || !finite(l.shadow_distance)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "shadow_distance must be positive (got %g)",
                 static_cast<double>(l.shadow_distance));
        }
        if (!(l.split_lambda >= 0.0f && l.split_lambda <= 1.0f)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "split_lambda must be in [0, 1] (got %g)",
                 static_cast<double>(l.split_lambda));
        }
    }
}

void Renderer::setCamera(const StrataCamera& camera) {
    validateCamera(camera);
    camera_ = camera;
}

SkyState Renderer::currentSky() const {
    SkyState sky = hasSunDirection_ ? evaluateSkyForSun(sunDirection_)
                                    : evaluateSky(lighting_.time_of_day, lighting_.latitude_degrees);
    applyLightLevels(sky, levels_);
    return sky;
}

namespace {

// Three components, each in [0, max]; nullptr is fallback in all three.
glm::vec3 levelTriple(const float* v, float fallback, float max, const char* what) {
    if (v == nullptr) return glm::vec3(fallback);
    for (int i = 0; i < 3; ++i) {
        if (!(v[i] >= 0.0f && v[i] <= max)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "%s must be in [0, %g] (got %g, %g, %g)", what,
                 static_cast<double>(max), static_cast<double>(v[0]), static_cast<double>(v[1]),
                 static_cast<double>(v[2]));
        }
    }
    return glm::vec3(v[0], v[1], v[2]);
}

}  // namespace

void Renderer::setAmbient(const float* tint, const float* lift) {
    const glm::vec3 t = levelTriple(tint, 1.0f, 16.0f, "ambient tint");
    const glm::vec3 l = levelTriple(lift, 0.0f, 1.0f, "ambient lift");
    levels_.ambientTint = t;
    levels_.ambientLift = l;
}

void Renderer::setSunIntensity(float intensity, const float* tint) {
    if (!(intensity >= 0.0f && intensity <= 16.0f)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "sun intensity must be in [0, 16] (got %g)",
             static_cast<double>(intensity));
    }
    const glm::vec3 t = levelTriple(tint, 1.0f, 16.0f, "sun tint");
    levels_.keyIntensity = intensity;
    levels_.keyTint = t;
}

void Renderer::setSunDirection(const float* toSun) {
    if (toSun == nullptr) {
        hasSunDirection_ = false;
        return;
    }
    const glm::vec3 d(toSun[0], toSun[1], toSun[2]);
    if (!std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z) || glm::length(d) < 1e-6f) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "sun direction must be a finite, non-zero vector");
    }
    sunDirection_ = glm::normalize(d);
    hasSunDirection_ = true;
}

void Renderer::setLighting(const StrataLighting& lighting) {
    validateLighting(lighting);
    lighting_ = lighting;
    if (lighting_.mode == STRATA_LIGHTING_LEGACY) {
        const glm::vec3 d = glm::normalize(
            glm::vec3(lighting.legacy_direction[0], lighting.legacy_direction[1], lighting.legacy_direction[2]));
        lighting_.legacy_direction[0] = d.x;
        lighting_.legacy_direction[1] = d.y;
        lighting_.legacy_direction[2] = d.z;
    }
}

size_t Renderer::maxPointLights() const {
    return lightCulling_ == STRATA_LIGHT_CULLING_CLUSTERED ? size_t{STRATA_MAX_CLUSTERED_LIGHTS}
                                                           : size_t{STRATA_MAX_POINT_LIGHTS};
}

size_t Renderer::maxSpotLights() const {
    return lightCulling_ == STRATA_LIGHT_CULLING_CLUSTERED ? size_t{STRATA_MAX_CLUSTERED_LIGHTS}
                                                           : size_t{STRATA_MAX_SPOT_LIGHTS};
}

void Renderer::setLightCulling(int32_t mode) {
    if (mode != STRATA_LIGHT_CULLING_NONE && mode != STRATA_LIGHT_CULLING_CLUSTERED) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "unknown light culling mode %d", mode);
    }
    if (mode == STRATA_LIGHT_CULLING_NONE &&
        (pointLights_.size() > STRATA_MAX_POINT_LIGHTS || spotLights_.size() > STRATA_MAX_SPOT_LIGHTS)) {
        fail(STRATA_ERROR_INVALID_STATE,
             "light culling: %lu point and %lu spot lights are set; without culling at most %d point and %d "
             "spot lights (set fewer lights first)",
             static_cast<unsigned long>(pointLights_.size()), static_cast<unsigned long>(spotLights_.size()),
             STRATA_MAX_POINT_LIGHTS, STRATA_MAX_SPOT_LIGHTS);
    }
    lightCulling_ = mode;
}

void Renderer::setPointLights(const StrataPointLight* lights, size_t count) {
    validatePointLights(lights, count, countShadowCasters(spotLights_.data(), spotLights_.size()), maxPointLights());
    pointLights_.assign(lights, lights + count);
    assignShadowSlots();
}

void Renderer::setSpotLights(const StrataSpotLight* lights, size_t count) {
    validateSpotLights(lights, count, countShadowCasters(pointLights_.data(), pointLights_.size()), maxSpotLights());
    spotLights_.assign(lights, lights + count);
    for (auto& l : spotLights_) {
        const glm::vec3 d = glm::normalize(glm::vec3(l.direction[0], l.direction[1], l.direction[2]));
        l.direction[0] = d.x;
        l.direction[1] = d.y;
        l.direction[2] = d.z;
    }
    assignShadowSlots();
}

void Renderer::assignShadowSlots() {
    pointShadowSlot_.assign(pointLights_.size(), -1);
    spotShadowSlot_.assign(spotLights_.size(), -1);
    pointShadowCount_ = 0;
    for (size_t i = 0; i < pointLights_.size(); ++i) {
        if (pointLights_[i].flags & STRATA_POINT_LIGHT_CAST_SHADOWS) pointShadowSlot_[i] = pointShadowCount_++;
    }
    for (size_t i = 0; i < spotLights_.size(); ++i) {
        if (spotLights_[i].flags & STRATA_SPOT_LIGHT_CAST_SHADOWS) spotShadowSlot_[i] = pointShadowCount_++;
    }
}

void Renderer::setColorOutput(uint32_t flags, float exposure) {
    const uint32_t known = STRATA_OUTPUT_SRGB | STRATA_OUTPUT_TONEMAP_REINHARD | STRATA_OUTPUT_TONEMAP_ACES;
    if (flags & ~known) fail(STRATA_ERROR_INVALID_ARGUMENT, "color output: unknown flags 0x%x", flags & ~known);
    if ((flags & STRATA_OUTPUT_TONEMAP_REINHARD) && (flags & STRATA_OUTPUT_TONEMAP_ACES)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "color output: at most one STRATA_OUTPUT_TONEMAP_* flag");
    }
    if (!(exposure > 0.0f && exposure <= 64.0f)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "color output: exposure must be in (0, 64] (got %g)",
             static_cast<double>(exposure));
    }
    outputFlags_ = flags;
    outputExposure_ = exposure;
}

void Renderer::setTerrain(const StrataTerrainTile* tile) {
    if (!tile) {
        device_.destroy(terrainVertices_);
        device_.destroy(terrainIndices_);
        terrainVertices_ = {};
        terrainIndices_ = {};
        terrainIndexCount_ = 0;
        return;
    }
    const TerrainMesh mesh = buildTerrainMesh(*tile);  // validates first; throws before anything changes
    const rhi::BufferHandle vertices = device_.createBuffer(mesh.vertices.size() * sizeof(float),
                                                            mesh.vertices.data(), false, "strata.terrain-vertices");
    const rhi::BufferHandle indices = device_.createIndexBuffer(mesh.indices.size() * sizeof(uint32_t),
                                                                mesh.indices.data(), "strata.terrain-indices");
    device_.destroy(terrainVertices_);
    device_.destroy(terrainIndices_);
    terrainVertices_ = vertices;
    terrainIndices_ = indices;
    terrainIndexCount_ = static_cast<uint32_t>(mesh.indices.size());
}

uint32_t Renderer::addMesh(const StrataMesh& m) { return addMeshEntry(m, nullptr, 0, 0); }

uint32_t Renderer::addTexturedMesh(const StrataTexturedMesh& m) {
    if (m.flags & ~static_cast<uint32_t>(STRATA_MESH_UNLIT | STRATA_MESH_TRANSLUCENT | STRATA_MESH_COLORS_RGBA |
                                         STRATA_MESH_FLAT_NORMALS)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: unknown flags 0x%x", m.flags);
    }
    if ((m.flags & STRATA_MESH_TRANSLUCENT) && !(m.mesh.color[3] >= 0.0f && m.mesh.color[3] <= 1.0f)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: a translucent mesh's alpha (color[3]) must be 0..1");
    }
    if (m.texture != 0 && !textureFor(m.texture)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: no texture with id %u", m.texture);
    }
    return addMeshEntry(m.mesh, m.uvs, m.texture, m.flags);
}

uint32_t Renderer::addMeshEntry(const StrataMesh& m, const float* uvs, uint32_t texture, uint32_t flags) {
    if (meshes_.size() >= static_cast<size_t>(STRATA_MAX_MESHES)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: the scene already holds STRATA_MAX_MESHES (%d) meshes",
             STRATA_MAX_MESHES);
    }
    if (nextMeshId_ == 0) fail(STRATA_ERROR_INVALID_STATE, "mesh: ids exhausted");
    const int colorComponents = (flags & STRATA_MESH_COLORS_RGBA) ? 4 : 3;  // ABI 1.15
    const bool flat = (flags & STRATA_MESH_FLAT_NORMALS) != 0;           // ABI 1.19
    const StaticMesh mesh = buildStaticMesh(m, uvs, colorComponents, flat);  // validates first; throws before anything changes
    MeshEntry entry;
    entry.pick = buildPickGeometry(m);
    entry.vertices = device_.createBuffer(mesh.vertices.size() * sizeof(float), mesh.vertices.data(), false,
                                          "strata.mesh-vertices");
    entry.indices = device_.createIndexBuffer(mesh.indices.size() * sizeof(uint32_t), mesh.indices.data(),
                                              "strata.mesh-indices");
    entry.indexCount = static_cast<uint32_t>(mesh.indices.size());
    entry.texture = texture;
    entry.flags = flags;
    entry.alpha = (flags & STRATA_MESH_TRANSLUCENT) ? m.color[3] : 1.0f;
    const uint32_t id = nextMeshId_++;
    entry.id = id;
    meshes_.push_back(std::move(entry));
    return id;
}

void Renderer::setMeshTransform(uint32_t id, const float* model) {
    for (auto& entry : meshes_) {
        if (entry.id != id) continue;
        if (model == nullptr) {
            entry.model = glm::mat4(1.0f);
            return;
        }
        for (int i = 0; i < 16; ++i) {
            if (!std::isfinite(model[i])) fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh transform: element %d is not finite", i);
        }
        const glm::mat4 m = glm::make_mat4(model);
        const float det = glm::determinant(glm::mat3(m));
        if (!(std::fabs(det) > 1e-12f) || !std::isfinite(det)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh transform: the upper 3x3 is not invertible");
        }
        entry.model = m;
        return;
    }
    fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: no mesh with id %u", id);
}

void Renderer::setMeshVisible(uint32_t id, bool visible) {
    for (auto& entry : meshes_) {
        if (entry.id != id) continue;
        entry.visible = visible;
        return;
    }
    fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: no mesh with id %u", id);
}

void Renderer::validateMaterial(const StrataMaterial& m) const {
    if (!(m.alpha_cutoff >= 0.0f && m.alpha_cutoff <= 1.0f)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "material: alpha_cutoff must be in [0, 1] (got %g)",
             static_cast<double>(m.alpha_cutoff));
    }
    for (float v : m.emissive) {
        if (!(v >= 0.0f) || !std::isfinite(v)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "material: emissive components must be finite and >= 0");
        }
    }
    if (!(m.emissive_strength >= 0.0f) || !std::isfinite(m.emissive_strength)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "material: emissive_strength must be finite and >= 0 (got %g)",
             static_cast<double>(m.emissive_strength));
    }
    if (!std::isfinite(m.normal_scale)) fail(STRATA_ERROR_INVALID_ARGUMENT, "material: normal_scale is not finite");
    if (m.emissive_texture != 0 && !textureFor(m.emissive_texture)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "material: no emissive texture with id %u", m.emissive_texture);
    }
    if (m.normal_texture != 0 && !textureFor(m.normal_texture)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "material: no normal texture with id %u", m.normal_texture);
    }
    if (m.flags & ~static_cast<uint32_t>(STRATA_MATERIAL_DOUBLE_SIDED)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "material: unknown flags 0x%x",
             m.flags & ~static_cast<uint32_t>(STRATA_MATERIAL_DOUBLE_SIDED));
    }
}

void Renderer::setMeshMaterial(uint32_t id, const StrataMaterial* material) {
    for (auto& entry : meshes_) {
        if (entry.id != id) continue;
        if (material == nullptr) {
            entry.material = StrataMaterial{};
            return;
        }
        validateMaterial(*material);
        entry.material = *material;
        return;
    }
    fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: no mesh with id %u", id);
}

void Renderer::bindMeshTextures(const MeshEntry& entry) {
    const rhi::TextureHandle albedo = textureFor(entry.texture);
    device_.bindTexture(4, albedo ? albedo : whiteTexture_);
    const rhi::TextureHandle emissive = entry.material.emissive_texture ? textureFor(entry.material.emissive_texture)
                                                                        : rhi::TextureHandle{};
    device_.bindTexture(8, emissive ? emissive : whiteTexture_);
    const rhi::TextureHandle normal = entry.material.normal_texture ? textureFor(entry.material.normal_texture)
                                                                    : rhi::TextureHandle{};
    device_.bindTexture(9, normal ? normal : whiteTexture_);
}

rhi::TextureHandle Renderer::textureFor(uint32_t id) const {
    for (const auto& t : textures_) {
        if (t.id == id) return t.handle;
    }
    return {};
}

uint32_t Renderer::createTexture(int32_t width, int32_t height, const uint8_t* rgba, uint32_t flags) {
    const uint32_t known =
        STRATA_TEXTURE_MIPMAPS | STRATA_TEXTURE_CLAMP_U | STRATA_TEXTURE_CLAMP_V | STRATA_TEXTURE_NEAREST |
        STRATA_TEXTURE_SRGB;
    if (flags & ~known) fail(STRATA_ERROR_INVALID_ARGUMENT, "texture: unknown flags 0x%x", flags);
    if (width < 1 || height < 1 || width > STRATA_MAX_TEXTURE_SIZE || height > STRATA_MAX_TEXTURE_SIZE) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "texture: width and height must be 1..%d (got %d x %d)",
             STRATA_MAX_TEXTURE_SIZE, width, height);
    }
    if (!rgba) fail(STRATA_ERROR_INVALID_ARGUMENT, "texture: rgba is NULL");
    if (textures_.size() >= static_cast<size_t>(STRATA_MAX_TEXTURES)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "texture: the engine already holds STRATA_MAX_TEXTURES (%d) textures",
             STRATA_MAX_TEXTURES);
    }
    if (nextTextureId_ == 0) fail(STRATA_ERROR_INVALID_STATE, "texture: ids exhausted");
    rhi::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.initialData = rgba;
    desc.mipmaps = (flags & STRATA_TEXTURE_MIPMAPS) != 0;
    desc.repeatU = (flags & STRATA_TEXTURE_CLAMP_U) == 0;
    desc.repeatV = (flags & STRATA_TEXTURE_CLAMP_V) == 0;
    desc.nearest = (flags & STRATA_TEXTURE_NEAREST) != 0;  // ABI 1.10
    desc.srgb = (flags & STRATA_TEXTURE_SRGB) != 0;        // ABI 1.20
    desc.debugName = "strata.mesh-texture";
    const rhi::TextureHandle texture = device_.createTexture(desc);
    const uint32_t id = nextTextureId_++;
    TextureEntry entry;
    entry.id = id;
    entry.handle = texture;
    entry.width = width;
    entry.height = height;
    textures_.push_back(entry);
    return id;
}

void Renderer::destroyTexture(uint32_t id) {
    for (auto it = textures_.begin(); it != textures_.end(); ++it) {
        if (it->id != id) continue;
        device_.destroy(it->handle);
        textures_.erase(it);
        return;
    }
    fail(STRATA_ERROR_INVALID_ARGUMENT, "texture: no texture with id %u", id);
}

void Renderer::updateTexture(uint32_t id, int32_t x, int32_t y, int32_t width, int32_t height, const uint8_t* rgba) {
    const TextureEntry* entry = nullptr;
    for (const auto& t : textures_) {
        if (t.id == id) entry = &t;
    }
    if (!entry) fail(STRATA_ERROR_INVALID_ARGUMENT, "texture: no texture with id %u", id);
    if (!rgba) fail(STRATA_ERROR_INVALID_ARGUMENT, "texture update: rgba is NULL");
    // In 64 bits, so x + width cannot overflow.
    if (x < 0 || y < 0 || width < 1 || height < 1 ||
        static_cast<int64_t>(x) + width > entry->width || static_cast<int64_t>(y) + height > entry->height) {
        fail(STRATA_ERROR_INVALID_ARGUMENT,
             "texture update: the %d x %d rectangle at (%d, %d) is not inside the %d x %d texture", width, height, x,
             y, entry->width, entry->height);
    }
    const bool host = surface_.isHostOwned();
    if (host) device_.captureHostState();
    device_.updateTexture(entry->handle, x, y, width, height, rgba);
    if (host) device_.restoreHostState();
}

StrataRayHit Renderer::raycastMeshes(const float* origin, const float* direction, float maxDistance) const {
    if (!origin || !direction) fail(STRATA_ERROR_INVALID_ARGUMENT, "raycast: origin and direction must not be NULL");
    const glm::vec3 o(origin[0], origin[1], origin[2]);
    const glm::vec3 d(direction[0], direction[1], direction[2]);
    if (!std::isfinite(o.x) || !std::isfinite(o.y) || !std::isfinite(o.z)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "raycast: origin must be finite");
    }
    const float length = glm::length(d);
    if (!std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z) || !(length > 0.0f) ||
        !std::isfinite(length)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "raycast: direction must be finite and non-zero");
    }
    if (!(maxDistance > 0.0f)) fail(STRATA_ERROR_INVALID_ARGUMENT, "raycast: max_distance must be > 0");
    const glm::vec3 unit = d / length;

    StrataRayHit out{};
    out.triangle = -1;
    PickHit best;
    uint32_t bestId = 0;
    for (const auto& entry : meshes_) {
        if (!entry.visible) continue;  // ABI 1.19: a hidden mesh is not there to hit
        const PickHit hit = raycastMesh(entry.pick, entry.model, o, unit, maxDistance);
        // Strictly nearer only, so a tie goes to the mesh added first.
        if (hit.hit && (!best.hit || hit.distance < best.distance)) {
            best = hit;
            bestId = entry.id;
        }
    }
    if (!best.hit) return out;
    out.mesh_id = bestId;
    out.triangle = best.triangle;
    out.distance = best.distance;
    for (int i = 0; i < 3; ++i) {
        out.point[i] = best.point[i];
        out.normal[i] = best.normal[i];
    }
    return out;
}

void Renderer::writeMeshUniforms() {
    if (meshes_.empty()) return;
    std::vector<uint8_t> bytes(meshUniformStride_ * meshes_.size(), 0);
    for (size_t i = 0; i < meshes_.size(); ++i) {
        MeshUniforms u;
        u.model = meshes_[i].model;
        u.normalMatrix = glm::mat4(glm::transpose(glm::inverse(glm::mat3(meshes_[i].model))));
        u.params = glm::vec4(meshes_[i].alpha, 0.0f, 0.0f, 0.0f);
        // ABI 1.20: the material. A texture destroyed since it was set turns
        // its part off, as for the albedo texture.
        const StrataMaterial& m = meshes_[i].material;
        const bool normalMap = m.normal_texture != 0 && textureFor(m.normal_texture);
        const bool emissiveMap = m.emissive_texture != 0 && textureFor(m.emissive_texture);
        u.material = glm::vec4(m.alpha_cutoff, m.normal_scale,
                               (m.flags & STRATA_MATERIAL_DOUBLE_SIDED) ? 1.0f : 0.0f, normalMap ? 1.0f : 0.0f);
        u.emissive = glm::vec4(glm::vec3(m.emissive[0], m.emissive[1], m.emissive[2]) * m.emissive_strength,
                               emissiveMap ? 1.0f : 0.0f);
        std::memcpy(bytes.data() + i * meshUniformStride_, &u, sizeof u);
    }
    device_.updateBuffer(meshUniforms_, 0, bytes.size(), bytes.data());
}

void Renderer::removeMesh(uint32_t id) {
    for (auto it = meshes_.begin(); it != meshes_.end(); ++it) {
        if (it->id != id) continue;
        device_.destroy(it->vertices);
        device_.destroy(it->indices);
        meshes_.erase(it);
        return;
    }
    fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: no mesh with id %u", id);
}

void Renderer::clearMeshes() {
    for (auto& entry : meshes_) {
        device_.destroy(entry.vertices);
        device_.destroy(entry.indices);
    }
    meshes_.clear();
}

void Renderer::ensurePointShadowMaps(int cubes) {
    if (pointShadowMaps_ && cubes <= pointShadowCubes_) return;
    for (auto target : pointShadowTargets_) device_.destroy(target);
    pointShadowTargets_.clear();
    device_.destroy(pointShadowMaps_);
    rhi::TextureDesc desc;
    desc.width = kPointShadowMapSize;
    desc.height = kPointShadowMapSize;
    desc.layers = cubes;
    desc.cube = true;
    desc.format = rhi::TextureFormat::Depth32F;
    desc.depthCompare = true;
    desc.debugName = "strata.point-shadow-cubes";
    pointShadowMaps_ = device_.createTexture(desc);
    for (int i = 0; i < cubes * 6; ++i) {
        rhi::TargetDesc target;
        target.depth.texture = pointShadowMaps_;
        target.depth.layer = i;
        target.debugName = "strata.point-shadow-face";
        pointShadowTargets_.push_back(device_.createTarget(target));
    }
    pointShadowCubes_ = cubes;
}

void Renderer::pointShadowPass() {
    struct FaceUniforms {
        glm::mat4 viewProjection;
        glm::vec4 lightPositionRange;
    };
    // Point lights' casters, then spot lights' (ABI 1.20), each into its slot.
    std::vector<std::pair<int, glm::vec4>> casters;
    for (size_t i = 0; i < pointLights_.size(); ++i) {
        const StrataPointLight& l = pointLights_[i];
        if (pointShadowSlot_[i] >= 0) {
            casters.emplace_back(pointShadowSlot_[i], glm::vec4(l.position[0], l.position[1], l.position[2], l.range));
        }
    }
    for (size_t i = 0; i < spotLights_.size(); ++i) {
        const StrataSpotLight& l = spotLights_[i];
        if (spotShadowSlot_[i] >= 0) {
            casters.emplace_back(spotShadowSlot_[i], glm::vec4(l.position[0], l.position[1], l.position[2], l.range));
        }
    }
    for (const auto& caster : casters) {
        const int slot = caster.first;
        const glm::vec3 position(caster.second);
        const float range = caster.second.w;
        const auto faces = cubeFaceViewProjections(position, range);
        for (size_t face = 0; face < 6; ++face) {
            const size_t layer = static_cast<size_t>(slot) * 6 + face;
            const FaceUniforms u{faces[face], glm::vec4(position, range)};
            device_.updateBuffer(pointShadowUniforms_, layer * pointShadowUniformStride_, sizeof u, &u);
            rhi::PassDesc pass;
            pass.name = "point_shadow";
            pass.target = pointShadowTargets_[layer];
            pass.width = kPointShadowMapSize;
            pass.height = kPointShadowMapSize;
            pass.clearDepth = true;
            device_.beginPass(pass);
            device_.bindPipeline(pointShadowPipeline_);
            device_.bindUniformBuffer(1, pointShadowUniforms_, layer * pointShadowUniformStride_, sizeof u);
            drawPrimitives(true);
            drawWorldMeshes(terrainPointShadowPipeline_, MeshPass::PointShadow);
            device_.endPass();
        }
    }
}

void Renderer::beginFrame(const StrataFrameDesc& desc) {
    if (frameOpen_) fail(STRATA_ERROR_INVALID_STATE, "begin_frame called twice without end_frame");
    frame_ = desc;
    frameOpen_ = true;
    itemsRejected_ = 0;
    for (auto& list : perKind_) list.clear();
    overlay_->clear();
}

void Renderer::submit(const StrataRenderItem* items, size_t count) {
    if (!frameOpen_) fail(STRATA_ERROR_INVALID_STATE, "submit_primitives called outside begin_frame/end_frame");
    for (size_t i = 0; i < count; ++i) {
        const StrataRenderItem& item = items[i];
        if (item.kind < 0 || item.kind >= STRATA_PRIMITIVE_KIND_COUNT) {
            ++itemsRejected_;
            continue;
        }
        perKind_[static_cast<size_t>(item.kind)].push_back(item);
    }
}

void Renderer::submitLines(const StrataLineVertex* vertices, size_t count) {
    if (!frameOpen_) fail(STRATA_ERROR_INVALID_STATE, "submit_lines called outside begin_frame/end_frame");
    overlay_->addLines(vertices, count);
}

void Renderer::submitPoints(const StrataPoint* points, size_t count) {
    if (!frameOpen_) fail(STRATA_ERROR_INVALID_STATE, "submit_points called outside begin_frame/end_frame");
    overlay_->addPoints(points, count);
}

void Renderer::submitText(const StrataText* texts, size_t count) {
    if (!frameOpen_) fail(STRATA_ERROR_INVALID_STATE, "submit_text called outside begin_frame/end_frame");
    overlay_->addText(texts, count);
}

void Renderer::submitWorldText(const StrataText* texts, const float* anchors, size_t count) {
    if (!frameOpen_) fail(STRATA_ERROR_INVALID_STATE, "submit_world_text called outside begin_frame/end_frame");
    overlay_->addWorldText(texts, anchors, count);
}

bool Renderer::projectPoint(const float world[3], int width, int height, float outPixels[2], float* outDepth) const {
    if (!std::isfinite(world[0]) || !std::isfinite(world[1]) || !std::isfinite(world[2])) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "world point must be finite");
    }
    return render::projectPoint(camera_, width, height, world, outPixels, outDepth);
}

void Renderer::submitLines(const StrataLineVertex* vertices, size_t count, uint32_t flags) {
    if (!frameOpen_) fail(STRATA_ERROR_INVALID_STATE, "submit_lines_ex called outside begin_frame/end_frame");
    overlay_->addLines(vertices, count, flags);
}

void Renderer::submitRects(const StrataRect* rects, size_t count) {
    if (!frameOpen_) fail(STRATA_ERROR_INVALID_STATE, "submit_rects called outside begin_frame/end_frame");
    overlay_->addRects(rects, count);
}

void Renderer::submitWideLines(const StrataWideLineVertex* vertices, size_t count, uint32_t flags) {
    if (!frameOpen_) fail(STRATA_ERROR_INVALID_STATE, "submit_wide_lines called outside begin_frame/end_frame");
    overlay_->addWideLines(vertices, count, flags);
}

void Renderer::ensureOffscreenTarget(int width, int height) {
    if (offscreenTarget_ && width == offscreenWidth_ && height == offscreenHeight_) return;
    device_.destroy(offscreenTarget_);
    device_.destroy(offscreenColor_);
    device_.destroy(offscreenDepth_);
    rhi::TextureDesc color;
    color.width = width;
    color.height = height;
    color.format = rhi::TextureFormat::RGBA8;
    color.debugName = "strata.offscreen-color";
    offscreenColor_ = device_.createTexture(color);
    rhi::TextureDesc depth = color;
    depth.format = rhi::TextureFormat::Depth32F;
    // Nearest: a float depth texture is only complete with nearest filtering
    // on GLES 3.0, and the depth readback (ABI 1.9) samples it.
    depth.nearest = true;
    depth.debugName = "strata.offscreen-depth";
    offscreenDepth_ = device_.createTexture(depth);
    rhi::TargetDesc target;
    target.color.texture = offscreenColor_;
    target.depth.texture = offscreenDepth_;
    target.debugName = "strata.offscreen";
    offscreenTarget_ = device_.createTarget(target);
    offscreenWidth_ = width;
    offscreenHeight_ = height;
}

void Renderer::ensureShadowMaps(int size, int layers) {
    if (shadowMaps_ && size == shadowSize_ && layers <= shadowLayers_) return;
    for (auto& target : shadowTargets_) {
        device_.destroy(target);
        target = {};
    }
    device_.destroy(shadowMaps_);
    rhi::TextureDesc desc;
    desc.width = size;
    desc.height = size;
    desc.layers = kMaxCascades;
    desc.format = rhi::TextureFormat::Depth32F;
    desc.depthCompare = true;
    desc.debugName = "strata.shadow-cascades";
    shadowMaps_ = device_.createTexture(desc);
    for (int i = 0; i < kMaxCascades; ++i) {
        rhi::TargetDesc target;
        target.depth.texture = shadowMaps_;
        target.depth.layer = i;
        target.debugName = "strata.shadow-cascade";
        shadowTargets_[static_cast<size_t>(i)] = device_.createTarget(target);
    }
    shadowSize_ = size;
    shadowLayers_ = kMaxCascades;
}

void Renderer::uploadInstances() {
    size_t total = 0;
    for (const auto& list : perKind_) total += list.size();
    const size_t bytes = std::max<size_t>(total, 1) * kInstanceStride;
    if (bytes > device_.bufferSize(instances_)) {
        device_.resizeBuffer(instances_, std::max(bytes, device_.bufferSize(instances_) * 2));
    }
    uint32_t base = 0;
    for (size_t kind = 0; kind < perKind_.size(); ++kind) {
        baseInstance_[kind] = base;
        const auto& list = perKind_[kind];
        device_.updateBuffer(instances_, base * size_t{kInstanceStride}, list.size() * kInstanceStride, list.data());
        base += static_cast<uint32_t>(list.size());
    }
}

void Renderer::writeFrameUniforms(int width, int height, const Cascades* cascades) {
    FrameUniforms u{};
    u.view = glm::make_mat4(camera_.view);
    u.projection = glm::make_mat4(camera_.projection);
    u.inverseViewProjection = glm::inverse(u.projection * u.view);
    u.eye = glm::vec4(camera_.eye[0], camera_.eye[1], camera_.eye[2], camera_.far_plane);
    (void)width;
    (void)height;
    u.output = glm::vec4(outputExposure_, static_cast<float>(outputFlags_), 0.0f, 0.0f);  // ABI 1.20

    if (lighting_.mode == STRATA_LIGHTING_LEGACY) {
        u.light = glm::vec4(lighting_.legacy_direction[0], lighting_.legacy_direction[1],
                            lighting_.legacy_direction[2], lighting_.legacy_ambient);
    } else if (lighting_.mode == STRATA_LIGHTING_SPACE) {
        // The sun shader with no key light (zero colour, zero direction), a
        // flat ambient from both hemispheres and fog off: point lights only.
        const glm::vec3 ambient = glm::vec3(lighting_.legacy_ambient) * levels_.ambientTint + levels_.ambientLift;
        u.skyAmbient = glm::vec4(ambient, 0.0f);
        u.groundAmbient = glm::vec4(ambient, 0.0f);
        u.zenith = glm::vec4(0.0f, 0.0f, 0.0f, camera_.far_plane);
    } else {
        const SkyState sky = currentSky();
        u.light = glm::vec4(sky.toLight, cascades ? sky.shadowStrength : 0.0f);
        u.lightColor = glm::vec4(sky.lightColor, 0.0f);
        u.skyAmbient = glm::vec4(sky.skyAmbient, lighting_.fog ? 1.0f : 0.0f);
        u.groundAmbient = glm::vec4(sky.groundAmbient, 0.0f);
        u.fogColor = glm::vec4(sky.fogColor, camera_.far_plane * 0.35f);
        u.zenith = glm::vec4(sky.zenith, camera_.far_plane);
        u.horizon = glm::vec4(sky.horizon, 0.0f);
        u.toSun = glm::vec4(sky.toSun, sky.sunElevation);
        if (cascades) {
            for (int i = 0; i < kMaxCascades; ++i) {
                const auto index = static_cast<size_t>(i);
                const bool used = i < cascades->count;
                u.cascadeViewProjection[i] = used ? cascades->viewProj[index] : glm::mat4(1.0f);
                u.cascadeSplits[i] = used ? cascades->splitFar[index] : 0.0f;
                u.cascadeTexel[i] = used ? cascades->texelWorld[index] : 0.0f;
            }
            u.shadowInfo = glm::ivec4(cascades->count, shadowSize_, 0, 0);
        }
    }
    device_.updateBuffer(frameUniforms_, 0, sizeof u, &u);
}

void Renderer::writePointLightUniforms(int width, int height) {
    PointLightUniforms u{};
    if (lightCulling_ == STRATA_LIGHT_CULLING_CLUSTERED) {
        // ABI 1.21: the lights live in clusterData_ (buildClusters); the
        // arrays stay empty and the shader reads its cluster's list instead.
        const ClusterCamera camera = clusterCamera();
        u.info = glm::ivec4(0, kPointShadowMapSize, 0, 0);
        u.clusterGrid = glm::ivec4(kClusterTilesX, kClusterTilesY, kClusterSlices, 1);
        u.clusterLayout = glm::ivec4(clusterHeaderBase_, clusterIndexBase_, 0, 0);
        u.clusterDepth = glm::vec4(camera.nearPlane, clusterSliceScale(camera), camera.orthographic ? 1.0f : 0.0f, 0.0f);
        u.clusterViewport = glm::vec4(1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height), 0.0f, 0.0f);
        device_.updateBuffer(pointLightUniforms_, 0, sizeof u, &u);
        return;
    }
    u.info = glm::ivec4(static_cast<int>(pointLights_.size()), 0, 0, 0);
    for (size_t i = 0; i < pointLights_.size(); ++i) {
        const StrataPointLight& l = pointLights_[i];
        u.positionRange[i] = glm::vec4(l.position[0], l.position[1], l.position[2], l.range);
        u.color[i] = glm::vec4(glm::vec3(l.color[0], l.color[1], l.color[2]) * l.intensity,
                               static_cast<float>(pointShadowSlot_[i]));
    }
    u.info.y = kPointShadowMapSize;
    u.info.z = static_cast<int>(spotLights_.size());  // ABI 1.20
    for (size_t i = 0; i < spotLights_.size(); ++i) {
        const StrataSpotLight& l = spotLights_[i];
        u.spotPositionRange[i] = glm::vec4(l.position[0], l.position[1], l.position[2], l.range);
        u.spotColor[i] = glm::vec4(glm::vec3(l.color[0], l.color[1], l.color[2]) * l.intensity,
                                   static_cast<float>(spotShadowSlot_[i]));
        u.spotDirection[i] = glm::vec4(l.direction[0], l.direction[1], l.direction[2],
                                       std::cos(glm::radians(l.outer_cone_degrees)));
        u.spotCone[i] = glm::vec4(std::cos(glm::radians(l.inner_cone_degrees)), 0.0f, 0.0f, 0.0f);
    }
    device_.updateBuffer(pointLightUniforms_, 0, sizeof u, &u);
}

ClusterCamera Renderer::clusterCamera() const {
    ClusterCamera c;
    c.view = glm::make_mat4(camera_.view);
    c.projection = glm::make_mat4(camera_.projection);
    c.nearPlane = camera_.near_plane;
    c.farPlane = camera_.far_plane;
    c.orthographic = camera_.projection[11] == 0.0f && camera_.projection[15] == 1.0f;
    return c;
}

void Renderer::ensureClusterData(size_t slot, int rows) {
    if (clusterData_[slot] && rows <= clusterDataRows_[slot]) return;
    int height = 1;
    while (height < rows) height *= 2;
    device_.destroy(clusterData_[slot]);
    std::vector<float> zeros(static_cast<size_t>(kClusterDataWidth) * static_cast<size_t>(height) * 4, 0.0f);
    rhi::TextureDesc desc;
    desc.width = kClusterDataWidth;
    desc.height = height;
    desc.format = rhi::TextureFormat::RGBA32F;
    desc.nearest = true;
    desc.initialData = zeros.data();
    desc.debugName = "strata.cluster-data";
    clusterData_[slot] = device_.createTexture(desc);
    clusterDataRows_[slot] = height;
}

void Renderer::buildClusters() {
    const auto start = Clock::now();
    const ClusterCamera camera = clusterCamera();
    const size_t lightCount = pointLights_.size() + spotLights_.size();
    std::vector<ClusterLight> lights;
    lights.reserve(lightCount);
    auto brightness = [](const float* color, float intensity) {
        return intensity * std::max(color[0], std::max(color[1], color[2]));
    };
    for (const auto& l : pointLights_) {
        lights.push_back({glm::make_vec3(l.position), l.range, brightness(l.color, l.intensity)});
    }
    for (const auto& l : spotLights_) {
        lights.push_back({glm::make_vec3(l.position), l.range, brightness(l.color, l.intensity)});
    }
    const ClusterLists lists = assignLightsToClusters(camera, lights);

    // One RGBA32F texture, kClusterDataWidth texels a row (see shaders.hpp,
    // clusterLights): 3 texels per light, a header per cluster, then the
    // lists, four indices a texel, each list starting on a texel of its own
    // so the shader fetches one texel per four lights.
    const size_t lightTexels = 3 * lightCount;
    clusterHeaderBase_ = static_cast<int>(lightTexels);
    clusterIndexBase_ = clusterHeaderBase_ + kClusterCount;
    size_t listTexels = 0;
    for (uint32_t count : lists.counts) listTexels += (count + 3) / 4;
    const size_t total = static_cast<size_t>(clusterIndexBase_) + listTexels;
    const int rows = static_cast<int>((total + kClusterDataWidth - 1) / kClusterDataWidth);
    clusterCurrent_ = (clusterCurrent_ + 1) % kClusterRing;
    ensureClusterData(clusterCurrent_, rows);
    clusterUpload_.assign(static_cast<size_t>(rows) * kClusterDataWidth * 4, 0.0f);
    float* texel = clusterUpload_.data();
    for (size_t i = 0; i < pointLights_.size(); ++i, texel += 12) {
        const StrataPointLight& l = pointLights_[i];
        const float data[12] = {l.position[0], l.position[1], l.position[2], l.range,
                                l.color[0] * l.intensity, l.color[1] * l.intensity, l.color[2] * l.intensity,
                                static_cast<float>(pointShadowSlot_[i]),
                                0.0f, 0.0f, 0.0f, 1.0f};  // a cone factor of 1 in every direction
        std::memcpy(texel, data, sizeof data);
    }
    for (size_t i = 0; i < spotLights_.size(); ++i, texel += 12) {
        const StrataSpotLight& l = spotLights_[i];
        const float cosOuter = std::cos(glm::radians(l.outer_cone_degrees));
        const float cosInner = std::cos(glm::radians(l.inner_cone_degrees));
        const float scale = 1.0f / std::max(cosInner - cosOuter, 1e-4f);
        const float data[12] = {l.position[0], l.position[1], l.position[2], l.range,
                                l.color[0] * l.intensity, l.color[1] * l.intensity, l.color[2] * l.intensity,
                                static_cast<float>(spotShadowSlot_[i]),
                                l.direction[0] * scale, l.direction[1] * scale, l.direction[2] * scale,
                                -cosOuter * scale};
        std::memcpy(texel, data, sizeof data);
    }
    float* header = clusterUpload_.data() + static_cast<size_t>(clusterHeaderBase_) * 4;
    float* list = clusterUpload_.data() + static_cast<size_t>(clusterIndexBase_) * 4;
    size_t listTexel = 0;
    for (int k = 0; k < kClusterCount; ++k, header += 4) {
        const auto ku = static_cast<size_t>(k);
        const uint32_t count = lists.counts[ku];
        header[0] = static_cast<float>(listTexel);
        header[1] = static_cast<float>(count);
        for (uint32_t j = 0; j < count; ++j) {
            list[listTexel * 4 + j] = static_cast<float>(lists.indices[lists.offsets[ku] + j]);
        }
        listTexel += (count + 3) / 4;
    }
    device_.updateTexture(clusterData_[clusterCurrent_], 0, 0, kClusterDataWidth, rows, clusterUpload_.data());

    lightStats_.lights_in_view = lists.stats.lightsInView;
    lightStats_.clusters = kClusterCount;
    lightStats_.clusters_lit = lists.stats.clustersLit;
    lightStats_.light_references = lists.stats.lightReferences;
    lightStats_.max_lights_per_cluster = lists.stats.maxLightsPerCluster;
    lightStats_.clusters_overflowed = lists.stats.clustersOverflowed;
    lightStats_.cpu_ms_culling = msSince(start);
}

void Renderer::drawPrimitives(bool forShadow) {
    (void)forShadow;
    device_.bindVertexBuffer(0, meshVertices_, 0);
    device_.bindVertexBuffer(1, instances_, 0);
    device_.bindIndexBuffer(meshIndices_);
    for (size_t kind = 0; kind < perKind_.size(); ++kind) {
        const auto count = static_cast<uint32_t>(perKind_[kind].size());
        if (count == 0) continue;
        rhi::DrawIndexed draw;
        draw.indexCount = kinds_[kind].indexCount;
        draw.firstIndex = kinds_[kind].firstIndex;
        draw.baseVertex = kinds_[kind].baseVertex;
        draw.instanceCount = count;
        draw.baseInstance = baseInstance_[kind];
        device_.drawIndexed(draw);
        ++stats_.draw_calls;
    }
}

void Renderer::drawTerrain() {
    if (terrainIndexCount_ == 0) return;
    device_.bindVertexBuffer(0, terrainVertices_, 0);
    device_.bindIndexBuffer(terrainIndices_);
    rhi::DrawIndexed draw;
    draw.indexCount = terrainIndexCount_;
    device_.drawIndexed(draw);
    ++stats_.draw_calls;
}

void Renderer::drawWorldMeshes(rhi::PipelineHandle terrainPipeline, MeshPass pass) {
    if (terrainIndexCount_ > 0) {
        device_.bindPipeline(terrainPipeline);
        drawTerrain();
    }
    if (meshes_.empty()) return;
    const bool opaque = pass == MeshPass::Opaque;
    const uint8_t litShading = lighting_.mode == STRATA_LIGHTING_LEGACY ? kShadingLegacy : kShadingSun;
    rhi::PipelineHandle bound;
    // Lit meshes first, then (opaque pass only) unlit ones; shadows skip unlit.
    for (int unlit = 0; unlit < 2; ++unlit) {
        if (unlit && !opaque) break;
        for (size_t i = 0; i < meshes_.size(); ++i) {
            const MeshEntry& entry = meshes_[i];
            if (!entry.visible) continue;                          // ABI 1.19
            if (entry.flags & STRATA_MESH_TRANSLUCENT) continue;  // the translucent pass draws these
            if (((entry.flags & STRATA_MESH_UNLIT) != 0) != (unlit != 0)) continue;
            MeshVariant variant;
            variant.cutout = entry.material.alpha_cutoff > 0.0f ? 1 : 0;  // ABI 1.20
            if (opaque) {
                variant.pass = kMeshOpaque;
                variant.shading = unlit ? kShadingUnlit : litShading;
                variant.doubleSided = (entry.material.flags & STRATA_MATERIAL_DOUBLE_SIDED) ? 1 : 0;
            } else {
                variant.pass = pass == MeshPass::Shadow ? kMeshShadow : kMeshPointShadow;
            }
            const rhi::PipelineHandle pipeline = meshPipeline(variant);
            if (pipeline != bound) {
                device_.bindPipeline(pipeline);
                bound = pipeline;
            }
            device_.bindUniformBuffer(3, meshUniforms_, i * meshUniformStride_, sizeof(MeshUniforms));
            if (opaque) {
                bindMeshTextures(entry);
            } else if (variant.cutout) {
                const rhi::TextureHandle texture = textureFor(entry.texture);
                device_.bindTexture(4, texture ? texture : whiteTexture_);
            }
            device_.bindVertexBuffer(0, entry.vertices, 0);
            device_.bindIndexBuffer(entry.indices);
            rhi::DrawIndexed draw;
            draw.indexCount = entry.indexCount;
            device_.drawIndexed(draw);
            ++stats_.draw_calls;
        }
    }
}

void Renderer::shadowPass(const Cascades& cascades) {
    for (int i = 0; i < cascades.count; ++i) {
        const auto index = static_cast<size_t>(i);
        device_.updateBuffer(shadowUniforms_, index * shadowUniformStride_, sizeof(glm::mat4),
                             glm::value_ptr(cascades.viewProj[index]));
        rhi::PassDesc pass;
        pass.name = "shadow";
        pass.target = shadowTargets_[index];
        pass.width = shadowSize_;
        pass.height = shadowSize_;
        pass.clearDepth = true;
        device_.beginPass(pass);
        device_.bindPipeline(shadowPipeline_);
        device_.bindUniformBuffer(1, shadowUniforms_, index * shadowUniformStride_, sizeof(glm::mat4));
        drawPrimitives(true);
        drawWorldMeshes(terrainShadowPipeline_, MeshPass::Shadow);
        device_.endPass();
    }
    stats_.shadow_cascades = cascades.count;
}

void Renderer::opaquePass(int width, int height) {
    rhi::PassDesc pass;
    pass.name = "opaque";
    pass.target = offscreenTarget_;
    pass.width = width;
    pass.height = height;
    pass.clearColor = (frame_.clear_flags & STRATA_CLEAR_COLOR) != 0;
    pass.clearDepth = (frame_.clear_flags & STRATA_CLEAR_DEPTH) != 0;
    std::memcpy(pass.color, frame_.clear_color, sizeof pass.color);
    device_.beginPass(pass);
    device_.bindPipeline(lighting_.mode == STRATA_LIGHTING_LEGACY ? legacyPipeline_ : sunPipeline_);
    device_.bindUniformBuffer(0, frameUniforms_, 0, sizeof(FrameUniforms));
    if (lighting_.mode != STRATA_LIGHTING_LEGACY) {
        device_.bindTexture(1, shadowMaps_);
        device_.bindTexture(2, pointShadowMaps_);
        device_.bindTexture(10, clusterData_[clusterCurrent_]);  // ABI 1.21
        device_.bindUniformBuffer(2, pointLightUniforms_, 0, sizeof(PointLightUniforms));
    }
    drawPrimitives(false);
    const bool legacy = lighting_.mode == STRATA_LIGHTING_LEGACY;
    drawWorldMeshes(legacy ? terrainLegacyPipeline_ : terrainSunPipeline_, MeshPass::Opaque);
    device_.endPass();
}

void Renderer::translucentPass(int width, int height) {
    // Back to front by the distance from the eye to each mesh's world-space
    // bounding-box centre: right for separate objects, approximate where
    // translucent meshes interpenetrate.
    const glm::vec3 eye = glm::make_vec3(camera_.eye);
    std::vector<std::pair<float, size_t>> order;
    for (size_t i = 0; i < meshes_.size(); ++i) {
        const MeshEntry& entry = meshes_[i];
        if (!(entry.flags & STRATA_MESH_TRANSLUCENT) || !entry.visible) continue;
        const glm::vec3 centre = 0.5f * (entry.pick.bounds.min + entry.pick.bounds.max);
        const glm::vec3 world = glm::vec3(entry.model * glm::vec4(centre, 1.0f));
        order.emplace_back(glm::dot(world - eye, world - eye), i);
    }
    if (order.empty()) return;
    std::stable_sort(order.begin(), order.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    rhi::PassDesc pass;
    pass.name = "translucent";
    pass.target = offscreenTarget_;
    pass.width = width;
    pass.height = height;
    device_.beginPass(pass);
    device_.bindUniformBuffer(0, frameUniforms_, 0, sizeof(FrameUniforms));
    const bool legacy = lighting_.mode == STRATA_LIGHTING_LEGACY;
    if (!legacy) {
        device_.bindTexture(1, shadowMaps_);
        device_.bindTexture(2, pointShadowMaps_);
        device_.bindTexture(10, clusterData_[clusterCurrent_]);  // ABI 1.21
        device_.bindUniformBuffer(2, pointLightUniforms_, 0, sizeof(PointLightUniforms));
    }
    rhi::PipelineHandle bound;
    for (const auto& item : order) {
        const MeshEntry& entry = meshes_[item.second];
        MeshVariant variant;
        variant.pass = kMeshTranslucent;
        variant.shading = (entry.flags & STRATA_MESH_UNLIT) ? kShadingUnlit : legacy ? kShadingLegacy : kShadingSun;
        variant.cutout = entry.material.alpha_cutoff > 0.0f ? 1 : 0;  // ABI 1.20
        const rhi::PipelineHandle pipeline = meshPipeline(variant);
        if (pipeline != bound) {
            device_.bindPipeline(pipeline);
            bound = pipeline;
        }
        device_.bindUniformBuffer(3, meshUniforms_, item.second * meshUniformStride_, sizeof(MeshUniforms));
        bindMeshTextures(entry);
        device_.bindVertexBuffer(0, entry.vertices, 0);
        device_.bindIndexBuffer(entry.indices);
        rhi::DrawIndexed draw;
        draw.indexCount = entry.indexCount;
        device_.drawIndexed(draw);
        ++stats_.draw_calls;
    }
    device_.endPass();
}

void Renderer::skyPass(int width, int height) {
    rhi::PassDesc pass;
    pass.name = "sky";
    pass.target = offscreenTarget_;
    pass.width = width;
    pass.height = height;
    device_.beginPass(pass);
    device_.bindPipeline(skyPipeline_);
    device_.bindUniformBuffer(0, frameUniforms_, 0, sizeof(FrameUniforms));
    device_.drawFullscreenTriangle();
    ++stats_.draw_calls;
    device_.endPass();
}

void Renderer::endFrame() {
    if (!frameOpen_) fail(STRATA_ERROR_INVALID_STATE, "end_frame called without begin_frame");
    frameOpen_ = false;
    const auto frameStart = Clock::now();

    StrataFrameStats stats{};
    stats.items_rejected = itemsRejected_;
    for (size_t kind = 0; kind < perKind_.size(); ++kind) {
        stats.instances_per_kind[kind] = static_cast<int32_t>(perKind_[kind].size());
        stats.instances += stats.instances_per_kind[kind];
    }
    stats_ = stats;

    int width = 0, height = 0;
    surface_.drawableSize(width, height);
    lastWidth_ = width;
    lastHeight_ = height;
    std::memcpy(lastProjection_, camera_.projection, sizeof lastProjection_);
    if (width <= 0 || height <= 0) {
        // A minimised window: nothing to draw into, and not an error.
        return;
    }

    const bool host = surface_.isHostOwned();
    if (host) device_.captureHostState();

    if (surface_.needsOffscreenTarget()) {
        ensureOffscreenTarget(width, height);
    }
    uploadInstances();
    writeMeshUniforms();

    Cascades cascades;
    const bool sun = lighting_.mode == STRATA_LIGHTING_SUN;
    // SUN and SPACE share the lit shader, its shadow samplers and point lights.
    const bool lit = lighting_.mode != STRATA_LIGHTING_LEGACY;
    bool shadows = false;
    if (sun && lighting_.cascade_count > 0) {
        const SkyState sky = currentSky();
        shadows = sky.shadowStrength > 0.0f;
        if (shadows) {
            ensureShadowMaps(lighting_.shadow_map_size, lighting_.cascade_count);
            CascadeInput input;
            input.view = glm::make_mat4(camera_.view);
            input.tanHalfFovY = 1.0f / camera_.projection[5];
            input.aspect = camera_.projection[5] / camera_.projection[0];
            // ABI 1.20: an orthographic camera's slices are boxes.
            input.orthographic = orthographicExtents(camera_.projection, input.halfWidth, input.halfHeight,
                                                     input.centreX, input.centreY);
            input.nearPlane = camera_.near_plane;
            input.farPlane = camera_.far_plane;
            input.shadowDistance = lighting_.shadow_distance;
            input.count = lighting_.cascade_count;
            input.lambda = lighting_.split_lambda;
            input.mapSize = lighting_.shadow_map_size;
            input.toLight = sky.toLight;
            cascades = computeCascades(input);
        }
    }
    if (lit && !shadowMaps_) {
        // The sun shader always samples a shadow array; give it a tiny one.
        ensureShadowMaps(256, 1);
    }
    // Likewise a cube array, even with no shadowed point lights.
    if (lit) ensurePointShadowMaps(std::max(pointShadowCount_, 1));
    writeFrameUniforms(width, height, shadows ? &cascades : nullptr);
    lightStats_ = StrataLightStats{};
    lightStats_.mode = lightCulling_;
    lightStats_.point_lights = static_cast<int32_t>(pointLights_.size());
    lightStats_.spot_lights = static_cast<int32_t>(spotLights_.size());
    if (lit && lightCulling_ == STRATA_LIGHT_CULLING_CLUSTERED) buildClusters();  // ABI 1.21
    if (lit) writePointLightUniforms(width, height);

    auto t = Clock::now();
    if (shadows) shadowPass(cascades);
    if (lit && pointShadowCount_ > 0) pointShadowPass();
    stats_.cpu_ms_shadow = msSince(t);

    t = Clock::now();
    opaquePass(width, height);
    stats_.cpu_ms_opaque = msSince(t);

    t = Clock::now();
    if (sun && (frame_.clear_flags & STRATA_CLEAR_SKY)) skyPass(width, height);
    stats_.cpu_ms_sky = msSince(t);

    translucentPass(width, height);

    stats_.draw_calls += overlay_->drawWorld(offscreenTarget_, width, height, frameUniforms_, sizeof(FrameUniforms));
    stats_.draw_calls += overlay_->drawText(offscreenTarget_, width, height, camera_);

    if (host) device_.restoreHostState();
    stats_.cpu_ms_frame = msSince(frameStart);
}

void Renderer::readPixels(std::vector<uint8_t>& rgbaTopDown, int& width, int& height) {
    width = lastWidth_;
    height = lastHeight_;
    if (width <= 0 || height <= 0) {
        fail(STRATA_ERROR_INVALID_STATE, "no frame has been rendered to read back");
    }
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    std::vector<uint8_t> bottomUp(rowBytes * static_cast<size_t>(height));
    const bool host = surface_.isHostOwned();
    if (host) device_.captureHostState();
    device_.readPixels(surface_.needsOffscreenTarget() ? offscreenTarget_ : rhi::TargetHandle{}, 0, 0, width, height,
                       bottomUp.data());
    if (host) device_.restoreHostState();
    rgbaTopDown.resize(bottomUp.size());
    for (int y = 0; y < height; ++y) {
        std::memcpy(rgbaTopDown.data() + static_cast<size_t>(y) * rowBytes,
                    bottomUp.data() + static_cast<size_t>(height - 1 - y) * rowBytes, rowBytes);
    }
}

void Renderer::readDepth(std::vector<float>& depthTopDown, int& width, int& height) {
    if (!surface_.needsOffscreenTarget()) {
        fail(STRATA_ERROR_UNSUPPORTED,
             "depth readback needs a headless (offscreen) surface; this surface draws into a window or host framebuffer");
    }
    width = lastWidth_;
    height = lastHeight_;
    if (width <= 0 || height <= 0 || !offscreenTarget_) {
        fail(STRATA_ERROR_INVALID_STATE, "no frame has been rendered to read back");
    }
    if (!depthPackTarget_ || depthPackWidth_ != width || depthPackHeight_ != height) {
        device_.destroy(depthPackTarget_);
        device_.destroy(depthPackColor_);
        rhi::TextureDesc color;
        color.width = width;
        color.height = height;
        color.format = rhi::TextureFormat::RGBA8;
        color.nearest = true;
        color.debugName = "strata.depth-pack";
        depthPackColor_ = device_.createTexture(color);
        rhi::TargetDesc target;
        target.color.texture = depthPackColor_;
        target.debugName = "strata.depth-pack";
        depthPackTarget_ = device_.createTarget(target);
        depthPackWidth_ = width;
        depthPackHeight_ = height;
    }
    // GLES 3.0 / WebGL 2 cannot glReadPixels a depth attachment, so on every
    // device a fullscreen pass copies its float bits into RGBA8 texels.
    rhi::PassDesc pass;
    pass.name = "depth_pack";
    pass.target = depthPackTarget_;
    pass.width = width;
    pass.height = height;
    device_.beginPass(pass);
    device_.bindPipeline(depthPackPipeline_);
    device_.bindTexture(0, offscreenDepth_);
    device_.drawFullscreenTriangle();
    device_.endPass();

    const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> bottomUp(pixels * 4);
    device_.readPixels(depthPackTarget_, 0, 0, width, height, bottomUp.data());
    depthTopDown.resize(pixels);
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = bottomUp.data() + static_cast<size_t>(height - 1 - y) * static_cast<size_t>(width) * 4;
        float* out = depthTopDown.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
        for (int x = 0; x < width; ++x) {
            out[x] = planarDepth(lastProjection_, unpackDepthTexel(row + static_cast<size_t>(x) * 4));
        }
    }
}

}  // namespace strata::render
