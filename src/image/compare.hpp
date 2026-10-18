// Golden-image comparison with a perceptual tolerance.
//
// Each pixel pair is converted from sRGB to CIE L*a*b* (D65) and compared
// with the CIE76 colour difference, Delta E. A difference of about 2.3 is
// the smallest a person can see side by side. A comparison passes when the
// share of pixels whose Delta E exceeds `pixelThreshold` is no more than
// `maxFraction`. Alpha is ignored. Images of different sizes never pass.
#pragma once

#include <cstdint>

#include "image/image.hpp"

namespace strata::image {

struct CompareOptions {
    double pixelThreshold = 4.0;  // Delta E above which a pixel counts as different
    double maxFraction = 0.005;   // share of pixels allowed over the threshold
};

struct CompareResult {
    bool sameSize = false;
    int64_t pixels = 0;
    int64_t pixelsOver = 0;
    double fractionOver = 1.0;
    double maxDeltaE = 0.0;
    double meanDeltaE = 0.0;
    bool passed = false;
};

double deltaE76(const uint8_t* rgbA, const uint8_t* rgbB);

CompareResult compare(const Image& expected, const Image& actual, const CompareOptions& options);

// A picture of where two images differ: the expected image, dimmed to grey,
// with every pixel over the threshold painted red (brighter = larger Delta E).
Image diffImage(const Image& expected, const Image& actual, double pixelThreshold);

}  // namespace strata::image
