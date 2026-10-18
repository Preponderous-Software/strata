#include "render/overlay.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

#include "core/error.hpp"
#include "render/font8x8.hpp"
#include "render/shaders.hpp"

namespace strata::render {
namespace {

constexpr uint32_t kKnownPointFlags = STRATA_POINT_SOFT;
constexpr uint32_t kKnownTextFlags = STRATA_TEXT_SHADOW;
constexpr float kMaxTextPixelSize = 512.0f;
constexpr size_t kMaxTextBytes = 65536;

bool finite(const float* values, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (!std::isfinite(values[i])) return false;
    }
    return true;
}

bool unitColor(const float color[4]) {
    if (!finite(color, 4)) return false;
    for (int i = 0; i < 4; ++i) {
        if (color[i] < 0.0f) return false;
    }
    return color[3] <= 1.0f;
}

}  // namespace

void validateLineVertices(const StrataLineVertex* vertices, size_t count) {
    if (count % 2 != 0) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "line vertex count must be even (pairs make segments), got %zu", count);
    }
    if (count > 0 && !vertices) fail(STRATA_ERROR_INVALID_ARGUMENT, "line vertices are NULL with a non-zero count");
    for (size_t i = 0; i < count; ++i) {
        if (!finite(vertices[i].position, 3)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "line vertex %zu: position must be finite", i);
        }
        if (!unitColor(vertices[i].color)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "line vertex %zu: colour must be finite and non-negative, alpha <= 1", i);
        }
    }
}

void validatePoints(const StrataPoint* points, size_t count) {
    if (count > 0 && !points) fail(STRATA_ERROR_INVALID_ARGUMENT, "points are NULL with a non-zero count");
    for (size_t i = 0; i < count; ++i) {
        const StrataPoint& p = points[i];
        if (!finite(p.position, 3)) fail(STRATA_ERROR_INVALID_ARGUMENT, "point %zu: position must be finite", i);
        if (!std::isfinite(p.size_pixels) || p.size_pixels < 0.0f) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "point %zu: size_pixels must be finite and >= 0", i);
        }
        if (!std::isfinite(p.world_diameter) || p.world_diameter < 0.0f) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "point %zu: world_diameter must be finite and >= 0", i);
        }
        if (!unitColor(p.color)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "point %zu: colour must be finite and non-negative, alpha <= 1", i);
        }
        if (p.flags & ~kKnownPointFlags) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "point %zu: unknown flag bits 0x%x", i, p.flags & ~kKnownPointFlags);
        }
    }
}

void validateTexts(const StrataText* texts, size_t count) {
    if (count > 0 && !texts) fail(STRATA_ERROR_INVALID_ARGUMENT, "texts are NULL with a non-zero count");
    for (size_t i = 0; i < count; ++i) {
        const StrataText& t = texts[i];
        if (!std::isfinite(t.x) || !std::isfinite(t.y)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "text %zu: position must be finite", i);
        }
        if (!(t.pixel_size >= 1.0f && t.pixel_size <= kMaxTextPixelSize)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "text %zu: pixel_size must be 1..%g", i,
                 static_cast<double>(kMaxTextPixelSize));
        }
        if (!unitColor(t.color)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "text %zu: colour must be finite and non-negative, alpha <= 1", i);
        }
        if (t.flags & ~kKnownTextFlags) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "text %zu: unknown flag bits 0x%x", i, t.flags & ~kKnownTextFlags);
        }
        if (!t.utf8) fail(STRATA_ERROR_INVALID_ARGUMENT, "text %zu: utf8 is NULL", i);
        if (std::strlen(t.utf8) > kMaxTextBytes) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "text %zu: longer than %zu bytes", i, kMaxTextBytes);
        }
    }
}

int glyphIndex(unsigned char c) {
    if (c < kFontFirstGlyph || c >= kFontFirstGlyph + kFontGlyphCount) c = '?';
    return c - kFontFirstGlyph;
}

void layoutText(const StrataText& text, std::vector<GlyphInstance>& out) {
    const float size = text.pixel_size;
    const bool shadow = (text.flags & STRATA_TEXT_SHADOW) != 0;
    const float shadowOffset = std::max(1.0f, size / 8.0f);
    float x = text.x, y = text.y;
    for (const char* p = text.utf8; *p; ++p) {
        const auto c = static_cast<unsigned char>(*p);
        if (c == '\n') {
            x = text.x;
            y += size;
            continue;
        }
        // A UTF-8 sequence is one character: show one '?' for its lead byte
        // and skip the continuation bytes.
        if ((c & 0xC0) == 0x80) continue;
        if (c != ' ') {
            GlyphInstance g;
            g.size = size;
            g.glyph = static_cast<float>(glyphIndex(c));
            if (shadow) {
                g.x = x + shadowOffset;
                g.y = y + shadowOffset;
                g.color[3] = text.color[3];
                out.push_back(g);
            }
            g.x = x;
            g.y = y;
            std::memcpy(g.color, text.color, sizeof g.color);
            out.push_back(g);
        }
        x += size;
    }
}

std::vector<uint8_t> buildFontAtlas() {
    const int width = kFontGlyphCount * 8;
    std::vector<uint8_t> texels(static_cast<size_t>(width) * 8, 0);
    for (int glyph = 0; glyph < kFontGlyphCount; ++glyph) {
        for (int row = 0; row < 8; ++row) {
            const unsigned bits = kFont8x8[glyph][row];
            for (int col = 0; col < 8; ++col) {
                if ((bits >> col) & 1u) texels[static_cast<size_t>(row * width + glyph * 8 + col)] = 255;
            }
        }
    }
    return texels;
}

Overlay::Overlay(rhi::GpuDevice& device) : device_(device) {
    using namespace rhi;

    PipelineDesc line;
    line.name = "overlay.lines";
    line.vertexSource = shaders::kLineVertex;
    line.fragmentSource = shaders::kLineFragment;
    line.buffers = {{0, sizeof(StrataLineVertex), false}};
    line.attributes = {{0, 0, 3, AttribType::Float, offsetof(StrataLineVertex, position)},
                       {1, 0, 4, AttribType::Float, offsetof(StrataLineVertex, color)}};
    line.topology = Topology::Lines;
    line.depthTest = DepthTest::LessEqual;
    line.depthWrite = false;
    line.depthClamp = true;
    line.cull = CullMode::None;
    line.blend = BlendMode::Alpha;
    linePipeline_ = device_.createPipeline(line);

    PipelineDesc point;
    point.name = "overlay.points";
    point.vertexSource = shaders::kPointVertex;
    point.fragmentSource = shaders::kSolidPointFragment;
    point.buffers = {{0, sizeof(StrataPoint), true}};
    point.attributes = {{0, 0, 3, AttribType::Float, offsetof(StrataPoint, position)},
                        {1, 0, 2, AttribType::Float, offsetof(StrataPoint, size_pixels)},
                        {2, 0, 4, AttribType::Float, offsetof(StrataPoint, color)}};
    point.depthTest = DepthTest::LessEqual;
    point.depthWrite = false;
    point.depthClamp = true;
    point.cull = CullMode::None;
    point.blend = BlendMode::Alpha;
    solidPointPipeline_ = device_.createPipeline(point);
    point.name = "overlay.points.soft";
    point.fragmentSource = shaders::kSoftPointFragment;
    point.blend = BlendMode::Additive;
    softPointPipeline_ = device_.createPipeline(point);

    PipelineDesc text;
    text.name = "overlay.text";
    text.vertexSource = shaders::kTextVertex;
    text.fragmentSource = shaders::kTextFragment;
    text.buffers = {{0, sizeof(GlyphInstance), true}};
    text.attributes = {{0, 0, 4, AttribType::Float, offsetof(GlyphInstance, x)},
                       {1, 0, 4, AttribType::Float, offsetof(GlyphInstance, color)}};
    text.depthTest = DepthTest::Off;
    text.depthWrite = false;
    text.cull = CullMode::None;
    text.blend = BlendMode::Alpha;
    textPipeline_ = device_.createPipeline(text);

    const std::vector<uint8_t> atlas = buildFontAtlas();
    TextureDesc font;
    font.width = kFontGlyphCount * 8;
    font.height = 8;
    font.format = TextureFormat::R8;
    font.nearest = true;
    font.initialData = atlas.data();
    font.debugName = "strata.overlay-font";
    font_ = device_.createTexture(font);

    lineBuffer_ = device_.createBuffer(256 * sizeof(StrataLineVertex), nullptr, true, "strata.overlay-lines");
    pointBuffer_ = device_.createBuffer(256 * sizeof(StrataPoint), nullptr, true, "strata.overlay-points");
    glyphBuffer_ = device_.createBuffer(256 * sizeof(GlyphInstance), nullptr, true, "strata.overlay-glyphs");
    viewportUniforms_ = device_.createBuffer(16, nullptr, true, "strata.overlay-viewport");
}

Overlay::~Overlay() {
    device_.destroy(linePipeline_);
    device_.destroy(solidPointPipeline_);
    device_.destroy(softPointPipeline_);
    device_.destroy(textPipeline_);
    device_.destroy(lineBuffer_);
    device_.destroy(pointBuffer_);
    device_.destroy(glyphBuffer_);
    device_.destroy(viewportUniforms_);
    device_.destroy(font_);
}

void Overlay::clear() {
    lines_.clear();
    solidPoints_.clear();
    softPoints_.clear();
    glyphs_.clear();
}

void Overlay::addLines(const StrataLineVertex* vertices, size_t count) {
    validateLineVertices(vertices, count);
    lines_.insert(lines_.end(), vertices, vertices + count);
}

void Overlay::addPoints(const StrataPoint* points, size_t count) {
    validatePoints(points, count);
    for (size_t i = 0; i < count; ++i) {
        (points[i].flags & STRATA_POINT_SOFT ? softPoints_ : solidPoints_).push_back(points[i]);
    }
}

void Overlay::addText(const StrataText* texts, size_t count) {
    validateTexts(texts, count);
    for (size_t i = 0; i < count; ++i) layoutText(texts[i], glyphs_);
}

template <typename T>
void Overlay::upload(rhi::BufferHandle buffer, const std::vector<T>& data) {
    const size_t bytes = data.size() * sizeof(T);
    if (bytes > device_.bufferSize(buffer)) device_.resizeBuffer(buffer, std::max(bytes, device_.bufferSize(buffer) * 2));
    device_.updateBuffer(buffer, 0, bytes, data.data());
}

void Overlay::writeViewport(int width, int height) {
    const float w = static_cast<float>(std::max(width, 1)), h = static_cast<float>(std::max(height, 1));
    const float viewport[4] = {w, h, 1.0f / w, 1.0f / h};
    device_.updateBuffer(viewportUniforms_, 0, sizeof viewport, viewport);
}

int Overlay::drawWorld(rhi::TargetHandle target, int width, int height, rhi::BufferHandle frameUniforms,
                       size_t frameBytes) {
    if (lines_.empty() && solidPoints_.empty() && softPoints_.empty()) return 0;
    writeViewport(width, height);
    int draws = 0;
    rhi::PassDesc pass;
    pass.name = "overlay";
    pass.target = target;
    pass.width = width;
    pass.height = height;
    device_.beginPass(pass);
    device_.bindUniformBuffer(0, frameUniforms, 0, frameBytes);
    device_.bindUniformBuffer(3, viewportUniforms_, 0, 16);
    if (!lines_.empty()) {
        upload(lineBuffer_, lines_);
        device_.bindPipeline(linePipeline_);
        device_.bindVertexBuffer(0, lineBuffer_, 0);
        rhi::DrawArrays draw;
        draw.vertexCount = static_cast<uint32_t>(lines_.size());
        device_.drawArrays(draw);
        ++draws;
    }
    if (!solidPoints_.empty() || !softPoints_.empty()) {
        // Solid points first, soft (additive) glows over them, in one buffer.
        const size_t bytes = pointCount() * sizeof(StrataPoint);
        if (bytes > device_.bufferSize(pointBuffer_)) {
            device_.resizeBuffer(pointBuffer_, std::max(bytes, device_.bufferSize(pointBuffer_) * 2));
        }
        device_.updateBuffer(pointBuffer_, 0, solidPoints_.size() * sizeof(StrataPoint), solidPoints_.data());
        device_.updateBuffer(pointBuffer_, solidPoints_.size() * sizeof(StrataPoint),
                             softPoints_.size() * sizeof(StrataPoint), softPoints_.data());
        const std::pair<rhi::PipelineHandle, size_t> batches[2] = {{solidPointPipeline_, solidPoints_.size()},
                                                                   {softPointPipeline_, softPoints_.size()}};
        uint32_t base = 0;
        for (const auto& [pipeline, count] : batches) {
            if (count > 0) {
                device_.bindPipeline(pipeline);
                device_.bindVertexBuffer(0, pointBuffer_, 0);
                rhi::DrawArrays draw;
                draw.vertexCount = 6;
                draw.instanceCount = static_cast<uint32_t>(count);
                draw.baseInstance = base;
                device_.drawArrays(draw);
                ++draws;
            }
            base += static_cast<uint32_t>(count);
        }
    }
    device_.endPass();
    return draws;
}

int Overlay::drawText(rhi::TargetHandle target, int width, int height) {
    if (glyphs_.empty()) return 0;
    writeViewport(width, height);
    upload(glyphBuffer_, glyphs_);
    rhi::PassDesc pass;
    pass.name = "text";
    pass.target = target;
    pass.width = width;
    pass.height = height;
    device_.beginPass(pass);
    device_.bindPipeline(textPipeline_);
    device_.bindUniformBuffer(3, viewportUniforms_, 0, 16);
    device_.bindTexture(3, font_);
    device_.bindVertexBuffer(0, glyphBuffer_, 0);
    rhi::DrawArrays draw;
    draw.vertexCount = 6;
    draw.instanceCount = static_cast<uint32_t>(glyphs_.size());
    device_.drawArrays(draw);
    device_.endPass();
    return 1;
}

}  // namespace strata::render
