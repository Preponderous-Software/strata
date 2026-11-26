// Image scaling and shelf packing for texture atlases (plan.cpp).
#pragma once

#include <cstdint>
#include <vector>

namespace strata::gltf {

struct AtlasImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;  // RGBA8, rows top to bottom
};

// Box-filtered resize (each output texel averages the input texels it
// covers; nearest when enlarging).
AtlasImage resizeImage(const AtlasImage& in, int width, int height);

struct AtlasInput {
    const AtlasImage* image;
};

// Where one input landed: the image's texels (not the gutter) start at
// (x, y) on page `page` and are width x height after scaling.
struct AtlasPlacement {
    int page = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct AtlasResult {
    std::vector<AtlasImage> pages;
    std::vector<AtlasPlacement> placements;  // one per input, in order
    float scale = 1.0f;                       // applied to every input
};

constexpr int kAtlasGutter = 4;  // edge-extended texels around each image

// Packs every input onto at most maxPages square-or-smaller pages of at most
// pageSize texels a side, scaling all inputs down together (by 0.8 a step)
// until they fit. Pages are cropped to the power-of-two size that holds
// what was placed on them. maxPages must be >= 1 and inputs non-empty.
AtlasResult packAtlas(const std::vector<AtlasInput>& inputs, int pageSize, int maxPages);

}  // namespace strata::gltf
