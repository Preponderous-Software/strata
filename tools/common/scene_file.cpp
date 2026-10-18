#include "scene_file.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace strata::scene {
namespace {

std::string directoryOf(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

[[noreturn]] void bad(const std::string& where, const std::string& message) {
    throw std::runtime_error(where + ": " + message);
}

template <typename T>
void read(std::istringstream& in, T& value, const std::string& where, const char* what) {
    if (!(in >> value)) bad(where, std::string("expected ") + what);
}

int kindFromName(const std::string& name, const std::string& where) {
    if (name == "cube") return STRATA_PRIMITIVE_CUBE;
    if (name == "capsule") return STRATA_PRIMITIVE_CAPSULE;
    if (name == "cylinder") return STRATA_PRIMITIVE_CYLINDER;
    if (name == "sphere") return STRATA_PRIMITIVE_SPHERE;
    bad(where, "unknown primitive '" + name + "'");
}

void parse(const std::string& path, Scene& scene, int depth) {
    if (depth > 8) bad(path, "includes nest too deeply");
    std::ifstream file(path);
    if (!file) bad(path, "cannot open");
    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        const std::string where = path + ":" + std::to_string(lineNumber);
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream in(line);
        std::string word;
        if (!(in >> word)) continue;
        if (word == "size") {
            read(in, scene.width, where, "width");
            read(in, scene.height, where, "height");
        } else if (word == "camera") {
            for (float& v : scene.eye) read(in, v, where, "eye");
            for (float& v : scene.target) read(in, v, where, "target");
            read(in, scene.fovY, where, "fov");
            read(in, scene.nearPlane, where, "near");
            read(in, scene.farPlane, where, "far");
        } else if (word == "lighting") {
            std::string mode;
            read(in, mode, where, "lighting mode");
            StrataLighting& l = scene.lighting;
            if (mode == "legacy") {
                l.mode = STRATA_LIGHTING_LEGACY;
                for (float& v : l.legacy_direction) read(in, v, where, "direction");
                read(in, l.legacy_ambient, where, "ambient");
            } else if (mode == "sun") {
                l.mode = STRATA_LIGHTING_SUN;
                read(in, l.time_of_day, where, "time of day");
                read(in, l.latitude_degrees, where, "latitude");
                read(in, l.cascade_count, where, "cascade count");
                read(in, l.shadow_map_size, where, "shadow map size");
                read(in, l.shadow_distance, where, "shadow distance");
                read(in, l.split_lambda, where, "split lambda");
                read(in, l.fog, where, "fog");
            } else {
                bad(where, "unknown lighting mode '" + mode + "'");
            }
        } else if (word == "clear") {
            for (float& v : scene.frame.clear_color) read(in, v, where, "clear colour");
            scene.frame.clear_flags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH;
            std::string sky;
            if (in >> sky) {
                if (sky != "sky") bad(where, "expected 'sky' or nothing after the clear colour");
                scene.frame.clear_flags |= STRATA_CLEAR_SKY;
            }
        } else if (word == "light") {
            std::string type;
            read(in, type, where, "light type");
            if (type != "point") bad(where, "unknown light type '" + type + "' (expected 'point')");
            StrataPointLight light{};
            for (float& v : light.position) read(in, v, where, "position");
            read(in, light.range, where, "range");
            for (float& v : light.color) read(in, v, where, "colour");
            read(in, light.intensity, where, "intensity");
            std::string option;
            if (in >> option) {
                if (option != "shadow") bad(where, "expected 'shadow' or nothing after the intensity");
                light.flags |= STRATA_POINT_LIGHT_CAST_SHADOWS;
            }
            scene.pointLights.push_back(light);
        } else if (word == "terrain") {
            StrataTerrainTile& t = scene.terrain;
            for (float& v : t.origin) read(in, v, where, "origin");
            read(in, t.cell_size, where, "cell size");
            read(in, t.samples_x, where, "samples x");
            read(in, t.samples_z, where, "samples z");
            for (int i = 0; i < 3; ++i) read(in, t.color[i], where, "colour");
            t.color[3] = 1.0f;
            scene.hasTerrain = true;
            scene.terrainHeights.clear();
            scene.terrainColors.clear();
        } else if (word == "heights" || word == "colors") {
            if (!scene.hasTerrain) bad(where, "'" + word + "' before 'terrain'");
            std::vector<float>& out = word == "heights" ? scene.terrainHeights : scene.terrainColors;
            float v;
            while (in >> v) out.push_back(v);
            if (!in.eof()) bad(where, "expected numbers after '" + word + "'");
        } else if (word == "mesh") {
            SceneMesh mesh;
            for (int i = 0; i < 3; ++i) read(in, mesh.color[i], where, "colour");
            scene.meshes.push_back(mesh);
        } else if (word == "vertices" || word == "vcolors") {
            if (scene.meshes.empty()) bad(where, "'" + word + "' before 'mesh'");
            std::vector<float>& out = word == "vertices" ? scene.meshes.back().positions : scene.meshes.back().colors;
            float v;
            while (in >> v) out.push_back(v);
            if (!in.eof()) bad(where, "expected numbers after '" + word + "'");
        } else if (word == "triangles") {
            if (scene.meshes.empty()) bad(where, "'triangles' before 'mesh'");
            uint32_t v;
            while (in >> v) scene.meshes.back().indices.push_back(v);
            if (!in.eof()) bad(where, "expected indices after 'triangles'");
        } else if (word == "include") {
            std::string other;
            read(in, other, where, "file name");
            parse(directoryOf(path) + other, scene, depth + 1);
        } else if (word == "item") {
            std::string kind;
            read(in, kind, where, "primitive kind");
            StrataRenderItem item{};
            item.kind = kindFromName(kind, where);
            for (float& v : item.position) read(in, v, where, "position");
            for (float& v : item.scale) read(in, v, where, "scale");
            for (int i = 0; i < 3; ++i) read(in, item.color[i], where, "colour");
            item.color[3] = 1.0f;
            float alpha;
            if (in >> alpha) item.color[3] = alpha;
            scene.items.push_back(item);
        } else {
            bad(where, "unknown directive '" + word + "'");
        }
    }
}

}  // namespace

StrataMesh SceneMesh::view() const {
    StrataMesh m{};
    m.positions = positions.data();
    m.colors = colors.empty() ? nullptr : colors.data();
    m.indices = indices.data();
    m.vertex_count = static_cast<int32_t>(positions.size() / 3);
    m.index_count = static_cast<int32_t>(indices.size());
    for (int i = 0; i < 4; ++i) m.color[i] = color[i];
    return m;
}

Scene loadScene(const std::string& path) {
    Scene scene;
    strata_lighting_defaults(STRATA_LIGHTING_LEGACY, &scene.lighting);
    scene.frame.clear_flags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH;
    scene.frame.clear_color[0] = 0.45f;  // BN's clear colour
    scene.frame.clear_color[1] = 0.65f;
    scene.frame.clear_color[2] = 0.90f;
    scene.frame.clear_color[3] = 1.0f;
    parse(path, scene, 0);
    if (scene.hasTerrain) {
        const size_t samples = static_cast<size_t>(scene.terrain.samples_x) * static_cast<size_t>(scene.terrain.samples_z);
        if (scene.terrainHeights.size() != samples) {
            bad(path, "terrain has " + std::to_string(scene.terrainHeights.size()) + " heights, expected " +
                          std::to_string(samples));
        }
        if (!scene.terrainColors.empty() && scene.terrainColors.size() != samples * 3) {
            bad(path, "terrain has " + std::to_string(scene.terrainColors.size()) + " colour values, expected " +
                          std::to_string(samples * 3));
        }
        scene.terrain.heights = scene.terrainHeights.data();
        scene.terrain.colors = scene.terrainColors.empty() ? nullptr : scene.terrainColors.data();
    }
    for (const SceneMesh& mesh : scene.meshes) {
        if (mesh.positions.size() % 3 != 0) bad(path, "a mesh's vertex values are not a multiple of 3");
        if (!mesh.colors.empty() && mesh.colors.size() != mesh.positions.size()) {
            bad(path, "a mesh has " + std::to_string(mesh.colors.size()) + " colour values, expected " +
                          std::to_string(mesh.positions.size()));
        }
    }
    return scene;
}

}  // namespace strata::scene
