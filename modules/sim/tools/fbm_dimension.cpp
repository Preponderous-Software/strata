// strata-sim-fbm-dimension: prints the gain -> fractal dimension table in
// strata/sim/noise.hpp. Usage: strata-sim-fbm-dimension [grid] [seeds]
// (defaults 2048 and 8).
//
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#include <cstdio>
#include <cstdlib>

#include "fbm_dimension.hpp"

int main(int argc, char** argv) {
    const int n = argc > 1 ? std::atoi(argv[1]) : 2048;
    const int seeds = argc > 2 ? std::atoi(argv[2]) : 8;
    struct Row { int octaves; float gain; };
    const Row rows[] = {{2, 0.5f}, {4, 0.5f}, {4, 0.7f}, {9, 0.5f}, {9, 0.6f}, {9, 0.65f}, {9, 0.7f}, {9, 0.75f}};
    std::printf("grid %d, %d seeds, lacunarity 2, box sizes 2-64\n", n, seeds);
    std::printf("octaves  gain   D (mean)  min    max\n");
    for (const Row& r : rows) {
        double sum = 0, lo = 9, hi = 0;
        for (int s = 0; s < seeds; ++s) {
            const double d = strata::sim::fbmdim::edgeDimension(n, r.octaves, r.gain, static_cast<uint32_t>(s + 1));
            sum += d;
            if (d < lo) lo = d;
            if (d > hi) hi = d;
        }
        std::printf("   %d     %.2f   %.3f     %.3f  %.3f\n", r.octaves, r.gain, sum / seeds, lo, hi);
    }
    return 0;
}
