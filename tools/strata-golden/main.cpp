// strata-golden: renders the golden-image scenes and checks them.
//
//   strata-golden render <scene-file> <out.png> [--surface headless|window]
//   strata-golden check  <scenes-dir> <golden-dir> <out-dir> [options]
//       --platform NAME      golden file name inside each test directory (default linux-llvmpipe)
//       --only NAME          check one scene
//       --bless              write the rendered images as the new goldens
//       --threshold DE       per-pixel Delta E threshold (default 4.0)
//       --max-fraction F     share of pixels allowed over it (default 0.005)
//       --surface KIND       headless (default) or window
//
// A scene named foo.scene is checked against <golden-dir>/foo/<platform>.png.
// Scene files whose names start with '_' are includes, not tests. On a
// failure the rendered image and a diff are written to <out-dir>.
//
// This file is the command line only; README.md describes the core, its ports
// and adapters, and service.cpp wires them.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "service.hpp"
#include "strata/strata.h"

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage: strata-golden render <scene> <out.png> [--surface headless|window]\n"
                 "       strata-golden check <scenes-dir> <golden-dir> <out-dir> [--platform NAME] [--only NAME]\n"
                 "                           [--bless] [--threshold DE] [--max-fraction F] [--surface KIND]\n");
    return 2;
}

int32_t parseSurface(const std::string& name) {
    if (name == "headless") return STRATA_SURFACE_HEADLESS;
    if (name == "window") return STRATA_SURFACE_WINDOW;
    std::fprintf(stderr, "unknown surface '%s' (headless or window)\n", name.c_str());
    std::exit(2);
}

int commandRender(int argc, char** argv) {
    if (argc < 4) return usage();
    int32_t surface = STRATA_SURFACE_HEADLESS;
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "--surface") == 0 && i + 1 < argc) surface = parseSurface(argv[++i]);
        else return usage();
    }
    return strata::golden::renderScene(argv[2], argv[3], surface);
}

int commandCheck(int argc, char** argv) {
    if (argc < 5) return usage();
    std::string platform = "linux-llvmpipe";
    strata::golden::CheckOptions options;
    for (int i = 5; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--platform" && hasValue) platform = argv[++i];
        else if (arg == "--only" && hasValue) options.only = argv[++i];
        else if (arg == "--bless") options.bless = true;
        else if (arg == "--threshold" && hasValue) options.compare.pixelThreshold = std::atof(argv[++i]);
        else if (arg == "--max-fraction" && hasValue) options.compare.maxFraction = std::atof(argv[++i]);
        else if (arg == "--surface" && hasValue) options.surface = parseSurface(argv[++i]);
        else return usage();
    }
    return strata::golden::checkScenes(argv[2], argv[3], argv[4], platform, options);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    std::printf("strata %s (ABI %u.%u)\n", strata_version_string(), strata_abi_version(), strata_abi_minor());
    if (std::strcmp(argv[1], "render") == 0) return commandRender(argc, argv);
    if (std::strcmp(argv[1], "check") == 0) return commandCheck(argc, argv);
    return usage();
}
