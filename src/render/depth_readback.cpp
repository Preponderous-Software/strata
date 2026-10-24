#include "render/depth_readback.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace strata::render {

float unpackDepthTexel(const uint8_t rgba[4]) {
    const uint32_t bits = static_cast<uint32_t>(rgba[0]) | (static_cast<uint32_t>(rgba[1]) << 8) |
                          (static_cast<uint32_t>(rgba[2]) << 16) | (static_cast<uint32_t>(rgba[3]) << 24);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

float planarDepth(const float p[16], float windowDepth) {
    const float kInfinity = std::numeric_limits<float>::infinity();
    if (!(windowDepth < 1.0f)) return kInfinity;
    // clip.z = p[2] x + p[6] y + p[10] z + p[14] w and clip.w = p[3] x + p[7] y
    // + p[11] z + p[15] w. Every Strata projection (and any perspective or
    // orthographic one a host builds) has p[2] = p[6] = p[3] = p[7] = 0, so
    // with a view point (x, y, z, 1): ndc = (p[10] z + p[14]) / (p[11] z + p[15]).
    const double ndc = 2.0 * static_cast<double>(windowDepth) - 1.0;
    const double denominator = ndc * p[11] - p[10];
    if (denominator == 0.0) return kInfinity;
    const double viewZ = (p[14] - ndc * p[15]) / denominator;
    const double distance = -viewZ;  // the camera looks down -Z in view space
    if (!std::isfinite(distance)) return kInfinity;
    return static_cast<float>(distance);
}

}  // namespace strata::render
