# Strata

Strata is a small C++17 engine for two simulation games: **Beyond Nations**,
whose simulation is C#, and a USD-native colony sim written in C++. It is not
a general-purpose engine. There is no editor, and nothing goes in without a
consumer in one of those two games.

The design is Preponderous Software's RFC 0020, *Shared C++ 3D Engine*.

- **Platform:** SDL3 for the window, the GL context and input. A headless EGL
  surface renders with no window and no X server, which is how CI runs.
- **Renderer:** OpenGL 4.5 core with direct state access. It sits behind a thin
  internal device interface (`GpuDevice`) and runs a fixed list of passes.
- **Two front doors:** a C++ API (`include/strata/strata.hpp`) for C++ games,
  and a versioned C ABI (`include/strata/strata.h`) that C# reaches through
  P/Invoke.

## Status

`v0.1.0` is the **skeleton milestone** plus lighting milestones **L1–L2**:

| Milestone | What it is | Golden images |
|---|---|---|
| Skeleton | CMake, the C ABI, the GL 4.5 device, SDL3 and EGL surfaces, golden-image harness, Beyond Nations' four primitives drawn from C# | `bn_primitives_legacy` (C++ **and** the C# harness) |
| L1 | A sun driven by time of day, with one shadow map | `l1_sun_single_map` |
| L2 | Four stabilised cascaded shadow maps, a moon at night, hemisphere ambient, fog and an analytic sky | `l2_csm_morning`, `l2_csm_noon`, `l2_csm_dusk`, `l2_csm_night` |
| L3 *(unreleased, ABI 1.1)* | Up to 32 unshadowed point lights with a range-windowed falloff | `l3_point_lights_night`, `l3_point_lights_falloff` |
| L4 *(unreleased, ABI 1.2)* | Up to 4 of them cast shadows through cube shadow maps | `l4_point_shadows_pillars`, `l4_point_shadows_night` |
| T1 *(unreleased, ABI 1.3)* | One static heightmapped terrain tile, lit, casting and receiving sun and point-light shadows | `t1_terrain_noon`, `t1_terrain_morning`, `t1_terrain_night_fire` |

Next come streaming and larger terrain (T2–T4), meshes and materials (M1–M4),
and optional OpenUSD modules (`STRATA_WITH_USD`, off by default).

## Building

You need CMake 3.24 or newer and a C++17 compiler (GCC 9+, Clang 10+ or MSVC
2022). CMake downloads SDL3 and glm at configure time, and each download is
pinned by SHA-256. On Linux, SDL3 needs the X11 development headers
(`libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev
libxss-dev`, or the matching `-devel` packages).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build --output-on-failure      # unit tests, no GPU needed
```

The build produces:

- `libstrata.so` (or `strata.dll`). It exports only the C ABI. SDL3 is linked
  in statically, and it does not link `libGL` or `libEGL`: GL entry points are
  loaded through the surface, and `libEGL.so.1` is opened only when a
  headless surface is requested.
- `strata-golden` and `strata-imgcmp`, the golden-image tools (see
  [docs/GOLDEN_IMAGES.md](docs/GOLDEN_IMAGES.md)).
- The `strata::static` CMake target, for C++ games that use the C++ API.

## Running the golden images headless

```sh
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
./build/strata-golden check tests/scenes tests/golden build/golden-out
```

The default surface is EGL surfaceless, so no display is needed. To test the
SDL3 window path instead, start Xvfb **with the llvmpipe variables in its own
environment** and pass `--surface window`:

```sh
LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe Xvfb :99 -screen 0 1024x768x24 &
DISPLAY=:99 ./build/strata-golden check tests/scenes tests/golden build/golden-out --surface window
```

## Using it from C#

`bindings/csharp/Strata.Interop` declares the C ABI for P/Invoke.
`tests/csharp/Strata.PrimitivesHarness` shows the whole path:

1. A struct laid out like Beyond Nations' `RenderItem` is checked against
   `strata_struct_size` and `strata_struct_field_offset`.
2. The camera is built with `System.Numerics`.
3. The managed array is passed to `strata_submit_primitives` as a pointer,
   with no copy or conversion.

```sh
dotnet run --project tests/csharp/Strata.PrimitivesHarness -c Release -- \
  tests/scenes/bn_primitives_legacy.scene out.png --native-dir build
./build/strata-imgcmp tests/golden/bn_primitives_legacy/linux-llvmpipe.png out.png
```

A host that already owns a window and a GL context, as Beyond Nations' Silk.NET
host does today, creates Strata with `STRATA_SURFACE_HOST_CONTEXT` and a
`get_proc_address` callback. Strata then draws into that context and puts back
the GL state it touched.

## Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md): layers, the C ABI rules, the pass list and
  the lighting model.
- [CONTRIBUTING.md](CONTRIBUTING.md): building, testing, golden-image policy and
  PR conventions.
- [docs/GOLDEN_IMAGES.md](docs/GOLDEN_IMAGES.md): how images are compared, and
  the tolerances.
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md): dependency licences.

## Licence

MIT. See [LICENSE](LICENSE).
