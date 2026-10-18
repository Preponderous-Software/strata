#include "render/primitive_mesh.hpp"

#include <cmath>

#include "core/error.hpp"
#include "strata/strata.h"

namespace strata::render {
namespace {

constexpr double kPi = 3.14159265358979323846;

class MeshBuilder {
public:
    uint32_t vertexCount() const { return static_cast<uint32_t>(mesh_.vertices.size() / PrimitiveMesh::kFloatsPerVertex); }

    void addVertex(float x, float y, float z, float nx, float ny, float nz) {
        mesh_.vertices.insert(mesh_.vertices.end(), {x, y, z, nx, ny, nz});
    }

    void addTriangle(uint32_t a, uint32_t b, uint32_t c) { mesh_.indices.insert(mesh_.indices.end(), {a, b, c}); }

    void addQuad(float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz, float dx,
                 float dy, float dz, float nx, float ny, float nz) {
        const uint32_t start = vertexCount();
        addVertex(ax, ay, az, nx, ny, nz);
        addVertex(bx, by, bz, nx, ny, nz);
        addVertex(cx, cy, cz, nx, ny, nz);
        addVertex(dx, dy, dz, nx, ny, nz);
        addTriangle(start, start + 1, start + 2);
        addTriangle(start, start + 2, start + 3);
    }

    PrimitiveMesh build() { return std::move(mesh_); }

private:
    PrimitiveMesh mesh_;
};

// Longitude before latitude, so the result faces outwards.
void addGridIndices(MeshBuilder& builder, int rings, int sectors) {
    for (int i = 0; i < rings - 1; i++) {
        for (int j = 0; j < sectors; j++) {
            const int next = (j + 1) % sectors;
            const auto a = static_cast<uint32_t>(i * sectors + j);
            const auto b = static_cast<uint32_t>(i * sectors + next);
            const auto c = static_cast<uint32_t>((i + 1) * sectors + next);
            const auto d = static_cast<uint32_t>((i + 1) * sectors + j);
            builder.addTriangle(a, b, c);
            builder.addTriangle(a, c, d);
        }
    }
}

}  // namespace

PrimitiveMesh createCube() {
    MeshBuilder builder;
    const float h = 0.5f;
    builder.addQuad(-h, -h, h, h, -h, h, h, h, h, -h, h, h, 0.f, 0.f, 1.f);
    builder.addQuad(h, -h, -h, -h, -h, -h, -h, h, -h, h, h, -h, 0.f, 0.f, -1.f);
    builder.addQuad(h, -h, h, h, -h, -h, h, h, -h, h, h, h, 1.f, 0.f, 0.f);
    builder.addQuad(-h, -h, -h, -h, -h, h, -h, h, h, -h, h, -h, -1.f, 0.f, 0.f);
    builder.addQuad(-h, h, h, h, h, h, h, h, -h, -h, h, -h, 0.f, 1.f, 0.f);
    builder.addQuad(-h, -h, -h, h, -h, -h, h, -h, h, -h, -h, h, 0.f, -1.f, 0.f);
    return builder.build();
}

PrimitiveMesh createSphere(int stacks, int sectors) {
    MeshBuilder builder;
    for (int i = 0; i <= stacks; i++) {
        const double theta = kPi * i / stacks;
        const auto sinTheta = static_cast<float>(std::sin(theta));
        const auto cosTheta = static_cast<float>(std::cos(theta));
        for (int j = 0; j < sectors; j++) {
            const double phi = 2.0 * kPi * j / sectors;
            const float nx = sinTheta * static_cast<float>(std::cos(phi));
            const float ny = cosTheta;
            const float nz = sinTheta * static_cast<float>(std::sin(phi));
            builder.addVertex(nx * 0.5f, ny * 0.5f, nz * 0.5f, nx, ny, nz);
        }
    }
    addGridIndices(builder, stacks + 1, sectors);
    return builder.build();
}

PrimitiveMesh createCapsule(int stacks, int sectors) {
    if (stacks % 2 != 0) stacks++;
    const int half = stacks / 2;
    const int rings = stacks + 2;
    MeshBuilder builder;
    for (int ring = 0; ring < rings; ring++) {
        // Rings 0..half belong to the top cap, half+1..rings-1 to the bottom
        // one; the two copies of the equator differ only in their centre.
        const int i = ring <= half ? ring : ring - 1;
        const float centreY = ring <= half ? 0.5f : -0.5f;
        const double theta = kPi * i / stacks;
        const auto sinTheta = static_cast<float>(std::sin(theta));
        const auto cosTheta = static_cast<float>(std::cos(theta));
        for (int j = 0; j < sectors; j++) {
            const double phi = 2.0 * kPi * j / sectors;
            const float nx = sinTheta * static_cast<float>(std::cos(phi));
            const float ny = cosTheta;
            const float nz = sinTheta * static_cast<float>(std::sin(phi));
            builder.addVertex(nx * 0.5f, ny * 0.5f + centreY, nz * 0.5f, nx, ny, nz);
        }
    }
    addGridIndices(builder, rings, sectors);
    return builder.build();
}

PrimitiveMesh createCylinder(int sectors) {
    MeshBuilder builder;
    // Wall: a bottom ring then a top ring, both with outward normals.
    for (int ring = 0; ring < 2; ring++) {
        const float y = ring == 0 ? -1.f : 1.f;
        for (int j = 0; j < sectors; j++) {
            const double phi = 2.0 * kPi * j / sectors;
            const auto nx = static_cast<float>(std::cos(phi));
            const auto nz = static_cast<float>(std::sin(phi));
            builder.addVertex(nx * 0.5f, y, nz * 0.5f, nx, 0.f, nz);
        }
    }
    for (int j = 0; j < sectors; j++) {
        const int next = (j + 1) % sectors;
        const auto bottom = static_cast<uint32_t>(j);
        const auto bottomNext = static_cast<uint32_t>(next);
        const auto top = static_cast<uint32_t>(sectors + j);
        const auto topNext = static_cast<uint32_t>(sectors + next);
        builder.addTriangle(bottom, top, topNext);
        builder.addTriangle(bottom, topNext, bottomNext);
    }
    // Caps, each a fan around its own centre vertex.
    const uint32_t topRingStart = builder.vertexCount();
    for (int j = 0; j < sectors; j++) {
        const double phi = 2.0 * kPi * j / sectors;
        builder.addVertex(static_cast<float>(std::cos(phi)) * 0.5f, 1.f, static_cast<float>(std::sin(phi)) * 0.5f, 0.f,
                          1.f, 0.f);
    }
    const uint32_t topCentre = builder.vertexCount();
    builder.addVertex(0.f, 1.f, 0.f, 0.f, 1.f, 0.f);
    const uint32_t bottomRingStart = builder.vertexCount();
    for (int j = 0; j < sectors; j++) {
        const double phi = 2.0 * kPi * j / sectors;
        builder.addVertex(static_cast<float>(std::cos(phi)) * 0.5f, -1.f, static_cast<float>(std::sin(phi)) * 0.5f,
                          0.f, -1.f, 0.f);
    }
    const uint32_t bottomCentre = builder.vertexCount();
    builder.addVertex(0.f, -1.f, 0.f, 0.f, -1.f, 0.f);
    for (int j = 0; j < sectors; j++) {
        const auto next = static_cast<uint32_t>((j + 1) % sectors);
        builder.addTriangle(topCentre, topRingStart + next, topRingStart + static_cast<uint32_t>(j));
        builder.addTriangle(bottomCentre, bottomRingStart + static_cast<uint32_t>(j), bottomRingStart + next);
    }
    return builder.build();
}

PrimitiveMesh createPrimitive(int kind) {
    switch (kind) {
        case STRATA_PRIMITIVE_CUBE: return createCube();
        case STRATA_PRIMITIVE_SPHERE: return createSphere(16, 24);
        case STRATA_PRIMITIVE_CYLINDER: return createCylinder(24);
        case STRATA_PRIMITIVE_CAPSULE: return createCapsule(16, 24);
        default: fail(STRATA_ERROR_INVALID_ARGUMENT, "no mesh is defined for primitive kind %d", kind);
    }
}

}  // namespace strata::render
