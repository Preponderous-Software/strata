// Clustered light culling (ABI 1.21): the CPU half of clustered forward
// shading. The view frustum is cut into a fixed grid of clusters (screen tiles
// times depth slices); every light is listed in each cluster its sphere of
// influence may touch, and a lit fragment loops over its own cluster's list
// only, instead of over every light in the scene. Pure maths with no GPU, so
// the unit tests can check that no fragment ever misses a light that reaches
// it. The shader picks a fragment's cluster with the same formulas
// (shaders.hpp, clusterLights).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "strata/strata.h"

namespace strata::render {

// The grid. Tiles split the viewport evenly (x left to right, y bottom to
// top, as gl_FragCoord counts); slices split view depth from the near plane to
// the far plane, exponentially for a perspective camera (each slice the same
// ratio deeper than the one before) and evenly for an orthographic one.
constexpr int kClusterTilesX = 16;
constexpr int kClusterTilesY = 9;
constexpr int kClusterSlices = 24;
constexpr int kClusterCount = kClusterTilesX * kClusterTilesY * kClusterSlices;
// The most lights one cluster lists. A cluster that more lights reach keeps
// the ones that light its centre most (ClusterStats::clustersOverflowed).
constexpr int kMaxLightsPerCluster = STRATA_MAX_LIGHTS_PER_CLUSTER;

struct ClusterCamera {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    bool orthographic = false;
};

// One light as the culling sees it: a sphere, plus how bright it is, which
// only matters when a cluster overflows.
struct ClusterLight {
    glm::vec3 position{0.0f};
    float range = 1.0f;
    float brightness = 1.0f;  // intensity times its brightest colour channel
};

struct ClusterStats {
    int lightsInView = 0;      // lights listed in at least one cluster
    int clustersLit = 0;       // clusters listing at least one light
    int lightReferences = 0;   // list entries over all clusters
    int maxLightsPerCluster = 0;
    int clustersOverflowed = 0;
};

struct ClusterLists {
    // Per cluster (index from clusterIndex): where its list starts in
    // `indices` and how many lights it holds.
    std::vector<uint32_t> offsets;
    std::vector<uint32_t> counts;
    std::vector<uint32_t> indices;  // light indices, each cluster's in ascending order
    ClusterStats stats;
};

// Slice of a view depth (distance in front of the eye), clamped to the grid.
// The shader computes exactly this.
int clusterSlice(float viewDepth, const ClusterCamera& camera);
// The factor the slice formula multiplies by: slices / ln(far / near) for a
// perspective camera, slices / (far - near) for an orthographic one.
float clusterSliceScale(const ClusterCamera& camera);
// Index of the cluster for tile (x, y) and slice z.
inline int clusterIndex(int x, int y, int z) { return (z * kClusterTilesY + y) * kClusterTilesX + x; }

// Lists every light in each cluster its sphere may touch (conservatively: a
// light can be listed in a cluster it does not quite reach, never left out of
// one it does), keeping at most kMaxLightsPerCluster per cluster.
ClusterLists assignLightsToClusters(const ClusterCamera& camera, const std::vector<ClusterLight>& lights);

}  // namespace strata::render
