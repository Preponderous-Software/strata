// A tiny text format for golden-image scenes, read by strata-golden and by the
// C# primitives harness so both draw exactly the same thing.
//
//   # comment                     ('#' ends any line, text included)
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
//   lighting space <ambient>                                         (ABI 1.5)
//   line <ax ay az> <bx by bz> <r g b a>                             (ABI 1.5)
//   circle <cx cy cz> <radius> <segments> <r g b a>  (line loop in the XZ plane)
//   point <px py pz> <size_pixels> <world_diameter> <r g b a> [soft] (ABI 1.5)
//   text <x> <y> <pixel_size> <r g b a> [shadow] : <text to the end of the line>
//   texture <name> checker <w> <h> <cells_u> <cells_v> <r g b> <r g b> [mipmaps] [clamp_v] [nearest]
//                                 (ABI 1.7; nearest is STRATA_TEXTURE_NEAREST, ABI 1.10)
//   texture <name> globe <w> <h> [mipmaps]  (a test planet: hued checker, white caps, red prime meridian;
//                                            always clamp_v)
//   sphere_mesh <slices> <stacks> <r g b>   (starts a unit-radius latitude-longitude sphere with planet uvs)
//   uvs <u v ...>                 (the current mesh's texture coordinates)
//   mesh_texture <name>           (the current mesh samples that texture)
//   mesh_unlit                    (the current mesh is STRATA_MESH_UNLIT)
//   mesh_transform <tx ty tz> <tilt axis x y z> <tilt_deg> <spin_deg> <scale>
//                                 (model = translate * rotate(tilt axis) * rotate(+Y, spin) * scale)
//   sun date <YYYY-MM-DD> <HH:MM> <utc_offset_hours> <latitude> <longitude>
//                                 (ABI 1.6: places the sun with strata_solar_position)
//   texture_update <name> <x> <y> <w> <h> <r g b>
//                                 (ABI 1.8: fills that rectangle of a texture with one colour through
//                                  strata_update_texture_rgba8, after the first frame and before the
//                                  captured one; updates apply in file order)
//   expect_ray <ox oy oz> <dx dy dz> <max_distance> <mesh_index|none> [distance]
//                                 (ABI 1.8: strata_raycast_meshes must hit that mesh, counted from 0 in
//                                  file order, at that distance within 0.001, or hit nothing)
//   expect_depth <x> <y> <distance|inf>
//                                 (ABI 1.9: strata_read_depth_f32 must read that planar distance at pixel
//                                  (x, y) of the captured frame, top row first, within 0.1 %, or +infinity;
//                                  skipped, with a note, where depth readback is unsupported)
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
    std::vector<float> uvs;   // ABI 1.7
    std::string texture;      // a scene texture's name, or empty
    bool unlit = false;
    bool hasTransform = false;
    float transform[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    bool textured() const { return !uvs.empty() || !texture.empty() || unlit; }

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
    bool hasSunDate = false;  // ABI 1.6
    double sunUnixSeconds = 0.0;
    double sunLatitude = 0.0;
    double sunLongitude = 0.0;
    StrataFrameDesc frame{};
    std::vector<StrataRenderItem> items;
    std::vector<StrataLineVertex> lines;
    std::vector<StrataPoint> points;
    struct Text {
        StrataText text{};  // utf8 is set from `value` when submitted
        std::string value;
    };
    std::vector<Text> texts;
    std::vector<StrataPointLight> pointLights;
    bool hasTerrain = false;
    StrataTerrainTile terrain{};  // heights/colors point into the vectors below once loaded
    std::vector<float> terrainHeights;
    std::vector<float> terrainColors;
    std::vector<SceneMesh> meshes;
    struct Texture {  // ABI 1.7
        std::string name;
        int width = 0, height = 0;
        uint32_t flags = 0;
        std::vector<uint8_t> rgba;
    };
    std::vector<Texture> textures;
    struct TextureUpdate {  // ABI 1.8
        std::string name;
        int x = 0, y = 0, width = 0, height = 0;
        std::vector<uint8_t> rgba;
    };
    std::vector<TextureUpdate> textureUpdates;
    struct RayExpectation {  // ABI 1.8
        float origin[3] = {0, 0, 0};
        float direction[3] = {0, 0, -1};
        float maxDistance = 0.0f;
        int mesh = -1;  // index into meshes, or -1 for no hit
        bool hasDistance = false;
        float distance = 0.0f;
        int line = 0;
    };
    std::vector<RayExpectation> rayExpectations;
    struct DepthExpectation {  // ABI 1.9
        int x = 0, y = 0;
        float distance = 0.0f;  // +infinity: nothing drawn there
        int line = 0;
    };
    std::vector<DepthExpectation> depthExpectations;
};

// Throws std::runtime_error with "file:line: message" on a malformed scene. The
// checks made once the whole file is read (terrain heights and colours, mesh
// vertex, uv and colour counts, texture_update names) report "file: message".
Scene loadScene(const std::string& path);

}  // namespace strata::scene
