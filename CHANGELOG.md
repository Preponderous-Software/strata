# Changelog

## Unreleased

- ABI 1.9: `strata_read_depth_f32` reads the last frame's depth as float32,
  top row first: the planar distance along the view axis in world units, or
  +infinity where nothing was drawn. Headless surfaces only; a window, a host
  context, and a headless request that fell back to a hidden window return
  `STRATA_ERROR_UNSUPPORTED`. Asked for by RFC 0026 (headless rendering for
  [usd-synth-data](https://github.com/Preponderous-Software/usd-synth-data)).
  A `depth_pack` draw copies the depth attachment's float bits into RGBA8,
  so it is exact on the GL 4.5 and GLES 3.0 devices alike, and
  `src/render/depth_readback.cpp` linearises it with the frame's projection
  (either depth convention, or orthographic), with unit tests.
  `Engine::readDepth`; C# P/Invoke. New golden `depth_readback_planes` and a
  scene directive, `expect_depth <x> <y> <distance|inf>`, that checks the
  readback after the captured frame (skipped where unsupported).
- The Linux release tarball keeps the executable bit on `bin/strata-golden`
  and `bin/strata-imgcmp`. In 0.4.0 they were mode 0644 (artifact uploads drop
  Unix file modes), so they ran only after a `chmod +x`. A new CI job,
  `Package release assets`, builds every release asset on every run, not
  only on tags, sets the modes, and fails unless the tarball's tools are
  executable and `strata-imgcmp` runs straight from it. The `Release` job
  publishes that job's assets.
- `strata.h` comments for `StrataPointLight`, `StrataTerrainTile` and
  `StrataMesh` now cover `STRATA_LIGHTING_SPACE` (ABI 1.5): point lights shade
  it, and the terrain and meshes are drawn under every lighting mode and take
  point-light shadows in it. The mesh comment no longer says meshes share the
  terrain's vertex format, which gained a uv in ABI 1.7. Comments only; no
  layout or signature changes.

## 0.4.0

The browser (WebGL 2 through Emscripten), a seasonal sun, textures, picking
and texture updates. Every ABI addition is compatible: `STRATA_ABI_VERSION`
stays 1 and `STRATA_ABI_MINOR` is 8.

- ABI 1.8, for [Kindling](https://github.com/Preponderous-Software/kindling)
  (a first-person cozy restoration game): grime cleaned patch by patch,
  knowing what is under the crosshair, and mouse-look. All three are
  compatible additions; every ABI 1.x host keeps working.
  - `strata_update_texture_rgba8` replaces a rectangle of an existing
    texture (same texel conventions as creation; the mip chain is rebuilt for
    `STRATA_TEXTURE_MIPMAPS` textures). New `GpuDevice::updateTexture` in the
    GL 4.5 and GLES 3.0 / WebGL 2 devices; `Engine::updateTextureRgba8`.
  - `StrataRayHit` (`STRATA_STRUCT_RAY_HIT`, 36 bytes) and
    `strata_raycast_meshes`: the nearest static-mesh triangle along a ray,
    through each mesh's model matrix, both faces counting. Meshes now keep a
    CPU copy of their positions and indices for this (12 bytes per vertex
    plus 4 per index). GL-free maths in `src/render/ray_pick.cpp`, with unit
    tests; `Engine::raycastMeshes`.
  - `strata_set_relative_mouse_mode`: on the SDL3 window, hides and captures
    the cursor and reports motion as `dx`/`dy` only (pointer lock in a
    browser); `STRATA_ERROR_UNSUPPORTED` on headless and host-context
    surfaces. `Engine::setRelativeMouseMode`.
  - C# mirror and P/Invokes in `Strata.Interop`, with layout tests.
  - New golden `tex_update_grime`, and two scene directives:
    `texture_update` (applied between the two frames `strata-golden` draws)
    and `expect_ray` (fails the scene unless the raycast hits the named mesh).

- The WebGL 2 / GLES device starts on contexts with fewer than 16 fragment
  texture units. Its scratch unit (where textures are bound while created)
  was fixed at 15, so a phone browser reporting 13 failed with "the GLES
  device needs 16 fragment texture units". The scratch unit is now the
  device's last unit, and 9 units (Strata's 8 plus scratch) are required.
- The USD and save modules build and pass their tests on Windows (MSVC).
  `modules/save` gains `fs_win32.cpp` (MoveFileExW with write-through,
  FlushFileBuffers, LockFileEx; UTF-8 paths converted to UTF-16), selected
  instead of `fs_posix.cpp` on Windows. The `--crash` suite (fork, setrlimit,
  SIGKILL) stays POSIX-only and reports itself skipped on Windows. CI builds a
  Windows OpenUSD prebuilt once (`key.sh windows`,
  `build-openusd-windows.sh`) and publishes it as a release asset beside the
  Linux one.
- Strata runs in the browser (WebGL 2). A second `GpuDevice` backend,
  OpenGL ES 3.0 (`src/rhi/gles3_device.cpp`), draws every pass: the GL 4.5
  shaders are rewritten to GLSL ES 3.00 when pipelines are created, cube map
  arrays become one cube map per shadowed light, base vertex/instance fold
  into attribute offsets, and depth clamp uses `EXT_depth_clamp` or a shader
  fallback. The C ABI is unchanged (no new functions; ABI stays 1.7). Desktop
  builds still default to OpenGL 4.5 and its goldens are unchanged.
- Emscripten builds (`emcmake cmake ...`, emsdk 6.0.11 in CI): SDL3's
  Emscripten driver for the canvas and input, `libstrata.a` (engine + SDL3 in
  one archive), C++ exceptions compiled as `-fwasm-exceptions` by default
  (`STRATA_WASM_EXCEPTIONS`), and `strata-golden` as a web page. See
  [docs/WEB.md](docs/WEB.md), including notes for .NET `browser-wasm` hosts.
- `examples/village-day`: the L2 village through a day, from the C ABI only;
  an SDL3 window natively, a canvas in the browser. README animation of it
  running in headless Chromium (`tools/make_web_media.py`).
- The desktop library can run the GLES device too: `STRATA_GL_API=gles3`
  selects it on an OpenGL ES 3.0 context (EGL or SDL3), and
  `STRATA_GLES_NO_DEPTH_CLAMP=1` forces the depth clamp fallback. CI draws
  the desktop goldens through it on llvmpipe (0 pixels over ΔE 4 on all 22).
- CI job `build-web`: builds with Emscripten, runs the unit tests under Node,
  checks `libstrata.a` defines the C ABI, and draws every golden scene in
  headless Chromium (Playwright 1.61.1, SwiftShader) against new
  `web-swiftshader.png` goldens, with and without `EXT_depth_clamp`, and
  against the desktop goldens at the default tolerance. Releases gain a
  `strata-<version>-wasm32-emscripten.tar.gz` asset.
- glad is regenerated with OpenGL ES 3.0 merged into `glad/gl.h`.
  `GpuDevice` gains `createIndexBuffer` (WebGL fixes a buffer's kind at its
  first binding).

- Seasonal sun (ABI 1.6): `StrataSolarPosition`, `strata_solar_position`
  (the sun's direction, elevation, azimuth, declination, hour angle and
  equation of time for a UTC moment, latitude and longitude, from NOAA's solar
  position equations) and `strata_set_sun_direction`, which places the
  `STRATA_LIGHTING_SUN` sun directly instead of by `time_of_day` and latitude.
  The existing time-of-day sun is unchanged and stays the default.
- Scene files take `sun date <YYYY-MM-DD> <HH:MM> <utc_offset> <lat> <lon>`.
  New goldens `sun_june_noon`, `sun_december_noon` and
  `sun_december_afternoon`.
- Textures and textured, transformable meshes (ABI 1.7):
  `strata_create_texture_rgba8` (RGBA8, optional mip chain with trilinear
  filtering, wrap or clamp per axis) and `strata_destroy_texture`;
  `StrataTexturedMesh` and `strata_add_textured_mesh` (a static mesh with
  uvs, a texture that multiplies its colour, and `STRATA_MESH_UNLIT` for
  emissive surfaces that ignore lights and fog and cast no shadow); and
  `strata_set_mesh_transform`, a per-mesh model matrix (normals through its
  inverse transpose) applied in the opaque and shadow passes, so a planet
  spins about a tilted axis with one call a frame and no re-upload.
- `GpuDevice` textures gain mip chains and repeat wrapping. Static meshes
  now have their own pipelines (a uv per vertex, a model matrix in uniform
  binding 3, the texture on unit 4); untextured meshes sample a white texel
  and draw exactly as before.
- Scene files take `texture`, `sphere_mesh`, `uvs`, `mesh_texture`,
  `mesh_unlit` and `mesh_transform`. New goldens `tex_globe_sun` and
  `tex_space_planets`.

## 0.3.0

Static meshes, overlays and space lighting, and the simulation module. Every
ABI addition is compatible: `STRATA_ABI_VERSION` stays 1 and
`STRATA_ABI_MINOR` is 5.

- Static meshes (ABI 1.4): `StrataMesh`, `strata_add_mesh`,
  `strata_remove_mesh` and `strata_clear_meshes`. Up to 256 indexed triangle
  meshes, already in world space, with a linear RGB colour per vertex or per
  mesh and host or engine-computed (area-weighted) normals. They share the
  terrain's vertex format and pipelines, so they are drawn under both lighting
  modes and cast and receive sun and point-light shadows. For geometry the
  primitives and the terrain tile cannot express, such as extruded footprints
  and ribbons; a narrow precursor to RFC 0020 §9's cooked meshes and
  materials, not milestone M1.
- Scene files take `mesh`, `vertices`, `vcolors` and `triangles` lines.
  `tools/gen_mesh_scene.py` writes the static-mesh goldens' world.
- Overlays (ABI 1.5): `StrataLineVertex`, `StrataPoint` and `StrataText`
  with `strata_submit_lines`, `strata_submit_points` and
  `strata_submit_text`. Unlit, alpha-blended lines and round points
  (fixed pixel size, true world size, or both; solid or a soft additive
  glow), depth-tested against the scene, and screen-space text in the
  public-domain font8x8 bitmap font, drawn in new `overlay` and `text`
  passes after the sky.
- `STRATA_LIGHTING_SPACE` (ABI 1.5): point lights over a flat ambient, with
  no sun, moon, fog or sky, for scenes lit by a light inside them.
- `GpuDevice`: line topology, alpha and additive blending, `drawArrays`, and
  `R8` textures with initial data.
- Scene files take `lighting space`, `line`, `circle`, `point` and `text`
  lines. New goldens `o1_space_system` and `o1_overlays_sun`.
- `modules/sim` (`strata::sim`, C++ only, `STRATA_WITH_SIM`, on by default;
  not in the C ABI and no GL): a seeded PCG32 `Rng`, a `Registry` with a fixed
  iteration order, `Field2D`/`Mask2D` with mass-conserving diffusion and
  decay, a `FixedStep` clock, a `StateHasher` fingerprint and colormaps.
- Optional OpenUSD modules behind `STRATA_WITH_USD` (off by default):
  `modules/usd` (the flat world and USD sync points) and `modules/save`
  (content-addressed, integrity-checked saves). CI fetches a prebuilt OpenUSD
  keyed by its pin.
- A simulation module, `modules/sim` (`strata::sim`, C++ only, on by default
  with `STRATA_WITH_SIM`): a seeded PCG32 `Rng`, a `Registry<Components...>`
  with a deterministic iteration order, `Field2D`/`Mask2D` scalar fields with
  mass-conserving diffusion and decay, a `FixedStep` clock, a `StateHasher`
  fingerprint and field colormaps for terrain-tile heatmaps. No GL, no new
  dependencies, no C ABI change. Tested by `strata-sim-tests`.

## 0.2.0

Lighting milestones L3–L4 and terrain milestone T1 from RFC 0020. Every ABI
addition is compatible: `STRATA_ABI_VERSION` stays 1 and the new
`STRATA_ABI_MINOR` is 3.

- Point lights (milestone L3, ABI 1.1): `StrataPointLight` and
  `strata_set_point_lights`, up to 32 unshadowed lights with a range-windowed
  falloff in sun lighting. `strata_abi_minor()` and `STRATA_ABI_MINOR`
  report compatible additions; `STRATA_ABI_VERSION` stays 1.
- Shadowed point lights (milestone L4, ABI 1.2):
  `STRATA_POINT_LIGHT_CAST_SHADOWS` gives up to four point lights cube shadow
  maps (one cube-map array, 512² faces, distance/range depth, nine-tap
  filtering), drawn in a new `point_shadow` pass.
- A terrain tile (milestone T1, ABI 1.3): `StrataTerrainTile` and
  `strata_set_terrain`, one static heightmapped tile with per-sample colour,
  drawn under both lighting modes and casting and receiving sun and
  point-light shadows.
- Scene files take `light point` lines, with an optional `shadow`, and
  `terrain`, `heights` and `colors` lines. `tools/gen_terrain_scene.py`
  writes the terrain goldens' world.

## 0.1.0

The skeleton milestone and lighting milestones L1–L2 from RFC 0020.

- A versioned C ABI (`strata.h`, ABI 1) with run-time struct layout queries,
  and a C++ API (`strata.hpp`).
- Surfaces: an SDL3 window, an EGL surfaceless headless surface, and the host's
  current GL context.
- `GpuDevice`, a thin interface with an OpenGL 4.5 core (DSA) backend.
- Beyond Nations' four primitives, instanced, with BN's exact legacy lighting.
- A sun and moon driven by time of day, four stabilised cascaded shadow maps,
  hemisphere ambient, fog and an analytic sky.
- `strata-golden` and `strata-imgcmp`, with ΔE-based golden-image comparison.
- A C# P/Invoke binding, ABI layout tests, and a C# harness that draws BN-shaped
  structs.
