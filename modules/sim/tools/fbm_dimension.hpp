// Box-counting fractal dimension of an fbm-perturbed disc's edge. Shared by
// the unit tests and tools/fbm_dimension.cpp, which measured the table in
// strata/sim/noise.hpp.
//
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "strata/sim/noise.hpp"

namespace strata::sim::fbmdim {

// f(p) = (R - |p - c|) / R + amplitude * fbm(p) on an n x n grid over the
// unit square (disc of radius R = 0.3 at the centre, first fbm layer at 4
// cycles per unit). Cells where f changes sign against the right or lower
// neighbour form the edge; the dimension is the least-squares slope of
// log(boxes touching the edge) against log(1 / box size), over box sizes
// minBox, 2 minBox, ... up to maxBox pixels.
inline double edgeDimension(int n, int octaves, float gain, uint32_t seed, float amplitude = 0.6f, int minBox = 2,
                            int maxBox = 64) {
    FbmParams params;
    params.octaves = octaves;
    params.gain = gain;
    params.lacunarity = 2.0f;
    params.frequency = 4.0f;
    params.seed = seed;
    const double radius = 0.3;
    std::vector<float> f(static_cast<size_t>(n) * static_cast<size_t>(n));
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const float x = (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
            const float y = (static_cast<float>(j) + 0.5f) / static_cast<float>(n);
            const double dx = x - 0.5, dy = y - 0.5;
            const double disc = (radius - std::sqrt(dx * dx + dy * dy)) / radius;
            f[static_cast<size_t>(j) * n + i] = static_cast<float>(disc + amplitude * fbm(x, y, params));
        }
    }
    std::vector<uint8_t> edge(f.size(), 0);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const bool in = f[static_cast<size_t>(j) * n + i] > 0.0f;
            const bool right = i + 1 < n && (f[static_cast<size_t>(j) * n + i + 1] > 0.0f) != in;
            const bool down = j + 1 < n && (f[static_cast<size_t>(j + 1) * n + i] > 0.0f) != in;
            if (right || down) edge[static_cast<size_t>(j) * n + i] = 1;
        }
    }
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int samples = 0;
    for (int s = minBox; s <= maxBox; s *= 2) {
        const int m = (n + s - 1) / s;
        std::vector<uint8_t> hit(static_cast<size_t>(m) * static_cast<size_t>(m), 0);
        long count = 0;
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                if (!edge[static_cast<size_t>(j) * n + i]) continue;
                uint8_t& h = hit[static_cast<size_t>(j / s) * m + i / s];
                if (!h) { h = 1; ++count; }
            }
        }
        if (count == 0) return 0.0;
        const double lx = std::log(1.0 / s), ly = std::log(static_cast<double>(count));
        sx += lx; sy += ly; sxx += lx * lx; sxy += lx * ly;
        ++samples;
    }
    return (samples * sxy - sx * sy) / (samples * sxx - sx * sx);
}

}  // namespace strata::sim::fbmdim
