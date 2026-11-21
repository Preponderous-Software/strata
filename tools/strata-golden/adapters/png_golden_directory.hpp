#pragma once

#include <string>

#include "ports/golden_store.hpp"

namespace strata::golden {

// GoldenStore as PNG files: scene foo's golden is <directory>/foo/<platform>.png.
class PngGoldenDirectory final : public GoldenStore {
public:
    PngGoldenDirectory(std::string directory, std::string platform);
    std::string locate(const std::string& name) const override;
    ImageRead read(const std::string& name) override;
    ImageWritten write(const std::string& name, const image::Image& image) override;

private:
    std::string directory_;
    std::string platform_;
};

}  // namespace strata::golden
