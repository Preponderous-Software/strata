// strata-golden's policy: which scenes to check, blessing, comparing,
// counting failures and what the report says. It does no I/O of its own:
// scenes, renders, goldens, artifacts and the report all go through ports.
// service.hpp wires the production adapters; main.cpp parses the command line.
#pragma once

#include <cstdint>
#include <string>

#include "model.hpp"
#include "ports/artifact_sink.hpp"
#include "ports/console.hpp"
#include "ports/golden_store.hpp"
#include "ports/scene_renderer.hpp"
#include "ports/scene_source.hpp"

namespace strata::golden {

// `check`: renders every test scene and compares it with its golden (or, with
// bless, stores the render as the golden). A failing scene's render and diff
// go to `artifacts`. Returns the exit code: 0 when every scene passed, else 1.
int runCheck(const CheckOptions& options, SceneSource& scenes, SceneRenderer& renderer, GoldenStore& goldens,
             ArtifactSink& artifacts, Console& console);

// `render`: draws the scene at `sceneRef` and writes it to `outFile` in
// `artifacts`. Returns the exit code: 0 on success, else 1.
int runRender(const std::string& sceneRef, const std::string& outFile, int32_t surface, SceneSource& scenes,
              SceneRenderer& renderer, ArtifactSink& artifacts, Console& console);

}  // namespace strata::golden
