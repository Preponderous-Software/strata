// Model -> Plan: see plan.hpp for the three steps.
#include "strata/gltf/plan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <sstream>
#include <tuple>

#include "atlas.hpp"

namespace strata::gltf {

float srgbToLinear(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float c) {
    c = std::max(c, 0.0f);
    if (c >= 1.0f) return c;  // keep over-range factors over range rather than clipping them
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

namespace {

constexpr uint32_t kTextureMipmaps = 0x1u, kTextureClampU = 0x2u, kTextureClampV = 0x4u, kTextureNearest = 0x8u;
constexpr uint32_t kMeshUnlit = 0x1u, kMeshTranslucent = 0x2u, kMeshColorsRgba = 0x4u;
constexpr float kUvSlack = 1e-3f;

const Material& defaultMaterial() {
    static const Material m{};
    return m;
}

// A texture as a mesh will sample it: a glTF texture, with MASK's cutoff
// baked into its alpha or not.
struct Variant {
    int texture = -1;
    bool mask = false;
    float maskFactor = 1.0f;  // baseColorFactor alpha
    float cutoff = 0.5f;
    bool repeating = false;   // some user's uvs leave 0..1
    size_t triangles = 0;     // drawn with it, for choosing what to keep
};

// How one primitive is drawn.
struct Draw {
    bool drawn = true;
    int variant = -1;
    bool translucent = false;
    bool unlit = false;
    float alpha = 1.0f;
    AlphaMode mode = AlphaMode::Opaque;
    float cutoff = 0.5f;
    bool doubleSided = false;
    int material = -1;
    float factor[4] = {1, 1, 1, 1};
    float emissive[3] = {0, 0, 0};    // recorded
    float addEmissive[3] = {0, 0, 0}; // added to the base colour (drawn unlit)
};

// Where a variant ended up.
struct Slot {
    int texture = -1;  // Plan::textures index, -1 untextured
    bool atlas = false;
    float offset[2] = {0, 0};
    float scale[2] = {1, 1};
    bool dropped = false;    // repeating texture past the budget: average colour instead
    float average[3] = {1, 1, 1};
};

using GroupKey = std::tuple<int, int, bool, bool, int, int>;  // prim (-1 merged), texture, translucent, unlit, alpha8, mode

class Planner {
public:
    Planner(const Model& model, const BudgetOptions& options) : model_(model), opt_(options) {}

    Plan run() {
        validateOptions();
        report().primitivesIn = static_cast<int>(model_.primitives.size());
        report().notes = model_.warnings;
        classify();

        // Step 1 and 2: no atlas.
        const bool texturesFit = static_cast<int>(variants_.size()) <= opt_.maxTextures;
        if (texturesFit) {
            directSlots();
            if (!opt_.alwaysMerge && estimateMeshes(false) <= static_cast<size_t>(opt_.maxMeshes)) {
                build(false);
                return finish();
            }
            if (estimateMeshes(true) <= static_cast<size_t>(opt_.maxMeshes)) {
                report().merged = true;
                build(true);
                return finish();
            }
        }
        // Step 3: atlas, merged.
        report().merged = true;
        report().atlased = true;
        atlasSlots();
        build(true);
        return finish();
    }

private:
    PlanReport& report() { return plan_.report; }

    void note(const std::string& line) { report().notes.push_back(line); }

    void validateOptions() {
        if (opt_.maxMeshes < 1) throw Error(Error::Kind::Invalid, "maxMeshes must be at least 1");
        if (opt_.maxTextures < 0) throw Error(Error::Kind::Invalid, "maxTextures must not be negative");
        if (opt_.maxTextureSize < 1 || opt_.maxTextureSize > 8192) {
            throw Error(Error::Kind::Invalid, "maxTextureSize must be 1..8192");
        }
        if (opt_.atlasSize < 16 || opt_.atlasSize > opt_.maxTextureSize) {
            throw Error(Error::Kind::Invalid, "atlasSize must be 16..maxTextureSize");
        }
        if (opt_.maxMeshVertices < 3) throw Error(Error::Kind::Invalid, "maxMeshVertices must be at least 3");
    }

    const Material& material(int index) const {
        return index >= 0 && static_cast<size_t>(index) < model_.materials.size()
                   ? model_.materials[static_cast<size_t>(index)]
                   : defaultMaterial();
    }

    // The decoded image behind a glTF texture, or nullptr.
    const Image* imageOf(int texture) const {
        if (texture < 0 || static_cast<size_t>(texture) >= model_.textures.size()) return nullptr;
        const int image = model_.textures[static_cast<size_t>(texture)].image;
        if (image < 0 || static_cast<size_t>(image) >= model_.images.size()) return nullptr;
        const Image& im = model_.images[static_cast<size_t>(image)];
        return im.rgba.empty() ? nullptr : &im;
    }

    void classify() {
        draws_.resize(model_.primitives.size());
        std::map<std::tuple<int, bool, float, float>, int> variantIndex;
        std::vector<bool> textureSeen(model_.textures.size(), false);
        for (size_t i = 0; i < model_.primitives.size(); ++i) {
            const Primitive& p = model_.primitives[i];
            const Material& m = material(p.material);
            Draw& d = draws_[i];
            d.material = p.material;
            d.mode = m.alphaMode;
            d.cutoff = m.alphaCutoff;
            d.doubleSided = m.doubleSided;
            d.unlit = m.unlit;
            std::copy(m.baseColorFactor, m.baseColorFactor + 4, d.factor);
            std::copy(m.emissive, m.emissive + 3, d.emissive);
            if (m.isEmissive() && opt_.emissive == EmissivePolicy::Unlit) {
                if (!d.unlit) ++report().emissiveUnlit;
                d.unlit = true;
                std::copy(m.emissive, m.emissive + 3, d.addEmissive);
            }
            const bool textured = !p.uvs.empty() && imageOf(m.baseColorTexture) != nullptr;
            if (m.baseColorTexture >= 0 && static_cast<size_t>(m.baseColorTexture) < textureSeen.size()) {
                textureSeen[static_cast<size_t>(m.baseColorTexture)] = true;
            }
            bool maskVariant = false;
            if (m.alphaMode == AlphaMode::Blend) {
                d.translucent = true;
                d.alpha = m.baseColorFactor[3];
            } else if (m.alphaMode == AlphaMode::Mask) {
                if (opt_.mask == MaskPolicy::Opaque) {
                    ++report().maskAsOpaque;
                } else if (textured) {
                    d.translucent = true;
                    maskVariant = true;
                    ++report().maskAsBlend;
                } else if (m.baseColorFactor[3] < m.alphaCutoff) {
                    d.drawn = false;  // fully cut away
                    note(p.node + "/" + p.mesh + ": alphaMode MASK with factor alpha under the cutoff and no texture; not drawn");
                } else {
                    ++report().maskAsOpaque;  // all texels pass the cutoff
                }
            }
            if (!d.drawn) continue;
            if (textured) {
                const auto key = std::make_tuple(m.baseColorTexture, maskVariant, maskVariant ? m.baseColorFactor[3] : 1.0f,
                                                 maskVariant ? m.alphaCutoff : 0.0f);
                auto it = variantIndex.find(key);
                if (it == variantIndex.end()) {
                    Variant v;
                    v.texture = m.baseColorTexture;
                    v.mask = maskVariant;
                    v.maskFactor = m.baseColorFactor[3];
                    v.cutoff = m.alphaCutoff;
                    it = variantIndex.emplace(key, static_cast<int>(variants_.size())).first;
                    variants_.push_back(v);
                }
                d.variant = it->second;
                Variant& v = variants_[static_cast<size_t>(d.variant)];
                v.triangles += p.triangleCount();
                for (float uv : p.uvs) {
                    if (uv < -kUvSlack || uv > 1.0f + kUvSlack) {
                        v.repeating = true;
                        break;
                    }
                }
            }
        }
        for (bool seen : textureSeen) report().texturesIn += seen ? 1 : 0;
    }

    // The texels of a variant: cutoff baked in, colour policy applied, and
    // scaled to fit maxTextureSize. Cached.
    const AtlasImage& variantImage(int index) {
        if (variantImages_.size() < variants_.size()) variantImages_.resize(variants_.size());
        AtlasImage& out = variantImages_[static_cast<size_t>(index)];
        if (out.width > 0) return out;
        const Variant& v = variants_[static_cast<size_t>(index)];
        const Image& src = *imageOf(v.texture);
        out.width = src.width;
        out.height = src.height;
        out.rgba = src.rgba;
        if (v.mask) {
            for (size_t i = 3; i < out.rgba.size(); i += 4) {
                out.rgba[i] = (static_cast<float>(out.rgba[i]) / 255.0f) * v.maskFactor >= v.cutoff ? 255 : 0;
            }
        }
        if (opt_.colors == ColorPolicy::Linear) {
            std::array<uint8_t, 256> lut{};
            for (int c = 0; c < 256; ++c) {
                lut[static_cast<size_t>(c)] =
                    static_cast<uint8_t>(std::lround(srgbToLinear(static_cast<float>(c) / 255.0f) * 255.0f));
            }
            for (size_t i = 0; i < out.rgba.size(); i += 4) {
                for (size_t k = 0; k < 3; ++k) out.rgba[i + k] = lut[out.rgba[i + k]];
            }
        }
        if (out.width > opt_.maxTextureSize || out.height > opt_.maxTextureSize) {
            const double s = static_cast<double>(opt_.maxTextureSize) / std::max(out.width, out.height);
            out = resizeImage(out, std::max(1, static_cast<int>(std::floor(out.width * s))),
                              std::max(1, static_cast<int>(std::floor(out.height * s))));
            ++report().texturesDownscaled;
        }
        return out;
    }

    uint32_t samplerFlags(const Variant& v) const {
        const Sampler& s = model_.textures[static_cast<size_t>(v.texture)].sampler;
        uint32_t flags = 0;
        if (opt_.mipmaps && s.mipmaps) flags |= kTextureMipmaps;
        if (s.clampU) flags |= kTextureClampU;
        if (s.clampV) flags |= kTextureClampV;
        if (s.nearest) flags |= kTextureNearest;
        return flags;
    }

    void directSlots() {
        slots_.assign(variants_.size(), Slot{});
        plan_.textures.clear();
        for (size_t i = 0; i < variants_.size(); ++i) {
            const Variant& v = variants_[i];
            const AtlasImage& im = variantImage(static_cast<int>(i));
            PlannedTexture t;
            t.label = model_.images[static_cast<size_t>(model_.textures[static_cast<size_t>(v.texture)].image)].name;
            if (v.mask) t.label += " (alpha cutoff " + std::to_string(v.cutoff).substr(0, 4) + ")";
            t.width = im.width;
            t.height = im.height;
            t.rgba = im.rgba;
            t.flags = samplerFlags(v);
            t.sourceTextures = {v.texture};
            slots_[i].texture = static_cast<int>(plan_.textures.size());
            plan_.textures.push_back(std::move(t));
        }
    }

    // Packs the non-repeating variants into atlas pages; repeating ones keep a
    // texture each while maxTextures and maxMeshes allow, most used first.
    void atlasSlots() {
        std::vector<int> atlasable, repeating;
        for (size_t i = 0; i < variants_.size(); ++i) {
            (variants_[i].repeating ? repeating : atlasable).push_back(static_cast<int>(i));
        }
        std::stable_sort(repeating.begin(), repeating.end(), [&](int a, int b) {
            return variants_[static_cast<size_t>(a)].triangles > variants_[static_cast<size_t>(b)].triangles;
        });
        std::vector<AtlasInput> inputs;
        for (int v : atlasable) {
            const AtlasImage& im = variantImage(v);
            inputs.push_back({&im});
        }

        // A white texel in the atlas lets untextured primitives share a mesh
        // with textured ones (their uvs all point at it): tried when the
        // budget is not met without it.
        white_.width = white_.height = 1;
        white_.rgba = {255, 255, 255, 255};
        std::vector<AtlasInput> withWhite = inputs;
        withWhite.push_back({&white_});

        size_t keep = repeating.size();
        int pageLimit = std::max(1, opt_.maxTextures);
        for (;;) {
            const int pagesAllowed = opt_.maxTextures - static_cast<int>(keep);
            if (!atlasable.empty() && pagesAllowed < 1) {
                if (keep == 0) throw Error(Error::Kind::Unsupported, "maxTextures is 0 but textures are used");
                --keep;
                continue;
            }
            AtlasResult packed;
            for (const bool white : {false, true}) {
                if (white && pagesAllowed < 1) break;
                packed = AtlasResult{};
                if (!atlasable.empty() || white) {
                    packed = packAtlas(white ? withWhite : inputs, opt_.atlasSize, std::min(pagesAllowed, pageLimit));
                }
                assignAtlasSlots(atlasable, repeating, keep, packed, white);
                if (estimateMeshes(true) <= static_cast<size_t>(opt_.maxMeshes)) {
                    report().atlasPages = static_cast<int>(packed.pages.size());
                    report().atlasScale = packed.scale;
                    if (white) note("untextured primitives sample a white texel in the atlas so they can share meshes");
                    return;
                }
            }
            if (keep > 0) {
                --keep;
            } else if (!atlasable.empty() && packed.pages.size() > 1) {
                pageLimit = static_cast<int>(packed.pages.size()) - 1;
            } else {
                throw Error(Error::Kind::Unsupported,
                            "cannot fit into " + std::to_string(opt_.maxMeshes) + " meshes even fully merged and atlased (" +
                                std::to_string(estimateMeshes(true)) + " needed)");
            }
        }
    }

    void assignAtlasSlots(const std::vector<int>& atlasable, const std::vector<int>& repeating, size_t keep,
                          const AtlasResult& packed, bool white) {
        slots_.assign(variants_.size(), Slot{});
        plan_.textures.clear();
        useWhite_ = white;
        if (white) {
            const AtlasPlacement& pl = packed.placements.back();
            const AtlasImage& page = packed.pages[static_cast<size_t>(pl.page)];
            whiteTexture_ = pl.page;
            whiteUv_[0] = (static_cast<float>(pl.x) + 0.5f) / static_cast<float>(page.width);
            whiteUv_[1] = (static_cast<float>(pl.y) + 0.5f) / static_cast<float>(page.height);
        }
        report().repeatingDropped = 0;
        for (size_t p = 0; p < packed.pages.size(); ++p) {
            PlannedTexture t;
            t.label = "atlas page " + std::to_string(p);
            t.width = packed.pages[p].width;
            t.height = packed.pages[p].height;
            t.rgba = packed.pages[p].rgba;
            bool allNearest = true, anyMip = false;
            for (size_t k = 0; k < atlasable.size(); ++k) {
                if (packed.placements[k].page != static_cast<int>(p)) continue;
                const uint32_t f = samplerFlags(variants_[static_cast<size_t>(atlasable[k])]);
                allNearest = allNearest && (f & kTextureNearest);
                anyMip = anyMip || (f & kTextureMipmaps);
                t.sourceTextures.push_back(variants_[static_cast<size_t>(atlasable[k])].texture);
            }
            t.flags = kTextureClampU | kTextureClampV | (anyMip ? kTextureMipmaps : 0u) | (allNearest ? kTextureNearest : 0u);
            plan_.textures.push_back(std::move(t));
        }
        for (size_t k = 0; k < atlasable.size(); ++k) {
            const AtlasPlacement& pl = packed.placements[k];
            Slot& s = slots_[static_cast<size_t>(atlasable[k])];
            const PlannedTexture& page = plan_.textures[static_cast<size_t>(pl.page)];
            s.texture = pl.page;
            s.atlas = true;
            s.offset[0] = static_cast<float>(pl.x) / static_cast<float>(page.width);
            s.offset[1] = static_cast<float>(pl.y) / static_cast<float>(page.height);
            s.scale[0] = static_cast<float>(pl.width) / static_cast<float>(page.width);
            s.scale[1] = static_cast<float>(pl.height) / static_cast<float>(page.height);
        }
        for (size_t r = 0; r < repeating.size(); ++r) {
            const int vi = repeating[r];
            Slot& s = slots_[static_cast<size_t>(vi)];
            const AtlasImage& im = variantImage(vi);
            if (r < keep) {
                const Variant& v = variants_[static_cast<size_t>(vi)];
                PlannedTexture t;
                t.label = model_.images[static_cast<size_t>(model_.textures[static_cast<size_t>(v.texture)].image)].name +
                          " (repeating)";
                t.width = im.width;
                t.height = im.height;
                t.rgba = im.rgba;
                t.flags = samplerFlags(v);
                t.sourceTextures = {v.texture};
                s.texture = static_cast<int>(plan_.textures.size());
                plan_.textures.push_back(std::move(t));
            } else {
                s.dropped = true;
                averageColour(im, s.average);
                ++report().repeatingDropped;
            }
        }
    }

    static void averageColour(const AtlasImage& im, float out[3]) {
        double sum[3] = {0, 0, 0};
        const size_t n = im.rgba.size() / 4;
        for (size_t i = 0; i < im.rgba.size(); i += 4) {
            for (size_t k = 0; k < 3; ++k) sum[k] += im.rgba[i + k];
        }
        for (size_t k = 0; k < 3; ++k) out[k] = n ? static_cast<float>(sum[k] / static_cast<double>(n) / 255.0) : 1.0f;
    }

    GroupKey keyOf(size_t prim, bool merged) const {
        const Draw& d = draws_[prim];
        const int texture = textureOf(d);
        const int alpha8 = d.translucent ? static_cast<int>(std::lround(std::clamp(d.alpha, 0.0f, 1.0f) * 255.0f)) : 255;
        return GroupKey(merged ? -1 : static_cast<int>(prim), texture, d.translucent, d.unlit, alpha8, static_cast<int>(d.mode));
    }

    // The Plan::textures index a draw samples: its slot's, or the atlas's
    // white texel page when it has none and that is in use.
    int textureOf(const Draw& d) const {
        const int t = d.variant >= 0 ? slots_[static_cast<size_t>(d.variant)].texture : -1;
        return t < 0 && useWhite_ ? whiteTexture_ : t;
    }

    size_t outputVertices(size_t prim) const {
        const Draw& d = draws_[prim];
        const Primitive& p = model_.primitives[prim];
        const bool back = d.doubleSided && !d.translucent && opt_.backFaces;
        return back ? p.vertexCount() * 2 : p.vertexCount();
    }

    size_t estimateMeshes(bool merged) const {
        std::map<GroupKey, size_t> vertices;
        for (size_t i = 0; i < draws_.size(); ++i) {
            if (!draws_[i].drawn) continue;
            vertices[keyOf(i, merged)] += outputVertices(i);
        }
        size_t meshes = 0;
        const size_t cap = static_cast<size_t>(opt_.maxMeshVertices);
        for (const auto& [key, n] : vertices) meshes += std::max<size_t>(1, (n + cap - 1) / cap);
        return meshes;
    }

    // Final per-vertex colour factor for a draw (policy applied), times a
    // dropped texture's average.
    void colourFactor(const Draw& d, float out[3]) const {
        for (size_t k = 0; k < 3; ++k) {
            out[k] = opt_.colors == ColorPolicy::Display ? linearToSrgb(d.factor[k]) : d.factor[k];
        }
        if (d.variant >= 0 && slots_[static_cast<size_t>(d.variant)].dropped) {
            for (size_t k = 0; k < 3; ++k) out[k] *= slots_[static_cast<size_t>(d.variant)].average[k];
        }
    }

    void build(bool merged) {
        // Groups in first-seen order, so output follows the file's order.
        std::map<GroupKey, size_t> groupIndex;
        std::vector<std::vector<size_t>> groups;
        for (size_t i = 0; i < draws_.size(); ++i) {
            if (!draws_[i].drawn) continue;
            const GroupKey key = keyOf(i, merged);
            auto it = groupIndex.find(key);
            if (it == groupIndex.end()) {
                it = groupIndex.emplace(key, groups.size()).first;
                groups.emplace_back();
            }
            groups[it->second].push_back(i);
        }
        plan_.meshes.clear();
        for (const std::vector<size_t>& prims : groups) buildGroup(prims, merged);
        if (plan_.meshes.size() > static_cast<size_t>(opt_.maxMeshes)) {
            throw Error(Error::Kind::Unsupported, "needs " + std::to_string(plan_.meshes.size()) + " meshes, more than " +
                                                      std::to_string(opt_.maxMeshes));
        }
    }

    PlannedMesh startMesh(const std::vector<size_t>& prims, bool merged) const {
        PlannedMesh m;
        const Draw& d = draws_[prims.front()];
        const Primitive& p = model_.primitives[prims.front()];
        m.label = merged ? "merged " + std::to_string(plan_.meshes.size()) : p.node + "/" + p.mesh;
        m.alphaMode = d.mode;
        m.alphaCutoff = d.cutoff;
        m.color[3] = d.translucent ? std::clamp(d.alpha, 0.0f, 1.0f) : 1.0f;
        m.flags = kMeshColorsRgba | (d.translucent ? kMeshTranslucent : 0u) | (d.unlit ? kMeshUnlit : 0u);
        m.texture = textureOf(d);
        return m;
    }

    void finishMesh(PlannedMesh& m) {
        if (m.indices.empty()) return;
        std::sort(m.materials.begin(), m.materials.end());
        m.materials.erase(std::unique(m.materials.begin(), m.materials.end()), m.materials.end());
        m.material = m.materials.size() == 1 ? m.materials.front() : -2;
        if (m.texture < 0) m.uvs.clear();
        plan_.meshes.push_back(std::move(m));
    }

    void buildGroup(const std::vector<size_t>& prims, bool merged) {
        const size_t cap = static_cast<size_t>(opt_.maxMeshVertices);
        const size_t meshesBefore = plan_.meshes.size();
        PlannedMesh m = startMesh(prims, merged);
        std::vector<uint32_t> remap;
        std::vector<uint32_t> touched;
        for (size_t prim : prims) {
            const Primitive& p = model_.primitives[prim];
            const Draw& d = draws_[prim];
            const Slot* slot = d.variant >= 0 ? &slots_[static_cast<size_t>(d.variant)] : nullptr;
            const bool textured = slot && slot->texture >= 0;
            const bool onWhite = !textured && useWhite_;
            float factor[3];
            colourFactor(d, factor);
            const bool back = d.doubleSided && !d.translucent && opt_.backFaces;
            ++m.primitives;
            m.materials.push_back(d.material);
            m.doubleSided = m.doubleSided || d.doubleSided;
            if (d.emissive[0] + d.emissive[1] + d.emissive[2] > m.emissive[0] + m.emissive[1] + m.emissive[2]) {
                std::copy(d.emissive, d.emissive + 3, m.emissive);
            }
            float glow[3];
            for (size_t k = 0; k < 3; ++k) {
                glow[k] = opt_.colors == ColorPolicy::Display ? linearToSrgb(d.addEmissive[k]) : d.addEmissive[k];
            }
            remap.assign(p.vertexCount(), std::numeric_limits<uint32_t>::max());
            touched.clear();

            auto vertex = [&](uint32_t src, bool flip) -> uint32_t {
                // Back faces get vertices of their own (their normals are flipped).
                if (!flip && remap[src] != std::numeric_limits<uint32_t>::max()) return remap[src];
                const uint32_t out = static_cast<uint32_t>(m.vertexCount());
                for (size_t k = 0; k < 3; ++k) m.positions.push_back(p.positions[src * 3 + k]);
                for (size_t k = 0; k < 3; ++k) m.normals.push_back(flip ? -p.normals[src * 3 + k] : p.normals[src * 3 + k]);
                for (size_t k = 0; k < 3; ++k) {
                    float c = p.colors.empty() ? 1.0f : p.colors[src * 4 + k];
                    if (opt_.colors == ColorPolicy::Display) c = linearToSrgb(c);
                    m.colors.push_back(c * factor[k] + glow[k]);
                }
                m.colors.push_back(p.colors.empty() ? 1.0f : p.colors[src * 4 + 3]);
                if (textured) {
                    float u = p.uvs[src * 2], v = p.uvs[src * 2 + 1];
                    if (slot->atlas) {
                        u = slot->offset[0] + std::clamp(u, 0.0f, 1.0f) * slot->scale[0];
                        v = slot->offset[1] + std::clamp(v, 0.0f, 1.0f) * slot->scale[1];
                    }
                    m.uvs.push_back(u);
                    m.uvs.push_back(v);
                } else {
                    m.uvs.push_back(onWhite ? whiteUv_[0] : 0.0f);
                    m.uvs.push_back(onWhite ? whiteUv_[1] : 0.0f);
                }
                if (!flip) {
                    remap[src] = out;
                    touched.push_back(src);
                }
                return out;
            };
            auto flushIfFull = [&](size_t needed) {
                if (m.vertexCount() + needed <= cap) return;
                PlannedMesh next = startMesh(prims, merged);
                next.primitives = 1;
                next.materials.push_back(d.material);
                next.doubleSided = m.doubleSided;
                finishMesh(m);
                m = std::move(next);
                for (uint32_t s : touched) remap[s] = std::numeric_limits<uint32_t>::max();
                touched.clear();
            };
            for (int pass = 0; pass < (back ? 2 : 1); ++pass) {
                const bool flip = pass == 1;
                for (size_t t = 0; t < p.indices.size(); t += 3) {
                    flushIfFull(3);
                    const uint32_t a = vertex(p.indices[t], flip);
                    const uint32_t b = vertex(p.indices[t + 1], flip);
                    const uint32_t c = vertex(p.indices[t + 2], flip);
                    if (flip) {
                        m.indices.insert(m.indices.end(), {a, c, b});
                        ++report().backFacesAdded;
                    } else {
                        m.indices.insert(m.indices.end(), {a, b, c});
                    }
                }
            }
        }
        finishMesh(m);
        const size_t made = plan_.meshes.size() - meshesBefore;
        if (made > 1) report().meshesSplit += static_cast<int>(made - 1);
    }

    Plan finish() {
        PlanReport& r = report();
        r.meshesOut = static_cast<int>(plan_.meshes.size());
        r.texturesOut = static_cast<int>(plan_.textures.size());
        r.vertices = 0;
        r.triangles = 0;
        for (const PlannedMesh& m : plan_.meshes) {
            r.vertices += m.vertexCount();
            r.triangles += m.triangleCount();
        }
        // Textures no mesh samples any more (all users dropped) are left out by
        // construction; untextured counts come from the slots.
        for (size_t i = 0; i < draws_.size(); ++i) {
            if (draws_[i].drawn && draws_[i].variant >= 0 && slots_[static_cast<size_t>(draws_[i].variant)].dropped) {
                ++r.primitivesUntextured;
            }
        }
        if (r.maskAsBlend > 0) {
            note(std::to_string(r.maskAsBlend) +
                 " MASK primitive(s) drawn translucent with the cutoff baked into texel alpha (no depth writes or shadows; "
                 "Strata has no alpha test)");
        }
        if (r.repeatingDropped > 0) {
            note(std::to_string(r.repeatingDropped) + " repeating texture(s) over budget replaced by their average colour on " +
                 std::to_string(r.primitivesUntextured) + " primitive(s)");
        }
        if (r.atlased && r.atlasScale < 1.0f) {
            note("atlas images scaled by " + std::to_string(r.atlasScale).substr(0, 6) + " to fit " +
                 std::to_string(r.atlasPages) + " page(s)");
        }
        return std::move(plan_);
    }

    const Model& model_;
    BudgetOptions opt_;
    Plan plan_;
    std::vector<Draw> draws_;
    std::vector<Variant> variants_;
    std::vector<AtlasImage> variantImages_;
    std::vector<Slot> slots_;
    AtlasImage white_;
    bool useWhite_ = false;
    int whiteTexture_ = -1;
    float whiteUv_[2] = {0, 0};
};

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out + "\"";
}

}  // namespace

std::string PlanReport::text() const {
    std::ostringstream o;
    o << "primitives in:        " << primitivesIn << "\n";
    o << "textures in:          " << texturesIn << "\n";
    o << "meshes out:           " << meshesOut << (merged ? " (merged by material)" : " (one per primitive)") << "\n";
    o << "textures out:         " << texturesOut;
    if (atlased) o << " (" << atlasPages << " atlas page(s), scale " << atlasScale << ")";
    o << "\n";
    o << "vertices / triangles: " << vertices << " / " << triangles << "\n";
    if (texturesDownscaled) o << "textures downscaled:  " << texturesDownscaled << "\n";
    if (repeatingDropped) o << "repeating dropped:    " << repeatingDropped << " (" << primitivesUntextured << " primitives)\n";
    if (maskAsBlend || maskAsOpaque) o << "MASK as blend/opaque: " << maskAsBlend << " / " << maskAsOpaque << "\n";
    if (emissiveUnlit) o << "emissive as unlit:    " << emissiveUnlit << "\n";
    if (backFacesAdded) o << "back faces added:     " << backFacesAdded << "\n";
    if (meshesSplit) o << "meshes split:         " << meshesSplit << "\n";
    for (const std::string& n : notes) o << "note: " << n << "\n";
    return o.str();
}

std::string PlanReport::json() const {
    std::ostringstream o;
    o << "{\"primitives_in\": " << primitivesIn << ", \"textures_in\": " << texturesIn << ", \"meshes_out\": " << meshesOut
      << ", \"textures_out\": " << texturesOut << ", \"merged\": " << (merged ? "true" : "false")
      << ", \"atlased\": " << (atlased ? "true" : "false") << ", \"atlas_pages\": " << atlasPages
      << ", \"atlas_scale\": " << atlasScale << ", \"textures_downscaled\": " << texturesDownscaled
      << ", \"repeating_dropped\": " << repeatingDropped << ", \"primitives_untextured\": " << primitivesUntextured
      << ", \"mask_as_blend\": " << maskAsBlend << ", \"mask_as_opaque\": " << maskAsOpaque
      << ", \"emissive_unlit\": " << emissiveUnlit
      << ", \"back_faces_added\": " << backFacesAdded << ", \"meshes_split\": " << meshesSplit
      << ", \"vertices\": " << vertices << ", \"triangles\": " << triangles << ", \"notes\": [";
    for (size_t i = 0; i < notes.size(); ++i) o << (i ? ", " : "") << jsonString(notes[i]);
    o << "]}";
    return o.str();
}

Plan plan(const Model& model, const BudgetOptions& options) { return Planner(model, options).run(); }

}  // namespace strata::gltf
