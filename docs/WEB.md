# Strata in the browser (WebGL 2)

Strata builds with [Emscripten](https://emscripten.org) and draws through
WebGL 2. The C ABI (`include/strata/strata.h`) is the same in the browser as on
the desktop, so a host that already calls it recompiles without code changes.
Only who runs the frame loop differs (see [The frame loop](#the-frame-loop)).

## What runs where

| | Desktop (default) | Browser |
|---|---|---|
| GL | OpenGL 4.5 core, direct state access (`src/rhi/gl45_device.cpp`) | WebGL 2 = OpenGL ES 3.0 (`src/rhi/gles3_device.cpp`) |
| Shaders | GLSL 4.50 in `src/render/shaders.hpp` | The same shaders, rewritten to GLSL ES 3.00 when each pipeline is created (`src/rhi/glsl_es.cpp`) |
| Window and input | SDL3 (X11, Wayland, Win32) | SDL3's Emscripten driver: a canvas, DOM keyboard and mouse events |
| Headless | EGL surfaceless | a hidden SDL3 window on the canvas |
| Loader | glad | none (Emscripten's WebGL bindings) |
| Output | `libstrata.so` / `strata.dll` | static archives: `libstrata.a` |

The GLES 3 device is also built into the desktop library. Setting
`STRATA_GL_API=gles3` makes the desktop library use it on any OpenGL ES 3.0
context (Mesa's GLES through EGL in CI), which is how its pixels are tested
without a browser.

### Features and differences

Every render path works on WebGL 2: the legacy primitives, the sun with four
cascaded shadow maps, the moon, hemisphere ambient, fog and the analytic sky,
up to 32 point lights, up to 4 point-light cube shadow maps, the terrain tile,
static and textured meshes (with mip maps), and the overlays (lines, points,
text). There is no lower-quality mode.

What GLES 3.0 lacks is emulated inside the device, so the passes are the same
code on both:

| Desktop GL 4.5 feature | On WebGL 2 | Visible difference |
|---|---|---|
| Cube map arrays (`samplerCubeArrayShadow`, point-light shadows) | One cube map per shadowed light, on texture units 2 and 5–7, chosen in the shader by a branch | None: the GLES device on llvmpipe draws the 22 desktop goldens with 0 pixels over ΔE 4 |
| `GL_DEPTH_CLAMP` (sun shadow casters, overlays past the far plane) | `EXT_depth_clamp` where the browser has it (Chromium does, SwiftShader included). Otherwise the vertex shader stops near/far clipping and the fragment shader writes the clamped depth plus the shadow pass's polygon offset to `gl_FragDepth` | None measured: both paths draw the same pixels in Chromium and on llvmpipe. Without the extension the shadow pass loses early depth testing (slower) |
| Base vertex and base instance in draws | Folded into the vertex attribute offsets at draw time | None |
| `layout(binding = N)` in GLSL | Set through the API after linking | None |
| Debug output and object labels | Not available | `STRATA_FLAG_DEBUG_CONTEXT` logs nothing extra |
| Depth readback (`strata_read_depth_f32`, ABI 1.9) | Not available: a browser's headless request is a hidden window, which has no offscreen depth target | Returns `STRATA_ERROR_UNSUPPORTED`. The `depth_pack` draw itself runs on the GLES device: natively under `STRATA_GL_API=gles3` it reads the same depths |

Browsers differ from the desktop only in rasterisation detail at polygon
edges: in headless Chromium (SwiftShader) the 22 golden scenes differ from the
desktop llvmpipe goldens by 0.004 % to 0.11 % of pixels, all inside the
desktop tolerance (0.5 %). CI checks them against their own goldens
(`tests/golden/*/web-swiftshader.png`, 0 pixels over ΔE 4) and against the
desktop goldens at the desktop tolerance.

Limits that matter in a browser:

- Strata needs 9 fragment texture units: it binds units 1 to 7 and uses the
  device's last unit as a scratch unit while creating textures. WebGL 2's
  minimum is 16, but some phone browsers report fewer (13 has been seen), so
  the scratch unit is not fixed at 15.
- Shadow maps are as on the desktop (cascades default to 2048², cube faces
  512²). On a weak GPU, lower `StrataLighting.shadow_map_size` and
  `cascade_count`. With fewer, smaller maps, keep `shadow_distance` close to
  the scene's size: a scene a few units across under the kit's default
  `shadowDistanceMin` of 40 (or the engine's 150) loses its small objects'
  shadows at tier 1 (one 1024² cascade). Lower `shadowDistanceMin` for such
  a scene; see ARCHITECTURE.md, "Cascaded shadow maps".
- Strata is single-threaded and needs no `SharedArrayBuffer`, so pages need no
  COOP/COEP headers on its account.

## Building

You need CMake 3.24 or newer and emsdk. CI uses **emsdk 6.0.11** (pinned in
`.github/workflows/ci.yml`).

```sh
git clone https://github.com/emscripten-core/emsdk.git && cd emsdk
./emsdk install 6.0.11 && ./emsdk activate 6.0.11 && source ./emsdk_env.sh
cd /path/to/strata
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web -j8
ctest --test-dir build-web --output-on-failure     # unit tests, under Node
```

The build produces, in `build-web/`:

- `libstrata.a`: the engine, its image code and SDL3 merged into one archive.
  It defines the whole C ABI. (`libstrata_static.a`, `libstrata_image.a` and
  SDL3's archive are also there for CMake consumers that link
  `strata::static`.)
- `strata-village-day.html` (+ `.js`, `.wasm`, `.data`): the demo below.
- `strata-golden.html`: the golden-image checker as a web page, for
  `tests/web/run_goldens.cjs`.

Options:

| CMake option | Default | Meaning |
|---|---|---|
| `STRATA_WASM_EXCEPTIONS` | `wasm` | How C++ exceptions are compiled: `wasm` (`-fwasm-exceptions`) or `js` (`-fexceptions`). Strata turns internal exceptions into status codes at the C ABI, so they must be catchable. **Everything linked into one `.wasm` must use the same mode.** |
| `STRATA_BUILD_EXAMPLES` | `ON` | the demo |
| `STRATA_WITH_SIM` | `ON` | `modules/sim` (`libstrata_sim.a`) |

## The demo

![The village-day demo page in headless Chromium at 05:25: the village before sunrise, lit by a campfire and two lamps under a dark red sky, with a clock in the corner.](media/browser-demo.png)

```sh
cd build-web && python3 -m http.server 8000
# open http://localhost:8000/strata-village-day.html
```

`examples/village-day/main.cpp` uses only the C ABI. The same file builds
natively (`./build/strata-village-day`, an SDL3 window) and for the browser.

## Linking Strata into your own page

Link `libstrata.a` and include `strata.h`. The link needs WebGL 2 and the same
exception mode:

```sh
emcc game.c build-web/libstrata.a -Iinclude \
  -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1 -fwasm-exceptions \
  -o game.html
```

From CMake, `target_link_libraries(game PRIVATE strata::static)` brings the
WebGL 2 link options with it.

### Surfaces in a browser

| Surface | In a browser |
|---|---|
| `STRATA_SURFACE_WINDOW` | SDL3 creates a WebGL 2 context on the canvas `#canvas` (change it with SDL's `SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR`) and sizes it to `width` x `height` CSS pixels times the device pixel ratio. Keyboard, mouse, wheel and text events arrive through `strata_poll_event` as on the desktop. `strata_set_relative_mouse_mode` (ABI 1.8) is the browser's pointer lock, which is granted only from a user gesture: SDL asks for it on the next click on the canvas, and Escape releases it. |
| `STRATA_SURFACE_HEADLESS` | No EGL in a browser: a hidden SDL3 window, so it still needs a canvas. |
| `STRATA_SURFACE_HOST_CONTEXT` | Strata draws into the WebGL 2 context the host made current (`emscripten_webgl_make_context_current`). `get_proc_address` must still be non-NULL, as the ABI requires, but it is never called: Emscripten's bindings are used. `strata_create` fails with `STRATA_ERROR_PLATFORM` if no WebGL context is current. Strata saves and restores the GL state it touches, as on the desktop. |

### The frame loop

A browser page must return to the browser between frames, so a host cannot
loop forever around `strata_begin_frame` ... `strata_present`. Run one frame
per callback instead: `emscripten_set_main_loop_arg` (as the demo does), or
`requestAnimationFrame` in JavaScript calling an exported function. Present is
a no-op in a browser: the canvas is shown when the callback returns.

## The web kit

`web/kit/` is a shell for pages that embed one Strata scene in the page's own
WebGL 2 context (`STRATA_SURFACE_HOST_CONTEXT`): data views, dashboards and
viewers, where the page owns the data, the HTML around the canvas and the
input, and a small C++ file draws. It covers the shell around the scene and
leaves the scene alone: no scene graph, no data-to-3D mapping. Games that use
`STRATA_SURFACE_WINDOW` and SDL's input can still use its loading shell and
its browser tools.

Each release's `strata-<v>-wasm32-emscripten.tar.gz` carries it as `kit/`,
from `v0.7.0` on (older tarballs have no `kit/`):

| File | What it is |
|---|---|
| `strata_kit.h` | Header-only C++ host shell. Creates the WebGL 2 context (`preserveDrawingBuffer` on, so frames can be read back) and the engine; resizing; three quality tiers (sun cascades, shadow map size, point-light budget, point thinning); an orbit camera and its view-projection matrix; the sun from latitude, longitude and time; per-frame batches of primitives, points (alpha clamped), lines, prioritised point lights and, from kit 1.2, engine-drawn world labels (`Host::label`, Strata ABI 1.14); frame stats (`sk_stats`: primitives, points, lines, lights, terrain samples and, from kit 1.1, the meshes the host counts in `Host::meshes`). Exports `sk_init`, `sk_restore`, `sk_resize`, `sk_set_quality`, `sk_set_camera`, `sk_set_sun`, `sk_view_proj`, `sk_stats`, `sk_frame` and `sk_kit_version`. Natively (no Emscripten) the same file opens a headless engine: `sk_init_headless` and `sk_save_png`, so a scene also renders stills |
| `strata-kit.js` | Dependency-free page shell (`window.StrataKit`): canvas sizing with a capped device pixel ratio; adaptive quality (pixel density first, then the tier, with settle windows, reset when the tab is hidden); orbit, pan, pinch, wheel and keyboard camera; `project()` and `pick()` in CSS pixels; `webglcontextlost`/`webglcontextrestored`; `?quality=`, `?fps=1` and `?drive=1`; label placement with collision and hysteresis (`StrataKit.placeLabels`); `window.__strataKit` for tools |
| `strata-loader.js` | The loading shell (`window.StrataLoader`): the poster as a blurred backdrop, byte-accurate download progress with the size (or, for a host that boots its own way, `StrataLoader.show()` and `StrataLoader.progress(fraction, label)`: see below), and a fallback to the trailer clip or the poster, with the reason in words, when WebGL 2 is missing, the engine fails, a lost context does not come back, or the device is too slow at the lowest tier |
| `build-kit.sh` | One parameterised build: emsdk version check, the Strata tarball's SHA-256 (or a source build's `libstrata.a`), fixed flags (`-O2 -fwasm-exceptions`, WebGL 2 only, `MODULARIZE`, and `INCOMING_MODULE_JS_API` with `wasmBinary`, so the glue takes the bytes `strata-loader.js` already fetched instead of downloading the `.wasm` again) and the export list. Copies the page scripts beside the `.wasm`. A host that links its own glue with `strata-loader.js` needs `wasmBinary` in `-sINCOMING_MODULE_JS_API` too: emsdk 6.0.11's default list leaves it out |
| `ci-snippet.yml` | A GitHub Actions job for a host that commits its build: rebuild, `git diff --exit-code`, and the smoke test |
| `testing/smoke.cjs` | Playwright smoke test: at DPR 1, 2 and 3 (desktop mouse, tablet and phone touch) the frame is not one flat colour, the drawing buffer matches the capped DPR, and a real tap at each of the page's targets selects exactly what a direct pick there returns. `--context-loss` also loses and restores the context. Every `.wasm` the page loads must be requested once (cache hits count). First, on a blank page, the loading shell: `progress()` clamps, stays monotonic and shows its label, `slow()` twice shows one bar, and at 390 px the slow bar's text and buttons keep one line each |
| `testing/capture.cjs` | Steps scene time with `drive(t)` and saves frames (PNG, and GIF or MP4 through ffmpeg) |
| `example/` | The smallest host: six pickable pillars. CI builds it with `build-kit.sh` against its own `libstrata.a` and runs the smoke test on it |

### A host in three parts

The C++ side includes the header once with `STRATA_KIT_MAIN` defined and
defines three hooks. Its own setters (the data it draws) are ordinary exports:

```cpp
#define STRATA_KIT_MAIN
#include "strata_kit.h"

void strata_kit_configure(strata_kit::Options& o) { o.name = "garden"; }
void strata_kit_engine_ready(strata_kit::Host& h) { /* terrain, meshes: again after a lost context */ }
void strata_kit_build(strata_kit::Host& h, double t) {
    h.add(STRATA_PRIMITIVE_CUBE, 0, 0.5f, 0, 1, 1, 1, {0.6f, 0.3f, 0.2f});
    h.light(0, 2, 0, 6.0f, {1, 0.8f, 0.5f}, 1.5f, /*priority*/ 1);
}
STRATA_KIT_EXPORT void my_set_rows(const float* rows, int n) { /* keep them */ }
```

Light levels (kit 1.2, ABI 1.16): `Host::ambientTint`, `ambientLift`,
`sunIntensity` and `sunTint` go to `strata_set_ambient` and
`strata_set_sun_intensity` every frame; the defaults change nothing. Tier 0
keeps only 4 point lights, so a fill light that stands in for ambient should
be added with `h.fill(light, lift, priority)`: when the tier drops it, its
`lift` (linear RGB) is added to the ambient instead, and the scene keeps its
level on the devices that run at the lowest tier.

`build-kit.sh` builds it (settings from the environment or a sourced file;
see the script's header):

```sh
KIT_SOURCE=scene/main.cpp KIT_OUT=web KIT_NAME=scene KIT_EXPORT_NAME=createScene \
KIT_EXPORTS=_my_set_rows STRATA_VERSION=<v> STRATA_SHA256=<from SHA256SUMS> kit/build-kit.sh
```

The page loads the module through the loader and hands it to the kit:

```html
<canvas id="view"></canvas>
<script src="strata-loader.js"></script>
<script src="strata-kit.js"></script>
<script>
(async () => {
  const module = await StrataLoader.load({script: "scene.js", factory: "createScene", wasm: "scene.wasm",
    title: "the scene", poster: "poster.jpg", trailer: {webm: "clip.webm", mp4: "clip.mp4"}});
  const kit = StrataKit.create({module, canvas: document.getElementById("view"),
    frame(t) { /* write this frame's rows: module._my_set_rows(...) */ },
    pick(x, y, radius) { /* nearest thing to (x, y) by kit.project(...) */ },
    onTap(hit) {}, onSlow: () => StrataLoader.slow(), onFatal: (c, m) => StrataLoader.fallback(c, m)});
  kit.start();
  StrataLoader.done();
})().catch(() => {});  // the loader already shows why
</script>
```

A host whose own loader already knows how far it has got, such as a .NET
`browser-wasm` app (below) or a Unity-style boot that fetches its own files,
skips `load()` and drives the same overlay itself. `show()` takes `load()`'s
display options (`title`, `poster`, `trailer`, `fallback`, `mount`) and
returns false, with the fallback already showing, when WebGL 2 is missing.
`progress(fraction, label)` clamps the fraction to 0..1, never moves the bar
back (a later, lower value is ignored), and shows the label, or the
percentage, under the bar; it does nothing once `done()` or a fallback has
run. `slow()` shows its bar once: a second call while it is up does nothing.
On a narrow screen (a 390 px phone) the bar's buttons wrap under its text.

The overlay covers the viewport by default. For a scene in one section of a
longer page (a hero render, a card), pass the section as `mount` and set
`contain: true`: the overlay is then `position:absolute` inside the section
and scrolls with it, and so does the slow bar. A section whose position is
`static` is made `position:relative`. The example page shows this with
`?embed=1`, and CI runs the smoke test against it too: the smoke test
scrolls a canvas that is not wholly on screen into view before it captures
and taps.

```js
await StrataLoader.load({script: "scene.js", factory: "createScene", wasm: "scene.wasm",
  title: "the scene", mount: document.getElementById("hero"), contain: true});
```

```js
if (StrataLoader.show({title: "the game", poster: "poster.jpg", fallback: {href: "/", text: "Back"}})) {
  runtime.onProgress = (loaded, total) => StrataLoader.progress(loaded / total, `${loaded} of ${total} files`);
  await runtime.start();
  StrataLoader.done();
}
```

Rules the kit keeps, which a hand-written shell tends to lose:

- **Pointer maths stay in CSS pixels.** `project()` maps to the canvas's
  CSS size and `pick()` takes client coordinates minus the canvas's rectangle,
  so the drawing buffer's size, and so the device pixel ratio and its cap,
  never enter picking. The smoke test's tap == pick check at DPR 3 (above the
  default cap of 2) is what fails if that is broken.
- **Pixel density is given up before shadows.** Frames over `slowMs`
  (default 28 ms) averaged over 1.5 s first lower the DPR cap by a quarter,
  down to 1, then the tier; each step waits 2.5 s to settle. Only then does
  `onSlow` fire, once.
- **Animation comes from scene time.** Under `?drive=1` the frame loop does
  not run and `__strataKit.drive(t)` draws time `t` synchronously, so captures
  and tests repeat.
- **A lost context is rebuilt, not reloaded.** On `webglcontextrestored` the
  kit calls `sk_restore`, which destroys the old engine (its GL objects died
  with the context), makes a new context handle on the same canvas and a new
  engine, and runs `strata_kit_engine_ready` again. Losing it a third time in
  a minute, or no restore within 5 s, falls back.

The kit has its own version (`STRATA_KIT_VERSION_STRING`, `StrataKit.version`;
1.2 now), and `strata-kit.js` warns when the page's and the wasm's major
versions differ. A host that pins a release takes the kit from that release's tarball
(`kit/build-kit.sh`, with `STRATA_VERSION` and `STRATA_SHA256` set to the
same release) and needs no vendored copy. A host that needs a kit fix before
the next Strata release can vendor `web/kit/` at a pinned commit and link the
last release's `libstrata.a`: the kit uses only the C ABI, and its parts
that need a newer ABI than that release's `strata.h` (`Host::label`, ABI
1.14) compile out.

## Beyond Nations (.NET `browser-wasm`)

Notes for linking Strata into a .NET WebAssembly app (`wasm-tools` workload,
`RuntimeIdentifier=browser-wasm`). These are what is known now. The .NET
side has not been built against Strata yet, so check each point on the first
attempt.

1. **Emscripten version.** The .NET wasm workload links with its own emsdk
   (the `Microsoft.NET.Runtime.Emscripten.*` packages; see the
   `EmscriptenVersion` it reports). Object files from a much newer Emscripten
   (Strata's CI uses 6.0.11) may not link with an older `wasm-ld`. Build
   Strata with the same emsdk version as the .NET workload:
   `emcmake cmake -S . -B build-web ...` with that emsdk activated. If SDL
   3.4.16 does not build with that older Emscripten, configure with
   `-DSTRATA_WITH_SDL=OFF` and use `STRATA_SURFACE_HOST_CONTEXT` (below).
2. **Exceptions.** .NET's native link uses one exception mode for the whole
   module (`WasmEnableExceptionHandling`, true by default in recent .NET:
   `-fwasm-exceptions`). Build Strata with the matching
   `-DSTRATA_WASM_EXCEPTIONS=wasm` (or `js` if exception handling is off).
3. **Native reference.** Add the archive and the WebGL link flags:

   ```xml
   <PropertyGroup>
     <WasmBuildNative>true</WasmBuildNative>
     <EmccExtraLDFlags>$(EmccExtraLDFlags) -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2</EmccExtraLDFlags>
   </PropertyGroup>
   <ItemGroup>
     <NativeFileReference Include="path/to/libstrata.a" />
   </ItemGroup>
   ```

   The .NET build generates its P/Invoke table from the `DllImport` library
   names and the native files. If Strata's `[DllImport("strata")]` does not
   resolve against `libstrata.a`, rename the archive to `strata.a` (the
   module name and file name then match) before referencing it.
4. **Surface.** .NET's browser runtime does not create a GL context. Either
   let Strata create one with `STRATA_SURFACE_WINDOW` (SDL3 then needs a
   `<canvas id="canvas">` on the page, or the hint above), or create a WebGL 2
   context on the page yourself, make it current, and use
   `STRATA_SURFACE_HOST_CONTEXT` with any non-NULL `get_proc_address`.
5. **Frame loop.** Drive frames from the browser (`requestAnimationFrame`
   through JS interop, or a `setInterval`-style timer), one
   `begin_frame`/`end_frame` per callback; a blocking `while` loop on the main
   thread freezes the page.
6. **Threads.** Strata is single-threaded and does not need .NET's threaded
   wasm build.

## Testing

| What | How | Golden set |
|---|---|---|
| The GLES 3 device, natively | `STRATA_GL_API=gles3 strata-golden check tests/scenes tests/golden out` (EGL, Mesa llvmpipe) | the desktop goldens, `linux-llvmpipe.png` |
| The same without `EXT_depth_clamp` | add `STRATA_GLES_NO_DEPTH_CLAMP=1` | the desktop goldens |
| The browser | `node tests/web/run_goldens.cjs build-web out --platform web-swiftshader` (headless Chromium, SwiftShader) | `web-swiftshader.png` |
| The browser without `EXT_depth_clamp` | `STRATA_WEB_ENV=STRATA_GLES_NO_DEPTH_CLAMP=1 node tests/web/run_goldens.cjs ...` | `web-swiftshader.png` |
| The browser against the desktop | `node tests/web/run_goldens.cjs build-web out --platform linux-llvmpipe` | the desktop goldens, default tolerance |

`run_goldens.cjs` needs Playwright (`npm install --prefix tests/web
playwright@1.61.1 && npx --prefix tests/web playwright install chromium`); the
Playwright version pins Chromium and therefore SwiftShader. To bless new web
goldens, add `--bless`: they are written to `<out>/golden/`. Bless from CI's
pinned Chromium, as for the desktop goldens (see
[GOLDEN_IMAGES.md](GOLDEN_IMAGES.md)).

| The web kit | `web/kit/build-kit.sh web/kit/example/kit.conf` (with `KIT_OUT`, `STRATA_LIB`, `STRATA_INCLUDE`), then `node web/kit/testing/smoke.cjs <url> out --context-loss` | none: the checks are behavioural |

The README's browser images come from `tools/make_web_media.py build-web`.
