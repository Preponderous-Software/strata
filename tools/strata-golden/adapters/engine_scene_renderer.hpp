#pragma once

#include <cstdint>
#include <string>

#include "ports/console.hpp"
#include "ports/scene_renderer.hpp"
#include "strata/strata.h"

namespace strata::golden {

// SceneRenderer through Strata's C ABI: a fresh engine per scene, two frames,
// the second captured. Also checks the scene's expect_ray and expect_depth
// lines, which need the engine. Reports problems and frame stats on `console`.
class EngineSceneRenderer final : public SceneRenderer {
public:
    explicit EngineSceneRenderer(Console& console);
    bool render(const scene::Scene& scene, int32_t surface, image::Image& out, std::string& gpu) override;

private:
    bool check(StrataStatus status, const char* what);
    bool checkDepth(StrataEngine* engine, const scene::Scene& scene, int32_t width, int32_t height);

    Console& console_;
};

}  // namespace strata::golden
