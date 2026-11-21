#pragma once

#include <map>
#include <string>

#include "ports/artifact_sink.hpp"

namespace strata::golden {

// ArtifactSink in memory; `failWrites` makes every write fail.
class FakeArtifactSink final : public ArtifactSink {
public:
    std::map<std::string, image::Image> files;
    int prepared = 0;
    bool failWrites = false;

    void prepare() override { ++prepared; }
    std::string locate(const std::string& file) const override { return "out/" + file; }
    ImageWritten write(const std::string& file, const image::Image& image) override {
        if (failWrites) return {false, "disk full"};
        files[file] = image;
        return {true, ""};
    }
};

}  // namespace strata::golden
