// PNG in and out, for screenshots and golden images. RGBA8, top row first.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace strata::image {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

// Writes RGB (alpha dropped). Returns false and fills *error on failure.
bool writePng(const std::string& path, const Image& image, std::string* error);

// Reads any PNG as RGBA8. Returns false and fills *error on failure.
bool readPng(const std::string& path, Image& image, std::string* error);

}  // namespace strata::image
