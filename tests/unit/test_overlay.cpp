// Overlays (ABI 1.5): validation, text layout and the font atlas, with no GL
// context. What they look like is checked by the o1_* golden images.
#include <cstring>
#include <limits>
#include <vector>

#include "render/font8x8.hpp"
#include "render/overlay.hpp"
#include "strata/strata.hpp"
#include "test.hpp"

using namespace strata::render;

namespace {

template <typename Fn>
bool rejectsWith(Fn fn, const char* fragment) {
    try {
        fn();
    } catch (const strata::Error& e) {
        return e.status() == STRATA_ERROR_INVALID_ARGUMENT && std::strstr(e.what(), fragment) != nullptr;
    }
    return false;
}

StrataPoint goodPoint() {
    StrataPoint p{};
    p.size_pixels = 4.0f;
    p.color[0] = 1.0f;
    p.color[3] = 1.0f;
    return p;
}

StrataText goodText(const char* s) {
    StrataText t{};
    t.x = 10.0f;
    t.y = 20.0f;
    t.pixel_size = 16.0f;
    t.color[0] = t.color[1] = t.color[2] = t.color[3] = 1.0f;
    t.utf8 = s;
    return t;
}

}  // namespace

TEST(line_vertices_come_in_pairs_and_must_be_finite) {
    StrataLineVertex v[2]{};
    v[0].color[3] = v[1].color[3] = 1.0f;
    validateLineVertices(v, 2);
    validateLineVertices(nullptr, 0);
    CHECK(rejectsWith([&] { validateLineVertices(v, 1); }, "even"));
    v[1].position[2] = std::numeric_limits<float>::infinity();
    CHECK(rejectsWith([&] { validateLineVertices(v, 2); }, "line vertex 1"));
    v[1].position[2] = 0.0f;
    v[0].color[3] = 1.5f;
    CHECK(rejectsWith([&] { validateLineVertices(v, 2); }, "line vertex 0"));
}

TEST(points_reject_negative_sizes_and_unknown_flags) {
    StrataPoint p[2] = {goodPoint(), goodPoint()};
    p[1].flags = STRATA_POINT_SOFT;
    p[1].world_diameter = 3.0f;
    validatePoints(p, 2);
    p[1].size_pixels = -1.0f;
    CHECK(rejectsWith([&] { validatePoints(p, 2); }, "point 1: size_pixels"));
    p[1].size_pixels = 1.0f;
    p[1].world_diameter = std::numeric_limits<float>::quiet_NaN();
    CHECK(rejectsWith([&] { validatePoints(p, 2); }, "point 1: world_diameter"));
    p[1].world_diameter = 1.0f;
    p[1].flags = 0x80;
    CHECK(rejectsWith([&] { validatePoints(p, 2); }, "unknown flag"));
}

TEST(text_rejects_null_strings_bad_sizes_and_unknown_flags) {
    StrataText t = goodText("ok");
    validateTexts(&t, 1);
    t.utf8 = nullptr;
    CHECK(rejectsWith([&] { validateTexts(&t, 1); }, "utf8 is NULL"));
    t = goodText("ok");
    t.pixel_size = 0.5f;
    CHECK(rejectsWith([&] { validateTexts(&t, 1); }, "pixel_size"));
    t.pixel_size = 1000.0f;
    CHECK(rejectsWith([&] { validateTexts(&t, 1); }, "pixel_size"));
    t = goodText("ok");
    t.flags = 0x4;
    CHECK(rejectsWith([&] { validateTexts(&t, 1); }, "unknown flag"));
}

TEST(glyph_index_maps_printable_ascii_and_replaces_the_rest) {
    CHECK(glyphIndex(' ') == 0);
    CHECK(glyphIndex('A') == 'A' - 0x20);
    CHECK(glyphIndex('~') == kFontGlyphCount - 1);
    CHECK(glyphIndex('\t') == '?' - 0x20);
    CHECK(glyphIndex(0x7F) == '?' - 0x20);
    CHECK(glyphIndex(0xE9) == '?' - 0x20);
}

TEST(text_layout_is_monospaced_skips_spaces_and_wraps_on_newline) {
    std::vector<GlyphInstance> glyphs;
    const StrataText t = goodText("Ab c\nd");
    layoutText(t, glyphs);
    REQUIRE(glyphs.size() == 4);  // A, b, c, d: the space advances but draws nothing
    CHECK_NEAR(glyphs[0].x, 10.0, 0.0);
    CHECK_NEAR(glyphs[0].y, 20.0, 0.0);
    CHECK_NEAR(glyphs[0].glyph, 'A' - 0x20, 0.0);
    CHECK_NEAR(glyphs[1].x, 26.0, 0.0);
    CHECK_NEAR(glyphs[2].x, 58.0, 0.0);  // after "Ab "
    CHECK_NEAR(glyphs[3].x, 10.0, 0.0);  // new line back at x
    CHECK_NEAR(glyphs[3].y, 36.0, 0.0);
    CHECK_NEAR(glyphs[3].size, 16.0, 0.0);
}

TEST(text_shadow_precedes_each_glyph_offset_one_font_pixel) {
    std::vector<GlyphInstance> glyphs;
    StrataText t = goodText("X");
    t.flags = STRATA_TEXT_SHADOW;
    layoutText(t, glyphs);
    REQUIRE(glyphs.size() == 2);
    CHECK_NEAR(glyphs[0].x, 12.0, 0.0);  // 16 / 8 = 2 pixels
    CHECK_NEAR(glyphs[0].y, 22.0, 0.0);
    CHECK_NEAR(glyphs[0].color[0], 0.0, 0.0);
    CHECK_NEAR(glyphs[0].color[3], 1.0, 0.0);
    CHECK_NEAR(glyphs[1].x, 10.0, 0.0);
    CHECK_NEAR(glyphs[1].color[0], 1.0, 0.0);
}

TEST(a_utf8_sequence_is_one_replacement_glyph) {
    std::vector<GlyphInstance> glyphs;
    const StrataText t = goodText("a\xC3\xA9z");  // "aéz"
    layoutText(t, glyphs);
    REQUIRE(glyphs.size() == 3);
    CHECK_NEAR(glyphs[1].glyph, '?' - 0x20, 0.0);
    CHECK_NEAR(glyphs[2].x, 10.0 + 2 * 16.0, 0.0);
}

TEST(font_atlas_holds_the_public_domain_8x8_glyphs) {
    const std::vector<uint8_t> atlas = buildFontAtlas();
    const int width = kFontGlyphCount * 8;
    REQUIRE(atlas.size() == static_cast<size_t>(width * 8));
    // 'A' row 0 is 0x0C: pixels 2 and 3 set (least significant bit leftmost).
    const int a = ('A' - 0x20) * 8;
    CHECK(atlas[static_cast<size_t>(a + 1)] == 0);
    CHECK(atlas[static_cast<size_t>(a + 2)] == 255);
    CHECK(atlas[static_cast<size_t>(a + 3)] == 255);
    CHECK(atlas[static_cast<size_t>(a + 4)] == 0);
    // The space glyph is empty.
    for (int row = 0; row < 8; ++row)
        for (int col = 0; col < 8; ++col) CHECK(atlas[static_cast<size_t>(row * width + col)] == 0);
}

TEST(on_top_point_flag_is_known) {
    StrataPoint p = goodPoint();
    p.flags = STRATA_POINT_ON_TOP | STRATA_POINT_SOFT;  // ABI 1.11
    validatePoints(&p, 1);
}

TEST(rects_reject_negative_sizes_bad_colours_and_null) {
    StrataRect r{10.0f, 20.0f, 100.0f, 40.0f, {0.0f, 0.0f, 0.0f, 0.6f}};
    validateRects(&r, 1);
    r.width = 0.0f;  // an empty rectangle is allowed and draws nothing
    validateRects(&r, 1);
    r.width = -1.0f;
    CHECK(rejectsWith([&] { validateRects(&r, 1); }, "rect 0: width and height"));
    r.width = 1.0f;
    r.y = std::numeric_limits<float>::infinity();
    CHECK(rejectsWith([&] { validateRects(&r, 1); }, "rect 0: position"));
    r.y = 0.0f;
    r.color[3] = 2.0f;
    CHECK(rejectsWith([&] { validateRects(&r, 1); }, "rect 0: colour"));
    CHECK(rejectsWith([&] { validateRects(nullptr, 1); }, "NULL"));
}

TEST(wide_line_vertices_come_in_pairs_with_non_negative_widths) {  // ABI 1.13
    StrataWideLineVertex v[2]{};
    v[0].color[3] = v[1].color[3] = 1.0f;
    v[0].width_pixels = 6.0f;
    v[1].world_width = 0.5f;
    validateWideLineVertices(v, 2);
    validateWideLineVertices(nullptr, 0);
    CHECK(rejectsWith([&] { validateWideLineVertices(v, 1); }, "even"));
    CHECK(rejectsWith([&] { validateWideLineVertices(nullptr, 2); }, "NULL"));
    v[1].width_pixels = -1.0f;
    CHECK(rejectsWith([&] { validateWideLineVertices(v, 2); }, "wide line vertex 1: width_pixels"));
    v[1].width_pixels = 0.0f;
    v[0].world_width = std::numeric_limits<float>::quiet_NaN();
    CHECK(rejectsWith([&] { validateWideLineVertices(v, 2); }, "wide line vertex 0: world_width"));
    v[0].world_width = 0.0f;
    v[0].position[1] = std::numeric_limits<float>::infinity();
    CHECK(rejectsWith([&] { validateWideLineVertices(v, 2); }, "wide line vertex 0: position"));
    v[0].position[1] = 0.0f;
    v[1].color[0] = -0.1f;
    CHECK(rejectsWith([&] { validateWideLineVertices(v, 2); }, "wide line vertex 1: colour"));
}

// ABI 1.14: world -> pixels with the engine's own look-at camera.
namespace {
StrataCamera lookDownMinusZ() {
    const float eye[3] = {0, 0, 10}, target[3] = {0, 0, 0}, up[3] = {0, 1, 0};
    StrataCamera c{};
    strata_camera_look_at(eye, target, up, 90.0f, 2.0f, 1.0f, 100.0f, &c);
    return c;
}
}  // namespace

TEST(project_point_maps_the_view_axis_to_the_drawable_centre) {
    const StrataCamera c = lookDownMinusZ();
    const float target[3] = {0, 0, 0};
    float px[2] = {-1, -1}, depth = 0;
    CHECK(projectPoint(c, 200, 100, target, px, &depth));
    CHECK_NEAR(px[0], 100.0, 1e-3);
    CHECK_NEAR(px[1], 50.0, 1e-3);
    CHECK_NEAR(depth, 10.0, 1e-4);
}

TEST(project_point_puts_up_and_right_at_the_top_right) {
    const StrataCamera c = lookDownMinusZ();
    // fov 90 at distance 10 spans y -10..10; aspect 2 spans x -20..20.
    const float topRight[3] = {20, 10, 0};
    float px[2] = {0, 0};
    CHECK(projectPoint(c, 200, 100, topRight, px, nullptr));
    CHECK_NEAR(px[0], 200.0, 1e-3);
    CHECK_NEAR(px[1], 0.0, 1e-3);
    const float offScreen[3] = {-40, 0, 0};  // still in front, left of the drawable
    CHECK(projectPoint(c, 200, 100, offScreen, px, nullptr));
    CHECK_NEAR(px[0], -100.0, 1e-3);
}

TEST(project_point_reports_points_behind_the_camera) {
    const StrataCamera c = lookDownMinusZ();
    const float behind[3] = {0, 0, 12};
    float px[2] = {7, 7}, depth = 0;
    CHECK(!projectPoint(c, 200, 100, behind, px, &depth));
    CHECK_NEAR(depth, -2.0, 1e-4);
    CHECK(px[0] == 0.0f && px[1] == 0.0f);
    const float atEye[3] = {0, 0, 10};
    CHECK(!projectPoint(c, 200, 100, atEye, px, &depth));
}

TEST(world_text_anchors_must_be_finite) {
    const float good[6] = {0, 1, 2, 3, 4, 5};
    validateAnchors(good, 2);
    const float bad[6] = {0, 1, 2, 3, std::numeric_limits<float>::quiet_NaN(), 5};
    CHECK(rejectsWith([&] { validateAnchors(bad, 2); }, "anchor 1"));
    CHECK(rejectsWith([&] { validateAnchors(nullptr, 1); }, "NULL"));
}
