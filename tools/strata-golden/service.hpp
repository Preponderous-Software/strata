// strata-golden's composition root: wires the production adapters (scene
// directory, Strata engine, PNG goldens and artifacts, stdio) into the core.
#pragma once

#include <cstdint>
#include <string>

#include "model.hpp"

namespace strata::golden {

// `check <scenes-dir> <golden-dir> <out-dir>` with goldens named <platform>.png.
int checkScenes(const std::string& scenesDir, const std::string& goldenDir, const std::string& outDir,
                const std::string& platform, const CheckOptions& options);

// `render <scene> <out.png>`.
int renderScene(const std::string& scenePath, const std::string& outPng, int32_t surface);

}  // namespace strata::golden
