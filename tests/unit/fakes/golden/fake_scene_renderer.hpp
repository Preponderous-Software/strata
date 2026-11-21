#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ports/scene_renderer.hpp"

namespace strata::golden {

// SceneRenderer without a GPU: a solid image of the scene's size in `grey`,
// "fake GPU" as the GPU. A width listed in `failWidths` fails the render
// (and, in `noEngineWidths`, fails before any engine, leaving `gpu` alone).
class FakeSceneRenderer final : public SceneRenderer {
public:
    uint8_t grey = 128;
    std::vector<int> failWidths;
    std::vector<int> noEngineWidths;
    int renders = 0;

    bool render(const scene::Scene& scene, int32_t, image::Image& out, std::string& gpu) override {
        ++renders;
        for (int w : noEngineWidths) {
            if (w == scene.width) return false;
        }
        gpu = "fake GPU";
        for (int w : failWidths) {
            if (w == scene.width) return false;
        }
        out.width = scene.width;
        out.height = scene.height;
        out.rgba.assign(static_cast<size_t>(scene.width) * static_cast<size_t>(scene.height) * 4, grey);
        for (size_t i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = 255;
        return true;
    }
};

}  // namespace strata::golden
