#include "render/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <glm/gtc/type_ptr.hpp>

#include "core/error.hpp"
#include "core/log.hpp"
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
};
static_assert(sizeof(FrameUniforms) == 640, "FrameUniforms must match the std140 Frame block");

// std140 mirror of the PointLights block in shaders.hpp.
struct PointLightUniforms {
    glm::ivec4 info;
    glm::vec4 positionRange[STRATA_MAX_POINT_LIGHTS];
    glm::vec4 color[STRATA_MAX_POINT_LIGHTS];
};
static_assert(sizeof(PointLightUniforms) == 16 + 2 * 16 * STRATA_MAX_POINT_LIGHTS,
              "PointLightUniforms must match the std140 PointLights block");

constexpr uint32_t kInstanceStride = sizeof(StrataRenderItem);
static_assert(kInstanceStride == 44, "StrataRenderItem must stay 44 bytes (Beyond Nations' RenderItem)");

bool finite(float v) { return std::isfinite(v); }

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
    device_.destroy(meshVertices_);
    device_.destroy(meshIndices_);
    device_.destroy(instances_);
    device_.destroy(frameUniforms_);
    device_.destroy(shadowUniforms_);
    device_.destroy(pointLightUniforms_);
    device_.destroy(pointShadowUniforms_);
    for (auto target : pointShadowTargets_) device_.destroy(target);
    device_.destroy(pointShadowMaps_);
    device_.destroy(pointShadowPipeline_);
    clearMeshes();
    device_.destroy(terrainVertices_);
    device_.destroy(terrainIndices_);
    device_.destroy(terrainLegacyPipeline_);
    device_.destroy(terrainSunPipeline_);
    device_.destroy(terrainShadowPipeline_);
    device_.destroy(terrainPointShadowPipeline_);
    for (auto target : shadowTargets_) device_.destroy(target);
    device_.destroy(shadowMaps_);
    device_.destroy(offscreenTarget_);
    device_.destroy(offscreenColor_);
    device_.destroy(offscreenDepth_);
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
    meshIndices_ = device_.createBuffer(indices.size() * sizeof(uint32_t), indices.data(), false, "strata.primitive-indices");
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
    if (l.mode != STRATA_LIGHTING_LEGACY && l.mode != STRATA_LIGHTING_SUN) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "unknown lighting mode %d", l.mode);
    }
    if (l.mode == STRATA_LIGHTING_LEGACY) {
        const glm::vec3 d(l.legacy_direction[0], l.legacy_direction[1], l.legacy_direction[2]);
        if (!allFinite(l.legacy_direction, 3) || glm::length(d) < 1e-6f) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "legacy light direction must be a finite, non-zero vector");
        }
        if (!(l.legacy_ambient >= 0.0f && l.legacy_ambient <= 1.0f)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "legacy ambient must be in [0, 1]");
        }
        return;
    }
    if (!finite(l.time_of_day) || !finite(l.latitude_degrees) || std::fabs(l.latitude_degrees) > 90.0f) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "time of day must be finite and latitude within [-90, 90]");
    }
    if (l.cascade_count < 0 || l.cascade_count > kMaxCascades) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "cascade_count must be 0..%d (got %d)", kMaxCascades, l.cascade_count);
    }
    if (l.cascade_count > 0) {
        if (l.shadow_map_size < 256 || l.shadow_map_size > 4096) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "shadow_map_size must be 256..4096 (got %d)", l.shadow_map_size);
        }
        if (!(l.shadow_distance > 0.0f) || !finite(l.shadow_distance)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "shadow_distance must be positive");
        }
        if (!(l.split_lambda >= 0.0f && l.split_lambda <= 1.0f)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "split_lambda must be in [0, 1]");
        }
    }
}

void Renderer::setCamera(const StrataCamera& camera) {
    validateCamera(camera);
    camera_ = camera;
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

void Renderer::setPointLights(const StrataPointLight* lights, size_t count) {
    validatePointLights(lights, count);
    pointLights_.assign(lights, lights + count);
    pointShadowSlot_.assign(count, -1);
    pointShadowCount_ = 0;
    for (size_t i = 0; i < count; ++i) {
        if (pointLights_[i].flags & STRATA_POINT_LIGHT_CAST_SHADOWS) pointShadowSlot_[i] = pointShadowCount_++;
    }
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
    const rhi::BufferHandle indices = device_.createBuffer(mesh.indices.size() * sizeof(uint32_t),
                                                           mesh.indices.data(), false, "strata.terrain-indices");
    device_.destroy(terrainVertices_);
    device_.destroy(terrainIndices_);
    terrainVertices_ = vertices;
    terrainIndices_ = indices;
    terrainIndexCount_ = static_cast<uint32_t>(mesh.indices.size());
}

uint32_t Renderer::addMesh(const StrataMesh& m) {
    if (meshes_.size() >= static_cast<size_t>(STRATA_MAX_MESHES)) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "mesh: the scene already holds STRATA_MAX_MESHES (%d) meshes",
             STRATA_MAX_MESHES);
    }
    if (nextMeshId_ == 0) fail(STRATA_ERROR_INVALID_STATE, "mesh: ids exhausted");
    const StaticMesh mesh = buildStaticMesh(m);  // validates first; throws before anything changes
    MeshEntry entry;
    entry.vertices = device_.createBuffer(mesh.vertices.size() * sizeof(float), mesh.vertices.data(), false,
                                          "strata.mesh-vertices");
    entry.indices = device_.createBuffer(mesh.indices.size() * sizeof(uint32_t), mesh.indices.data(), false,
                                         "strata.mesh-indices");
    entry.indexCount = static_cast<uint32_t>(mesh.indices.size());
    entry.id = nextMeshId_++;
    meshes_.push_back(entry);
    return entry.id;
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
    for (size_t i = 0; i < pointLights_.size(); ++i) {
        const int slot = pointShadowSlot_[i];
        if (slot < 0) continue;
        const StrataPointLight& l = pointLights_[i];
        const glm::vec3 position(l.position[0], l.position[1], l.position[2]);
        const auto faces = cubeFaceViewProjections(position, l.range);
        for (size_t face = 0; face < 6; ++face) {
            const size_t layer = static_cast<size_t>(slot) * 6 + face;
            const FaceUniforms u{faces[face], glm::vec4(position, l.range)};
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
            drawWorldMeshes(terrainPointShadowPipeline_);
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

    if (lighting_.mode == STRATA_LIGHTING_LEGACY) {
        u.light = glm::vec4(lighting_.legacy_direction[0], lighting_.legacy_direction[1],
                            lighting_.legacy_direction[2], lighting_.legacy_ambient);
    } else {
        const SkyState sky = evaluateSky(lighting_.time_of_day, lighting_.latitude_degrees);
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

void Renderer::writePointLightUniforms() {
    PointLightUniforms u{};
    u.info = glm::ivec4(static_cast<int>(pointLights_.size()), 0, 0, 0);
    for (size_t i = 0; i < pointLights_.size(); ++i) {
        const StrataPointLight& l = pointLights_[i];
        u.positionRange[i] = glm::vec4(l.position[0], l.position[1], l.position[2], l.range);
        u.color[i] = glm::vec4(glm::vec3(l.color[0], l.color[1], l.color[2]) * l.intensity,
                               static_cast<float>(pointShadowSlot_[i]));
    }
    u.info.y = kPointShadowMapSize;
    device_.updateBuffer(pointLightUniforms_, 0, sizeof u, &u);
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

void Renderer::drawWorldMeshes(rhi::PipelineHandle pipeline) {
    if (terrainIndexCount_ == 0 && meshes_.empty()) return;
    device_.bindPipeline(pipeline);
    drawTerrain();
    for (const auto& entry : meshes_) {
        device_.bindVertexBuffer(0, entry.vertices, 0);
        device_.bindIndexBuffer(entry.indices);
        rhi::DrawIndexed draw;
        draw.indexCount = entry.indexCount;
        device_.drawIndexed(draw);
        ++stats_.draw_calls;
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
        drawWorldMeshes(terrainShadowPipeline_);
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
    if (lighting_.mode == STRATA_LIGHTING_SUN) {
        device_.bindTexture(1, shadowMaps_);
        device_.bindTexture(2, pointShadowMaps_);
        device_.bindUniformBuffer(2, pointLightUniforms_, 0, sizeof(PointLightUniforms));
    }
    drawPrimitives(false);
    drawWorldMeshes(lighting_.mode == STRATA_LIGHTING_LEGACY ? terrainLegacyPipeline_ : terrainSunPipeline_);
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

    Cascades cascades;
    const bool sun = lighting_.mode == STRATA_LIGHTING_SUN;
    bool shadows = false;
    if (sun && lighting_.cascade_count > 0) {
        const SkyState sky = evaluateSky(lighting_.time_of_day, lighting_.latitude_degrees);
        shadows = sky.shadowStrength > 0.0f;
        if (shadows) {
            ensureShadowMaps(lighting_.shadow_map_size, lighting_.cascade_count);
            CascadeInput input;
            input.view = glm::make_mat4(camera_.view);
            input.tanHalfFovY = 1.0f / camera_.projection[5];
            input.aspect = camera_.projection[5] / camera_.projection[0];
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
    if (sun && !shadowMaps_) {
        // The sun shader always samples a shadow array; give it a tiny one.
        ensureShadowMaps(256, 1);
    }
    // Likewise a cube array, even with no shadowed point lights.
    if (sun) ensurePointShadowMaps(std::max(pointShadowCount_, 1));
    writeFrameUniforms(width, height, shadows ? &cascades : nullptr);
    if (sun) writePointLightUniforms();

    auto t = Clock::now();
    if (shadows) shadowPass(cascades);
    if (sun && pointShadowCount_ > 0) pointShadowPass();
    stats_.cpu_ms_shadow = msSince(t);

    t = Clock::now();
    opaquePass(width, height);
    stats_.cpu_ms_opaque = msSince(t);

    t = Clock::now();
    if (sun && (frame_.clear_flags & STRATA_CLEAR_SKY)) skyPass(width, height);
    stats_.cpu_ms_sky = msSince(t);

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

}  // namespace strata::render
