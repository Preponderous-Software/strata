#include "image/compare.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace strata::image {
namespace {

const std::array<double, 256>& srgbToLinear() {
    static const std::array<double, 256> table = [] {
        std::array<double, 256> t{};
        for (int i = 0; i < 256; ++i) {
            const double c = i / 255.0;
            t[static_cast<size_t>(i)] = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
        }
        return t;
    }();
    return table;
}

double labF(double t) {
    const double delta = 6.0 / 29.0;
    return t > delta * delta * delta ? std::cbrt(t) : t / (3.0 * delta * delta) + 4.0 / 29.0;
}

void toLab(const uint8_t* rgb, double lab[3]) {
    const auto& lin = srgbToLinear();
    const double r = lin[rgb[0]], g = lin[rgb[1]], b = lin[rgb[2]];
    // sRGB (D65) to XYZ, normalised by the D65 white point.
    const double x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047;
    const double y = (0.2126729 * r + 0.7151522 * g + 0.0721750 * b) / 1.00000;
    const double z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * b) / 1.08883;
    const double fx = labF(x), fy = labF(y), fz = labF(z);
    lab[0] = 116.0 * fy - 16.0;
    lab[1] = 500.0 * (fx - fy);
    lab[2] = 200.0 * (fy - fz);
}

}  // namespace

double deltaE76(const uint8_t* rgbA, const uint8_t* rgbB) {
    if (rgbA[0] == rgbB[0] && rgbA[1] == rgbB[1] && rgbA[2] == rgbB[2]) return 0.0;
    double a[3], b[3];
    toLab(rgbA, a);
    toLab(rgbB, b);
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

CompareResult compare(const Image& expected, const Image& actual, const CompareOptions& options) {
    CompareResult r;
    r.sameSize = expected.width == actual.width && expected.height == actual.height && expected.width > 0 &&
                 expected.height > 0;
    if (!r.sameSize) return r;
    r.pixels = static_cast<int64_t>(expected.width) * expected.height;
    double sum = 0.0;
    for (int64_t i = 0; i < r.pixels; ++i) {
        const double d = deltaE76(&expected.rgba[static_cast<size_t>(i) * 4], &actual.rgba[static_cast<size_t>(i) * 4]);
        sum += d;
        r.maxDeltaE = std::max(r.maxDeltaE, d);
        if (d > options.pixelThreshold) ++r.pixelsOver;
    }
    r.meanDeltaE = sum / static_cast<double>(r.pixels);
    r.fractionOver = static_cast<double>(r.pixelsOver) / static_cast<double>(r.pixels);
    r.passed = r.fractionOver <= options.maxFraction;
    return r;
}

Image diffImage(const Image& expected, const Image& actual, double pixelThreshold) {
    Image out;
    out.width = expected.width;
    out.height = expected.height;
    out.rgba.assign(expected.rgba.size(), 255);
    const bool sameSize = expected.width == actual.width && expected.height == actual.height;
    for (size_t i = 0; i + 3 < expected.rgba.size(); i += 4) {
        const uint8_t* e = &expected.rgba[i];
        const double grey = (0.299 * e[0] + 0.587 * e[1] + 0.114 * e[2]) * 0.35;
        const double d = sameSize ? deltaE76(e, &actual.rgba[i]) : 100.0;
        if (d > pixelThreshold) {
            out.rgba[i] = static_cast<uint8_t>(std::min(255.0, 128.0 + d * 4.0));
            out.rgba[i + 1] = 0;
            out.rgba[i + 2] = 0;
        } else {
            out.rgba[i] = out.rgba[i + 1] = out.rgba[i + 2] = static_cast<uint8_t>(grey);
        }
    }
    return out;
}

}  // namespace strata::image
