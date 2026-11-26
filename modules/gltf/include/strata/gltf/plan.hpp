// Fits a Model into what Strata can hold: at most STRATA_MAX_MESHES meshes,
// STRATA_MAX_TEXTURES textures of at most STRATA_MAX_TEXTURE_SIZE texels a
// side, and STRATA_MAX_MESH_VERTICES vertices a mesh. The result is a Plan:
// finished vertex arrays and RGBA8 images, ready to pass to the C ABI
// unchanged (upload.hpp does that). Pure CPU work; no engine needed.
//
// What it does, in order (each step only when the previous one is not enough):
//
//   1. One Strata mesh per glTF primitive, one Strata texture per glTF
//      texture used.
//   2. Merge: primitives that draw the same way (same texture, same
//      opaque/translucent/unlit flags and the same mesh alpha) are
//      concatenated into one mesh. Per-material base colour factors are baked
//      into per-vertex colours, so different materials can share a mesh.
//   3. Atlas: textures whose uvs stay inside 0..1 are packed into atlas pages
//      (with an edge-extended gutter), scaled down together until they fit,
//      and their uvs remapped. Textures that repeat (uvs outside 0..1) keep a
//      texture of their own while the budget lasts; past it, their primitives
//      lose the texture and take its average colour instead.
//
// Every step taken is recorded in Plan::report.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "strata/gltf/model.hpp"

namespace strata::gltf {

// How glTF's colours become Strata's. Strata draws colours and texels as
// given, with no sRGB decode and no output encode (ARCHITECTURE.md, "Colour
// space"), and a PNG/screen shows the result as sRGB.
enum class ColorPolicy : int32_t {
    // Texels as stored (already sRGB); linear factors and vertex colours are
    // sRGB-encoded first, so unlit surfaces show what a glTF viewer shows.
    Display = 0,
    // Texels sRGB-decoded to linear, factors and vertex colours as given: for
    // a host that encodes Strata's output itself.
    Linear = 1,
    // Everything exactly as stored in the file, with no conversion at all.
    AsStored = 2,
};

// What alphaMode MASK becomes. Strata 1.19 has no alpha test.
enum class MaskPolicy : int32_t {
    Blend = 0,   // a STRATA_MESH_TRANSLUCENT mesh: cut-out texels disappear, but the mesh writes no depth and casts no shadow
    Opaque = 1,  // drawn opaque: texel alpha ignored, the cutout shows as solid
};

// What an emissive material (emissiveFactor not black) becomes.
enum class EmissivePolicy : int32_t {
    Unlit = 0,   // a STRATA_MESH_UNLIT mesh coloured base colour + emissive: it glows, unshaded and casting no shadow
    Ignore = 1,  // drawn lit from its base colour alone; the emissive colour is only recorded
};

struct BudgetOptions {
    int maxMeshes = 256;          // STRATA_MAX_MESHES, or fewer to leave room for the host's own
    int maxTextures = 256;        // STRATA_MAX_TEXTURES, or fewer
    int maxTextureSize = 8192;    // STRATA_MAX_TEXTURE_SIZE; larger images are scaled down
    int atlasSize = 4096;         // side of an atlas page, when atlasing is needed
    int maxMeshVertices = 16777216;  // STRATA_MAX_MESH_VERTICES; merged meshes are split past it
    bool alwaysMerge = false;     // merge (step 2) even when one mesh per primitive fits
    bool backFaces = true;        // give doubleSided opaque primitives a reversed copy of each triangle
    bool mipmaps = true;          // honour mipmapping samplers (STRATA_TEXTURE_MIPMAPS)
    ColorPolicy colors = ColorPolicy::Display;
    MaskPolicy mask = MaskPolicy::Blend;
    EmissivePolicy emissive = EmissivePolicy::Unlit;
};

// One RGBA8 image to create with strata_create_texture_rgba8.
struct PlannedTexture {
    std::string label;  // the glTF image's name, or "atlas page <n>"
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
    uint32_t flags = 0;  // STRATA_TEXTURE_*
    std::vector<int> sourceTextures;  // glTF texture indices it holds
};

// One mesh to add with strata_add_textured_mesh. Arrays are complete and in
// world space; colors are RGBA (pass STRATA_MESH_COLORS_RGBA).
struct PlannedMesh {
    std::string label;             // "node/mesh" for one primitive, else "merged <n>"
    std::vector<float> positions;  // 3 per vertex
    std::vector<float> normals;    // 3 per vertex
    std::vector<float> colors;     // 4 per vertex (alpha validated, then ignored by Strata)
    std::vector<float> uvs;        // empty, or 2 per vertex
    std::vector<uint32_t> indices;
    int texture = -1;              // index into Plan::textures, or -1
    uint32_t flags = 0;            // STRATA_MESH_* (always includes STRATA_MESH_COLORS_RGBA)
    float color[4] = {1, 1, 1, 1}; // mesh colour; [3] is the translucent alpha
    // The glTF material(s) it was made from, for hosts and later material work.
    int material = -1;             // the single glTF material, -1 for the default one, -2 when several were merged
    std::vector<int> materials;    // every glTF material index merged in (-1 = default)
    AlphaMode alphaMode = AlphaMode::Opaque;  // the source alphaMode (MASK kept as MASK even when drawn as BLEND)
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    float emissive[3] = {0, 0, 0}; // the source emissive colour (linear; the brightest when several were merged)
    int primitives = 0;            // glTF primitives merged into it

    size_t vertexCount() const { return positions.size() / 3; }
    size_t triangleCount() const { return indices.size() / 3; }
};

struct PlanReport {
    int primitivesIn = 0;
    int texturesIn = 0;        // glTF textures referenced by a drawn material
    int meshesOut = 0;
    int texturesOut = 0;
    bool merged = false;       // step 2 ran
    bool atlased = false;      // step 3 ran
    int atlasPages = 0;
    float atlasScale = 1.0f;   // texel scale applied to atlased images (1 = full size)
    int texturesDownscaled = 0;   // images scaled down to maxTextureSize
    int repeatingDropped = 0;     // repeating textures replaced by their average colour
    int primitivesUntextured = 0; // primitives that lost their texture that way
    int maskAsBlend = 0;          // primitives with alphaMode MASK drawn translucent
    int maskAsOpaque = 0;         // primitives with alphaMode MASK drawn opaque
    int emissiveUnlit = 0;        // emissive primitives drawn unlit (EmissivePolicy::Unlit)
    int backFacesAdded = 0;       // triangles added for doubleSided primitives
    int meshesSplit = 0;          // extra meshes made by splitting at maxMeshVertices
    size_t vertices = 0;
    size_t triangles = 0;
    std::vector<std::string> notes;  // the Model's warnings, then the plan's own

    std::string text() const;  // a short human-readable summary, one fact per line
    std::string json() const;  // the same as one JSON object
};

struct Plan {
    std::vector<PlannedTexture> textures;
    std::vector<PlannedMesh> meshes;
    PlanReport report;
};

// Throws Error(Unsupported) only when even full merging and atlasing cannot
// meet maxMeshes/maxTextures (for example maxMeshes 1 with an opaque and a
// translucent material).
Plan plan(const Model& model, const BudgetOptions& options = {});

// sRGB transfer functions on 0..1 values (exposed for tests).
float srgbToLinear(float c);
float linearToSrgb(float c);

}  // namespace strata::gltf
