#pragma once

#include <string>
#include <vector>

#include "model.hpp"

namespace strata::golden {

// Where the test scenes come from.
//
// Contract:
// - list(only) returns the test scenes sorted by ref: files named *.scene
//   whose names do not start with '_' (those are includes), only the one
//   named `only` when it is not empty. An unreadable location throws
//   std::filesystem::filesystem_error, which nothing catches (the tool stops,
//   as it always has).
// - location() names where the scenes are, for messages.
// - load(ref) reads one scene, by a ref from list() or a path the user gave.
//   A scene that does not parse is a value: ok false and the parser's message.
// - Blocking file I/O; single-threaded.
class SceneSource {
public:
    virtual ~SceneSource() = default;
    virtual std::vector<SceneRef> list(const std::string& only) = 0;
    virtual std::string location() const = 0;
    virtual Loaded load(const std::string& ref) = 0;
};

}  // namespace strata::golden
