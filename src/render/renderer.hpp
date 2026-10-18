// The renderer: BN's primitives through the fixed pass list (shadow, opaque,
// sky, readback), drawn with GpuDevice only.
#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "platform/surface.hpp"
#include "render/cascades.hpp"
#include "render/overlay.hpp"
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
    void setPointLights(const StrataPointLight* lights, size_t count);
    void setTerrain(const StrataTerrainTile* tile);
    uint32_t addMesh(const StrataMesh& mesh);
    void removeMesh(uint32_t id);
    void clearMeshes();

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
    // Draws the terrain and every static mesh with whichever terrain-format
    // pipeline the caller picks (nothing is bound when there are none).
    void drawWorldMeshes(rhi::PipelineHandle pipeline);
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

    // Static meshes (ABI 1.4): the terrain's vertex format and pipelines.
    struct MeshEntry {
        uint32_t id = 0;
        rhi::BufferHandle vertices, indices;
        uint32_t indexCount = 0;
    };
    std::vector<MeshEntry> meshes_;
    uint32_t nextMeshId_ = 1;

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
    std::vector<StrataPointLight> pointLights_;
    StrataFrameDesc frame_{};
    StrataFrameStats stats_{};
    bool frameOpen_ = false;
    int itemsRejected_ = 0;
    int lastWidth_ = 0, lastHeight_ = 0;
};

}  // namespace strata::render
