#pragma once

#include <string>

#include "ports/artifact_sink.hpp"

namespace strata::golden {

// ArtifactSink as PNG files in `directory`. With an empty directory a file
// name is used exactly as given (render's <out.png>).
class PngFileSink final : public ArtifactSink {
public:
    explicit PngFileSink(std::string directory);
    void prepare() override;
    std::string locate(const std::string& file) const override;
    ImageWritten write(const std::string& file, const image::Image& image) override;

private:
    std::string directory_;
};

}  // namespace strata::golden
