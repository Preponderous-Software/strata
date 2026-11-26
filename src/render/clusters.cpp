#include "render/clusters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "render/lights.hpp"

namespace strata::render {

namespace {

// A small widening of every bound, in tiles, slices and world units, so the
// rounding of the CPU and the shader cannot put a fragment in a cluster its
// light was not listed in.
constexpr float kTileSlack = 1e-3f;
constexpr float kDepthSlack = 1e-4f;

struct Box {
    glm::vec3 min{0.0f}, max{0.0f};
};

// The view-space point on the ray through NDC (x, y) at view depth d.
struct Ray {
    glm::vec3 near{0.0f}, far{0.0f};
    glm::vec3 at(float depth) const {
        const float dz = far.z - near.z;
        const float t = std::fabs(dz) > 1e-12f ? (-depth - near.z) / dz : 0.0f;
        return near + t * (far - near);
    }
};

Ray rayThrough(const glm::mat4& inverseProjection, float x, float y) {
    glm::vec4 n = inverseProjection * glm::vec4(x, y, -1.0f, 1.0f);
    glm::vec4 f = inverseProjection * glm::vec4(x, y, 1.0f, 1.0f);
    Ray r;
    r.near = glm::vec3(n) / n.w;
    r.far = glm::vec3(f) / f.w;
    return r;
}

float sliceDepth(int slice, const ClusterCamera& camera) {
    const float t = static_cast<float>(slice) / static_cast<float>(kClusterSlices);
    if (camera.orthographic) return camera.nearPlane + t * (camera.farPlane - camera.nearPlane);
    return camera.nearPlane * std::pow(camera.farPlane / camera.nearPlane, t);
}

// View-space bounding boxes of every cluster, widened a little.
std::vector<Box> clusterBoxes(const ClusterCamera& camera) {
    const glm::mat4 inverse = glm::inverse(camera.projection);
    std::array<Ray, (kClusterTilesX + 1) * (kClusterTilesY + 1)> rays{};
    for (int y = 0; y <= kClusterTilesY; ++y) {
        for (int x = 0; x <= kClusterTilesX; ++x) {
            rays[static_cast<size_t>(y * (kClusterTilesX + 1) + x)] =
                rayThrough(inverse, -1.0f + 2.0f * static_cast<float>(x) / kClusterTilesX,
                           -1.0f + 2.0f * static_cast<float>(y) / kClusterTilesY);
        }
    }
    std::array<float, kClusterSlices + 1> depths{};
    for (int z = 0; z <= kClusterSlices; ++z) depths[static_cast<size_t>(z)] = sliceDepth(z, camera);
    // The last slice also holds everything beyond the far plane the slicing
    // assumed (clusterSlice clamps), and fragments there are clipped anyway.
    std::vector<Box> boxes(static_cast<size_t>(kClusterCount));
    for (int z = 0; z < kClusterSlices; ++z) {
        const float d0 = depths[static_cast<size_t>(z)], d1 = depths[static_cast<size_t>(z) + 1];
        const float slack = kDepthSlack * d1 + 1e-5f;
        for (int y = 0; y < kClusterTilesY; ++y) {
            for (int x = 0; x < kClusterTilesX; ++x) {
                Box b;
                b.min = glm::vec3(1e30f);
                b.max = glm::vec3(-1e30f);
                for (int cy = 0; cy < 2; ++cy) {
                    for (int cx = 0; cx < 2; ++cx) {
                        const Ray& r = rays[static_cast<size_t>((y + cy) * (kClusterTilesX + 1) + x + cx)];
                        for (float d : {d0, d1}) {
                            const glm::vec3 p = r.at(d);
                            b.min = glm::min(b.min, p);
                            b.max = glm::max(b.max, p);
                        }
                    }
                }
                b.min -= glm::vec3(slack);
                b.max += glm::vec3(slack);
                boxes[static_cast<size_t>(clusterIndex(x, y, z))] = b;
            }
        }
    }
    return boxes;
}

float distanceSquaredToBox(const glm::vec3& p, const Box& b) {
    const glm::vec3 q = glm::clamp(p, b.min, b.max);
    const glm::vec3 d = p - q;
    return glm::dot(d, d);
}

int tileOf(float ndc, int tiles, float slack) {
    const float t = (ndc * 0.5f + 0.5f) * static_cast<float>(tiles) + slack;
    return std::clamp(static_cast<int>(std::floor(t)), 0, tiles - 1);
}

}  // namespace

float clusterSliceScale(const ClusterCamera& camera) {
    if (camera.orthographic) return static_cast<float>(kClusterSlices) / (camera.farPlane - camera.nearPlane);
    return static_cast<float>(kClusterSlices) / std::log(camera.farPlane / camera.nearPlane);
}

int clusterSlice(float viewDepth, const ClusterCamera& camera) {
    const float scale = clusterSliceScale(camera);
    const float s = camera.orthographic ? (viewDepth - camera.nearPlane) * scale
                                        : std::log(std::max(viewDepth, camera.nearPlane) / camera.nearPlane) * scale;
    if (!(s > 0.0f)) return 0;
    return std::min(static_cast<int>(std::floor(s)), kClusterSlices - 1);
}

ClusterLists assignLightsToClusters(const ClusterCamera& camera, const std::vector<ClusterLight>& lights) {
    ClusterLists out;
    out.offsets.assign(static_cast<size_t>(kClusterCount), 0u);
    out.counts.assign(static_cast<size_t>(kClusterCount), 0u);
    if (lights.empty()) return out;

    const std::vector<Box> boxes = clusterBoxes(camera);
    // (cluster, light) pairs in light order, then a counting sort by cluster
    // keeps each cluster's lights in ascending order.
    std::vector<std::pair<uint32_t, uint32_t>> pairs;
    pairs.reserve(lights.size() * 8);
    for (size_t i = 0; i < lights.size(); ++i) {
        const ClusterLight& light = lights[i];
        const glm::vec3 c = glm::vec3(camera.view * glm::vec4(light.position, 1.0f));
        const float r = light.range;
        float dmin = -c.z - r, dmax = -c.z + r;
        if (dmax < camera.nearPlane || dmin > camera.farPlane) continue;
        dmin = std::max(dmin, camera.nearPlane);
        dmax = std::min(dmax, camera.farPlane);
        // The light's view-space bounding box, cut to the depth range, onto
        // the screen: its corners' extremes bound the whole box, which lies
        // in front of the eye.
        glm::vec2 lo(1e30f), hi(-1e30f);
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec4 p((corner & 1) ? c.x + r : c.x - r, (corner & 2) ? c.y + r : c.y - r,
                              (corner & 4) ? -dmax : -dmin, 1.0f);
            const glm::vec4 clip = camera.projection * p;
            const glm::vec2 ndc = glm::vec2(clip) / std::max(clip.w, 1e-6f);
            lo = glm::min(lo, ndc);
            hi = glm::max(hi, ndc);
        }
        if (hi.x < -1.0f || lo.x > 1.0f || hi.y < -1.0f || lo.y > 1.0f) continue;
        const int x0 = tileOf(lo.x, kClusterTilesX, -kTileSlack), x1 = tileOf(hi.x, kClusterTilesX, kTileSlack);
        const int y0 = tileOf(lo.y, kClusterTilesY, -kTileSlack), y1 = tileOf(hi.y, kClusterTilesY, kTileSlack);
        const int z0 = clusterSlice(dmin * (1.0f - kDepthSlack), camera);
        const int z1 = clusterSlice(dmax * (1.0f + kDepthSlack), camera);
        const float r2 = r * r;
        bool listed = false;
        for (int z = z0; z <= z1; ++z) {
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const int k = clusterIndex(x, y, z);
                    // The sphere against the cluster's box drops the corners of
                    // the screen rectangle the sphere does not reach.
                    if (distanceSquaredToBox(c, boxes[static_cast<size_t>(k)]) > r2) continue;
                    pairs.emplace_back(static_cast<uint32_t>(k), static_cast<uint32_t>(i));
                    listed = true;
                }
            }
        }
        if (listed) ++out.stats.lightsInView;
    }

    std::vector<uint32_t> all(static_cast<size_t>(kClusterCount) + 1, 0u);
    for (const auto& p : pairs) ++all[p.first + 1];
    for (size_t k = 1; k < all.size(); ++k) all[k] += all[k - 1];
    std::vector<uint32_t> sorted(pairs.size());
    {
        std::vector<uint32_t> cursor(all.begin(), all.end() - 1);
        for (const auto& p : pairs) sorted[cursor[p.first]++] = p.second;
    }

    out.indices.reserve(sorted.size());
    std::vector<std::pair<float, uint32_t>> scored;
    for (int k = 0; k < kClusterCount; ++k) {
        const auto ku = static_cast<size_t>(k);
        const uint32_t begin = all[ku], count = all[ku + 1] - all[ku];
        out.offsets[ku] = static_cast<uint32_t>(out.indices.size());
        if (count == 0) continue;
        ++out.stats.clustersLit;
        out.stats.maxLightsPerCluster = std::max(out.stats.maxLightsPerCluster, static_cast<int>(count));
        if (count <= static_cast<uint32_t>(kMaxLightsPerCluster)) {
            out.indices.insert(out.indices.end(), sorted.begin() + begin, sorted.begin() + begin + count);
            out.counts[ku] = count;
            continue;
        }
        // Too many: keep the lights that light the cluster's centre most
        // (measured from the nearest point of a sphere around the cluster).
        ++out.stats.clustersOverflowed;
        const Box& box = boxes[ku];
        const glm::vec3 centre = 0.5f * (box.min + box.max);
        const float radius = 0.5f * glm::length(box.max - box.min);
        scored.clear();
        for (uint32_t j = begin; j < begin + count; ++j) {
            const ClusterLight& light = lights[sorted[j]];
            const glm::vec3 c = glm::vec3(camera.view * glm::vec4(light.position, 1.0f));
            const float d = std::max(glm::length(c - centre) - radius, 0.0f);
            scored.emplace_back(-pointLightAttenuation(d, light.range, light.brightness), sorted[j]);
        }
        std::partial_sort(scored.begin(), scored.begin() + kMaxLightsPerCluster, scored.end());
        scored.resize(static_cast<size_t>(kMaxLightsPerCluster));
        std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
        for (const auto& s : scored) out.indices.push_back(s.second);
        out.counts[ku] = static_cast<uint32_t>(kMaxLightsPerCluster);
    }
    out.stats.lightReferences = static_cast<int>(out.indices.size());
    return out;
}

}  // namespace strata::render
