#include "render/ray_pick.hpp"

#include <algorithm>
#include <cmath>

namespace strata::render {

Aabb computeBounds(const float* positions, size_t vertexCount) {
    Aabb box;
    if (vertexCount == 0) return box;
    box.min = box.max = glm::vec3(positions[0], positions[1], positions[2]);
    for (size_t v = 1; v < vertexCount; ++v) {
        const glm::vec3 p(positions[v * 3], positions[v * 3 + 1], positions[v * 3 + 2]);
        box.min = glm::min(box.min, p);
        box.max = glm::max(box.max, p);
    }
    // Pad by a small fraction of the box's size and position, so a ray that
    // meets a triangle on the box's face still passes the slab test after
    // rounding. A flat mesh gets a box of non-zero thickness this way too.
    const glm::vec3 extent = box.max - box.min;
    const float size = std::max(extent.x, std::max(extent.y, extent.z));
    const glm::vec3 magnitude = glm::max(glm::abs(box.min), glm::abs(box.max));
    const float reach = std::max(magnitude.x, std::max(magnitude.y, magnitude.z));
    const float pad = 1e-4f * size + 1e-6f * reach + 1e-12f;
    box.min -= glm::vec3(pad);
    box.max += glm::vec3(pad);
    return box;
}

PickGeometry buildPickGeometry(const StrataMesh& mesh) {
    PickGeometry g;
    const size_t vertices = static_cast<size_t>(mesh.vertex_count);
    g.positions.assign(mesh.positions, mesh.positions + vertices * 3);
    g.indices.assign(mesh.indices, mesh.indices + mesh.index_count);
    g.bounds = computeBounds(g.positions.data(), vertices);
    return g;
}

bool rayHitsAabb(const glm::vec3& origin, const glm::vec3& dir, const Aabb& box, float maxT) {
    float tNear = 0.0f, tFar = maxT;
    for (int axis = 0; axis < 3; ++axis) {
        const float o = origin[axis], d = dir[axis];
        if (d == 0.0f) {
            // Parallel to this slab: inside it everywhere or nowhere.
            if (o < box.min[axis] || o > box.max[axis]) return false;
            continue;
        }
        float t0 = (box.min[axis] - o) / d;
        float t1 = (box.max[axis] - o) / d;
        if (t0 > t1) std::swap(t0, t1);
        tNear = std::max(tNear, t0);
        tFar = std::min(tFar, t1);
        if (tNear > tFar) return false;
    }
    return true;
}

bool rayHitsTriangle(const glm::vec3& origin, const glm::vec3& dir, const glm::vec3& a, const glm::vec3& b,
                     const glm::vec3& c, float maxT, float* t) {
    const glm::vec3 e1 = b - a, e2 = c - a;
    const glm::vec3 p = glm::cross(dir, e2);
    const float det = glm::dot(e1, p);
    // Both faces count, so only a ray in the triangle's plane (or a
    // degenerate triangle) is rejected here.
    if (det == 0.0f || !std::isfinite(det)) return false;
    const float invDet = 1.0f / det;
    const glm::vec3 s = origin - a;
    const float u = glm::dot(s, p) * invDet;
    if (!(u >= 0.0f && u <= 1.0f)) return false;
    const glm::vec3 q = glm::cross(s, e1);
    const float v = glm::dot(dir, q) * invDet;
    if (!(v >= 0.0f && u + v <= 1.0f)) return false;
    const float hitT = glm::dot(e2, q) * invDet;
    if (!(hitT >= 0.0f && hitT <= maxT)) return false;
    *t = hitT;
    return true;
}

PickHit raycastMesh(const PickGeometry& mesh, const glm::mat4& model, const glm::vec3& worldOrigin,
                    const glm::vec3& worldDir, float maxDistance) {
    PickHit result;
    // Only the affine part (bottom row taken as 0 0 0 1), which keeps the
    // ray parameter the same in world and model space.
    const glm::mat3 linear(model);
    const glm::vec3 translation(model[3]);
    const glm::mat3 inverse = glm::inverse(linear);
    const glm::vec3 origin = inverse * (worldOrigin - translation);
    const glm::vec3 dir = inverse * worldDir;
    if (!rayHitsAabb(origin, dir, mesh.bounds, maxDistance)) return result;

    float nearest = maxDistance;
    size_t nearestTriangle = 0;
    bool found = false;
    const float* p = mesh.positions.data();
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const uint32_t ia = mesh.indices[i], ib = mesh.indices[i + 1], ic = mesh.indices[i + 2];
        const glm::vec3 a(p[ia * 3], p[ia * 3 + 1], p[ia * 3 + 2]);
        const glm::vec3 b(p[ib * 3], p[ib * 3 + 1], p[ib * 3 + 2]);
        const glm::vec3 c(p[ic * 3], p[ic * 3 + 1], p[ic * 3 + 2]);
        float t = 0.0f;
        // Strictly nearer only, so a tie keeps the earlier triangle.
        if (rayHitsTriangle(origin, dir, a, b, c, nearest, &t) && (!found || t < nearest)) {
            nearest = t;
            nearestTriangle = i;
            found = true;
        }
    }
    if (!found) return result;

    const uint32_t ia = mesh.indices[nearestTriangle], ib = mesh.indices[nearestTriangle + 1],
                   ic = mesh.indices[nearestTriangle + 2];
    const glm::vec3 a(p[ia * 3], p[ia * 3 + 1], p[ia * 3 + 2]);
    const glm::vec3 b(p[ib * 3], p[ib * 3 + 1], p[ib * 3 + 2]);
    const glm::vec3 c(p[ic * 3], p[ic * 3 + 1], p[ic * 3 + 2]);
    // Normals go through the inverse transpose, as in the shaders.
    glm::vec3 normal = glm::transpose(inverse) * glm::cross(b - a, c - a);
    const float length = glm::length(normal);
    normal = length > 0.0f && std::isfinite(length) ? normal / length : -worldDir;
    if (glm::dot(normal, worldDir) > 0.0f) normal = -normal;

    result.hit = true;
    result.triangle = static_cast<int32_t>(nearestTriangle / 3);
    result.distance = nearest;
    result.point = worldOrigin + worldDir * nearest;
    result.normal = normal;
    return result;
}

}  // namespace strata::render
