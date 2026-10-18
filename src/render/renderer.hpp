// The renderer: BN's primitives through the fixed pass list (shadow, opaque,
// sky, readback), drawn with GpuDevice only.
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "platform/surface.hpp"
#include "render/cascades.hpp"
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

    void beginFrame(const StrataFrameDesc& desc);
    void submit(const StrataRenderItem* items, size_t count);
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
    void drawPrimitives(bool forShadow);
    void shadowPass(const Cascades& cascades);
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

    rhi::BufferHandle frameUniforms_, shadowUniforms_;
    size_t shadowUniformStride_ = 256;

    rhi::PipelineHandle legacyPipeline_, sunPipeline_, shadowPipeline_, skyPipeline_;

    // Headless surfaces draw here instead of a default framebuffer.
    rhi::TextureHandle offscreenColor_, offscreenDepth_;
    rhi::TargetHandle offscreenTarget_;
    int offscreenWidth_ = 0, offscreenHeight_ = 0;

    rhi::TextureHandle shadowMaps_;
    std::array<rhi::TargetHandle, kMaxCascades> shadowTargets_{};
    int shadowSize_ = 0, shadowLayers_ = 0;

    StrataCamera camera_{};
    StrataLighting lighting_{};
    StrataFrameDesc frame_{};
    StrataFrameStats stats_{};
    bool frameOpen_ = false;
    int itemsRejected_ = 0;
    int lastWidth_ = 0, lastHeight_ = 0;
};

}  // namespace strata::render
