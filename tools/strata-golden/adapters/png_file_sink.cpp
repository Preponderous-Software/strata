#include "adapters/png_file_sink.hpp"

#include <filesystem>
#include <utility>

namespace fs = std::filesystem;

namespace strata::golden {

PngFileSink::PngFileSink(std::string directory) : directory_(std::move(directory)) {}

void PngFileSink::prepare() { fs::create_directories(fs::path(directory_)); }

std::string PngFileSink::locate(const std::string& file) const {
    return directory_.empty() ? file : (fs::path(directory_) / file).string();
}

ImageWritten PngFileSink::write(const std::string& file, const image::Image& image) {
    ImageWritten out;
    out.ok = image::writePng(locate(file), image, &out.error);
    return out;
}

}  // namespace strata::golden
