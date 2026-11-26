// Clustered light culling (ABI 1.21, render/clusters.cpp): the lists must be
// conservative (a fragment's cluster lists every light that reaches it), cull
// lights out of view, and cap a crowded cluster at its brightest lights.
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "render/clusters.hpp"
#include "test.hpp"

using namespace strata::render;

namespace {

ClusterCamera perspectiveCamera() {
    ClusterCamera c;
    c.view = glm::lookAt(glm::vec3(0, 12, 30), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    c.projection = glm::perspective(glm::radians(55.0f), 16.0f / 9.0f, 0.3f, 200.0f);
    c.nearPlane = 0.3f;
    c.farPlane = 200.0f;
    return c;
}

ClusterCamera orthographicCamera() {
    ClusterCamera c;
    c.view = glm::lookAt(glm::vec3(10, 40, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    c.projection = glm::ortho(-32.0f, 32.0f, -18.0f, 18.0f, 1.0f, 120.0f);
    c.nearPlane = 1.0f;
    c.farPlane = 120.0f;
    c.orthographic = true;
    return c;
}

// The cluster a fragment at world point p falls in, as the shader picks it
// from gl_FragCoord and the view depth. Returns -1 off screen.
int fragmentCluster(const ClusterCamera& camera, const glm::vec3& p) {
    const glm::vec4 view = camera.view * glm::vec4(p, 1.0f);
    const float depth = -view.z;
    if (depth < camera.nearPlane || depth > camera.farPlane) return -1;
    const glm::vec4 clip = camera.projection * view;
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    if (std::fabs(ndc.x) >= 1.0f || std::fabs(ndc.y) >= 1.0f) return -1;
    const int x = std::min(static_cast<int>(std::floor((ndc.x * 0.5f + 0.5f) * kClusterTilesX)), kClusterTilesX - 1);
    const int y = std::min(static_cast<int>(std::floor((ndc.y * 0.5f + 0.5f) * kClusterTilesY)), kClusterTilesY - 1);
    return clusterIndex(x, y, clusterSlice(depth, camera));
}

bool listed(const ClusterLists& lists, int cluster, uint32_t light) {
    const auto k = static_cast<size_t>(cluster);
    const auto begin = lists.indices.begin() + lists.offsets[k];
    return std::binary_search(begin, begin + lists.counts[k], light);
}

// Scatters lights over a street-sized area and checks many points on and
// above the ground: every light that reaches a point is in its cluster.
int missedLights(const ClusterCamera& camera, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> spread(-40.0f, 40.0f), height(0.0f, 6.0f), range(0.5f, 9.0f);
    std::vector<ClusterLight> lights(400);
    for (auto& l : lights) {
        l.position = glm::vec3(spread(rng), height(rng), spread(rng));
        l.range = range(rng);
    }
    const ClusterLists lists = assignLightsToClusters(camera, lights);
    int missed = 0;
    for (int n = 0; n < 20000; ++n) {
        const glm::vec3 p(spread(rng), height(rng) * 0.5f, spread(rng));
        const int cluster = fragmentCluster(camera, p);
        if (cluster < 0) continue;
        for (size_t i = 0; i < lights.size(); ++i) {
            if (glm::length(p - lights[i].position) < lights[i].range && !listed(lists, cluster, static_cast<uint32_t>(i))) {
                ++missed;
            }
        }
    }
    return missed;
}

}  // namespace

TEST(cluster_slices_run_from_the_near_plane_to_the_far_plane) {
    const ClusterCamera c = perspectiveCamera();
    CHECK(clusterSlice(0.0f, c) == 0);
    CHECK(clusterSlice(c.nearPlane, c) == 0);
    CHECK(clusterSlice(c.farPlane * 2.0f, c) == kClusterSlices - 1);
    int previous = 0;
    for (float d = c.nearPlane; d < c.farPlane; d *= 1.01f) {
        const int s = clusterSlice(d, c);
        CHECK(s >= previous);
        previous = s;
    }
    CHECK(previous == kClusterSlices - 1);
    // Exponential: the slice boundary halfway through the grid sits at the
    // geometric mean of near and far.
    const float middle = std::sqrt(c.nearPlane * c.farPlane);
    CHECK(clusterSlice(middle * 0.99f, c) == kClusterSlices / 2 - 1);
    CHECK(clusterSlice(middle * 1.01f, c) == kClusterSlices / 2);
    // Orthographic: even slices.
    const ClusterCamera o = orthographicCamera();
    CHECK(clusterSlice(o.nearPlane + (o.farPlane - o.nearPlane) * 0.51f, o) == kClusterSlices / 2);
}

TEST(clusters_list_every_light_that_reaches_a_fragment_in_perspective) {
    CHECK(missedLights(perspectiveCamera(), 7) == 0);
    CHECK(missedLights(perspectiveCamera(), 1234) == 0);
}

TEST(clusters_list_every_light_that_reaches_a_fragment_orthographically) {
    CHECK(missedLights(orthographicCamera(), 42) == 0);
}

TEST(clusters_skip_lights_out_of_view_and_keep_lists_sorted) {
    const ClusterCamera c = perspectiveCamera();
    std::vector<ClusterLight> lights(4);
    lights[0].position = glm::vec3(0, 0, 0);  // in front
    lights[0].range = 2.0f;
    lights[1].position = glm::vec3(0, 12, 60);  // behind the eye
    lights[1].range = 5.0f;
    lights[2].position = glm::vec3(0, 0, -400);  // beyond the far plane
    lights[2].range = 10.0f;
    lights[3].position = glm::vec3(300, 0, 0);  // far off to the side
    lights[3].range = 10.0f;
    const ClusterLists lists = assignLightsToClusters(c, lights);
    CHECK(lists.stats.lightsInView == 1);
    CHECK(lists.stats.clustersLit > 0);
    CHECK(lists.stats.lightReferences == static_cast<int>(lists.indices.size()));
    for (uint32_t index : lists.indices) CHECK(index == 0u);
    // A small light lights a few clusters, not a whole column of them.
    CHECK(lists.stats.clustersLit < 40);
    // Each list ascends, so the shader sums lights in the order they were set.
    std::vector<ClusterLight> many(50);
    for (size_t i = 0; i < many.size(); ++i) {
        many[i].position = glm::vec3(static_cast<float>(i % 10) - 5.0f, 1.0f, static_cast<float>(i / 10) - 2.0f);
        many[i].range = 4.0f;
    }
    const ClusterLists dense = assignLightsToClusters(c, many);
    for (int k = 0; k < kClusterCount; ++k) {
        const auto begin = dense.indices.begin() + dense.offsets[static_cast<size_t>(k)];
        CHECK(std::is_sorted(begin, begin + dense.counts[static_cast<size_t>(k)]));
    }
    CHECK(dense.stats.clustersOverflowed == 0);
}

TEST(a_crowded_cluster_keeps_its_brightest_lights) {
    const ClusterCamera c = perspectiveCamera();
    std::vector<ClusterLight> lights;
    // 100 dim lights, then 256 bright ones, all on top of each other.
    for (int i = 0; i < 100 + kMaxLightsPerCluster; ++i) {
        ClusterLight l;
        l.position = glm::vec3(0, 1, 0);
        l.range = 3.0f;
        l.brightness = i < 100 ? 0.1f : 5.0f;
        lights.push_back(l);
    }
    const ClusterLists lists = assignLightsToClusters(c, lights);
    CHECK(lists.stats.clustersOverflowed > 0);
    CHECK(lists.stats.maxLightsPerCluster == 100 + kMaxLightsPerCluster);
    for (int k = 0; k < kClusterCount; ++k) {
        const auto ku = static_cast<size_t>(k);
        CHECK(lists.counts[ku] <= static_cast<uint32_t>(kMaxLightsPerCluster));
        if (lists.counts[ku] == static_cast<uint32_t>(kMaxLightsPerCluster)) {
            // Only bright lights survive the cut (indices 100 and up).
            CHECK(lists.indices[lists.offsets[ku]] >= 100u);
        }
    }
    CHECK(assignLightsToClusters(c, {}).indices.empty());
}
