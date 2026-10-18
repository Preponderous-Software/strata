// Overlays (ABI 1.5): unlit lines, points and screen text, drawn after the lit
// scene. Validation and text layout are free functions so the unit tests can
// reach them without a GL context.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rhi/gpu_device.hpp"
#include "strata/strata.h"

namespace strata::render {

// One glyph quad: top-left corner and cell size in pixels, the glyph's index
// in the font atlas, and its colour. Mirrors the text pipeline's instance
// attributes.
struct GlyphInstance {
    float x = 0, y = 0, size = 0, glyph = 0;
    float color[4] = {0, 0, 0, 0};
};
static_assert(sizeof(GlyphInstance) == 32, "GlyphInstance is the text pipeline's 32-byte instance record");

// Each throws strata::Error(STRATA_ERROR_INVALID_ARGUMENT) naming the element
// and the bad field, before anything is added.
void validateLineVertices(const StrataLineVertex* vertices, size_t count);
void validatePoints(const StrataPoint* points, size_t count);
void validateTexts(const StrataText* texts, size_t count);

// Index of a byte in the font atlas: printable ASCII maps to itself, anything
// else to '?'.
int glyphIndex(unsigned char c);

// Lays one text run out as glyph quads, appending to `out`. Spaces advance but
// add no quad; '\n' returns to x and moves down one cell. With
// STRATA_TEXT_SHADOW every glyph is preceded by a black copy offset one font
// pixel (pixel_size / 8) right and down.
void layoutText(const StrataText& text, std::vector<GlyphInstance>& out);

// The font atlas: kFontGlyphCount glyphs of 8 x 8 side by side, one byte per
// texel (255 set, 0 clear), top row first.
std::vector<uint8_t> buildFontAtlas();

class Overlay {
public:
    explicit Overlay(rhi::GpuDevice& device);
    ~Overlay();

    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    void clear();
    void addLines(const StrataLineVertex* vertices, size_t count);
    void addPoints(const StrataPoint* points, size_t count);
    void addText(const StrataText* texts, size_t count);

    // Lines and points against the scene's depth, with the renderer's Frame
    // uniform block (camera matrices). Returns the number of draw calls.
    int drawWorld(rhi::TargetHandle target, int width, int height, rhi::BufferHandle frameUniforms,
                  size_t frameBytes);
    // Text on top of everything. Returns the number of draw calls.
    int drawText(rhi::TargetHandle target, int width, int height);

    size_t lineVertexCount() const { return lines_.size(); }
    size_t pointCount() const { return solidPoints_.size() + softPoints_.size(); }
    size_t glyphCount() const { return glyphs_.size(); }

private:
    void writeViewport(int width, int height);
    template <typename T>
    void upload(rhi::BufferHandle buffer, const std::vector<T>& data);

    rhi::GpuDevice& device_;
    rhi::PipelineHandle linePipeline_, solidPointPipeline_, softPointPipeline_, textPipeline_;
    rhi::BufferHandle lineBuffer_, pointBuffer_, glyphBuffer_, viewportUniforms_;
    rhi::TextureHandle font_;

    std::vector<StrataLineVertex> lines_;
    std::vector<StrataPoint> solidPoints_, softPoints_;
    std::vector<GlyphInstance> glyphs_;
};

}  // namespace strata::render
