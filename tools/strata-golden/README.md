# strata-golden (`tools/strata-golden/`)

Renders the golden-image scenes and checks them (see
[docs/GOLDEN_IMAGES.md](../../docs/GOLDEN_IMAGES.md) for how goldens are used).
The tool is a core with ports and adapters: the core decides what `check` and
`render` do and what they report; scenes, the engine, PNG files and the
console come in through five small interfaces.

```text
main.cpp (command line) ──> service ──> golden (core) ──> ports <── adapters
                                         model, text      SceneSource     DirectorySceneSource
                                                          SceneRenderer   EngineSceneRenderer (C ABI)
                                                          GoldenStore     PngGoldenDirectory
                                                          ArtifactSink    PngFileSink
                                                          Console         StdioConsole
```

| File | Layer | What it holds |
|---|---|---|
| `model.hpp` | model | `CheckOptions`, `SceneRef`, `Loaded`, `ImageRead`, `ImageWritten` |
| `text.hpp` | core | `sformat()`: printf into a string |
| `golden.{hpp,cpp}` | core | `runCheck` (list, render, bless or compare, artifacts, the report, the exit code) and `runRender` |
| `ports/*.hpp` | ports | one interface each, its contract in its comment |
| `adapters/*.{hpp,cpp}` | adapters | one implementation each |
| `service.{hpp,cpp}` | composition root | `checkScenes`, `renderScene`: the production adapters wired into the core |
| `main.cpp` | caller | the command line: usage, options, `--surface`, the version banner |

| Port | Production adapter | Test fake (`tests/unit/fakes/golden/`) |
|---|---|---|
| `SceneSource` | `DirectorySceneSource`: `*.scene` files, `_*` are includes | `FakeSceneSource` |
| `SceneRenderer` | `EngineSceneRenderer`: a fresh engine per scene through the C ABI, two frames, `expect_ray` and `expect_depth` | `FakeSceneRenderer` |
| `GoldenStore` | `PngGoldenDirectory`: `<golden-dir>/<scene>/<platform>.png` | `FakeGoldenStore` |
| `ArtifactSink` | `PngFileSink`: `<out-dir>/<scene>.actual.png` and `.diff.png`; render's `<out.png>` | `FakeArtifactSink` |
| `Console` | `StdioConsole`: stdout and stderr | `FakeConsole` |

`tests/unit/test_golden.cpp` runs the core with the fakes (no GPU, no files)
and the file adapters on a temporary directory.

**Rules.** `model`, `text`, `golden` and `ports/` never include `adapters/`,
`service` or `main`; each port and adapter header declares one class.
`tests/python/test_golden_layers.py` enforces both.

**Adding a backend** (say, goldens fetched from a CI artifact): one new file
pair in `adapters/` implementing the port, and one line in `service.cpp`
choosing it. The core does not change.
