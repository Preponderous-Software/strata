// GpuDevice: the thin internal interface every render pass draws through.
//
// It names the handful of things the passes need -- buffers, textures,
// render targets, pipelines and indexed instanced draws -- and nothing more.
// OpenGL 4.5 with direct state access is the only backend. The interface
// exists so that the passes never touch GL directly, which keeps a later
// backend possible without rewriting them; it is not a promise to write one.
//
// Handles are small typed integers; 0 is "none". Target 0 is the surface's
// default framebuffer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace strata::rhi {

template <typename Tag>
struct Handle {
    uint32_t value = 0;
    explicit operator bool() const { return value != 0; }
    bool operator==(const Handle& other) const { return value == other.value; }
    bool operator!=(const Handle& other) const { return value != other.value; }
};
using BufferHandle = Handle<struct BufferTag>;
using TextureHandle = Handle<struct TextureTag>;
using TargetHandle = Handle<struct TargetTag>;
using PipelineHandle = Handle<struct PipelineTag>;

enum class TextureFormat { RGBA8, R8, Depth32F };

struct TextureDesc {
    int width = 0;
    int height = 0;
    int layers = 0;  // 0: a 2D texture; >= 1: a 2D array of that many layers
    bool cube = false;  // with layers >= 1: a cube map array of `layers` cubes (6 * layers layer-faces)
    TextureFormat format = TextureFormat::RGBA8;
    bool depthCompare = false;  // sample as sampler2DShadow / sampler2DArrayShadow / samplerCubeArrayShadow
    bool nearest = false;       // nearest-texel filtering instead of linear
    // 2D RGBA8 textures only: a full mip chain built from initialData and
    // sampled trilinearly.
    bool mipmaps = false;
    bool repeatU = false, repeatV = false;  // wrap instead of clamping to the edge
    // 2D colour textures only: tightly packed texels, top row first as stored
    // (row 0 is v = 0). nullptr leaves the content undefined.
    const void* initialData = nullptr;
    const char* debugName = nullptr;
};

struct Attachment {
    TextureHandle texture;
    int layer = -1;  // -1: the whole texture; >= 0: one layer of an array (cube arrays: 6 * cube + face)
};

struct TargetDesc {
    Attachment color;  // may be empty (depth-only target)
    Attachment depth;
    const char* debugName = nullptr;
};

enum class AttribType { Float, Int };

struct VertexAttribute {
    uint32_t location = 0;
    uint32_t binding = 0;
    int components = 3;
    AttribType type = AttribType::Float;
    uint32_t offset = 0;
};

struct VertexBufferLayout {
    uint32_t binding = 0;
    uint32_t stride = 0;
    bool perInstance = false;
};

enum class CullMode { None, Back, Front };
enum class Topology { Triangles, Lines };
// Alpha: src * a + dst * (1 - a). Additive: src * a + dst.
enum class BlendMode { None, Alpha, Additive };
enum class DepthTest { Off, Less, LessEqual };

struct PipelineDesc {
    const char* name = "pipeline";
    const char* vertexSource = nullptr;
    const char* fragmentSource = nullptr;
    std::vector<VertexBufferLayout> buffers;
    std::vector<VertexAttribute> attributes;
    DepthTest depthTest = DepthTest::Less;
    bool depthWrite = true;
    bool colorWrite = true;
    CullMode cull = CullMode::Back;
    float depthBiasConstant = 0.0f;  // glPolygonOffset units
    float depthBiasSlope = 0.0f;     // glPolygonOffset factor
    bool depthClamp = false;
    Topology topology = Topology::Triangles;  // what drawArrays assembles; drawIndexed is always triangles
    BlendMode blend = BlendMode::None;
};

struct PassDesc {
    const char* name = "pass";
    TargetHandle target;  // empty: the default framebuffer
    int x = 0, y = 0, width = 0, height = 0;
    bool clearColor = false;
    bool clearDepth = false;
    float color[4] = {0, 0, 0, 1};
    float depth = 1.0f;
};

struct DrawIndexed {
    uint32_t indexCount = 0;
    uint32_t instanceCount = 1;
    uint32_t firstIndex = 0;
    int32_t baseVertex = 0;
    uint32_t baseInstance = 0;
};

struct DrawArrays {
    uint32_t vertexCount = 0;
    uint32_t instanceCount = 1;
    uint32_t firstVertex = 0;
    uint32_t baseInstance = 0;
};

class GpuDevice {
public:
    virtual ~GpuDevice() = default;

    virtual std::string description() const = 0;
    virtual size_t uniformBufferAlignment() const = 0;

    // Resources. Creation failures throw strata::Error.
    virtual BufferHandle createBuffer(size_t bytes, const void* initial, bool dynamic, const char* debugName) = 0;
    // Replaces the buffer's storage with `bytes` of undefined content.
    virtual void resizeBuffer(BufferHandle buffer, size_t bytes) = 0;
    virtual size_t bufferSize(BufferHandle buffer) const = 0;
    virtual void updateBuffer(BufferHandle buffer, size_t offset, size_t bytes, const void* data) = 0;
    virtual TextureHandle createTexture(const TextureDesc& desc) = 0;
    virtual TargetHandle createTarget(const TargetDesc& desc) = 0;
    virtual PipelineHandle createPipeline(const PipelineDesc& desc) = 0;
    virtual void destroy(BufferHandle buffer) = 0;
    virtual void destroy(TextureHandle texture) = 0;
    virtual void destroy(TargetHandle target) = 0;
    virtual void destroy(PipelineHandle pipeline) = 0;

    // Commands, recorded and executed immediately.
    virtual void beginPass(const PassDesc& desc) = 0;
    virtual void bindPipeline(PipelineHandle pipeline) = 0;
    virtual void bindVertexBuffer(uint32_t binding, BufferHandle buffer, size_t offset) = 0;
    virtual void bindIndexBuffer(BufferHandle buffer) = 0;  // 32-bit indices
    virtual void bindUniformBuffer(uint32_t slot, BufferHandle buffer, size_t offset, size_t bytes) = 0;
    virtual void bindTexture(uint32_t unit, TextureHandle texture) = 0;
    virtual void drawIndexed(const DrawIndexed& draw) = 0;
    // Non-indexed, instanced, assembled with the bound pipeline's topology.
    virtual void drawArrays(const DrawArrays& draw) = 0;
    virtual void drawFullscreenTriangle() = 0;
    virtual void endPass() = 0;

    // Reads RGBA8 from a target (empty: default framebuffer), bottom row first.
    virtual void readPixels(TargetHandle target, int x, int y, int width, int height, void* rgba) = 0;
    virtual void finish() = 0;

    // Host-context surfaces: the host's GL state is captured before Strata
    // draws and put back afterwards.
    virtual void captureHostState() = 0;
    virtual void restoreHostState() = 0;
};

// Expects a current GL 4.5 core context whose entry points glad has loaded.
std::unique_ptr<GpuDevice> createGl45Device(bool debugOutput);

}  // namespace strata::rhi
