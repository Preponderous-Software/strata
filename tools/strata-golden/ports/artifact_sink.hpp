#pragma once

#include <string>

#include "model.hpp"

namespace strata::golden {

// Where rendered images go: a failing scene's render and diff, or the output
// of `render`.
//
// Contract:
// - prepare() makes the place ready before any scene renders; failure throws
//   std::filesystem::filesystem_error, as it always has.
// - locate(file) is where `file` is written, for messages.
// - write(file, image) writes it; failure is ok false with a reason.
// - Blocking file I/O; single-threaded.
class ArtifactSink {
public:
    virtual ~ArtifactSink() = default;
    virtual void prepare() = 0;
    virtual std::string locate(const std::string& file) const = 0;
    virtual ImageWritten write(const std::string& file, const image::Image& image) = 0;
};

}  // namespace strata::golden
