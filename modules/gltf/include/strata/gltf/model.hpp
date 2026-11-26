// glTF 2.0 scenes read into plain CPU-side data: every triangle primitive of
// the chosen scene in world space (node transforms applied), its material,
// and the decoded images its textures use. No GL and no engine: plan.hpp
// turns a Model into Strata meshes and textures, upload.hpp sends those to an
// engine.
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace strata::gltf {

// Thrown by loadFile/loadMemory. kind says which StrataStatus the C API maps
// it to.
class Error : public std::runtime_error {
public:
    enum class Kind { Io, Invalid, Unsupported };
    Error(Kind kind, const std::string& what) : std::runtime_error(what), kind_(kind) {}
    Kind kind() const { return kind_; }

private:
    Kind kind_;
};

enum class AlphaMode : int32_t { Opaque = 0, Mask = 1, Blend = 2 };

const char* alphaModeName(AlphaMode mode);

// A decoded image: RGBA8, rows top to bottom, texels exactly as stored in
// the file (a glTF base colour image is sRGB-encoded).
struct Image {
    std::string name;  // the image's name, else its URI, else "image <n>"
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;  // empty when the image could not be decoded (see Model::warnings)
};

// The sampler state Strata can express (STRATA_TEXTURE_* flags).
struct Sampler {
    bool clampU = false;    // wrapS is CLAMP_TO_EDGE (mirrored repeat is treated as repeat)
    bool clampV = false;    // wrapT is CLAMP_TO_EDGE
    bool nearest = false;   // magFilter is NEAREST
    bool mipmaps = true;    // minFilter is unset or one of the *_MIPMAP_* filters
};

struct Texture {
    int image = -1;  // index into Model::images; -1 when the texture has no usable source
    Sampler sampler;
};

struct Material {
    std::string name;
    float baseColorFactor[4] = {1, 1, 1, 1};  // linear RGBA, as glTF defines it
    int baseColorTexture = -1;                // index into Model::textures, or -1
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;                 // meaningful for AlphaMode::Mask
    bool doubleSided = false;
    bool unlit = false;                       // KHR_materials_unlit
    float emissive[3] = {0, 0, 0};            // emissiveFactor times KHR_materials_emissive_strength, linear
    bool emissiveTexture = false;             // an emissiveTexture is present (recorded, not sampled)

    bool isEmissive() const { return emissive[0] > 0 || emissive[1] > 0 || emissive[2] > 0; }
};

// One triangle primitive under one node, in world space.
struct Primitive {
    std::string node;   // the node's name, else "node <n>"
    std::string mesh;   // the mesh's name, else "mesh <n>"
    int material = -1;  // index into Model::materials, or -1 for glTF's default material
    std::vector<float> positions;  // 3 per vertex, world space
    std::vector<float> normals;    // 3 per vertex, world space, unit length (made flat when the file has none)
    std::vector<float> colors;     // empty, or 4 per vertex: COLOR_0 as linear RGBA
    std::vector<float> uvs;        // empty, or 2 per vertex: the base colour texture's set (KHR_texture_transform applied)
    std::vector<uint32_t> indices; // triangle list, counter-clockwise seen from the front

    size_t vertexCount() const { return positions.size() / 3; }
    size_t triangleCount() const { return indices.size() / 3; }
};

struct Model {
    std::string source;  // the path loaded, or "<memory>"
    std::vector<Primitive> primitives;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<Image> images;
    float boundsMin[3] = {0, 0, 0};  // world-space bounds of every position; all zero when empty
    float boundsMax[3] = {0, 0, 0};
    int nodesVisited = 0;
    int primitivesSkipped = 0;          // points, lines, compressed or malformed primitives
    std::vector<std::string> warnings;  // one line each: what was skipped or approximated, and why

    size_t vertexCount() const;
    size_t triangleCount() const;
};

struct LoadOptions {
    int scene = -1;            // -1: the file's default scene, else scene 0, else every root node
    bool decodeImages = true;  // false leaves every Image::rgba empty (faster, for inspection)
};

// Reads a .gltf (JSON, with embedded data: URIs or external .bin/.png/.jpg
// files beside it) or a .glb (binary container). Throws Error.
Model loadFile(const std::string& path, const LoadOptions& options = {});

// The same from memory. External URIs are resolved against baseDirectory
// (may be empty, then any external URI fails to load).
Model loadMemory(const void* data, size_t size, const std::string& baseDirectory, const LoadOptions& options = {});

}  // namespace strata::gltf
