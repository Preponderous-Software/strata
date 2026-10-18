# Changelog

## Unreleased

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
