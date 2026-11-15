// strata/sim/noise.hpp -- seeded 2D value noise and fractal Brownian motion.
//
// valueNoise() is lattice value noise: an integer hash gives every lattice
// point a value in [-1, 1), and a point between them is the bilinear blend
// of its four corners under smoothstep weights (3t^2 - 2t^3). fbm() sums
// `octaves` layers of it; each layer's frequency is the last one's times
// `lacunarity` and its amplitude the last one's times `gain`. There is no
// global state: the result depends only on the arguments, and every
// operation is + - * / or floor on doubles, so the same seed gives the same
// numbers on every compiler (the module is built with -ffp-contract=off).
//
// Choosing octaves and gain
// -------------------------
// The edge of a region cut out of noise (land where fbm > threshold, a
// coastline, a patch of grime) is only as rough as the noise has octaves.
// With 1-4 octaves the edges are smooth, round blobs whatever the gain. With
// eight or more, gain sets the roughness. Natural coastlines have a
// box-counting fractal dimension D of about 1.1-1.3 (Britain's west coast is
// about 1.25; Richardson, Mandelbrot 1967); a smooth curve has D = 1.
//
// Measured with this implementation (modules/sim/tools/fbm_dimension.cpp,
// built as strata-sim-fbm-dimension): the zero level set of a disc plus
// 0.6 * fbm, lacunarity 2, on a 2048 x 2048 grid, box sizes 2-64 pixels,
// mean (and range) over seeds 1-8:
//
//     octaves  gain   D (measured)
//        2     0.50   0.98  (0.97-0.99)
//        4     0.50   0.98  (0.97-0.99)
//        4     0.70   0.99  (0.97-1.00)
//        9     0.50   1.01  (1.00-1.03)
//        9     0.60   1.12  (1.08-1.13)
//        9     0.65   1.19  (1.16-1.21)
//        9     0.70   1.25  (1.22-1.30)
//        9     0.75   1.31  (1.27-1.35)
//
// (A smooth closed curve reads slightly under 1 at these box sizes.)
// So for coastline-like edges use octaves >= 8 with gain 0.6-0.7.
//
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#pragma once

#include <cstdint>

namespace strata::sim {

// The 32-bit integer hash behind the noise: a lattice point and a seed to
// a well-mixed word. Exposed for callers that want per-cell randomness on
// the same lattice.
uint32_t latticeHash(int32_t x, int32_t y, uint32_t seed);

// One octave of value noise in [-1, 1]. The lattice spacing is 1, so
// features are about one unit across; scale the coordinates to change it.
// Lattice cells wrap every 2^32 units; a NaN or infinite coordinate
// reads as 0.
float valueNoise(float x, float y, uint32_t seed);

struct FbmParams {
    int octaves = 8;           // number of layers; clamped to [1, 30]
    float gain = 0.5f;         // amplitude ratio between layers (roughness)
    float lacunarity = 2.0f;   // frequency ratio between layers
    float frequency = 1.0f;    // frequency of the first layer
    uint32_t seed = 0u;
};

// Fractal Brownian motion: the amplitude-weighted sum of `octaves` layers of
// valueNoise, divided by the sum of the amplitudes, so the result stays in
// [-1, 1]. Each layer has its own seed and lattice offset (both derived from
// `seed`), so layers do not line up at the origin.
float fbm(float x, float y, const FbmParams& params);

}  // namespace strata::sim
