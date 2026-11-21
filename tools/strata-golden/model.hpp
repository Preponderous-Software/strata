// strata-golden's plain values: what the check is asked to do, and what the
// ports hand back. No I/O here.
#pragma once

#include <cstdint>
#include <string>

#include "image/compare.hpp"
#include "image/image.hpp"
#include "scene_file.hpp"

namespace strata::golden {

// `check`'s options after the command line is parsed.
struct CheckOptions {
    std::string only;                 // check this one scene (its name), or all
    bool bless = false;               // write the renders as the new goldens
    image::CompareOptions compare;    // Delta E threshold and allowed share
    int32_t surface = STRATA_SURFACE_HEADLESS;
};

// One test scene: its name (the file's stem) and how its source finds it.
struct SceneRef {
    std::string name;
    std::string ref;
};

// A scene read by a SceneSource; `error` is the parser's message when !ok.
struct Loaded {
    bool ok = false;
    scene::Scene scene;
    std::string error;
};

// An image read from a store; `error` says why when !ok.
struct ImageRead {
    bool ok = false;
    image::Image image;
    std::string error;
};

// The outcome of writing an image; `error` says why when !ok.
struct ImageWritten {
    bool ok = false;
    std::string error;
};

}  // namespace strata::golden
