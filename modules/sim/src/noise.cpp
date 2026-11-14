// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#include "strata/sim/noise.hpp"

#include <cmath>

namespace strata::sim {

namespace {

// "lowbias32" (Chris Wellons' hash prospector): a cheap, well-mixed 32-bit
// finaliser.
uint32_t mix32(uint32_t h) {
    h ^= h >> 16u;
    h *= 0x7feb352du;
    h ^= h >> 15u;
    h *= 0x846ca68bu;
    h ^= h >> 16u;
    return h;
}

// A hash word to [-1, 1): the top 24 bits, so every value is exact.
double toSigned(uint32_t h) { return static_cast<double>(h >> 8u) * (2.0 / 16777216.0) - 1.0; }

double smooth(double t) { return t * t * (3.0 - 2.0 * t); }

uint32_t hashU(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = mix32(seed ^ 0x9e3779b9u);
    h = mix32(h ^ (x * 0x85ebca6bu));
    h = mix32(h ^ (y * 0xc2b2ae35u));
    return h;
}

// The lattice cell of a coordinate, wrapped to 32 bits (two's complement),
// so the hash is defined for every finite input.
uint32_t cell(double f) {
    if (f < -4.0e18 || f > 4.0e18) return f > 0.0 ? 0x7fffffffu : 0x80000000u;
    return static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(f)));
}

double noise(double x, double y, uint32_t seed) {
    if (!std::isfinite(x)) x = 0.0;
    if (!std::isfinite(y)) y = 0.0;
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const uint32_t ix = cell(fx);
    const uint32_t iy = cell(fy);
    const double tx = smooth(x - fx);
    const double ty = smooth(y - fy);
    const double v00 = toSigned(hashU(ix, iy, seed));
    const double v10 = toSigned(hashU(ix + 1u, iy, seed));
    const double v01 = toSigned(hashU(ix, iy + 1u, seed));
    const double v11 = toSigned(hashU(ix + 1u, iy + 1u, seed));
    const double a = v00 + (v10 - v00) * tx;
    const double b = v01 + (v11 - v01) * tx;
    return a + (b - a) * ty;
}

}  // namespace

uint32_t latticeHash(int32_t x, int32_t y, uint32_t seed) {
    return hashU(static_cast<uint32_t>(x), static_cast<uint32_t>(y), seed);
}

float valueNoise(float x, float y, uint32_t seed) { return static_cast<float>(noise(x, y, seed)); }

float fbm(float x, float y, const FbmParams& params) {
    const int octaves = params.octaves < 1 ? 1 : (params.octaves > 30 ? 30 : params.octaves);
    double freq = params.frequency;
    double amp = 1.0;
    double sum = 0.0;
    double norm = 0.0;
    for (int o = 0; o < octaves; ++o) {
        const uint32_t layer = hashU(static_cast<uint32_t>(o), 0x5eedu, params.seed);
        // A per-layer offset in [0, 256) on each axis, so the layers' lattices
        // do not all meet at the origin.
        const double ox = static_cast<double>(layer >> 24u) + static_cast<double>((layer >> 8u) & 0xffffu) / 65536.0;
        const uint32_t layer2 = mix32(layer);
        const double oy = static_cast<double>(layer2 >> 24u) + static_cast<double>((layer2 >> 8u) & 0xffffu) / 65536.0;
        sum += amp * noise(static_cast<double>(x) * freq + ox, static_cast<double>(y) * freq + oy, layer);
        norm += amp;
        freq *= params.lacunarity;
        amp *= params.gain;
    }
    return norm > 0.0 ? static_cast<float>(sum / norm) : 0.0f;
}

}  // namespace strata::sim
