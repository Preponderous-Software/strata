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
  addition arrived in (1.1: point lights; 1.2: point-light shadows).
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
shadow:   instances, camera, lighting -> shadow_map
point_shadow: instances, point_lights -> point_shadow_maps
opaque:   instances, camera, lighting, point_lights, shadow_map, point_shadow_maps -> color, depth
sky:      camera, lighting, depth -> color
readback: color -> pixels
```

**Later** passes slot into this list: depth pre-pass, transparent and labels,
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

### Point lights (L3)

- `strata_set_point_lights` replaces the scene's point lights with a copy of
  up to 32 `StrataPointLight`s (position, range, linear colour, intensity).
  They persist until the next call. Every light is validated before any is
  replaced.
- They shade `STRATA_LIGHTING_SUN` scenes only, in the opaque pass: Lambert
  diffuse, added to the key light and ambient, before fog. Legacy lighting is
  untouched, so it stays pixel-identical with Beyond Nations' renderer.
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

## Optional modules

Engine modules that pull in heavy dependencies live under `modules/<name>/`,
are **off by default**, and link `strata_static`. `STRATA_WITH_USD` builds
`modules/usd` and `modules/save` when they exist. Jobs that leave it off never
fetch or build OpenUSD.

## Threading and errors

The API is main-thread only, and the GL context lives on that thread.
Internally, C++ code throws `strata::Error(status, message)`. The C shim
catches it and returns the status. The C++ API lets it propagate.
