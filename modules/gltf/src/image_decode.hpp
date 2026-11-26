// PNG and JPEG decoding for glTF images (stb_image, compiled static here so
// it cannot clash with the copy inside strata_image).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace strata::gltf {

// Decodes a PNG or JPEG to RGBA8, rows top to bottom. False and *error on failure.
bool decodeImage(const uint8_t* bytes, size_t size, int& width, int& height, std::vector<uint8_t>& rgba,
                 std::string* error);

}  // namespace strata::gltf
