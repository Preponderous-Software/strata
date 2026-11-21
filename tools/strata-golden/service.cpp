#include "service.hpp"

#include "adapters/directory_scene_source.hpp"
#include "adapters/engine_scene_renderer.hpp"
#include "adapters/png_file_sink.hpp"
#include "adapters/png_golden_directory.hpp"
#include "adapters/stdio_console.hpp"
#include "golden.hpp"

namespace strata::golden {

int checkScenes(const std::string& scenesDir, const std::string& goldenDir, const std::string& outDir,
                const std::string& platform, const CheckOptions& options) {
    StdioConsole console;
    DirectorySceneSource scenes(scenesDir);
    EngineSceneRenderer renderer(console);
    PngGoldenDirectory goldens(goldenDir, platform);
    PngFileSink artifacts(outDir);
    return runCheck(options, scenes, renderer, goldens, artifacts, console);
}

int renderScene(const std::string& scenePath, const std::string& outPng, int32_t surface) {
    StdioConsole console;
    DirectorySceneSource scenes("");
    EngineSceneRenderer renderer(console);
    PngFileSink artifacts("");
    return runRender(scenePath, outPng, surface, scenes, renderer, artifacts, console);
}

}  // namespace strata::golden
