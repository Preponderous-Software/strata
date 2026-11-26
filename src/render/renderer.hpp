// The renderer: BN's primitives through the fixed pass list in pass_list.cpp
// (shadow, point_shadow, opaque, sky, translucent, overlay, text, readback),
// drawn with GpuDevice only.
#pragma once

#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "platform/surface.hpp"
#include "render/cascades.hpp"
#include "render/clusters.hpp"
#include "render/overlay.hpp"
#include "render/ray_pick.hpp"
#include "render/sun.hpp"
#include "rhi/gpu_device.hpp"
#include "strata/strata.h"

namespace strata::render {

class Renderer {
public:
    Renderer(rhi::GpuDevice& device, platform::Surface& surface);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void setCamera(const StrataCamera& camera);
    void setLighting(const StrataLighting& lighting);
    // ABI 1.6. Unit vector towards the sun for STRATA_LIGHTING_SUN, used
    // instead of time_of_day and latitude; nullptr goes back to them.
    void setSunDirection(const float* toSun);
    // ABI 1.16. Ambient tint and lift (SUN and SPACE) and the key light's
    // intensity and tint (SUN); nullptr means the default (1s, or 0s for the
    // lift). They stay across setLighting.
    void setAmbient(const float* tint, const float* lift);
    void setSunIntensity(float intensity, const float* tint);
    void setPointLights(const StrataPointLight* lights, size_t count);
    void setSpotLights(const StrataSpotLight* lights, size_t count);  // ABI 1.20
    void setColorOutput(uint32_t flags, float exposure);              // ABI 1.20
    // ABI 1.21: STRATA_LIGHT_CULLING_*. Back to NONE with more lights set
    // than it allows throws INVALID_STATE.
    void setLightCulling(int32_t mode);
    const StrataLightStats& lightStats() const { return lightStats_; }  // ABI 1.21
    void setTerrain(const StrataTerrainTile* tile);
    uint32_t addMesh(const StrataMesh& mesh);
    uint32_t addTexturedMesh(const StrataTexturedMesh& mesh);  // ABI 1.7
    void setMeshTransform(uint32_t id, const float* model);    // ABI 1.7; nullptr = identity
    void setMeshVisible(uint32_t id, bool visible);            // ABI 1.19
    void setMeshMaterial(uint32_t id, const StrataMaterial* material);  // ABI 1.20; nullptr = default
    void removeMesh(uint32_t id);
    void clearMeshes();
    // ABI 1.7.
    uint32_t createTexture(int32_t width, int32_t height, const uint8_t* rgba, uint32_t flags);
    void destroyTexture(uint32_t id);
    // ABI 1.8.
    void updateTexture(uint32_t id, int32_t x, int32_t y, int32_t width, int32_t height, const uint8_t* rgba);
    StrataRayHit raycastMeshes(const float* origin, const float* direction, float maxDistance) const;

    void beginFrame(const StrataFrameDesc& desc);
    void submit(const StrataRenderItem* items, size_t count);
    void submitLines(const StrataLineVertex* vertices, size_t count);
    void submitPoints(const StrataPoint* points, size_t count);
    void submitText(const StrataText* texts, size_t count);
    void submitWorldText(const StrataText* texts, const float* anchors, size_t count);  // ABI 1.14
    // ABI 1.14: with the current camera onto a width x height drawable.
    bool projectPoint(const float world[3], int width, int height, float outPixels[2], float* outDepth) const;
    void submitLines(const StrataLineVertex* vertices, size_t count, uint32_t flags);  // ABI 1.11
    void submitRects(const StrataRect* rects, size_t count);                          // ABI 1.11
    void submitWideLines(const StrataWideLineVertex* vertices, size_t count, uint32_t flags);  // ABI 1.13
    void endFrame();

    const StrataFrameStats& stats() const { return stats_; }
    void readPixels(std::vector<uint8_t>& rgbaTopDown, int& width, int& height);
    // ABI 1.9. The last frame's planar view distance per pixel, top row first;
    // +infinity where nothing was drawn. Offscreen (headless) targets only:
    // throws UNSUPPORTED on a window or host context.
    void readDepth(std::vector<float>& depthTopDown, int& width, int& height);

    static void validateCamera(const StrataCamera& camera);
    static void validateLighting(const StrataLighting& lighting);

private:
    struct KindRange {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        int32_t baseVertex = 0;
    };

    void buildMeshes();
    void buildPipelines();
    void ensureOffscreenTarget(int width, int height);
    void ensureShadowMaps(int size, int layers);
    void uploadInstances();
    void writeFrameUniforms(int width, int height, const Cascades* cascades);
    void writePointLightUniforms(int width, int height);
    void drawPrimitives(bool forShadow);
    void drawTerrain();
    // Draws the terrain with terrainPipeline, then the static meshes, each
    // with the variant of the mesh pipeline its flags and material ask for.
    // Shadow passes skip unlit meshes; the opaque pass draws them last.
    enum class MeshPass { Shadow, PointShadow, Opaque };
    void drawWorldMeshes(rhi::PipelineHandle terrainPipeline, MeshPass pass);

    void writeMeshUniforms();
    void translucentPass(int width, int height);  // ABI 1.11
    uint32_t addMeshEntry(const StrataMesh& mesh, const float* uvs, uint32_t texture, uint32_t flags);
    void shadowPass(const Cascades& cascades);
    void ensurePointShadowMaps(int cubes);
    void pointShadowPass();
    void opaquePass(int width, int height);
    void skyPass(int width, int height);

    rhi::GpuDevice& device_;
    platform::Surface& surface_;

    // Geometry: the four primitives in one vertex and one index buffer.
    rhi::BufferHandle meshVertices_, meshIndices_;
    std::array<KindRange, STRATA_PRIMITIVE_KIND_COUNT> kinds_{};

    // Per-frame instances, partitioned by kind as they are submitted, then
    // uploaded back to back so each kind is one draw with a base instance.
    std::array<std::vector<StrataRenderItem>, STRATA_PRIMITIVE_KIND_COUNT> perKind_;
    std::array<uint32_t, STRATA_PRIMITIVE_KIND_COUNT> baseInstance_{};
    rhi::BufferHandle instances_;

    rhi::BufferHandle frameUniforms_, shadowUniforms_, pointLightUniforms_;
    size_t shadowUniformStride_ = 256;

    rhi::PipelineHandle legacyPipeline_, sunPipeline_, shadowPipeline_, skyPipeline_, pointShadowPipeline_;

    // The terrain tile (T1): its own mesh and pipelines.
    rhi::BufferHandle terrainVertices_, terrainIndices_;
    uint32_t terrainIndexCount_ = 0;
    rhi::PipelineHandle terrainLegacyPipeline_, terrainSunPipeline_, terrainShadowPipeline_,
        terrainPointShadowPipeline_;

    // Static mesh pipelines (ABI 1.20 made them variants): which pass, which
    // shading, two-sided faces (opaque pass only) and alpha cutout.
    static constexpr uint8_t kMeshOpaque = 0, kMeshTranslucent = 1, kMeshShadow = 2, kMeshPointShadow = 3;
    static constexpr uint8_t kShadingLegacy = 0, kShadingSun = 1, kShadingUnlit = 2;
    struct MeshVariant {
        uint8_t pass = kMeshOpaque;
        uint8_t shading = kShadingLegacy;  // 0 in the shadow passes
        uint8_t doubleSided = 0;           // opaque pass only
        uint8_t cutout = 0;
        uint32_t key() const {
            return static_cast<uint32_t>(pass) | (static_cast<uint32_t>(shading) << 8) |
                   (static_cast<uint32_t>(doubleSided) << 16) | (static_cast<uint32_t>(cutout) << 24);
        }
    };
    struct CachedPipeline {
        rhi::PipelineHandle handle;
        std::string vertex, fragment;  // kept alive for the pipeline description
    };
    std::map<uint32_t, CachedPipeline> meshPipelines_;
    rhi::PipelineDesc meshPipelineDesc(const MeshVariant& variant, std::string& vertex, std::string& fragment) const;
    rhi::PipelineHandle meshPipeline(const MeshVariant& variant);

    // Static meshes (ABI 1.4; textures, model matrices and unlit in 1.7).
    struct MeshEntry {
        uint32_t id = 0;
        rhi::BufferHandle vertices, indices;
        uint32_t indexCount = 0;
        glm::mat4 model{1.0f};
        uint32_t texture = 0;  // texture id; 0 or a destroyed id draws white
        uint32_t flags = 0;    // STRATA_MESH_*
        float alpha = 1.0f;    // STRATA_MESH_TRANSLUCENT (ABI 1.11): color[3]
        bool visible = true;   // ABI 1.19: a hidden mesh is skipped by every pass and by picking
        StrataMaterial material{};  // ABI 1.20; all zero is the default
        // ABI 1.8: a CPU copy of the model-space positions and indices, and
        // their bounds, for picking.
        PickGeometry pick;
    };
    std::vector<MeshEntry> meshes_;
    // ABI 1.20: binds a mesh's albedo, emissive and normal textures (white
    // where it has none) for the opaque and translucent passes.
    void bindMeshTextures(const MeshEntry& entry);
    // ABI 1.20: throws INVALID_ARGUMENT naming the bad field.
    void validateMaterial(const StrataMaterial& material) const;
    uint32_t nextMeshId_ = 1;
    rhi::BufferHandle meshUniforms_;
    size_t meshUniformStride_ = 256;

    // Textures (ABI 1.7) by id, and the 1 x 1 white texture untextured meshes sample.
    struct TextureEntry {
        uint32_t id = 0;
        rhi::TextureHandle handle;
        int32_t width = 0, height = 0;
    };
    std::vector<TextureEntry> textures_;
    uint32_t nextTextureId_ = 1;
    rhi::TextureHandle whiteTexture_;
    rhi::TextureHandle textureFor(uint32_t id) const;

    // Headless surfaces draw here instead of a default framebuffer.
    rhi::TextureHandle offscreenColor_, offscreenDepth_;
    rhi::TargetHandle offscreenTarget_;
    int offscreenWidth_ = 0, offscreenHeight_ = 0;
    // Depth readback (ABI 1.9): the depth attachment's floats, bit for bit, in
    // an RGBA8 target the size of the offscreen one, made on first use.
    rhi::TextureHandle depthPackColor_;
    rhi::TargetHandle depthPackTarget_;
    int depthPackWidth_ = 0, depthPackHeight_ = 0;
    rhi::PipelineHandle depthPackPipeline_;
    float lastProjection_[16] = {};  // the projection the last frame was drawn with

    rhi::TextureHandle shadowMaps_;
    std::array<rhi::TargetHandle, kMaxCascades> shadowTargets_{};
    int shadowSize_ = 0, shadowLayers_ = 0;

    // Cube shadow maps for shadow-casting point lights (L4): one cube map
    // array, a depth target per layer-face, and the lights' slots this frame.
    rhi::TextureHandle pointShadowMaps_;
    std::vector<rhi::TargetHandle> pointShadowTargets_;
    int pointShadowCubes_ = 0;
    rhi::BufferHandle pointShadowUniforms_;
    size_t pointShadowUniformStride_ = 256;
    std::vector<int> pointShadowSlot_;  // per light: slot or -1
    int pointShadowCount_ = 0;

    // Unlit lines, points and text (ABI 1.4), drawn after the sky.
    std::unique_ptr<Overlay> overlay_;

    StrataCamera camera_{};
    StrataLighting lighting_{};
    bool hasSunDirection_ = false;
    glm::vec3 sunDirection_{0, 1, 0};
    LightLevels levels_{};
    SkyState currentSky() const;
    std::vector<StrataPointLight> pointLights_;
    std::vector<StrataSpotLight> spotLights_;  // ABI 1.20, directions normalised
    // Cube shadow slots: point lights' casters first, then spot lights'.
    void assignShadowSlots();
    std::vector<int> spotShadowSlot_;  // per spot light: slot or -1
    // Clustered light culling (ABI 1.21; render/clusters.hpp). A cluster data
    // texture holds the lights, a header per cluster and the lists (RGBA32F,
    // kClusterDataWidth texels a row), rebuilt every lit frame into the next
    // of a ring of three, so the upload never waits for a frame the GPU may
    // still be drawing with the one before (llvmpipe stalls for that).
    static constexpr int kClusterDataWidth = 1024;  // shaders.hpp clusterTexel: i & 1023, i >> 10
    static constexpr size_t kClusterRing = 3;
    int32_t lightCulling_ = STRATA_LIGHT_CULLING_NONE;
    StrataLightStats lightStats_{};
    std::array<rhi::TextureHandle, kClusterRing> clusterData_{};
    std::array<int, kClusterRing> clusterDataRows_{};
    size_t clusterCurrent_ = 0;  // the ring entry the lit passes bind
    int clusterHeaderBase_ = 0, clusterIndexBase_ = 0;
    std::vector<float> clusterUpload_;
    size_t maxPointLights() const;
    size_t maxSpotLights() const;
    ClusterCamera clusterCamera() const;
    void ensureClusterData(size_t slot, int rows);
    void buildClusters();
    uint32_t outputFlags_ = 0;         // ABI 1.20: STRATA_OUTPUT_*
    float outputExposure_ = 1.0f;
    StrataFrameDesc frame_{};
    StrataFrameStats stats_{};
    bool frameOpen_ = false;
    int itemsRejected_ = 0;
    int lastWidth_ = 0, lastHeight_ = 0;
};

}  // namespace strata::render
