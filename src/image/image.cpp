#include "image/image.hpp"

#if defined(_WIN32)
#define STBI_WINDOWS_UTF8
#define STBIW_WINDOWS_UTF8
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunknown-warning-option"  // -Wmaybe-uninitialized is GCC's
#endif
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "stb_image.h"
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace strata::image {

bool writePng(const std::string& path, const Image& image, std::string* error) {
    if (image.width <= 0 || image.height <= 0 ||
        image.rgba.size() != static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4) {
        if (error) *error = "image has no pixels or a size that does not match its dimensions";
        return false;
    }
    std::vector<uint8_t> rgb(static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 3);
    for (size_t i = 0, j = 0; i < image.rgba.size(); i += 4, j += 3) {
        rgb[j] = image.rgba[i];
        rgb[j + 1] = image.rgba[i + 1];
        rgb[j + 2] = image.rgba[i + 2];
    }
    if (!stbi_write_png(path.c_str(), image.width, image.height, 3, rgb.data(), image.width * 3)) {
        if (error) *error = "stbi_write_png failed (does the directory exist?)";
        return false;
    }
    return true;
}

bool readPng(const std::string& path, Image& image, std::string* error) {
    int width = 0, height = 0, channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) {
        if (error) *error = stbi_failure_reason() ? stbi_failure_reason() : "could not read image";
        return false;
    }
    image.width = width;
    image.height = height;
    image.rgba.assign(data, data + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    stbi_image_free(data);
    return true;
}

}  // namespace strata::image
