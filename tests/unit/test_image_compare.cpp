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
