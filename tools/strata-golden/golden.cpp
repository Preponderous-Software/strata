#include "golden.hpp"

#include "text.hpp"

namespace strata::golden {

int runRender(const std::string& sceneRef, const std::string& outFile, int32_t surface, SceneSource& scenes,
              SceneRenderer& renderer, ArtifactSink& artifacts, Console& console) {
    const Loaded loaded = scenes.load(sceneRef);
    if (!loaded.ok) {
        console.err(sformat("%s\n", loaded.error.c_str()));
        return 1;
    }
    image::Image image;
    std::string gpu;
    console.out(sformat("%s\n", sceneRef.c_str()));
    if (!renderer.render(loaded.scene, surface, image, gpu)) return 1;
    console.out(sformat("  GPU: %s\n", gpu.c_str()));
    const ImageWritten written = artifacts.write(outFile, image);
    if (!written.ok) {
        console.err(sformat("could not write %s: %s\n", artifacts.locate(outFile).c_str(), written.error.c_str()));
        return 1;
    }
    console.out(sformat("  wrote %s\n", artifacts.locate(outFile).c_str()));
    return 0;
}

int runCheck(const CheckOptions& options, SceneSource& scenes, SceneRenderer& renderer, GoldenStore& goldens,
             ArtifactSink& artifacts, Console& console) {
    const std::vector<SceneRef> refs = scenes.list(options.only);
    if (refs.empty()) {
        console.err(sformat("no scenes found in %s\n", scenes.location().c_str()));
        return 1;
    }
    artifacts.prepare();

    int failures = 0;
    std::string gpu;
    for (const SceneRef& ref : refs) {
        const std::string& name = ref.name;
        console.out(sformat("%s\n", name.c_str()));
        const Loaded loaded = scenes.load(ref.ref);
        if (!loaded.ok) {
            console.err(sformat("  %s\n", loaded.error.c_str()));
            ++failures;
            continue;
        }
        image::Image actual;
        if (!renderer.render(loaded.scene, options.surface, actual, gpu)) {
            ++failures;
            continue;
        }
        if (options.bless) {
            const ImageWritten written = goldens.write(name, actual);
            if (!written.ok) {
                console.err(sformat("  could not write %s: %s\n", goldens.locate(name).c_str(), written.error.c_str()));
                ++failures;
            } else {
                console.out(sformat("  blessed %s\n", goldens.locate(name).c_str()));
            }
            continue;
        }
        const std::string actualFile = name + ".actual.png";
        const ImageRead golden = goldens.read(name);
        if (!golden.ok) {
            console.err(sformat("  FAIL: no golden at %s (%s)\n", goldens.locate(name).c_str(), golden.error.c_str()));
            artifacts.write(actualFile, actual);
            ++failures;
            continue;
        }
        const image::Image& expected = golden.image;
        const auto result = image::compare(expected, actual, options.compare);
        if (!result.sameSize) {
            console.out(sformat("  FAIL: size %dx%d, golden is %dx%d\n", actual.width, actual.height, expected.width,
                               expected.height));
        } else {
            console.out(sformat("  %s: %lld of %lld pixels over Delta E %.1f (%.4f%%, limit %.4f%%), max %.2f, mean %.4f\n",
                               result.passed ? "pass" : "FAIL", static_cast<long long>(result.pixelsOver),
                               static_cast<long long>(result.pixels), options.compare.pixelThreshold,
                               result.fractionOver * 100.0, options.compare.maxFraction * 100.0, result.maxDeltaE,
                               result.meanDeltaE));
        }
        if (!result.passed) {
            ++failures;
            artifacts.write(actualFile, actual);
            artifacts.write(name + ".diff.png", image::diffImage(expected, actual, options.compare.pixelThreshold));
            console.out(sformat("  wrote %s and its diff\n", artifacts.locate(actualFile).c_str()));
        }
    }
    console.out(sformat("GPU: %s\n%zu scene(s), %d failure(s)\n", gpu.c_str(), refs.size(), failures));
    return failures == 0 ? 0 : 1;
}

}  // namespace strata::golden
