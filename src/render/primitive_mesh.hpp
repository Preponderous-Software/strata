// Beyond Nations' four analytic primitives, generated at start-up.
//
// A line-for-line port of BN's PrimitiveMesh.cs: same tessellation (sphere and
// capsule 16 stacks x 24 sectors, cylinder 24 sectors), same vertex order,
// same winding (counter-clockwise seen from outside), and Unity's dimensions:
//
//   cube      1 unit on every side, centred on the origin
//   sphere    1 unit across, centred on the origin
//   cylinder  1 unit across, 2 units tall, centred on the origin
//   capsule   1 unit across, 2 units tall, centred on the origin
//
// Matching it exactly is what lets the native renderer be compared with BN's
// own renderer pixel for pixel.
#pragma once

#include <cstdint>
#include <vector>

namespace strata::render {

struct PrimitiveMesh {
    static constexpr int kFloatsPerVertex = 6;  // position xyz, normal xyz
    std::vector<float> vertices;
    std::vector<uint32_t> indices;

    int vertexCount() const { return static_cast<int>(vertices.size() / kFloatsPerVertex); }
    int indexCount() const { return static_cast<int>(indices.size()); }
};

PrimitiveMesh createCube();
PrimitiveMesh createSphere(int stacks, int sectors);
PrimitiveMesh createCapsule(int stacks, int sectors);
PrimitiveMesh createCylinder(int sectors);

// The mesh for a STRATA_PRIMITIVE_* kind, at BN's tessellation.
PrimitiveMesh createPrimitive(int kind);

}  // namespace strata::render
