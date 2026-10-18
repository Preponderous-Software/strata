// strata-imgcmp: compares two PNGs with the golden-image tolerance.
//
//   strata-imgcmp <expected.png> <actual.png> [--diff out.png] [--threshold DE] [--max-fraction F]
//
// Exit status 0 when they match within tolerance, 1 when they do not, 2 on a
// usage or I/O error. See docs/GOLDEN_IMAGES.md for what the numbers mean.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "image/compare.hpp"
#include "image/image.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: strata-imgcmp <expected.png> <actual.png> [--diff out.png] [--threshold DE] "
                             "[--max-fraction F]\n");
        return 2;
    }
    strata::image::CompareOptions options;
    std::string diffPath;
    for (int i = 3; i < argc; ++i) {
        const bool hasValue = i + 1 < argc;
        if (std::strcmp(argv[i], "--diff") == 0 && hasValue) diffPath = argv[++i];
        else if (std::strcmp(argv[i], "--threshold") == 0 && hasValue) options.pixelThreshold = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--max-fraction") == 0 && hasValue) options.maxFraction = std::atof(argv[++i]);
        else {
            std::fprintf(stderr, "unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    strata::image::Image expected, actual;
    std::string error;
    if (!strata::image::readPng(argv[1], expected, &error)) {
        std::fprintf(stderr, "cannot read %s: %s\n", argv[1], error.c_str());
        return 2;
    }
    if (!strata::image::readPng(argv[2], actual, &error)) {
        std::fprintf(stderr, "cannot read %s: %s\n", argv[2], error.c_str());
        return 2;
    }
    const auto r = strata::image::compare(expected, actual, options);
    if (!r.sameSize) {
        std::printf("FAIL: %s is %dx%d but %s is %dx%d\n", argv[1], expected.width, expected.height, argv[2],
                    actual.width, actual.height);
    } else {
        std::printf("%s: %lld of %lld pixels over Delta E %.1f (%.4f%%, limit %.4f%%), max %.2f, mean %.4f\n",
                    r.passed ? "pass" : "FAIL", static_cast<long long>(r.pixelsOver), static_cast<long long>(r.pixels),
                    options.pixelThreshold, r.fractionOver * 100.0, options.maxFraction * 100.0, r.maxDeltaE,
                    r.meanDeltaE);
    }
    if (!r.passed && !diffPath.empty()) {
        strata::image::writePng(diffPath, strata::image::diffImage(expected, actual, options.pixelThreshold), nullptr);
        std::printf("wrote %s\n", diffPath.c_str());
    }
    return r.passed ? 0 : 1;
}
