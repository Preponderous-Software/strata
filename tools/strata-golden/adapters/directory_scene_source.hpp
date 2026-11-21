#pragma once

#include <string>
#include <vector>

#include "ports/scene_source.hpp"

namespace strata::golden {

// SceneSource over a directory of .scene files, read with the scene parser.
// A ref is the scene file's path.
class DirectorySceneSource final : public SceneSource {
public:
    explicit DirectorySceneSource(std::string directory);
    std::vector<SceneRef> list(const std::string& only) override;
    std::string location() const override;
    Loaded load(const std::string& ref) override;

private:
    std::string directory_;
};

}  // namespace strata::golden
