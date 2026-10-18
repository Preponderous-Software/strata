// A tiny text format for golden-image scenes, read by strata-golden and by the
// C# primitives harness so both draw exactly the same thing.
//
//   # comment
//   size <width> <height>
//   camera <eye x y z> <target x y z> <fov_y_degrees> <near> <far>
//   lighting legacy <dir x y z> <ambient>
//   lighting sun <time_of_day> <latitude> <cascades> <map_size> <shadow_distance> <lambda> <fog 0|1>
//   clear <r> <g> <b> <a> [sky]
//   light point <px py pz> <range> <r g b> <intensity> [shadow]
//   terrain <ox oy oz> <cell_size> <samples_x> <samples_z> <r g b>
//   heights <h ...>               (any number per line, x fastest, until samples_x * samples_z)
//   colors <r g b ...>            (optional: one linear RGB triple per sample)
//   mesh <r g b>                  (starts a static mesh in that colour)
//   vertices <x y z ...>          (any number per line: the current mesh's positions)
//   vcolors <r g b ...>           (optional: one linear RGB triple per vertex)
//   triangles <i ...>             (indices, counter-clockwise seen from outside)
//   include <file>                (relative to the including file)
//   item <cube|capsule|cylinder|sphere> <px py pz> <sx sy sz> <r g b> [a]
#pragma once

#include <string>
#include <vector>

#include "strata/strata.h"

namespace strata::scene {

struct SceneMesh {
    float color[4] = {1, 1, 1, 1};
    std::vector<float> positions;
    std::vector<float> colors;
    std::vector<uint32_t> indices;

    // A StrataMesh pointing into this mesh's vectors (normals computed by the engine).
    StrataMesh view() const;
};

struct Scene {
    int width = 640;
    int height = 360;
    float eye[3] = {0, 10, 20};
    float target[3] = {0, 0, 0};
    float fovY = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 300.0f;
    StrataLighting lighting{};
    StrataFrameDesc frame{};
    std::vector<StrataRenderItem> items;
    std::vector<StrataPointLight> pointLights;
    bool hasTerrain = false;
    StrataTerrainTile terrain{};  // heights/colors point into the vectors below once loaded
    std::vector<float> terrainHeights;
    std::vector<float> terrainColors;
    std::vector<SceneMesh> meshes;
};

// Throws std::runtime_error with "file:line: message" on a malformed scene.
Scene loadScene(const std::string& path);

}  // namespace strata::scene
