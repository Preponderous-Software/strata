#include "adapters/png_golden_directory.hpp"

#include <filesystem>
#include <utility>

namespace fs = std::filesystem;

namespace strata::golden {

PngGoldenDirectory::PngGoldenDirectory(std::string directory, std::string platform)
    : directory_(std::move(directory)), platform_(std::move(platform)) {}

std::string PngGoldenDirectory::locate(const std::string& name) const {
    return (fs::path(directory_) / name / (platform_ + ".png")).string();
}

ImageRead PngGoldenDirectory::read(const std::string& name) {
    ImageRead out;
    out.ok = image::readPng(locate(name), out.image, &out.error);
    return out;
}

ImageWritten PngGoldenDirectory::write(const std::string& name, const image::Image& image) {
    const fs::path golden = fs::path(directory_) / name / (platform_ + ".png");
    fs::create_directories(golden.parent_path());
    ImageWritten out;
    out.ok = image::writePng(golden.string(), image, &out.error);
    return out;
}

}  // namespace strata::golden
