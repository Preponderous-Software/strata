#include "render/cascades.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace strata::render {

std::array<float, kMaxCascades + 1> practicalSplits(float nearPlane, float farPlane, int count, float lambda) {
    count = std::clamp(count, 1, kMaxCascades);
    std::array<float, kMaxCascades + 1> splits{};
    splits.fill(farPlane);
    splits[0] = nearPlane;
    for (int i = 1; i < count; ++i) {
        const float p = static_cast<float>(i) / static_cast<float>(count);
        const float logarithmic = nearPlane * std::pow(farPlane / nearPlane, p);
        const float uniform = nearPlane + (farPlane - nearPlane) * p;
        splits[i] = lambda * logarithmic + (1.0f - lambda) * uniform;
    }
    splits[count] = farPlane;
    return splits;
}

void sliceBoundingSphere(float tanHalfFovY, float aspect, float a, float b, float& centreDistance, float& radius) {
    // A corner at view depth z sits k*z off the view axis, where
    // k^2 = tan^2 (1 + aspect^2). The sphere through the near and far corners
    // has its centre on the axis at c = (a + b)(1 + k^2) / 2; when that falls
    // beyond the far plane the far cap alone bounds the slice.
    const float k2 = tanHalfFovY * tanHalfFovY * (1.0f + aspect * aspect);
    float c = 0.5f * (a + b) * (1.0f + k2);
    if (c > b) c = b;
    centreDistance = c;
    radius = std::sqrt((b - c) * (b - c) + b * b * k2);
}

glm::mat4 lightRotation(const glm::vec3& toLight) {
    const glm::vec3 forward = -glm::normalize(toLight);
    const glm::vec3 up = std::fabs(forward.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    return glm::lookAt(glm::vec3(0.0f), forward, up);
}

Cascades computeCascades(const CascadeInput& in) {
    Cascades out;
    out.count = std::clamp(in.count, 1, kMaxCascades);
    const float far = std::max(std::min(in.shadowDistance, in.farPlane), in.nearPlane * 2.0f);
    const auto splits = practicalSplits(in.nearPlane, far, out.count, in.lambda);
    const glm::mat4 inverseView = glm::inverse(in.view);
    const glm::mat4 rotation = lightRotation(in.toLight);

    for (int i = 0; i < out.count; ++i) {
        float centreDistance = 0.0f, radius = 0.0f;
        sliceBoundingSphere(in.tanHalfFovY, in.aspect, splits[i], splits[i + 1], centreDistance, radius);
        // Quantise the radius so floating-point noise cannot change the
        // projection's scale from one frame to the next.
        radius = std::ceil(radius * 16.0f) / 16.0f;

        const glm::vec3 centre = glm::vec3(inverseView * glm::vec4(0.0f, 0.0f, -centreDistance, 1.0f));
        const float texel = 2.0f * radius / static_cast<float>(in.mapSize);

        // Snap the centre to whole texels in light space.
        glm::vec3 lightSpace = glm::vec3(rotation * glm::vec4(centre, 1.0f));
        lightSpace.x = std::floor(lightSpace.x / texel) * texel;
        lightSpace.y = std::floor(lightSpace.y / texel) * texel;

        // Casters outside the slice but between it and the light still have
        // to land in the map; depth clamp in the shadow pass catches the rest.
        const float casterReach = std::max(radius, 50.0f);
        const float zNear = -lightSpace.z - radius - casterReach;
        const float zFar = -lightSpace.z + radius;
        const glm::mat4 projection = glm::ortho(lightSpace.x - radius, lightSpace.x + radius, lightSpace.y - radius,
                                                lightSpace.y + radius, zNear, zFar);
        out.viewProj[i] = projection * rotation;
        out.splitFar[i] = splits[i + 1];
        out.texelWorld[i] = texel;
        out.radius[i] = radius;
        out.centre[i] = centre;
    }
    return out;
}

}  // namespace strata::render
