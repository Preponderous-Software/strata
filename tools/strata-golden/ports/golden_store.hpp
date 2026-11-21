#pragma once

#include <string>

#include "model.hpp"

namespace strata::golden {

// Where the approved image of each scene lives.
//
// Contract:
// - locate(name) is where scene `name`'s golden is, for messages.
// - read(name) loads it; a missing or unreadable golden is ok false with a
//   reason, not an exception.
// - write(name, image) stores it as the new golden (--bless), creating what
//   it needs first. A failed write is ok false with a reason; a location that
//   cannot be created throws std::filesystem::filesystem_error, as it always
//   has.
// - Blocking file I/O; single-threaded.
class GoldenStore {
public:
    virtual ~GoldenStore() = default;
    virtual std::string locate(const std::string& name) const = 0;
    virtual ImageRead read(const std::string& name) = 0;
    virtual ImageWritten write(const std::string& name, const image::Image& image) = 0;
};

}  // namespace strata::golden
