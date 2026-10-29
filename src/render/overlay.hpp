// Overlays (ABI 1.5): unlit lines, points and screen text, drawn after the lit
// scene. Validation and text layout are free functions so the unit tests can
// reach them without a GL context.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
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
void validateWideLineVertices(const StrataWideLineVertex* vertices, size_t count);  // ABI 1.13
void validatePoints(const StrataPoint* points, size_t count);
void validateTexts(const StrataText* texts, size_t count);
void validateRects(const StrataRect* rects, size_t count);  // ABI 1.11

// Index of a byte in the font atlas: printable ASCII maps to itself, anything
// else to '?'.
int glyphIndex(unsigned char c);

// Lays one text run out as glyph quads, appending to `out`. Spaces advance but
// add no quad; '\n' returns to x and moves down one cell. With
// STRATA_TEXT_SHADOW every glyph is preceded by a black copy offset one font
// pixel (pixel_size / 8) right and down.
void layoutText(const StrataText& text, std::vector<GlyphInstance>& out);

// ABI 1.14. Projects a world point with `camera` onto a width x height
// drawable: pixels from its top-left (set to 0, 0 when not in front) and the
// distance along the view axis (the camera looks down -Z in view space).
// Returns whether the point is in front of the camera: that distance and its
// clip w are both > 0.
bool projectPoint(const StrataCamera& camera, int width, int height, const float world[3], float outPixels[2],
                  float* outDepth);

// ABI 1.14. Checks world-text anchors (count x 3 floats, all finite); throws
// like the validators above.
void validateAnchors(const float* anchors, size_t count);

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
    // flags: STRATA_LINES_* (ABI 1.11); 0 is ABI 1.5's depth-tested lines.
    void addLines(const StrataLineVertex* vertices, size_t count, uint32_t flags = 0);
    void addPoints(const StrataPoint* points, size_t count);
    void addText(const StrataText* texts, size_t count);
    void addRects(const StrataRect* rects, size_t count);  // ABI 1.11
    // ABI 1.13. flags: STRATA_LINES_ON_TOP, STRATA_LINES_ROUND_CAPS.
    void addWideLines(const StrataWideLineVertex* vertices, size_t count, uint32_t flags);
    // ABI 1.14: text anchored at world positions, projected when drawn.
    void addWorldText(const StrataText* texts, const float* anchors, size_t count);

    // Lines and points against the scene's depth, then (ABI 1.11) the ones
    // drawn on top of it, with the renderer's Frame uniform block (camera
    // matrices). Returns the number of draw calls.
    int drawWorld(rhi::TargetHandle target, int width, int height, rhi::BufferHandle frameUniforms,
                  size_t frameBytes);
    // Filled rectangles (ABI 1.11), then world-anchored text (ABI 1.14,
    // projected with `camera`), then screen text, on top of everything.
    // Returns the number of draw calls.
    int drawText(rhi::TargetHandle target, int width, int height, const StrataCamera& camera);

    size_t lineVertexCount() const { return lines_.size() + linesOnTop_.size(); }
    size_t wideLineVertexCount() const {
        return wideLines_.size() + roundLines_.size() + wideLinesOnTop_.size() + roundLinesOnTop_.size();
    }
    size_t pointCount() const {
        return solidPoints_.size() + softPoints_.size() + solidPointsOnTop_.size() + softPointsOnTop_.size();
    }
    size_t glyphCount() const { return glyphs_.size(); }
    size_t rectCount() const { return rects_.size(); }
    size_t worldTextCount() const { return worldTexts_.size(); }

private:
    void writeViewport(int width, int height);
    template <typename T>
    void upload(rhi::BufferHandle buffer, const std::vector<T>& data);

    rhi::GpuDevice& device_;
    rhi::PipelineHandle linePipeline_, solidPointPipeline_, softPointPipeline_, textPipeline_;
    // ABI 1.11: the same pipelines with no depth test, and filled rectangles.
    rhi::PipelineHandle lineOnTopPipeline_, solidPointOnTopPipeline_, softPointOnTopPipeline_, rectPipeline_;
    // ABI 1.13: wide lines with square or round ends, depth-tested or on top.
    rhi::PipelineHandle widePipeline_, roundPipeline_, wideOnTopPipeline_, roundOnTopPipeline_;
    rhi::BufferHandle wideBuffer_;
    std::vector<StrataWideLineVertex> wideLines_, roundLines_, wideLinesOnTop_, roundLinesOnTop_;
    rhi::BufferHandle lineBuffer_, pointBuffer_, glyphBuffer_, rectBuffer_, viewportUniforms_;
    rhi::TextureHandle font_;

    std::vector<StrataLineVertex> lines_, linesOnTop_;
    std::vector<StrataPoint> solidPoints_, softPoints_, solidPointsOnTop_, softPointsOnTop_;
    std::vector<GlyphInstance> glyphs_;
    std::vector<StrataRect> rects_;
    // ABI 1.14. A copy of each world label (its utf8 points into `value` when
    // laid out) and its anchor; laid out at draw time into drawGlyphs_.
    struct WorldText {
        StrataText text{};
        std::string value;
        float anchor[3] = {0, 0, 0};
    };
    std::vector<WorldText> worldTexts_;
    std::vector<GlyphInstance> drawGlyphs_;
};

}  // namespace strata::render
