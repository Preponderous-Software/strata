# Golden images

Every render path is checked in CI by drawing a fixed scene on **llvmpipe**
(Mesa's software rasteriser, which provides OpenGL 4.5 core with no GPU) and
comparing the result with a committed PNG.

## Layout

```text
tests/scenes/<name>.scene                    the scene (camera, lighting, items)
tests/scenes/_<anything>.scene               an include, not a test
tests/golden/<name>/<platform>.png           the expected image
```

`<platform>` is one of:

| Platform | Drawn by | Pinned by |
|---|---|---|
| `linux-llvmpipe` | the GL 4.5 device on Mesa llvmpipe (EGL headless and an SDL3 window under Xvfb), and the GLES 3.0 device on the same Mesa (`STRATA_GL_API=gles3`) | a dated Ubuntu archive snapshot (see `.github/workflows/ci.yml`) |
| `web-swiftshader` | the Emscripten build in headless Chromium, WebGL 2 on SwiftShader (`tests/web/run_goldens.cjs`) | the Playwright version (`PLAYWRIGHT_VERSION` in CI), which fixes Chromium |

The browser build is also checked against `linux-llvmpipe` at the default
tolerance, so it cannot drift from the desktop. See [WEB.md](WEB.md).

The scene format is documented at the top of `tools/common/scene_file.hpp`.
The C# harness reads the same files, so `bn_primitives_legacy` is checked twice:
once as drawn by `strata-golden` through the C ABI from C++, and once from C#
with a Beyond Nations-shaped struct and `System.Numerics` matrices. Both must
match the one golden.

## How images are compared

`strata-imgcmp` and `strata-golden check` work the same way:

1. Each pixel pair is converted from sRGB to CIE L\*a\*b\* (D65).
2. They are compared with the CIE76 colour difference, **ΔE**. A ΔE of about
   2.3 is the smallest difference a person notices side by side.
3. A pixel **differs** when its ΔE is greater than **4.0**.
4. A comparison **passes** when no more than **0.5 %** of pixels differ, and
   the images are the same size.

Alpha is ignored.

### Why these numbers

| Measured on llvmpipe | Pixels over ΔE 4 |
|---|---|
| Same Mesa, same scene, two runs (headless) | 0 |
| C# harness (System.Numerics, depth 0..1 projection) vs C++ golden (glm, depth −1..1) | 0 |
| SDL3 window under Xvfb vs headless EGL golden | 2–6 of 230,400 (≤ 0.003 %) |
| Goldens blessed on Mesa 21.2.6 (LLVM 12) vs CI's pinned Mesa 25.2.8 (LLVM 20) | 0 (max ΔE 0.66) |
| GLES 3.0 device vs the GL 4.5 goldens, same Mesa (with and without `EXT_depth_clamp`) | 0 (max ΔE 0.51) |
| Chromium/SwiftShader (WebGL 2) vs the llvmpipe goldens | 10–254 of 230,400 (0.004–0.11 %), polygon edges |

The tolerance therefore leaves more than 100× headroom over the noise
measured between surfaces. It is still small enough that a missing object or a
wrong shadow fails: one 20×20-pixel object is 0.17 % of a 640×360 frame. The
per-pixel threshold of 4.0 stops anti-aliasing and rounding at edges from
counting. Mesa is pinned in CI, so drift between Mesa releases does not cause
failures. When the snapshot date is moved forward, every golden is re-checked
and, if necessary, re-blessed in the same PR.

The numbers can be changed per run (`--threshold`, `--max-fraction`). The
defaults are the policy.

## When a golden fails

CI uploads `golden-out/` as a workflow artifact. It holds `<name>.actual.png`
(what was rendered) and `<name>.diff.png`, in which pixels over the threshold
are red and the rest is the expected image dimmed to grey. Look at both before
deciding whether the change is intended.

## Updating a golden

A golden changes only in a PR that says why and shows the before and after
images:

```sh
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
./build/strata-golden check tests/scenes tests/golden build/golden-out --bless --only <name>
```

Bless from the CI environment's Mesa, not a different local one. The simplest
route is to download the CI run's `golden-out` artifact and commit its
`.actual.png` as the new golden.
