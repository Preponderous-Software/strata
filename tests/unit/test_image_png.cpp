// PNG in and out (src/image/image.cpp): a round trip through a file in the
// working directory, and the error paths that need no file at all.
#include <cstdio>
#include <string>

#include "image/image.hpp"
#include "test.hpp"

using namespace strata::image;

TEST(png_round_trip_keeps_rgb_and_reads_alpha_as_opaque) {
    Image img;
    img.width = 3;
    img.height = 2;
    img.rgba = {
        255, 0,   0,   7,  0,   255, 0,   7,  0,   0,   255, 7,  // top row first
        10,  20,  30,  7,  200, 100, 50,  7,  1,   2,   3,   7,
    };
    const std::string path = "strata-unit-test-round-trip.png";
    std::string error;
    REQUIRE(writePng(path, img, &error));

    Image back;
    const bool read = readPng(path, back, &error);
    std::remove(path.c_str());
    REQUIRE(read);
    REQUIRE(back.width == 3 && back.height == 2);
    REQUIRE(back.rgba.size() == img.rgba.size());
    for (size_t i = 0; i < img.rgba.size(); i += 4) {
        CHECK(back.rgba[i] == img.rgba[i]);
        CHECK(back.rgba[i + 1] == img.rgba[i + 1]);
        CHECK(back.rgba[i + 2] == img.rgba[i + 2]);
        CHECK(back.rgba[i + 3] == 255);  // writePng drops alpha
    }
}

TEST(write_png_refuses_an_image_whose_size_does_not_match_its_pixels) {
    Image empty;
    std::string error;
    CHECK(!writePng("strata-unit-test-never-written.png", empty, &error));
    CHECK(!error.empty());

    Image mismatched;
    mismatched.width = 2;
    mismatched.height = 2;
    mismatched.rgba.assign(12, 0);  // one pixel short
    error.clear();
    CHECK(!writePng("strata-unit-test-never-written.png", mismatched, &error));
    CHECK(!error.empty());
    CHECK(!writePng("strata-unit-test-never-written.png", mismatched, nullptr));
}

TEST(read_png_of_a_missing_file_fails_with_a_reason) {
    Image img;
    std::string error;
    CHECK(!readPng("strata-unit-test-does-not-exist.png", img, &error));
    CHECK(!error.empty());
    CHECK(img.width == 0 && img.height == 0 && img.rgba.empty());
}
