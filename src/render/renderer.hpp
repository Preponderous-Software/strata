// The renderer: BN's primitives through the fixed pass list (shadow, opaque,
// sky, readback), drawn with GpuDevice only.
#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "platform/surface.hpp"
#include "render/cascades.hpp"
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
    void setPointLights(const StrataPointLight* lights, size_t count);
    void setTerrain(const StrataTerrainTile* tile);
    uint32_t addMesh(const StrataMesh& mesh);
    uint32_t addTexturedMesh(const StrataTexturedMesh& mesh);  // ABI 1.7
    void setMeshTransform(uint32_t id, const float* model);    // ABI 1.7; nullptr = identity
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
    void endFrame();

    const StrataFrameStats& stats() const { return stats_; }
    void readPixels(std::vector<uint8_t>& rgbaTopDown, int& width, int& height);

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
    void writePointLightUniforms();
    void drawPrimitives(bool forShadow);
    void drawTerrain();
    // Draws the terrain with one pipeline and the static meshes with another
    // (nothing is bound when there are none). Shadow passes skip unlit meshes;
    // the opaque pass draws them with unlitPipeline.
    enum class MeshPass { Shadow, Opaque };
    void drawWorldMeshes(rhi::PipelineHandle terrainPipeline, rhi::PipelineHandle meshPipeline, MeshPass pass,
                         rhi::PipelineHandle unlitPipeline = {});
    void writeMeshUniforms();
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

    // Static meshes (ABI 1.4; textures, model matrices and unlit in 1.7).
    struct MeshEntry {
        uint32_t id = 0;
        rhi::BufferHandle vertices, indices;
        uint32_t indexCount = 0;
        glm::mat4 model{1.0f};
        uint32_t texture = 0;  // texture id; 0 or a destroyed id draws white
        uint32_t flags = 0;    // STRATA_MESH_*
        // ABI 1.8: a CPU copy of the model-space positions and indices, and
        // their bounds, for picking.
        PickGeometry pick;
    };
    std::vector<MeshEntry> meshes_;
    uint32_t nextMeshId_ = 1;
    rhi::PipelineHandle meshLegacyPipeline_, meshSunPipeline_, meshUnlitPipeline_, meshShadowPipeline_,
        meshPointShadowPipeline_;
    std::string meshLegacyFragment_, meshSunFragment_;  // kept alive for the pipeline descriptions
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
    SkyState currentSky() const;
    std::vector<StrataPointLight> pointLights_;
    StrataFrameDesc frame_{};
    StrataFrameStats stats_{};
    bool frameOpen_ = false;
    int itemsRejected_ = 0;
    int lastWidth_ = 0, lastHeight_ = 0;
};

}  // namespace strata::render
