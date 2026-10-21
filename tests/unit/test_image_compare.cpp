#include "image/compare.hpp"
#include "test.hpp"

using namespace strata::image;

namespace {

Image solid(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < img.rgba.size(); i += 4) {
        img.rgba[i] = r;
        img.rgba[i + 1] = g;
        img.rgba[i + 2] = b;
        img.rgba[i + 3] = 255;
    }
    return img;
}

}  // namespace

TEST(delta_e_of_black_and_white_is_one_hundred) {
    const uint8_t black[3] = {0, 0, 0}, white[3] = {255, 255, 255};
    CHECK_NEAR(deltaE76(black, white), 100.0, 0.01);
    CHECK(deltaE76(black, black) == 0.0);
}

TEST(identical_images_pass_with_zero_difference) {
    const Image a = solid(32, 16, 100, 150, 200);
    const CompareResult r = compare(a, a, CompareOptions{});
    CHECK(r.passed);
    CHECK(r.pixelsOver == 0);
    CHECK(r.maxDeltaE == 0.0);
}

TEST(one_changed_pixel_is_counted_and_judged_by_fraction) {
    const Image a = solid(100, 100, 100, 150, 200);
    Image b = a;
    b.rgba[0] = 255;
    CompareOptions strict;
    strict.maxFraction = 0.0;
    const CompareResult r = compare(a, b, strict);
    CHECK(r.pixelsOver == 1);
    CHECK_NEAR(r.fractionOver, 0.0001, 1e-12);
    CHECK(!r.passed);
    CompareOptions lenient;
    lenient.maxFraction = 0.001;
    CHECK(compare(a, b, lenient).passed);
}

TEST(tiny_shifts_stay_under_the_threshold) {
    const Image a = solid(8, 8, 100, 150, 200);
    const Image b = solid(8, 8, 101, 151, 199);
    const CompareResult r = compare(a, b, CompareOptions{});
    CHECK(r.passed);
    CHECK(r.pixelsOver == 0);
    CHECK(r.maxDeltaE > 0.0);
}

TEST(different_sizes_never_pass) {
    CHECK(!compare(solid(4, 4, 0, 0, 0), solid(4, 5, 0, 0, 0), CompareOptions{}).passed);
}

TEST(diff_of_identical_images_is_the_expected_image_dimmed_to_grey) {
    const Image a = solid(4, 3, 100, 150, 200);
    const Image d = diffImage(a, a, 4.0);
    REQUIRE(d.width == 4 && d.height == 3);
    REQUIRE(d.rgba.size() == a.rgba.size());
    // (0.299 * 100 + 0.587 * 150 + 0.114 * 200) * 0.35 = 49.26
    for (size_t i = 0; i < d.rgba.size(); i += 4) {
        CHECK(d.rgba[i] == 49);
        CHECK(d.rgba[i + 1] == 49);
        CHECK(d.rgba[i + 2] == 49);
        CHECK(d.rgba[i + 3] == 255);
    }
}

TEST(diff_paints_pixels_over_the_threshold_red_by_delta_e) {
    const Image a = solid(4, 4, 100, 150, 200);
    Image b = a;
    b.rgba[0] = 130;              // a moderate change: red below full brightness
    b.rgba[4] = b.rgba[5] = 255;  // a large one: red clamped to 255
    b.rgba[6] = 0;
    const double small = deltaE76(&a.rgba[0], &b.rgba[0]);
    const double large = deltaE76(&a.rgba[4], &b.rgba[4]);
    REQUIRE(small > 4.0 && 128.0 + small * 4.0 < 255.0);
    REQUIRE(128.0 + large * 4.0 > 255.0);

    const Image d = diffImage(a, b, 4.0);
    CHECK(d.rgba[0] == static_cast<uint8_t>(128.0 + small * 4.0));
    CHECK(d.rgba[1] == 0 && d.rgba[2] == 0 && d.rgba[3] == 255);
    CHECK(d.rgba[4] == 255);
    CHECK(d.rgba[5] == 0 && d.rgba[6] == 0 && d.rgba[7] == 255);
    CHECK(d.rgba[8] == 49 && d.rgba[9] == 49 && d.rgba[10] == 49);  // untouched pixel stays grey

    // The same change under a threshold it does not exceed is not painted.
    const Image lenient = diffImage(a, b, large + 1.0);
    CHECK(lenient.rgba[0] == 49 && lenient.rgba[4] == 49);
}

TEST(diff_of_different_sizes_is_all_red_at_the_expected_size) {
    const Image a = solid(3, 2, 100, 150, 200);
    const Image d = diffImage(a, solid(2, 2, 100, 150, 200), 4.0);
    REQUIRE(d.width == 3 && d.height == 2);
    REQUIRE(d.rgba.size() == a.rgba.size());
    for (size_t i = 0; i < d.rgba.size(); i += 4) {
        CHECK(d.rgba[i] == 255);
        CHECK(d.rgba[i + 1] == 0);
        CHECK(d.rgba[i + 2] == 0);
        CHECK(d.rgba[i + 3] == 255);
    }
}
