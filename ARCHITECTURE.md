# Architecture

Strata follows RFC 0020 (*Shared C++ 3D Engine*). This document describes what
is built now. Where the RFC plans more, that is called out as **later**.

```text
  C++ game ──> strata.hpp (strata::Engine)         C# game ──> strata.h (C ABI, versioned)
                     │                                             │  (capi.cpp: a thin shim)
                     └──────────────────────┬──────────────────────┘
                                            v
  platform/            rhi/                 render/                     image/
  Surface: SDL3        GpuDevice: GL 4.5    Renderer + fixed pass list  PNG I/O, Delta E
  window, EGL          DSA backend          primitives, sun, cascades   comparison
  headless, host ctx
```

## Layers

| Directory | What it owns | Touches GL? |
|---|---|---|
| `include/strata/` | The public C ABI (`strata.h`) and C++ API (`strata.hpp`) | no |
| `src/capi.cpp` | The C shim: argument checks, exception to status, thread-local error text, the struct layout tables | no |
| `src/engine.cpp` | `strata::Engine`: creates the surface, loads GL with glad, owns the device and the renderer | only to load it |
| `src/platform/` | `Surface` and its three implementations: `SdlSurface`, `EglSurface`, `HostSurface` | context creation only |
| `src/rhi/` | `GpuDevice`, the interface the passes draw through, and its GL 4.5 DSA backend | **yes, only here** |
| `src/render/` | Primitive meshes, sun and sky model, cascade maths, pass list, `Renderer` | no: draws through `GpuDevice` |
| `src/image/` | PNG reading and writing (stb), golden-image comparison | no |
| `tools/` | `strata-golden`, `strata-imgcmp`, the scene-file parser | no |
| `modules/sim/` | Deterministic simulation building blocks: seeded RNG, entity-component registry, 2D fields with diffusion, fixed-step clock, state hash, colormaps. C++ only (`strata::sim`), not in the C ABI | no |

`render/` never calls GL directly. Everything goes through `GpuDevice`, so a
later backend would not mean rewriting the passes. Strata does **not** promise
to write one.

## The C ABI

- **Versioned.** `STRATA_ABI_VERSION` (the major version) is 1. A host passes
  the version it was built against in `StrataEngineDesc.abi_version`, and also
  passes `struct_size`. `strata_create` refuses a mismatch with
  `STRATA_ERROR_ABI_MISMATCH`. Compatible additions (a new function, a new
  struct with its own id, a new enum or flag value) bump `STRATA_ABI_MINOR`
  instead, so every ABI 1 host keeps working. A host that uses an addition
  checks `strata_abi_minor()` at run time; `strata.h` names the minor each
  addition arrived in (1.1: point lights; 1.2: point-light shadows; 1.3: a
  terrain tile; 1.4: static meshes).
- **Layouts are checkable.** `strata_struct_size(id)` and
  `strata_struct_field_offset(id, field)` report every boundary struct. The C++
  unit tests `static_assert` the layouts. The C# tests
  (`tests/csharp/Strata.Interop.Tests`) assert `Unsafe.SizeOf` and
  `Marshal.OffsetOf` against them. A field added on one side therefore fails a
  test; it cannot silently corrupt the other side.
- **`StrataRenderItem` is Beyond Nations' `RenderItem`.** It is 44 bytes: an
  `int32` kind, a `float[3]` position, a `float[3]` scale and a `float[4]`
  colour. The primitive kind values match BN's `PrimitiveKind`. A BN snapshot
  list crosses as a raw pointer, with zero copies on the C# side.
- **No exceptions cross the boundary.** Every entry point returns a
  `StrataStatus`. `strata_last_error()` gives thread-local text for the last
  failure.
- **Main thread only.** The engine never calls the host back, apart from the
  optional log sink.
- **Coarse calls.** A frame is `begin_frame`, one or more
  `submit_primitives`, `end_frame` and `present`. The cost of crossing is per
  call, not per item.

## Surfaces

| Surface | Context from | Draws into | Used by |
|---|---|---|---|
| `STRATA_SURFACE_WINDOW` | SDL3 (`SDL_GL_CreateContext`, 4.5 core) | the window's back buffer | games; CI's Xvfb check |
| `STRATA_SURFACE_HEADLESS` | EGL. Display: `EGL_MESA_platform_surfaceless`, else `EGL_EXT_platform_device`, else default; no surface | an offscreen RGBA8 + depth target | golden images, CI |
| `STRATA_SURFACE_HOST_CONTEXT` | whatever is current; GL loaded through the host's `get_proc_address` | the host's bound framebuffer | Beyond Nations phase B1 (inside Silk.NET's window) |

On platforms without EGL (Windows), a headless request falls back to a hidden
SDL3 window.

A host-context engine records the GL state it is about to change: program,
VAO, framebuffers, viewport, depth/cull/blend/scissor/offset/clamp enables,
masks, texture bindings on units 0–2 (2D, 2D array and cube-map array) and
uniform-buffer bindings on slots 0–2. It restores them
after each frame and readback, so the host's own renderers (ImGui, labels)
keep working.

## The frame

A **fixed, ordered pass list**, not a frame graph (`src/render/pass_list.cpp`).
Each pass declares what it reads and writes. `validatePassOrder` checks at
start-up that every read has an earlier writer. With `STRATA_LOG_DEBUG=1` set,
the list is logged:

```text
shadow:   instances, terrain, meshes, camera, lighting -> shadow_map
point_shadow: instances, terrain, meshes, point_lights -> point_shadow_maps
opaque:   instances, terrain, meshes, camera, lighting, point_lights, shadow_map, point_shadow_maps -> color, depth
sky:      camera, lighting, depth -> color
overlay:  lines, points, camera, depth -> color
text:     text -> color
readback: color -> pixels
```

**Later** passes slot into this list: depth pre-pass, transparent geometry,
post (tonemap) and UI.

**Instances.** `submit_primitives` partitions items by kind as they arrive. At
`end_frame` the four ranges go into one instance buffer, back to back. The
vertex shader reads `StrataRenderItem` fields in place (offsets 4, 16 and 28),
so the engine never builds model matrices. Each kind is then one
`glDrawElementsInstancedBaseVertexBaseInstance` from a shared vertex and index
buffer. A frame of primitives is at most four draws per pass. Items with an
out-of-range kind are skipped and counted in `items_rejected`.

**Primitives.** `src/render/primitive_mesh.cpp` is a line-for-line port of BN's
`PrimitiveMesh.cs`. It keeps BN's tessellation (sphere and capsule 16 × 24,
cylinder 24), its vertex order and winding, and Unity's dimensions (cylinder
and capsule 2 units tall).

## Lighting

### `STRATA_LIGHTING_LEGACY`

This is BN's `PrimitiveRenderer` model, term for term: `light = ambient + (1 −
ambient) · max(n · −dir, 0)`, with no shadows and no fog. The vertex maths is
also BN's (normal divided by the squared scale; `projection · view · world`).
It exists so the native renderer can be compared image-for-image with BN's own
renderer. The C# harness reproduces the C++ golden with zero differing pixels.

### `STRATA_LIGHTING_SUN` (L1–L2)

- **The game owns time.** The host passes `time_of_day` ∈ [0,1) and a latitude,
  and Strata only evaluates them (`src/render/sun.cpp`). World axes are +Y up,
  +X east and +Z south. The sun follows an equinox path: it rises due east at
  0.25, is highest at 0.5 and sets due west at 0.75.
- **Seasonal sun (ABI 1.6).** `strata_solar_position` turns a moment (Unix
  seconds, UTC) and a latitude and longitude into the sun's direction,
  elevation, azimuth, declination, hour angle and equation of time with NOAA's
  solar position equations (after Meeus; geometric, no refraction). It needs no
  engine and agrees with pyephem to about a minute of arc. The host passes the
  direction to `strata_set_sun_direction`, which replaces `time_of_day` and
  latitude for the sun, moon, sky, ambient and shadows until it is cleared with
  NULL. The engine keeps no calendar or clock of its own: the game still owns
  time, and a host whose map is turned rotates the vector about +Y itself.
- **Key light.** By day it is the sun: warm when low, white when high. At night
  it is a dim, bluish **moon** that casts **no shadows**. Shadow strength fades
  in from a sun elevation of about 2° to 7°, so the sun casts no long,
  flickering shadows at the horizon.
- **Cascaded shadow maps** (`src/render/cascades.cpp`):
  - 1–4 cascades in one `GL_TEXTURE_2D_ARRAY` of `DEPTH_COMPONENT32F`, with
    2048² per cascade by default.
  - Practical split scheme: λ = 0.6, from the near plane to `shadow_distance`.
  - Each cascade fits a **bounding sphere** of its frustum slice, so its size
    is constant as the camera turns. The radius is quantised to 1/16 unit.
  - The centre is snapped to whole shadow texels in a light space that rotates
    only with the light, so shadows do not shimmer as the camera moves.
  - Slope-scaled depth bias (`glPolygonOffset`), a normal offset of 1.5 texels,
    depth clamp in the shadow pass, and 3×3 hardware PCF
    (`sampler2DArrayShadow`).
  - Each cascade blends into the next across its last 10%. The last cascade
    fades out to unshadowed.
- **Ambient and sky.** Hemisphere ambient (sky colour above, ground colour
  below), distance fog to the horizon colour, and an analytic sky gradient with
  a sun disc and glow (`STRATA_CLEAR_SKY`). Sky, ambient, fog and light colour
  all come from the same time-of-day curve.
- **L1** is the same path with `cascade_count = 1`.

### `STRATA_LIGHTING_SPACE` (ABI 1.4)

The sun fragment shader with the key light switched off: no sun or moon, no
cascades, no fog and no sky, and a flat ambient term from `legacy_ambient`.
Only point lights light the scene. It is for scenes whose light source sits
in them, such as a star at the centre of a planetary system: a point light at
the star with a range far beyond the scene gives near-constant intensity and
the correct per-fragment direction to every body. Point-light shadows still
work.

### Point lights (L3)

- `strata_set_point_lights` replaces the scene's point lights with a copy of
  up to 32 `StrataPointLight`s (position, range, linear colour, intensity).
  They persist until the next call. Every light is validated before any is
  replaced.
- They shade `STRATA_LIGHTING_SUN` and `STRATA_LIGHTING_SPACE` scenes, in the
  opaque pass: Lambert diffuse, added to the key light and ambient, before
  fog. Legacy lighting is untouched, so it stays pixel-identical with Beyond
  Nations' renderer.
- Falloff, with x = distance / range: `intensity · w² / (1 + 16x²)`, where
  `w = clamp(1 − x⁴, 0, 1)` (`src/render/lights.cpp`, mirrored in the shader).
  The shape depends only on x, so a pool of light looks the same at any world
  scale; it reaches exactly zero at the range.
- The lights live in a 1 KiB uniform block (binding 2), so a frame with lights
  costs no extra draws.

### Shadowed point lights (L4)

- A light with `STRATA_POINT_LIGHT_CAST_SHADOWS` in its flags renders the
  scene into a cube shadow map every frame. At most four lights may carry the
  flag; `strata_set_point_lights` rejects a fifth.
- All cubes live in one `GL_TEXTURE_CUBE_MAP_ARRAY` of `DEPTH_COMPONENT32F`,
  512² per face. Face i of light slot s is layer `6s + i`. The faces use GL's
  cube conventions (`cubeFaceViewProjections` in `src/render/lights.cpp`): a
  90° perspective from the light out to its range.
- The `point_shadow` pass writes **distance / range** from the fragment shader
  rather than projected depth, so a lookup is one direction and one distance
  compare (`samplerCubeArrayShadow`). Polygon offset does not apply to written
  depth, so the bias is in the lookup instead. The world position is pushed
  along the normal by one texel, growing to four as the light grazes the
  surface, and the compared distance is pulled in by 1.5 texels.
- Nine taps around the lookup direction, about 1.5 texels apart, with hardware
  depth comparison, soften the edge.
- Shadow-casting lights cost six extra passes each, every frame, and each pass
  draws every primitive.

## Terrain (T1)

- `strata_set_terrain` takes one `StrataTerrainTile`: an origin, a cell size,
  a `samples_x` × `samples_z` grid of heights (2..1025 per side, x fastest)
  and either a linear RGB colour per sample or one colour for the tile. The
  engine validates it, builds a mesh (`src/render/terrain_mesh.cpp`) and
  copies it into GPU buffers. The host's arrays are not kept. NULL removes
  the tile. On an invalid tile the old one stays.
- Mesh: one vertex per sample (position, normal and colour, 36 bytes) and two
  counter-clockwise triangles per cell. Normals come from central differences,
  one-sided at the edges.
- The tile is part of the `terrain` frame input. It is drawn after the
  primitives in the opaque pass, with its own vertex shader feeding the same
  legacy or sun fragment shader. It is also drawn into every sun cascade and
  every point-light cube face, so it casts shadows and receives them, both
  from itself and from primitives.
- **Later (T2–T4):** several tiles, streaming, LOD and texture splatting.

## Static meshes (ABI 1.4)

- `strata_add_mesh` takes one `StrataMesh`: world-space positions, optional
  normals, optional linear RGB colour per vertex (else one colour for the
  mesh) and 32-bit triangle indices. The engine validates it, builds the
  terrain's vertex format (`src/render/static_mesh.cpp`) and copies it into
  GPU buffers; the host's arrays are not kept. It returns an id (never 0, not
  reused); `strata_remove_mesh(id)` and `strata_clear_meshes` free them. At
  most `STRATA_MAX_MESHES` (256) at once, each up to 2^24 vertices.
- Without host normals the engine sums each triangle's unnormalised face
  normal into its three vertices (area weighting) and normalises. Flat
  shading is had by not sharing vertices between faces.
- Meshes are the `meshes` frame input. They are drawn right after the terrain
  with the terrain's pipelines, one draw call each: in the opaque pass (back
  faces culled, so triangles wind counter-clockwise seen from outside), and
  into every sun cascade and point-light cube face with culling off, so open
  surfaces such as ribbons still cast.
- This is a narrow precursor to RFC 0020 §9 (cooked meshes, instancing,
  materials): no UVs, tangents, instancing, transforms or LOD.

## Overlays (ABI 1.5)

Unlit lines, points and screen text, for orbits, markers, labels and debug
drawing. They are frame inputs like primitives (submitted between
`begin_frame` and `end_frame`, copied, never retained) and are drawn by
`src/render/overlay.cpp` in two passes after the sky:

- **overlay** draws lines (`GL_LINES`, one pixel wide) and then points, alpha
  blended, depth-tested (`LEQUAL`) against the opaque scene without writing
  depth, with depth clamp on so nothing is cut by the far plane. A point is a
  camera-facing quad built in the vertex shader from `gl_VertexID` (six
  vertices per instance, no vertex buffer). Its diameter is
  `max(size_pixels, world_diameter · P₁₁ · height / 2w)`, so it can be a fixed
  marker, a true-size disc, or a true-size disc with a minimum size. Solid
  points get a one-pixel antialiased edge; `STRATA_POINT_SOFT` points are a
  quadratic glow blended additively after them.
- **text** draws every glyph as an instanced quad with no depth test. The font
  is font8x8 (public domain, `third_party/font8x8`): printable ASCII in an
  8-pixel cell, uploaded once as a 760 × 8 `R8` texture and read with
  `texelFetch`, so whole-number scales stay sharp. Layout is monospaced and
  happens on the CPU (`layoutText`): a UTF-8 sequence shows as one `?`, `\n`
  starts a new line, and `STRATA_TEXT_SHADOW` adds a black copy one font pixel
  down and right.

`GpuDevice` gained what this needed: a pipeline topology (triangles or lines),
blend modes (alpha, additive), non-indexed instanced draws, and `R8` textures
with initial data and nearest filtering. A host context gets its blend state
and texture unit 3 back after each frame.

## Optional modules

Engine modules that pull in heavy dependencies live under `modules/<name>/`,
are **off by default**, and link `strata_static`. `STRATA_WITH_USD` builds
`modules/usd` and `modules/save` when they exist. Jobs that leave it off never
fetch or build OpenUSD.

## Threading and errors

The API is main-thread only, and the GL context lives on that thread.
Internally, C++ code throws `strata::Error(status, message)`. The C shim
catches it and returns the status. The C++ API lets it propagate.
