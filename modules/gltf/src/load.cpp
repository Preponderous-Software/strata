// glTF 2.0 -> Model: parsing with cgltf, images with stb_image.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "cgltf.h"
#include "image_decode.hpp"
#include "strata/gltf/model.hpp"

namespace strata::gltf {

const char* alphaModeName(AlphaMode mode) {
    switch (mode) {
        case AlphaMode::Opaque: return "OPAQUE";
        case AlphaMode::Mask: return "MASK";
        case AlphaMode::Blend: return "BLEND";
    }
    return "?";
}

size_t Model::vertexCount() const {
    size_t n = 0;
    for (const Primitive& p : primitives) n += p.vertexCount();
    return n;
}

size_t Model::triangleCount() const {
    size_t n = 0;
    for (const Primitive& p : primitives) n += p.triangleCount();
    return n;
}

namespace {

using Mat4 = std::array<float, 16>;  // column-major, as cgltf writes it

struct DataDeleter {
    void operator()(cgltf_data* data) const { cgltf_free(data); }
};
using DataPtr = std::unique_ptr<cgltf_data, DataDeleter>;

const char* resultName(cgltf_result result) {
    switch (result) {
        case cgltf_result_success: return "success";
        case cgltf_result_data_too_short: return "data too short";
        case cgltf_result_unknown_format: return "not glTF (unknown format)";
        case cgltf_result_invalid_json: return "invalid JSON";
        case cgltf_result_invalid_gltf: return "invalid glTF";
        case cgltf_result_invalid_options: return "invalid options";
        case cgltf_result_file_not_found: return "file not found";
        case cgltf_result_io_error: return "I/O error";
        case cgltf_result_out_of_memory: return "out of memory";
        case cgltf_result_legacy_gltf: return "glTF 1.0 (only 2.0 is supported)";
        default: return "unknown error";
    }
}

std::string orName(const char* name, const char* kind, size_t index) {
    if (name && *name) return name;
    return std::string(kind) + " " + std::to_string(index);
}

int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

bool decodeBase64(const char* text, std::vector<uint8_t>& out) {
    out.clear();
    uint32_t buffer = 0;
    int bits = 0;
    for (const char* p = text; *p; ++p) {
        if (*p == '=') break;
        const int v = base64Value(*p);
        if (v < 0) {
            if (*p == '\n' || *p == '\r' || *p == ' ') continue;
            return false;
        }
        buffer = (buffer << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xffu));
        }
    }
    return true;
}

bool readWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

std::string directoryOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// Upper 3x3 of a column-major 4x4: inverse transpose for normals, and its determinant.
void normalMatrix(const Mat4& m, float out[9], float* determinant) {
    const float a = m[0], b = m[4], c = m[8];
    const float d = m[1], e = m[5], f = m[9];
    const float g = m[2], h = m[6], i = m[10];
    const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const float D = -(b * i - c * h), E = a * i - c * g, F = -(a * h - b * g);
    const float G = b * f - c * e, H = -(a * f - c * d), I = a * e - b * d;
    const float det = a * A + b * B + c * C;
    *determinant = det;
    const float s = det != 0.0f ? 1.0f / det : 0.0f;
    // inverse = adjugate / det = cofactor^T / det; inverse transpose = cofactor / det.
    // out is row-major: out[row * 3 + col].
    out[0] = A * s; out[1] = B * s; out[2] = C * s;
    out[3] = D * s; out[4] = E * s; out[5] = F * s;
    out[6] = G * s; out[7] = H * s; out[8] = I * s;
}

void normalize3(float* v, const float fallback[3]) {
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1e-20f && std::isfinite(len)) {
        v[0] /= len; v[1] /= len; v[2] /= len;
    } else {
        v[0] = fallback[0]; v[1] = fallback[1]; v[2] = fallback[2];
    }
}

const cgltf_attribute* findAttribute(const cgltf_primitive& prim, cgltf_attribute_type type, int index) {
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        if (prim.attributes[i].type == type && prim.attributes[i].index == index) return &prim.attributes[i];
    }
    return nullptr;
}

bool unpack(const cgltf_accessor* accessor, size_t components, size_t expectCount, std::vector<float>& out) {
    if (!accessor || accessor->count != expectCount) return false;
    if (cgltf_num_components(accessor->type) != components) return false;
    out.assign(expectCount * components, 0.0f);
    const cgltf_size written = cgltf_accessor_unpack_floats(accessor, out.data(), out.size());
    if (written != out.size()) return false;
    for (float v : out) {
        if (!std::isfinite(v)) return false;
    }
    return true;
}

struct Loader {
    const cgltf_data* data = nullptr;
    std::string baseDirectory;
    LoadOptions options;
    Model model;

    void warn(const std::string& line) { model.warnings.push_back(line); }

    void readMaterials() {
        model.materials.resize(data->materials_count);
        for (cgltf_size i = 0; i < data->materials_count; ++i) {
            const cgltf_material& src = data->materials[i];
            Material& dst = model.materials[i];
            dst.name = orName(src.name, "material", i);
            const cgltf_texture_view* view = nullptr;
            if (src.has_pbr_metallic_roughness) {
                std::copy(src.pbr_metallic_roughness.base_color_factor,
                          src.pbr_metallic_roughness.base_color_factor + 4, dst.baseColorFactor);
                view = &src.pbr_metallic_roughness.base_color_texture;
            } else if (src.has_pbr_specular_glossiness) {
                // KHR_materials_pbrSpecularGlossiness: diffuse is the closest thing to a base colour.
                std::copy(src.pbr_specular_glossiness.diffuse_factor, src.pbr_specular_glossiness.diffuse_factor + 4,
                          dst.baseColorFactor);
                view = &src.pbr_specular_glossiness.diffuse_texture;
            }
            if (view && view->texture) dst.baseColorTexture = static_cast<int>(cgltf_texture_index(data, view->texture));
            switch (src.alpha_mode) {
                case cgltf_alpha_mode_mask: dst.alphaMode = AlphaMode::Mask; break;
                case cgltf_alpha_mode_blend: dst.alphaMode = AlphaMode::Blend; break;
                default: dst.alphaMode = AlphaMode::Opaque; break;
            }
            dst.alphaCutoff = src.alpha_cutoff;
            dst.doubleSided = src.double_sided != 0;
            dst.unlit = src.unlit != 0;
            const float strength = src.has_emissive_strength ? src.emissive_strength.emissive_strength : 1.0f;
            for (int k = 0; k < 3; ++k) {
                const float e = src.emissive_factor[k] * strength;
                dst.emissive[k] = std::isfinite(e) ? std::max(e, 0.0f) : 0.0f;
            }
            dst.emissiveTexture = src.emissive_texture.texture != nullptr;
            for (float& f : dst.baseColorFactor) f = std::isfinite(f) ? std::max(f, 0.0f) : 1.0f;
            dst.baseColorFactor[3] = std::min(dst.baseColorFactor[3], 1.0f);
        }
    }

    void readTextures() {
        model.textures.resize(data->textures_count);
        for (cgltf_size i = 0; i < data->textures_count; ++i) {
            const cgltf_texture& src = data->textures[i];
            Texture& dst = model.textures[i];
            if (src.image) {
                dst.image = static_cast<int>(cgltf_image_index(data, src.image));
            } else if (src.has_basisu || src.has_webp) {
                warn("texture " + std::to_string(i) + ": only KHR_texture_basisu/EXT_texture_webp sources, which are not decoded");
            } else {
                warn("texture " + std::to_string(i) + ": no image source");
            }
            if (src.sampler) {
                const cgltf_sampler& s = *src.sampler;
                dst.sampler.clampU = s.wrap_s == cgltf_wrap_mode_clamp_to_edge;
                dst.sampler.clampV = s.wrap_t == cgltf_wrap_mode_clamp_to_edge;
                dst.sampler.nearest = s.mag_filter == cgltf_filter_type_nearest;
                dst.sampler.mipmaps = !(s.min_filter == cgltf_filter_type_nearest || s.min_filter == cgltf_filter_type_linear);
            }
        }
    }

    // Decodes the images some material's base colour texture uses.
    void readImages() {
        model.images.resize(data->images_count);
        std::vector<bool> used(data->images_count, false);
        for (const Material& m : model.materials) {
            if (m.baseColorTexture < 0) continue;
            const int image = model.textures[static_cast<size_t>(m.baseColorTexture)].image;
            if (image >= 0) used[static_cast<size_t>(image)] = true;
        }
        for (cgltf_size i = 0; i < data->images_count; ++i) {
            const cgltf_image& src = data->images[i];
            Image& dst = model.images[i];
            dst.name = src.name && *src.name ? src.name : (src.uri && std::strncmp(src.uri, "data:", 5) != 0 ? src.uri : "image " + std::to_string(i));
            if (!used[i] || !options.decodeImages) continue;
            std::vector<uint8_t> owned;
            const uint8_t* bytes = nullptr;
            size_t size = 0;
            if (src.buffer_view) {
                const cgltf_buffer_view& v = *src.buffer_view;
                if (!v.buffer || !v.buffer->data || v.offset + v.size > v.buffer->size) {
                    warn("image '" + dst.name + "': its buffer view's data is missing");
                    continue;
                }
                bytes = static_cast<const uint8_t*>(v.buffer->data) + v.offset;
                size = v.size;
            } else if (src.uri && std::strncmp(src.uri, "data:", 5) == 0) {
                const char* comma = std::strchr(src.uri, ',');
                const std::string header = comma ? std::string(static_cast<const char*>(src.uri), comma) : std::string();
                if (!comma || header.find(";base64") == std::string::npos || !decodeBase64(comma + 1, owned)) {
                    warn("image '" + dst.name + "': unreadable data: URI (only base64 is supported)");
                    continue;
                }
                bytes = owned.data();
                size = owned.size();
            } else if (src.uri) {
                std::string relative = src.uri;
                std::vector<char> decoded(relative.begin(), relative.end());
                decoded.push_back('\0');
                cgltf_decode_uri(decoded.data());
                const std::string path = baseDirectory + decoded.data();
                if (baseDirectory.empty() && model.source == "<memory>") {
                    warn("image '" + dst.name + "': external URI with no base directory");
                    continue;
                }
                if (!readWholeFile(path, owned)) {
                    warn("image '" + dst.name + "': cannot read " + path);
                    continue;
                }
                bytes = owned.data();
                size = owned.size();
            } else {
                warn("image '" + dst.name + "': no uri and no bufferView");
                continue;
            }
            std::string error;
            if (!decodeImage(bytes, size, dst.width, dst.height, dst.rgba, &error)) {
                warn("image '" + dst.name + "': " + error);
                dst.width = dst.height = 0;
                dst.rgba.clear();
            }
        }
    }

    void readPrimitive(const cgltf_node& node, size_t nodeIndex, const cgltf_mesh& mesh, size_t meshIndex,
                       size_t primIndex, const Mat4& world) {
        const cgltf_primitive& prim = mesh.primitives[primIndex];
        const std::string where = orName(node.name, "node", nodeIndex) + "/" + orName(mesh.name, "mesh", meshIndex) +
                                  " primitive " + std::to_string(primIndex);
        auto skip = [&](const std::string& why) {
            ++model.primitivesSkipped;
            warn(where + ": skipped (" + why + ")");
        };
        if (prim.has_draco_mesh_compression) return skip("KHR_draco_mesh_compression is not decoded");
        if (prim.type != cgltf_primitive_type_triangles && prim.type != cgltf_primitive_type_triangle_strip &&
            prim.type != cgltf_primitive_type_triangle_fan) {
            return skip("points or lines; only triangles are drawn");
        }
        const cgltf_attribute* posAttr = findAttribute(prim, cgltf_attribute_type_position, 0);
        if (!posAttr || !posAttr->data) return skip("no POSITION");
        const size_t vertexCount = posAttr->data->count;
        if (vertexCount < 3) return skip("fewer than 3 vertices");

        Primitive out;
        out.node = orName(node.name, "node", nodeIndex);
        out.mesh = orName(mesh.name, "mesh", meshIndex);
        out.material = prim.material ? static_cast<int>(cgltf_material_index(data, prim.material)) : -1;

        std::vector<float> positions;
        if (!unpack(posAttr->data, 3, vertexCount, positions)) return skip("POSITION is not readable VEC3 data");

        std::vector<float> normals;
        if (const cgltf_attribute* a = findAttribute(prim, cgltf_attribute_type_normal, 0)) {
            if (!unpack(a->data, 3, vertexCount, normals)) {
                normals.clear();
                warn(where + ": NORMAL unreadable, flat normals made instead");
            }
        }
        if (const cgltf_attribute* a = findAttribute(prim, cgltf_attribute_type_color, 0)) {
            const size_t comps = a->data ? cgltf_num_components(a->data->type) : 0;
            std::vector<float> c;
            if ((comps == 3 || comps == 4) && unpack(a->data, comps, vertexCount, c)) {
                out.colors.resize(vertexCount * 4);
                for (size_t v = 0; v < vertexCount; ++v) {
                    for (size_t k = 0; k < 3; ++k) out.colors[v * 4 + k] = std::max(c[v * comps + k], 0.0f);
                    out.colors[v * 4 + 3] = comps == 4 ? std::clamp(c[v * comps + 3], 0.0f, 1.0f) : 1.0f;
                }
            } else {
                warn(where + ": COLOR_0 unreadable, ignored");
            }
        }

        // The base colour texture's uv set and KHR_texture_transform.
        const cgltf_texture_view* view = nullptr;
        if (prim.material) {
            if (prim.material->has_pbr_metallic_roughness) view = &prim.material->pbr_metallic_roughness.base_color_texture;
            else if (prim.material->has_pbr_specular_glossiness) view = &prim.material->pbr_specular_glossiness.diffuse_texture;
            if (view && !view->texture) view = nullptr;
        }
        if (view) {
            int set = view->texcoord;
            if (view->has_transform && view->transform.has_texcoord) set = view->transform.texcoord;
            const cgltf_attribute* a = findAttribute(prim, cgltf_attribute_type_texcoord, set);
            if (a && unpack(a->data, 2, vertexCount, out.uvs)) {
                if (view->has_transform) {
                    const cgltf_texture_transform& t = view->transform;
                    const float c = std::cos(t.rotation), s = std::sin(t.rotation);
                    for (size_t v = 0; v < vertexCount; ++v) {
                        const float u = out.uvs[v * 2] * t.scale[0], w = out.uvs[v * 2 + 1] * t.scale[1];
                        out.uvs[v * 2] = c * u + s * w + t.offset[0];
                        out.uvs[v * 2 + 1] = -s * u + c * w + t.offset[1];
                    }
                }
            } else {
                out.uvs.clear();
                warn(where + ": TEXCOORD_" + std::to_string(set) + " missing or unreadable, drawn untextured");
            }
        }

        // Indices, as a plain triangle list.
        std::vector<uint32_t> raw;
        if (prim.indices) {
            raw.resize(prim.indices->count);
            for (cgltf_size i = 0; i < prim.indices->count; ++i) {
                const cgltf_size index = cgltf_accessor_read_index(prim.indices, i);
                if (index >= vertexCount) return skip("an index is out of range");
                raw[i] = static_cast<uint32_t>(index);
            }
        } else {
            raw.resize(vertexCount);
            for (size_t i = 0; i < vertexCount; ++i) raw[i] = static_cast<uint32_t>(i);
        }
        std::vector<uint32_t>& tri = out.indices;
        if (prim.type == cgltf_primitive_type_triangles) {
            tri.assign(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(raw.size() / 3 * 3));
        } else if (prim.type == cgltf_primitive_type_triangle_strip) {
            for (size_t i = 2; i < raw.size(); ++i) {
                if (i % 2 == 0) tri.insert(tri.end(), {raw[i - 2], raw[i - 1], raw[i]});
                else tri.insert(tri.end(), {raw[i - 1], raw[i - 2], raw[i]});
            }
        } else {
            for (size_t i = 2; i < raw.size(); ++i) tri.insert(tri.end(), {raw[0], raw[i - 1], raw[i]});
        }
        if (tri.empty()) return skip("no whole triangles");

        float nm[9];
        float det = 1.0f;
        normalMatrix(world, nm, &det);
        if (det == 0.0f || !std::isfinite(det)) return skip("its node transform is singular");

        // Positions to world space.
        out.positions.resize(vertexCount * 3);
        for (size_t v = 0; v < vertexCount; ++v) {
            const float x = positions[v * 3], y = positions[v * 3 + 1], z = positions[v * 3 + 2];
            for (int r = 0; r < 3; ++r) {
                out.positions[v * 3 + static_cast<size_t>(r)] =
                    world[static_cast<size_t>(r)] * x + world[static_cast<size_t>(4 + r)] * y +
                    world[static_cast<size_t>(8 + r)] * z + world[static_cast<size_t>(12 + r)];
            }
        }
        // A mirroring transform turns counter-clockwise into clockwise.
        if (det < 0.0f) {
            for (size_t t = 0; t < tri.size(); t += 3) std::swap(tri[t + 1], tri[t + 2]);
        }

        const float up[3] = {0, 1, 0};
        if (!normals.empty()) {
            out.normals.resize(vertexCount * 3);
            for (size_t v = 0; v < vertexCount; ++v) {
                const float* n = &normals[v * 3];
                float* o = &out.normals[v * 3];
                for (int r = 0; r < 3; ++r) o[r] = nm[r * 3] * n[0] + nm[r * 3 + 1] * n[1] + nm[r * 3 + 2] * n[2];
                normalize3(o, up);
            }
        } else {
            // glTF: no normals means flat shading. Every triangle gets its own three vertices.
            Primitive flat;
            flat.node = out.node;
            flat.mesh = out.mesh;
            flat.material = out.material;
            const size_t n = tri.size();
            flat.positions.resize(n * 3);
            flat.normals.resize(n * 3);
            if (!out.colors.empty()) flat.colors.resize(n * 4);
            if (!out.uvs.empty()) flat.uvs.resize(n * 2);
            flat.indices.resize(n);
            for (size_t t = 0; t < n; t += 3) {
                const float* p0 = &out.positions[tri[t] * 3];
                const float* p1 = &out.positions[tri[t + 1] * 3];
                const float* p2 = &out.positions[tri[t + 2] * 3];
                const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
                const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
                float fn[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                normalize3(fn, up);
                for (size_t k = 0; k < 3; ++k) {
                    const uint32_t src = tri[t + k];
                    const size_t dst = t + k;
                    std::copy_n(&out.positions[src * 3], 3, &flat.positions[dst * 3]);
                    std::copy_n(fn, 3, &flat.normals[dst * 3]);
                    if (!out.colors.empty()) std::copy_n(&out.colors[src * 4], 4, &flat.colors[dst * 4]);
                    if (!out.uvs.empty()) std::copy_n(&out.uvs[src * 2], 2, &flat.uvs[dst * 2]);
                    flat.indices[dst] = static_cast<uint32_t>(dst);
                }
            }
            out = std::move(flat);
        }
        model.primitives.push_back(std::move(out));
    }

    void visit(const cgltf_node* node, int depth) {
        if (depth > 256) {
            warn("node hierarchy deeper than 256 levels; the rest is skipped");
            return;
        }
        ++model.nodesVisited;
        const size_t nodeIndex = cgltf_node_index(data, node);
        if (node->mesh) {
            Mat4 world{};
            cgltf_node_transform_world(node, world.data());
            const size_t meshIndex = cgltf_mesh_index(data, node->mesh);
            if (node->skin) warn(orName(node->name, "node", nodeIndex) + ": skinned; drawn in its bind pose");
            if (node->mesh->weights_count > 0 || node->weights_count > 0) {
                warn(orName(node->name, "node", nodeIndex) + ": morph targets ignored (base shape drawn)");
            }
            for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
                readPrimitive(*node, nodeIndex, *node->mesh, meshIndex, p, world);
            }
        }
        for (cgltf_size c = 0; c < node->children_count; ++c) visit(node->children[c], depth + 1);
    }

    void run() {
        for (cgltf_size i = 0; i < data->extensions_required_count; ++i) {
            const std::string ext = data->extensions_required[i];
            if (ext != "KHR_materials_unlit" && ext != "KHR_texture_transform" && ext != "KHR_materials_emissive_strength" &&
                ext != "KHR_materials_pbrSpecularGlossiness" && ext != "KHR_mesh_quantization") {
                warn("required extension " + ext + " is not supported; the result may be wrong");
            }
        }
        readMaterials();
        readTextures();
        readImages();

        const cgltf_scene* scene = nullptr;
        if (options.scene >= 0) {
            if (static_cast<size_t>(options.scene) >= data->scenes_count) {
                throw Error(Error::Kind::Invalid, "scene " + std::to_string(options.scene) + " does not exist (the file has " +
                                                      std::to_string(data->scenes_count) + ")");
            }
            scene = &data->scenes[options.scene];
        } else if (data->scene) {
            scene = data->scene;
        } else if (data->scenes_count > 0) {
            scene = &data->scenes[0];
        }
        if (scene) {
            for (cgltf_size i = 0; i < scene->nodes_count; ++i) visit(scene->nodes[i], 0);
        } else {
            for (cgltf_size i = 0; i < data->nodes_count; ++i) {
                if (!data->nodes[i].parent) visit(&data->nodes[i], 0);
            }
        }

        bool first = true;
        for (const Primitive& p : model.primitives) {
            for (size_t v = 0; v < p.positions.size(); v += 3) {
                for (size_t k = 0; k < 3; ++k) {
                    const float x = p.positions[v + k];
                    if (first || x < model.boundsMin[k]) model.boundsMin[k] = x;
                    if (first || x > model.boundsMax[k]) model.boundsMax[k] = x;
                }
                first = false;
            }
        }
        if (data->animations_count > 0) warn(std::to_string(data->animations_count) + " animation(s) ignored (rest pose drawn)");
    }
};

Model finish(DataPtr data, const std::string& source, const std::string& gltfPath, const std::string& baseDirectory,
             const LoadOptions& options) {
    cgltf_options cgo{};
    cgltf_result r = cgltf_load_buffers(&cgo, data.get(), gltfPath.empty() ? nullptr : gltfPath.c_str());
    if (r != cgltf_result_success) {
        throw Error(r == cgltf_result_file_not_found || r == cgltf_result_io_error ? Error::Kind::Io : Error::Kind::Invalid,
                    source + ": cannot load buffers: " + resultName(r));
    }
    r = cgltf_validate(data.get());
    if (r != cgltf_result_success) throw Error(Error::Kind::Invalid, source + ": " + resultName(r));
    Loader loader;
    loader.data = data.get();
    loader.baseDirectory = baseDirectory;
    loader.options = options;
    loader.model.source = source;
    loader.run();
    return std::move(loader.model);
}

}  // namespace

Model loadFile(const std::string& path, const LoadOptions& options) {
    cgltf_options cgo{};
    cgltf_data* raw = nullptr;
    const cgltf_result r = cgltf_parse_file(&cgo, path.c_str(), &raw);
    DataPtr data(raw);
    if (r == cgltf_result_file_not_found || r == cgltf_result_io_error) {
        throw Error(Error::Kind::Io, path + ": cannot read the file");
    }
    if (r != cgltf_result_success) throw Error(Error::Kind::Invalid, path + ": " + resultName(r));
    return finish(std::move(data), path, path, directoryOf(path), options);
}

Model loadMemory(const void* bytes, size_t size, const std::string& baseDirectory, const LoadOptions& options) {
    cgltf_options cgo{};
    cgltf_data* raw = nullptr;
    const cgltf_result r = cgltf_parse(&cgo, bytes, size, &raw);
    DataPtr data(raw);
    if (r != cgltf_result_success) throw Error(Error::Kind::Invalid, std::string("<memory>: ") + resultName(r));
    std::string base = baseDirectory;
    if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '/';
    // cgltf resolves relative buffer URIs against the directory of this path.
    return finish(std::move(data), "<memory>", base.empty() ? std::string() : base + "memory.gltf", base, options);
}

}  // namespace strata::gltf
