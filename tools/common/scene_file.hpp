// A tiny text format for golden-image scenes, read by strata-golden and by the
// C# primitives harness so both draw exactly the same thing.
//
//   # comment                     ('#' ends any line, text included)
//
// Nothing may follow a directive's last field (or its last option), and an
// optional field that is present must parse.
//
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
//   line <ax ay az> <bx by bz> <r g b a> [on_top]
//                                 (ABI 1.5; on_top submits it with STRATA_LINES_ON_TOP, ABI 1.11)
//   wide_line <ax ay az> <bx by bz> <r g b a> <width_pixels> <world_width> [to <r g b a> <width_pixels> <world_width>]
//             [round] [on_top]
//                                 (ABI 1.13: strata_submit_wide_lines; 'to' gives the end its own colour and
//                                  widths, round is STRATA_LINES_ROUND_CAPS, on_top STRATA_LINES_ON_TOP)
//   rect <x> <y> <w> <h> <r g b a> (ABI 1.11: a filled screen rectangle, pixels from the top left)
//   circle <cx cy cz> <radius> <segments> <r g b a>  (line loop in the XZ plane)
//   point <px py pz> <size_pixels> <world_diameter> <r g b a> [soft] [on_top]
//                                 (ABI 1.5; on_top is STRATA_POINT_ON_TOP, ABI 1.11)
//   text <x> <y> <pixel_size> <r g b a> [shadow] : <text to the end of the line>
//   label <x y z> <dx> <dy> <pixel_size> <r g b a> [shadow] : <text>
//                                 (ABI 1.14: text anchored at a world point, offset dx dy pixels)
//   texture <name> checker <w> <h> <cells_u> <cells_v> <r g b> <r g b> [mipmaps] [clamp_v] [nearest]
//                                 (ABI 1.7; nearest is STRATA_TEXTURE_NEAREST, ABI 1.10)
//   texture <name> globe <w> <h> [mipmaps] [clamp_v] [nearest]
//                                 (a test planet: hued checker, white caps, red prime meridian;
//                                  always clamp_v)
//   sphere_mesh <slices> <stacks> <r g b>   (starts a unit-radius latitude-longitude sphere with planet uvs)
//   uvs <u v ...>                 (the current mesh's texture coordinates)
//   mesh_texture <name>           (the current mesh samples that texture)
//   mesh_unlit                    (the current mesh is STRATA_MESH_UNLIT)
//   mesh_translucent <alpha>      (ABI 1.11: the current mesh is STRATA_MESH_TRANSLUCENT with that alpha)
//   mesh_flat                     (ABI 1.19: the current mesh is STRATA_MESH_FLAT_NORMALS)
//   mesh_hidden                   (ABI 1.19: the current mesh is hidden with strata_set_mesh_visible once added)
//   mesh_transform <tx ty tz> <tilt axis x y z> <tilt_deg> <spin_deg> <scale>
//                                 (model = translate * rotate(tilt axis) * rotate(+Y, spin) * scale)
//   sun date <YYYY-MM-DD> <HH:MM> <utc_offset_hours> <latitude> <longitude>
//                                 (ABI 1.6: places the sun with strata_solar_position)
//   ambient <tint r g b> <lift r g b>
//                                 (ABI 1.16: strata_set_ambient; ambient becomes ambient * tint + lift)
//   sun_intensity <intensity> <tint r g b>
//                                 (ABI 1.16: strata_set_sun_intensity; scales the sun or moon)
//   texture_update <name> <x> <y> <w> <h> <r g b>
//                                 (ABI 1.8: fills that rectangle of a texture with one colour through
//                                  strata_update_texture_rgba8, after the first frame and before the
//                                  captured one; updates apply in file order)
//   expect_ray <ox oy oz> <dx dy dz> <max_distance> <mesh_index|none> [distance]
//                                 (ABI 1.8: strata_raycast_meshes must hit that mesh, counted from 0 in
//                                  file order, at that distance within 0.001, or hit nothing)
//   camera_ortho <eye x y z> <target x y z> <half_height> <near> <far>
//                                 (ABI 1.20: strata_camera_ortho instead of the perspective camera)
//   light spot <px py pz> <range> <dx dy dz> <r g b> <intensity> <inner_deg> <outer_deg> [shadow]
//                                 (ABI 1.20: strata_set_spot_lights; shadow is STRATA_SPOT_LIGHT_CAST_SHADOWS)
//   output [srgb] [reinhard|aces] [exposure <e>]
//                                 (ABI 1.20: strata_set_color_output; at least one option)
//   texture <name> discs <w> <h> <cells_u> <cells_v> <r g b> [options]
//                                 (ABI 1.20 test asset: a round blob per cell, opaque inside and fully
//                                  transparent between, for alpha cutout; texture options as above, plus
//                                  srgb for STRATA_TEXTURE_SRGB)
//   texture <name> bumps <w> <h> <cells_u> <cells_v> [options]
//                                 (ABI 1.20 test asset: a tangent-space normal map of one dome per cell)
//   mesh_cutoff <alpha>           (ABI 1.20: the current mesh's material alpha_cutoff)
//   mesh_emissive <r g b> <strength> [<texture>]
//                                 (ABI 1.20: the current mesh's emission, optionally times a texture)
//   mesh_normal_map <texture> <scale>
//                                 (ABI 1.20: the current mesh's normal map)
//   mesh_double_sided             (ABI 1.20: STRATA_MATERIAL_DOUBLE_SIDED)
//                                 (a mesh with any mesh_* material line gets strata_set_mesh_material once added)
//   light_culling <none|clustered>
//                                 (ABI 1.21: strata_set_light_culling, called before the lights are set,
//                                  so a clustered scene may hold up to STRATA_MAX_CLUSTERED_LIGHTS of each)
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
    bool translucent = false;  // ABI 1.11; the alpha is color[3]
    bool flat = false;         // ABI 1.19: STRATA_MESH_FLAT_NORMALS
    bool hidden = false;       // ABI 1.19: strata_set_mesh_visible(.., 0) after it is added
    bool hasMaterial = false;  // ABI 1.20: strata_set_mesh_material after it is added
    StrataMaterial material{};  // textures are filled in by name when it is added
    std::string emissiveTexture, normalTexture;
    bool hasTransform = false;
    float transform[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    bool textured() const { return !uvs.empty() || !texture.empty() || unlit || translucent || flat; }

    // A StrataMesh pointing into this mesh's vectors (normals computed by the engine).
    StrataMesh view() const;
};

struct Scene {
    int width = 640;
    int height = 360;
    float eye[3] = {0, 10, 20};
    float target[3] = {0, 0, 0};
    float fovY = 60.0f;
    bool ortho = false;  // ABI 1.20: camera_ortho
    float orthoHalfHeight = 1.0f;
    float nearPlane = 0.1f;
    float farPlane = 300.0f;
    StrataLighting lighting{};
    bool hasSunDate = false;  // ABI 1.6
    double sunUnixSeconds = 0.0;
    double sunLatitude = 0.0;
    double sunLongitude = 0.0;
    bool hasAmbient = false;  // ABI 1.16
    float ambientTint[3] = {1, 1, 1};
    float ambientLift[3] = {0, 0, 0};
    bool hasSunIntensity = false;  // ABI 1.16
    float sunIntensity = 1.0f;
    float sunTint[3] = {1, 1, 1};
    StrataFrameDesc frame{};
    std::vector<StrataRenderItem> items;
    std::vector<StrataLineVertex> lines;
    std::vector<StrataLineVertex> linesOnTop;  // ABI 1.11
    std::vector<StrataRect> rects;             // ABI 1.11
    std::vector<StrataWideLineVertex> wideLines[4];  // ABI 1.13, indexed by their STRATA_LINES_* flags
    std::vector<StrataPoint> points;
    struct Text {
        StrataText text{};  // utf8 is set from `value` when submitted
        std::string value;
    };
    std::vector<Text> texts;
    struct Label {  // ABI 1.14
        float anchor[3] = {0, 0, 0};
        Text text;
    };
    std::vector<Label> labels;
    std::vector<StrataPointLight> pointLights;
    std::vector<StrataSpotLight> spotLights;  // ABI 1.20
    int32_t lightCulling = -1;                // ABI 1.21: STRATA_LIGHT_CULLING_*, or -1 for unset
    bool hasOutput = false;                   // ABI 1.20
    uint32_t outputFlags = 0;
    float outputExposure = 1.0f;
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
