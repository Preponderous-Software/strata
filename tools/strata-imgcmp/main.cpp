// strata-imgcmp: compares two PNGs with the golden-image tolerance.
//
//   strata-imgcmp <expected.png> <actual.png> [--diff out.png] [--always-diff] [--json]
//                 [--threshold DE] [--max-fraction F]
//
// Exit status 0 when they match within tolerance, 1 when they do not, 2 on a
// usage or I/O error. See docs/GOLDEN_IMAGES.md for what the numbers mean.
//
// --diff writes the diff image when the comparison fails; with --always-diff
// it is written on a pass too (a visual-diff report wants one for every pair).
// --json prints one JSON object on stdout instead of the text line, including
// on an I/O error (with an "error" member), so a script reads the result
// without parsing prose. The exit status is the same either way.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "image/compare.hpp"
#include "image/image.hpp"

namespace {

const char* kUsage =
    "usage: strata-imgcmp <expected.png> <actual.png> [--diff out.png] [--always-diff] [--json] "
    "[--threshold DE] [--max-fraction F]\n";

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out + "\"";
}

// JSON has no NaN or infinity; neither can come out of a comparison, but be safe.
std::string jsonNumber(double v) {
    if (!std::isfinite(v)) return "null";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

int fail(bool json, const std::string& message) {
    std::fprintf(stderr, "%s\n", message.c_str());
    if (json) std::printf("{\"passed\": false, \"error\": %s}\n", jsonString(message).c_str());
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    strata::image::CompareOptions options;
    std::string diffPath;
    bool alwaysDiff = false, json = false;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--json") == 0) json = true;
    }
    for (int i = 3; i < argc; ++i) {
        const bool hasValue = i + 1 < argc;
        if (std::strcmp(argv[i], "--diff") == 0 && hasValue) diffPath = argv[++i];
        else if (std::strcmp(argv[i], "--always-diff") == 0) alwaysDiff = true;
        else if (std::strcmp(argv[i], "--json") == 0) continue;
        else if (std::strcmp(argv[i], "--threshold") == 0 && hasValue) options.pixelThreshold = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--max-fraction") == 0 && hasValue) options.maxFraction = std::atof(argv[++i]);
        else return fail(json, std::string("unknown argument ") + argv[i] + "\n" + kUsage);
    }
    if (alwaysDiff && diffPath.empty()) return fail(json, "--always-diff needs --diff <out.png>");

    strata::image::Image expected, actual;
    std::string error;
    if (!strata::image::readPng(argv[1], expected, &error)) {
        return fail(json, std::string("cannot read ") + argv[1] + ": " + error);
    }
    if (!strata::image::readPng(argv[2], actual, &error)) {
        return fail(json, std::string("cannot read ") + argv[2] + ": " + error);
    }
    const auto r = strata::image::compare(expected, actual, options);

    bool wroteDiff = false;
    if (!diffPath.empty() && (!r.passed || alwaysDiff)) {
        if (!strata::image::writePng(diffPath, strata::image::diffImage(expected, actual, options.pixelThreshold),
                                     &error)) {
            return fail(json, "cannot write " + diffPath + ": " + error);
        }
        wroteDiff = true;
    }

    if (json) {
        std::printf("{\"passed\": %s, \"same_size\": %s, ", r.passed ? "true" : "false", r.sameSize ? "true" : "false");
        std::printf("\"expected\": %s, \"actual\": %s, ", jsonString(argv[1]).c_str(), jsonString(argv[2]).c_str());
        std::printf("\"expected_size\": [%d, %d], \"actual_size\": [%d, %d], ", expected.width, expected.height,
                    actual.width, actual.height);
        std::printf("\"pixels\": %lld, \"pixels_over\": %lld, \"fraction_over\": %s, ", static_cast<long long>(r.pixels),
                    static_cast<long long>(r.pixelsOver), jsonNumber(r.fractionOver).c_str());
        std::printf("\"max_delta_e\": %s, \"mean_delta_e\": %s, ", jsonNumber(r.maxDeltaE).c_str(),
                    jsonNumber(r.meanDeltaE).c_str());
        std::printf("\"threshold\": %s, \"max_fraction\": %s, \"diff\": %s}\n",
                    jsonNumber(options.pixelThreshold).c_str(), jsonNumber(options.maxFraction).c_str(),
                    wroteDiff ? jsonString(diffPath).c_str() : "null");
        return r.passed ? 0 : 1;
    }

    if (!r.sameSize) {
        std::printf("FAIL: %s is %dx%d but %s is %dx%d\n", argv[1], expected.width, expected.height, argv[2],
                    actual.width, actual.height);
    } else {
        std::printf("%s: %lld of %lld pixels over Delta E %.1f (%.4f%%, limit %.4f%%), max %.2f, mean %.4f\n",
                    r.passed ? "pass" : "FAIL", static_cast<long long>(r.pixelsOver), static_cast<long long>(r.pixels),
                    options.pixelThreshold, r.fractionOver * 100.0, options.maxFraction * 100.0, r.maxDeltaE,
                    r.meanDeltaE);
    }
    if (wroteDiff) std::printf("wrote %s\n", diffPath.c_str());
    return r.passed ? 0 : 1;
}
