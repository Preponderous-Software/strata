#include "image_decode.hpp"

#include <climits>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#if defined(__GNUC__)
#pragma GCC diagnostic push
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#endif
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace strata::gltf {

bool decodeImage(const uint8_t* bytes, size_t size, int& width, int& height, std::vector<uint8_t>& rgba,
                 std::string* error) {
    if (!bytes || size == 0 || size > static_cast<size_t>(INT_MAX)) {
        if (error) *error = "no image data";
        return false;
    }
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes, static_cast<int>(size), &w, &h, &channels, 4);
    if (!pixels) {
        if (error) *error = std::string("cannot decode (PNG and JPEG only): ") + stbi_failure_reason();
        return false;
    }
    width = w;
    height = h;
    rgba.assign(pixels, pixels + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    stbi_image_free(pixels);
    return true;
}

}  // namespace strata::gltf
