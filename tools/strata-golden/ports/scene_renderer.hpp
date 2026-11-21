#pragma once

#include <cstdint>
#include <string>

#include "model.hpp"

namespace strata::golden {

// Draws a scene and captures the frame.
//
// Contract: render() draws `scene` with a fresh engine on `surface`
// (STRATA_SURFACE_HEADLESS or _WINDOW), checks the scene's expect_ray and
// expect_depth lines, and on success fills `out` with RGBA8 pixels (alpha
// forced to 255). `gpu` is set to the GPU's description as soon as an engine
// exists and is left alone otherwise, so after a failed create it still names
// the last GPU used. Returns false when anything failed; the reason has
// already been reported on the renderer's console. It never throws for a
// scene the engine refuses. Blocking; main thread only (GL).
//
// Scene and Image are the scene parser's and the image module's own types:
// they are the contract.
class SceneRenderer {
public:
    virtual ~SceneRenderer() = default;
    virtual bool render(const scene::Scene& scene, int32_t surface, image::Image& out, std::string& gpu) = 0;
};

}  // namespace strata::golden
