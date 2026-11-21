#pragma once

#include <map>
#include <set>
#include <string>

#include "ports/golden_store.hpp"

namespace strata::golden {

// GoldenStore in memory; names in `unwritable` refuse a write.
class FakeGoldenStore final : public GoldenStore {
public:
    std::map<std::string, image::Image> images;
    std::set<std::string> unwritable;

    std::string locate(const std::string& name) const override { return "golden/" + name + ".png"; }
    ImageRead read(const std::string& name) override {
        ImageRead out;
        const auto it = images.find(name);
        if (it == images.end()) {
            out.error = "no such file";
        } else {
            out.ok = true;
            out.image = it->second;
        }
        return out;
    }
    ImageWritten write(const std::string& name, const image::Image& image) override {
        if (unwritable.count(name)) return {false, "read-only"};
        images[name] = image;
        return {true, ""};
    }
};

}  // namespace strata::golden
