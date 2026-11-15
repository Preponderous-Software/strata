# modules/sim

Deterministic, CPU-only building blocks for grid simulations. The module has
no dependencies and touches no GL, so it is built by default
(`-DSTRATA_WITH_SIM=OFF` turns it off). It is C++ only: nothing here is part
of the C ABI. Link `strata::sim`.

| Header | What it gives you |
|---|---|
| `strata/sim/rng.hpp` | `Rng`: PCG32 with bias-free bounded integers, exact floats in [0, 1) and unit vectors without `sin`/`cos`. The same seed gives the same numbers on every compiler. |
| `strata/sim/registry.hpp` | `Registry<Components...>`: entities with generation-checked handles and dense component arrays. `each<Q...>()` visits in ascending slot order; entities created during a pass are not visited by it, and freed slots are recycled lowest-first only outside a pass. |
| `strata/sim/field.hpp` | `Field2D` (scalar concentrations on a grid) and `Mask2D` (blocked cells). Flux-form explicit diffusion with closed walls and edges (mass conserved to float rounding), decay, take/add, bilinear sampling and a wall-aware gradient. |
| `strata/sim/fixed_step.hpp` | `FixedStep`: the host feeds it real elapsed time and runs the whole ticks it returns. The simulation itself never reads a clock. |
| `strata/sim/hash.hpp` | `StateHasher`: FNV-1a 64 over exact bit patterns, for "same seed, same state" checks. |
| `strata/sim/noise.hpp` | `valueNoise` and `fbm` (octaves, gain, lacunarity, frequency, seed): smoothstep-blended lattice value noise from an integer hash, with no global state, and the octaves summed into fractal Brownian motion in [-1, 1]. The header has a measured table of how gain sets the roughness (box-counting dimension) of a level set's edge. |
| `strata/sim/colormap.hpp` | Viridis, Magma, Ice and Grey lookup tables, and `fieldToColors`, which fills `StrataTerrainTile.colors` for a field drawn as a heatmap. |

## Determinism

A simulation built from these pieces gives bit-identical state for the same
seed and tick count when it:

- draws every random number from an `Rng` it seeds itself (never `rand()`,
  `std::random_device` or a `std::*_distribution`);
- advances in fixed ticks and never reads wall-clock time inside a tick;
- iterates entities with `Registry::each` and cells in row order;
- is compiled without fused multiply-adds (`strata_sim` itself is built with
  `-ffp-contract=off`; consumers should do the same for their own
  simulation code) and uses only `+ - * /` and `sqrt` on floats, whose results
  IEEE 754 fixes.

## Tests

`strata-sim-tests` (run by `ctest`) checks the PCG32 reference sequence,
registry ordering under mutation, mass conservation with walls, gradient
direction, decay, the fixed-step clock, colormap layout, pinned noise
values and that more fbm gain gives a rougher edge.
`strata-sim-fbm-dimension` (built with the tests, not run by `ctest`)
prints the gain-to-dimension table in `noise.hpp`.

The module was added for Microbiome (`Preponderous-Software/microbiome`), whose
opt-in Strata build simulates diffusing nutrient, waste and oxygen fields with
it and draws them as a terrain-tile heatmap.

## Rough edges need octaves

Terrain, coastlines and patches cut from one or two octaves of noise have
smooth, round edges. Measured on this module's `fbm` (box counting on the
zero level set of a disc plus fbm; full table in `noise.hpp`), the edge's
fractal dimension stays at about 1.0 for 1-4 octaves whatever the gain. With
9 octaves it is 1.01 at gain 0.5, 1.12 at 0.6, 1.19 at 0.65, 1.25 at 0.7
and 1.31 at 0.75. Natural coastlines measure about 1.1-1.3 (Britain's west
coast about 1.25), so for natural-looking edges use 8 or more octaves with
gain 0.6-0.7.
