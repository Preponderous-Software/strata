#include "render/pass_list.hpp"

#include <algorithm>
#include <cstring>

namespace strata::render {
namespace {

bool contains(const std::vector<std::string>& list, const char* name) {
    return std::find(list.begin(), list.end(), std::string(name)) != list.end();
}

}  // namespace

const std::vector<PassInfo>& framePasses() {
    // Later milestones slot in here: a depth pre-pass,
    // transparent and labels, post (tonemap, fog), and UI.
    static const std::vector<PassInfo> passes = {
        {"shadow", {"instances", "terrain", "meshes", "camera", "lighting"}, {"shadow_map"}},
        {"point_shadow", {"instances", "terrain", "meshes", "point_lights"}, {"point_shadow_maps"}},
        {"opaque",
         {"instances", "terrain", "meshes", "camera", "lighting", "point_lights", "shadow_map", "point_shadow_maps"},
         {"color", "depth"}},
        {"sky", {"camera", "lighting", "depth"}, {"color"}},
        {"readback", {"color"}, {"pixels"}},
    };
    return passes;
}

const std::vector<const char*>& frameInputs() {
    static const std::vector<const char*> inputs = {"instances", "terrain", "meshes", "camera", "lighting", "point_lights"};
    return inputs;
}

std::vector<std::string> validatePassOrder(const std::vector<PassInfo>& passes,
                                           const std::vector<const char*>& inputs) {
    std::vector<std::string> available(inputs.begin(), inputs.end());
    std::vector<std::string> problems;
    std::vector<std::string> names;
    for (const PassInfo& pass : passes) {
        if (contains(names, pass.name)) {
            problems.push_back(std::string("pass '") + pass.name + "' appears twice");
        }
        names.emplace_back(pass.name);
        for (const char* read : pass.reads) {
            if (!contains(available, read)) {
                problems.push_back(std::string("pass '") + pass.name + "' reads '" + read +
                                   "' before any pass writes it");
            }
        }
        for (const char* write : pass.writes) {
            if (!contains(available, write)) available.emplace_back(write);
        }
    }
    return problems;
}

std::string describePasses(const std::vector<PassInfo>& passes) {
    std::string out;
    for (const PassInfo& pass : passes) {
        out += pass.name;
        out += ":";
        for (size_t i = 0; i < pass.reads.size(); ++i) out += std::string(i ? ", " : " ") + pass.reads[i];
        out += " ->";
        for (size_t i = 0; i < pass.writes.size(); ++i) out += std::string(i ? ", " : " ") + pass.writes[i];
        out += "\n";
    }
    return out;
}

}  // namespace strata::render
