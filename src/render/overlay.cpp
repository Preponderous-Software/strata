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

constexpr uint32_t kKnownPointFlags = STRATA_POINT_SOFT | STRATA_POINT_ON_TOP;
constexpr uint32_t kKnownLineFlags = STRATA_LINES_ON_TOP;
constexpr uint32_t kKnownWideLineFlags = STRATA_LINES_ON_TOP | STRATA_LINES_ROUND_CAPS;
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

void validateWideLineVertices(const StrataWideLineVertex* vertices, size_t count) {
    if (count % 2 != 0) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "wide line vertex count must be even (pairs make segments), got %zu", count);
    }
    if (count > 0 && !vertices) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "wide line vertices are NULL with a non-zero count");
    }
    for (size_t i = 0; i < count; ++i) {
        const StrataWideLineVertex& v = vertices[i];
        if (!finite(v.position, 3)) fail(STRATA_ERROR_INVALID_ARGUMENT, "wide line vertex %zu: position must be finite", i);
        if (!unitColor(v.color)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT,
                 "wide line vertex %zu: colour must be finite and non-negative, alpha <= 1", i);
        }
        if (!std::isfinite(v.width_pixels) || v.width_pixels < 0.0f) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "wide line vertex %zu: width_pixels must be finite and >= 0, got %g", i,
                 static_cast<double>(v.width_pixels));
        }
        if (!std::isfinite(v.world_width) || v.world_width < 0.0f) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "wide line vertex %zu: world_width must be finite and >= 0, got %g", i,
                 static_cast<double>(v.world_width));
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

void validateRects(const StrataRect* rects, size_t count) {
    if (count > 0 && !rects) fail(STRATA_ERROR_INVALID_ARGUMENT, "rects are NULL with a non-zero count");
    for (size_t i = 0; i < count; ++i) {
        const StrataRect& r = rects[i];
        if (!std::isfinite(r.x) || !std::isfinite(r.y)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "rect %zu: position must be finite", i);
        }
        if (!std::isfinite(r.width) || !std::isfinite(r.height) || r.width < 0.0f || r.height < 0.0f) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "rect %zu: width and height must be finite and >= 0", i);
        }
        if (!unitColor(r.color)) {
            fail(STRATA_ERROR_INVALID_ARGUMENT, "rect %zu: colour must be finite and non-negative, alpha <= 1", i);
        }
    }
}

void validateAnchors(const float* anchors, size_t count) {
    if (count > 0 && !anchors) fail(STRATA_ERROR_INVALID_ARGUMENT, "anchors are NULL with a non-zero count");
    for (size_t i = 0; i < count; ++i) {
        if (!finite(anchors + 3 * i, 3)) fail(STRATA_ERROR_INVALID_ARGUMENT, "anchor %zu: position must be finite", i);
    }
}

bool projectPoint(const StrataCamera& camera, int width, int height, const float world[3], float outPixels[2],
                  float* outDepth) {
    // Column-major: element (row r, column c) is m[c * 4 + r].
    const float* v = camera.view;
    const float* p = camera.projection;
    float view[4];
    for (int r = 0; r < 4; ++r) view[r] = v[r] * world[0] + v[4 + r] * world[1] + v[8 + r] * world[2] + v[12 + r];
    float clip[4];
    for (int r = 0; r < 4; ++r) {
        clip[r] = p[r] * view[0] + p[4 + r] * view[1] + p[8 + r] * view[2] + p[12 + r] * view[3];
    }
    const float depth = -view[2];  // the camera looks down -Z in view space
    if (outDepth) *outDepth = depth;
    const bool inFront = depth > 0.0f && clip[3] > 0.0f && std::isfinite(clip[0] / clip[3]) &&
                         std::isfinite(clip[1] / clip[3]);
    if (outPixels) {
        outPixels[0] = 0.0f;
        outPixels[1] = 0.0f;
        if (inFront) {
            outPixels[0] = (clip[0] / clip[3] * 0.5f + 0.5f) * static_cast<float>(width);
            outPixels[1] = (0.5f - clip[1] / clip[3] * 0.5f) * static_cast<float>(height);
        }
    }
    return inFront;
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
    line.name = "overlay.lines.on_top";
    line.depthTest = DepthTest::Off;
    lineOnTopPipeline_ = device_.createPipeline(line);

    PipelineDesc wide;
    wide.name = "overlay.wide_lines";
    wide.vertexSource = shaders::kWideLineVertex;
    wide.fragmentSource = shaders::kWideLineFragment;
    constexpr uint32_t kEnd = sizeof(StrataWideLineVertex);
    wide.buffers = {{0, 2 * kEnd, true}};
    wide.attributes = {{0, 0, 3, AttribType::Float, offsetof(StrataWideLineVertex, position)},
                       {1, 0, 4, AttribType::Float, offsetof(StrataWideLineVertex, color)},
                       {2, 0, 2, AttribType::Float, offsetof(StrataWideLineVertex, width_pixels)},
                       {3, 0, 3, AttribType::Float, kEnd + offsetof(StrataWideLineVertex, position)},
                       {4, 0, 4, AttribType::Float, kEnd + offsetof(StrataWideLineVertex, color)},
                       {5, 0, 2, AttribType::Float, kEnd + offsetof(StrataWideLineVertex, width_pixels)}};
    wide.depthTest = DepthTest::LessEqual;
    wide.depthWrite = false;
    wide.depthClamp = true;
    wide.cull = CullMode::None;
    wide.blend = BlendMode::Alpha;
    widePipeline_ = device_.createPipeline(wide);
    wide.name = "overlay.wide_lines.round";
    wide.fragmentSource = shaders::kWideLineRoundFragment;
    roundPipeline_ = device_.createPipeline(wide);
    wide.depthTest = DepthTest::Off;
    wide.name = "overlay.wide_lines.round.on_top";
    roundOnTopPipeline_ = device_.createPipeline(wide);
    wide.name = "overlay.wide_lines.on_top";
    wide.fragmentSource = shaders::kWideLineFragment;
    wideOnTopPipeline_ = device_.createPipeline(wide);

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
    point.depthTest = DepthTest::Off;
    point.name = "overlay.points.soft.on_top";
    softPointOnTopPipeline_ = device_.createPipeline(point);
    point.name = "overlay.points.on_top";
    point.fragmentSource = shaders::kSolidPointFragment;
    point.blend = BlendMode::Alpha;
    solidPointOnTopPipeline_ = device_.createPipeline(point);

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

    PipelineDesc rect = text;
    rect.name = "overlay.rects";
    rect.vertexSource = shaders::kRectVertex;
    rect.fragmentSource = shaders::kRectFragment;
    rect.buffers = {{0, sizeof(StrataRect), true}};
    rect.attributes = {{0, 0, 4, AttribType::Float, offsetof(StrataRect, x)},
                       {1, 0, 4, AttribType::Float, offsetof(StrataRect, color)}};
    rectPipeline_ = device_.createPipeline(rect);

    const std::vector<uint8_t> atlas = buildFontAtlas();
    TextureDesc font;
    font.width = kFontGlyphCount * 8;
    font.height = 8;
    font.format = TextureFormat::R8;
    font.nearest = true;
    font.initialData = atlas.data();
    font.debugName = "strata.overlay-font";
    font_ = device_.createTexture(font);

    wideBuffer_ = device_.createBuffer(64 * 2 * sizeof(StrataWideLineVertex), nullptr, true, "strata.overlay-wide-lines");
    lineBuffer_ = device_.createBuffer(256 * sizeof(StrataLineVertex), nullptr, true, "strata.overlay-lines");
    pointBuffer_ = device_.createBuffer(256 * sizeof(StrataPoint), nullptr, true, "strata.overlay-points");
    glyphBuffer_ = device_.createBuffer(256 * sizeof(GlyphInstance), nullptr, true, "strata.overlay-glyphs");
    rectBuffer_ = device_.createBuffer(64 * sizeof(StrataRect), nullptr, true, "strata.overlay-rects");
    viewportUniforms_ = device_.createBuffer(16, nullptr, true, "strata.overlay-viewport");
}

Overlay::~Overlay() {
    device_.destroy(linePipeline_);
    device_.destroy(solidPointPipeline_);
    device_.destroy(softPointPipeline_);
    device_.destroy(textPipeline_);
    device_.destroy(lineOnTopPipeline_);
    device_.destroy(solidPointOnTopPipeline_);
    device_.destroy(softPointOnTopPipeline_);
    device_.destroy(rectPipeline_);
    device_.destroy(rectBuffer_);
    device_.destroy(widePipeline_);
    device_.destroy(roundPipeline_);
    device_.destroy(wideOnTopPipeline_);
    device_.destroy(roundOnTopPipeline_);
    device_.destroy(wideBuffer_);
    device_.destroy(lineBuffer_);
    device_.destroy(pointBuffer_);
    device_.destroy(glyphBuffer_);
    device_.destroy(viewportUniforms_);
    device_.destroy(font_);
}

void Overlay::clear() {
    lines_.clear();
    linesOnTop_.clear();
    wideLines_.clear();
    roundLines_.clear();
    wideLinesOnTop_.clear();
    roundLinesOnTop_.clear();
    solidPoints_.clear();
    softPoints_.clear();
    solidPointsOnTop_.clear();
    softPointsOnTop_.clear();
    glyphs_.clear();
    rects_.clear();
    worldTexts_.clear();
}

void Overlay::addLines(const StrataLineVertex* vertices, size_t count, uint32_t flags) {
    if (flags & ~kKnownLineFlags) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "lines: unknown flag bits 0x%x", flags & ~kKnownLineFlags);
    }
    validateLineVertices(vertices, count);
    auto& to = (flags & STRATA_LINES_ON_TOP) ? linesOnTop_ : lines_;
    to.insert(to.end(), vertices, vertices + count);
}

void Overlay::addWideLines(const StrataWideLineVertex* vertices, size_t count, uint32_t flags) {
    if (flags & ~kKnownWideLineFlags) {
        fail(STRATA_ERROR_INVALID_ARGUMENT, "wide lines: unknown flag bits 0x%x", flags & ~kKnownWideLineFlags);
    }
    validateWideLineVertices(vertices, count);
    const bool round = (flags & STRATA_LINES_ROUND_CAPS) != 0;
    auto& to = (flags & STRATA_LINES_ON_TOP) ? (round ? roundLinesOnTop_ : wideLinesOnTop_)
                                             : (round ? roundLines_ : wideLines_);
    to.insert(to.end(), vertices, vertices + count);
}

void Overlay::addPoints(const StrataPoint* points, size_t count) {
    validatePoints(points, count);
    for (size_t i = 0; i < count; ++i) {
        const bool soft = (points[i].flags & STRATA_POINT_SOFT) != 0;
        if (points[i].flags & STRATA_POINT_ON_TOP) {
            (soft ? softPointsOnTop_ : solidPointsOnTop_).push_back(points[i]);
        } else {
            (soft ? softPoints_ : solidPoints_).push_back(points[i]);
        }
    }
}

void Overlay::addRects(const StrataRect* rects, size_t count) {
    validateRects(rects, count);
    rects_.insert(rects_.end(), rects, rects + count);
}

void Overlay::addText(const StrataText* texts, size_t count) {
    validateTexts(texts, count);
    for (size_t i = 0; i < count; ++i) layoutText(texts[i], glyphs_);
}

void Overlay::addWorldText(const StrataText* texts, const float* anchors, size_t count) {
    validateTexts(texts, count);
    validateAnchors(anchors, count);
    for (size_t i = 0; i < count; ++i) {
        WorldText w;
        w.text = texts[i];
        w.text.utf8 = nullptr;  // the caller's string is not kept; `value` is
        w.value = texts[i].utf8;
        std::memcpy(w.anchor, anchors + 3 * i, sizeof w.anchor);
        worldTexts_.push_back(std::move(w));
    }
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
    if (lineVertexCount() == 0 && wideLineVertexCount() == 0 && pointCount() == 0) return 0;
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
    // One buffer each for lines and points; the depth-tested ones come
    // first, then (ABI 1.11) the ones drawn on top of the scene.
    if (lineVertexCount() > 0) {
        const size_t bytes = lineVertexCount() * sizeof(StrataLineVertex);
        if (bytes > device_.bufferSize(lineBuffer_)) {
            device_.resizeBuffer(lineBuffer_, std::max(bytes, device_.bufferSize(lineBuffer_) * 2));
        }
        device_.updateBuffer(lineBuffer_, 0, lines_.size() * sizeof(StrataLineVertex), lines_.data());
        device_.updateBuffer(lineBuffer_, lines_.size() * sizeof(StrataLineVertex),
                             linesOnTop_.size() * sizeof(StrataLineVertex), linesOnTop_.data());
    }
    if (wideLineVertexCount() > 0) {
        const size_t bytes = wideLineVertexCount() * sizeof(StrataWideLineVertex);
        if (bytes > device_.bufferSize(wideBuffer_)) {
            device_.resizeBuffer(wideBuffer_, std::max(bytes, device_.bufferSize(wideBuffer_) * 2));
        }
        size_t at = 0;
        for (const auto* list : {&wideLines_, &roundLines_, &wideLinesOnTop_, &roundLinesOnTop_}) {
            device_.updateBuffer(wideBuffer_, at * sizeof(StrataWideLineVertex),
                                 list->size() * sizeof(StrataWideLineVertex), list->data());
            at += list->size();
        }
    }
    if (pointCount() > 0) {
        const size_t bytes = pointCount() * sizeof(StrataPoint);
        if (bytes > device_.bufferSize(pointBuffer_)) {
            device_.resizeBuffer(pointBuffer_, std::max(bytes, device_.bufferSize(pointBuffer_) * 2));
        }
        size_t at = 0;
        for (const auto* list : {&solidPoints_, &softPoints_, &solidPointsOnTop_, &softPointsOnTop_}) {
            device_.updateBuffer(pointBuffer_, at * sizeof(StrataPoint), list->size() * sizeof(StrataPoint),
                                 list->data());
            at += list->size();
        }
    }
    auto drawLines = [&](rhi::PipelineHandle pipeline, size_t first, size_t count) {
        if (count == 0) return;
        device_.bindPipeline(pipeline);
        device_.bindVertexBuffer(0, lineBuffer_, 0);
        rhi::DrawArrays draw;
        draw.firstVertex = static_cast<uint32_t>(first);
        draw.vertexCount = static_cast<uint32_t>(count);
        device_.drawArrays(draw);
        ++draws;
    };
    // A wide segment is an instance of two ends; base and count are vertices.
    auto drawWide = [&](rhi::PipelineHandle pipeline, size_t base, size_t count) {
        if (count == 0) return;
        device_.bindPipeline(pipeline);
        device_.bindVertexBuffer(0, wideBuffer_, 0);
        rhi::DrawArrays draw;
        draw.vertexCount = 6;
        draw.instanceCount = static_cast<uint32_t>(count / 2);
        draw.baseInstance = static_cast<uint32_t>(base / 2);
        device_.drawArrays(draw);
        ++draws;
    };
    auto drawPoints = [&](rhi::PipelineHandle pipeline, size_t base, size_t count) {
        if (count == 0) return;
        // Solid points first, soft (additive) glows over them.
        device_.bindPipeline(pipeline);
        device_.bindVertexBuffer(0, pointBuffer_, 0);
        rhi::DrawArrays draw;
        draw.vertexCount = 6;
        draw.instanceCount = static_cast<uint32_t>(count);
        draw.baseInstance = static_cast<uint32_t>(base);
        device_.drawArrays(draw);
        ++draws;
    };
    drawLines(linePipeline_, 0, lines_.size());
    drawWide(widePipeline_, 0, wideLines_.size());
    drawWide(roundPipeline_, wideLines_.size(), roundLines_.size());
    drawPoints(solidPointPipeline_, 0, solidPoints_.size());
    drawPoints(softPointPipeline_, solidPoints_.size(), softPoints_.size());
    drawLines(lineOnTopPipeline_, lines_.size(), linesOnTop_.size());
    const size_t wideOnTop = wideLines_.size() + roundLines_.size();
    drawWide(wideOnTopPipeline_, wideOnTop, wideLinesOnTop_.size());
    drawWide(roundOnTopPipeline_, wideOnTop + wideLinesOnTop_.size(), roundLinesOnTop_.size());
    const size_t onTop = solidPoints_.size() + softPoints_.size();
    drawPoints(solidPointOnTopPipeline_, onTop, solidPointsOnTop_.size());
    drawPoints(softPointOnTopPipeline_, onTop + solidPointsOnTop_.size(), softPointsOnTop_.size());
    device_.endPass();
    return draws;
}

int Overlay::drawText(rhi::TargetHandle target, int width, int height, const StrataCamera& camera) {
    // World labels first, so this frame's screen text (a HUD) sits on them.
    drawGlyphs_.clear();
    for (const WorldText& w : worldTexts_) {
        float at[2];
        if (!projectPoint(camera, width, height, w.anchor, at, nullptr)) continue;
        StrataText t = w.text;
        t.x += at[0];
        t.y += at[1];
        t.utf8 = w.value.c_str();
        layoutText(t, drawGlyphs_);
    }
    drawGlyphs_.insert(drawGlyphs_.end(), glyphs_.begin(), glyphs_.end());
    if (drawGlyphs_.empty() && rects_.empty()) return 0;
    writeViewport(width, height);
    rhi::PassDesc pass;
    pass.name = "text";
    pass.target = target;
    pass.width = width;
    pass.height = height;
    device_.beginPass(pass);
    int draws = 0;
    if (!rects_.empty()) {
        // Panels first, so text submitted in the same frame sits on them.
        upload(rectBuffer_, rects_);
        device_.bindPipeline(rectPipeline_);
        device_.bindUniformBuffer(3, viewportUniforms_, 0, 16);
        device_.bindVertexBuffer(0, rectBuffer_, 0);
        rhi::DrawArrays draw;
        draw.vertexCount = 6;
        draw.instanceCount = static_cast<uint32_t>(rects_.size());
        device_.drawArrays(draw);
        ++draws;
    }
    if (!drawGlyphs_.empty()) {
        upload(glyphBuffer_, drawGlyphs_);
        device_.bindPipeline(textPipeline_);
        device_.bindUniformBuffer(3, viewportUniforms_, 0, 16);
        device_.bindTexture(3, font_);
        device_.bindVertexBuffer(0, glyphBuffer_, 0);
        rhi::DrawArrays draw;
        draw.vertexCount = 6;
        draw.instanceCount = static_cast<uint32_t>(drawGlyphs_.size());
        device_.drawArrays(draw);
        ++draws;
    }
    device_.endPass();
    return draws;
}

}  // namespace strata::render
