// Depth readback (ABI 1.9): the maths behind strata_read_depth_f32, kept free
// of GL so it is unit-tested directly.
//
// The offscreen target's depth attachment holds window-space depth d in
// [0, 1] (glDepthRange's default). GLES 3.0 and WebGL 2 cannot read a depth
// attachment back with glReadPixels, so the renderer draws a fullscreen pass
// that samples it and writes each float's 32 bits into an RGBA8 texel
// (unpackDepthTexel reverses that), then turns d into a planar view distance
// with the projection the frame was drawn with.
#pragma once

#include <cstdint>

namespace strata::render {

// The float whose bits a depth-pack texel carries (r is the low byte).
float unpackDepthTexel(const uint8_t rgba[4]);

// Window depth d (0..1) to the distance along the camera's view axis, in
// world units, for a column-major projection of either depth convention: the
// GPU's NDC z = 2d - 1 is taken back through the projection's z and w rows.
// d >= 1 (the cleared value: nothing drawn) and a non-finite result give
// +infinity.
float planarDepth(const float projection[16], float windowDepth);

}  // namespace strata::render
