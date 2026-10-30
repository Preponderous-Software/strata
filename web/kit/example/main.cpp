// The web kit's example scene: six pillars round a lamp on a small hill.
// It is the smallest host strata_kit.h has: the kit owns the context, the
// camera, the quality tiers and the frame; this file only fills the batches.
// CI builds it with build-kit.sh and runs testing/smoke.cjs against it.
#define STRATA_KIT_MAIN
#include "strata_kit.h"

#include <vector>

namespace {

constexpr int kPillars = 6;
constexpr float kRing = 5.0f;
int selected = -1;

const strata_kit::Rgb kColours[kPillars] = {{0.70f, 0.20f, 0.15f}, {0.80f, 0.55f, 0.10f}, {0.20f, 0.55f, 0.20f},
                                            {0.15f, 0.40f, 0.70f}, {0.45f, 0.25f, 0.65f}, {0.65f, 0.65f, 0.60f}};

float pillarHeight(int i) { return 1.5f + 0.5f * static_cast<float>(i % 3); }

}  // namespace

void strata_kit_configure(strata_kit::Options& o) { o.name = "kit-example"; }

// Terrain is uploaded once, and again after a lost context comes back.
void strata_kit_engine_ready(strata_kit::Host& h) {
    const int n = 97;
    const float cell = 0.75f, x0 = -(n - 1) * cell * 0.5f;
    std::vector<float> heights(n * n), colors(n * n * 3);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const float x = x0 + i * cell, z = x0 + j * cell, r2 = x * x + z * z;
            const size_t k = static_cast<size_t>(j) * n + i;
            heights[k] = 0.6f * std::exp(-r2 / 60.0f);
            colors[k * 3] = 0.12f;
            colors[k * 3 + 1] = 0.30f + 0.05f * std::sin(x * 1.3f) * std::cos(z * 1.1f);
            colors[k * 3 + 2] = 0.08f;
        }
    StrataTerrainTile tile{};
    tile.origin[0] = x0;
    tile.origin[1] = -0.6f;
    tile.origin[2] = x0;
    tile.cell_size = cell;
    tile.samples_x = n;
    tile.samples_z = n;
    tile.heights = heights.data();
    tile.colors = colors.data();
    tile.color[3] = 1.0f;
    strata_set_terrain(h.engine, &tile);
    h.terrainSamples = n * n;
}

void strata_kit_build(strata_kit::Host& h, double t) {
    const float spin = h.reducedMotion ? 0.0f : static_cast<float>(t) * 0.6f;
    for (int i = 0; i < kPillars; ++i) {
        const float a = i * 2.0f * 3.14159265f / kPillars;
        const float x = std::cos(a) * kRing, z = std::sin(a) * kRing, ph = pillarHeight(i);
        h.add(STRATA_PRIMITIVE_CYLINDER, x, ph * 0.5f, z, 0.6f, ph * 0.5f, 0.6f, kColours[i]);
        h.add(STRATA_PRIMITIVE_SPHERE, x, ph + 0.35f, z, 0.7f, 0.7f, 0.7f, kColours[i]);
        if (i == selected) {
            for (int k = 0; k < 24; ++k) {
                const float b0 = k * 6.2831853f / 24 + spin, b1 = (k + 0.6f) * 6.2831853f / 24 + spin;
                h.line(x + std::cos(b0) * 1.0f, 0.05f, z + std::sin(b0) * 1.0f, x + std::cos(b1) * 1.0f, 0.05f,
                       z + std::sin(b1) * 1.0f, {1.0f, 1.0f, 1.0f}, 0.95f);
            }
        }
    }
    h.add(STRATA_PRIMITIVE_CUBE, 0.0f, 1.0f, 0.0f, 0.15f, 2.0f, 0.15f, {0.1f, 0.1f, 0.11f});
    const float glow = 0.3f + 0.7f * h.night;
    h.add(STRATA_PRIMITIVE_SPHERE, 0.0f, 2.15f, 0.0f, 0.4f, 0.4f, 0.4f, {0.9f * glow, 0.65f * glow, 0.3f * glow});
    h.point(0.0f, 2.9f, 0.0f, 10.0f, 0.3f, {1.0f, 0.85f, 0.45f}, 1.0f);
#if STRATA_ABI_MINOR >= 14
    h.label(0.0f, 3.3f, 0.0f, "lamp", 12.0f, {1.0f, 0.9f, 0.7f});  // kit 1.2: drawn by the engine, in stills too
#endif
    h.light(0.0f, 2.2f, 0.0f, 9.0f, {1.0f, 0.75f, 0.45f}, 0.6f + 1.4f * h.night, 1);
}

// The page's one setter: which pillar is selected (-1 for none).
STRATA_KIT_EXPORT void ex_set_selected(int i) { selected = i; }
