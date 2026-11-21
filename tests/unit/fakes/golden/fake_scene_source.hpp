#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "ports/scene_source.hpp"

namespace strata::golden {

// SceneSource in memory: scenes by name; a name in `broken` fails to load
// with that message. Refs are "mem:<name>".
class FakeSceneSource final : public SceneSource {
public:
    std::map<std::string, scene::Scene> scenes;
    std::map<std::string, std::string> broken;

    std::vector<SceneRef> list(const std::string& only) override {
        std::vector<SceneRef> refs;
        auto add = [&](const std::string& name) {
            if (name[0] != '_' && (only.empty() || name == only)) refs.push_back({name, "mem:" + name});
        };
        for (const auto& s : scenes) add(s.first);
        for (const auto& b : broken) add(b.first);
        std::sort(refs.begin(), refs.end(), [](const SceneRef& a, const SceneRef& b) { return a.ref < b.ref; });
        return refs;
    }
    std::string location() const override { return "mem:"; }
    Loaded load(const std::string& ref) override {
        Loaded out;
        const std::string name = ref.rfind("mem:", 0) == 0 ? ref.substr(4) : ref;
        if (broken.count(name)) {
            out.error = broken.at(name);
        } else if (scenes.count(name)) {
            out.ok = true;
            out.scene = scenes.at(name);
        } else {
            out.error = ref + ": cannot open";
        }
        return out;
    }
};

}  // namespace strata::golden
