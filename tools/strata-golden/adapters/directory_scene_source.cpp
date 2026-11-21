#include "adapters/directory_scene_source.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <utility>

namespace fs = std::filesystem;

namespace strata::golden {

DirectorySceneSource::DirectorySceneSource(std::string directory) : directory_(std::move(directory)) {}

std::vector<SceneRef> DirectorySceneSource::list(const std::string& only) {
    std::vector<fs::path> scenes;
    for (const auto& entry : fs::directory_iterator(fs::path(directory_))) {
        const std::string name = entry.path().filename().string();
        if (entry.path().extension() == ".scene" && name[0] != '_') {
            if (only.empty() || entry.path().stem() == only) scenes.push_back(entry.path());
        }
    }
    std::sort(scenes.begin(), scenes.end());
    std::vector<SceneRef> refs;
    for (const fs::path& p : scenes) refs.push_back({p.stem().string(), p.string()});
    return refs;
}

std::string DirectorySceneSource::location() const { return fs::path(directory_).string(); }

Loaded DirectorySceneSource::load(const std::string& ref) {
    Loaded out;
    try {
        out.scene = scene::loadScene(ref);
        out.ok = true;
    } catch (const std::exception& e) {
        out.error = e.what();
    }
    return out;
}

}  // namespace strata::golden
