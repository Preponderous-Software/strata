// strata/sim/colormap.hpp -- turning field values into colours for drawing.
//
// A field drawn as a heatmap usually goes onto a terrain tile's per-sample
// colours (StrataTerrainTile.colors). These helpers map a value to linear RGB
// with piecewise-linear lookup tables, so they need no pow or exp and give the
// same bytes everywhere.
//
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "strata/sim/field.hpp"

namespace strata::sim {

enum class Colormap {
    Viridis,  // dark blue -> green -> yellow; perceptually ordered
    Magma,    // black -> purple -> orange -> pale yellow
    Ice,      // black -> deep blue -> cyan -> white
    Grey      // black -> white
};

namespace detail {
struct Stop {
    float r, g, b;
};
// Eight evenly spaced stops per map, in linear RGB. Viridis and Magma are
// matplotlib's sRGB values squared (a cheap approximation of the sRGB transfer
// curve); Ice is hand-picked; Grey is (i / 7)^2.
inline const Stop* colormapStops(Colormap map) {
    static const Stop viridis[8] = {{0.071f, 0.000f, 0.109f}, {0.076f, 0.038f, 0.246f}, {0.045f, 0.129f, 0.304f},
                                    {0.024f, 0.247f, 0.311f}, {0.015f, 0.401f, 0.281f}, {0.083f, 0.575f, 0.184f},
                                    {0.393f, 0.730f, 0.050f}, {0.987f, 0.821f, 0.021f}};
    static const Stop magma[8] = {{0.000f, 0.000f, 0.000f}, {0.018f, 0.005f, 0.099f}, {0.138f, 0.009f, 0.249f},
                                  {0.353f, 0.031f, 0.251f}, {0.687f, 0.069f, 0.185f}, {0.947f, 0.213f, 0.131f},
                                  {0.995f, 0.538f, 0.255f}, {0.974f, 0.983f, 0.562f}};
    static const Stop ice[8] = {{0.000f, 0.000f, 0.000f}, {0.004f, 0.010f, 0.060f}, {0.010f, 0.040f, 0.180f},
                                {0.020f, 0.120f, 0.380f}, {0.040f, 0.280f, 0.560f}, {0.120f, 0.500f, 0.720f},
                                {0.400f, 0.760f, 0.880f}, {1.000f, 1.000f, 1.000f}};
    static const Stop grey[8] = {{0.000f, 0.000f, 0.000f}, {0.020f, 0.020f, 0.020f}, {0.082f, 0.082f, 0.082f},
                                 {0.184f, 0.184f, 0.184f}, {0.327f, 0.327f, 0.327f}, {0.510f, 0.510f, 0.510f},
                                 {0.735f, 0.735f, 0.735f}, {1.000f, 1.000f, 1.000f}};
    switch (map) {
        case Colormap::Magma: return magma;
        case Colormap::Ice: return ice;
        case Colormap::Grey: return grey;
        case Colormap::Viridis:
        default: return viridis;
    }
}
}  // namespace detail

// Linear RGB for t in [0, 1] (clamped).
inline void colormapRgb(Colormap map, float t, float rgb[3]) {
    t = std::min(std::max(t, 0.0f), 1.0f);
    const detail::Stop* stops = detail::colormapStops(map);
    const float scaled = t * 7.0f;
    int i = static_cast<int>(scaled);
    if (i > 6) i = 6;
    const float f = scaled - static_cast<float>(i);
    rgb[0] = stops[i].r + (stops[i + 1].r - stops[i].r) * f;
    rgb[1] = stops[i].g + (stops[i + 1].g - stops[i].g) * f;
    rgb[2] = stops[i].b + (stops[i + 1].b - stops[i].b) * f;
}

// Writes width * height RGB triples (x fastest, row by row) for a field,
// mapping [lo, hi] onto the colormap. Blocked cells get `blockedRgb` when a
// mask is given. The layout matches StrataTerrainTile.colors for a tile whose
// samples are the field's cells.
inline void fieldToColors(const Field2D& field, Colormap map, float lo, float hi, std::vector<float>& outRgb,
                          const Mask2D* mask = nullptr, const float blockedRgb[3] = nullptr) {
    const size_t n = field.values().size();
    outRgb.resize(n * 3);
    const float span = hi > lo ? hi - lo : 1.0f;
    const auto& v = field.values();
    for (size_t i = 0; i < n; ++i) {
        float* rgb = &outRgb[i * 3];
        if (mask != nullptr && blockedRgb != nullptr && mask->cells()[i] != 0) {
            rgb[0] = blockedRgb[0];
            rgb[1] = blockedRgb[1];
            rgb[2] = blockedRgb[2];
            continue;
        }
        colormapRgb(map, (v[i] - lo) / span, rgb);
    }
}

}  // namespace strata::sim
