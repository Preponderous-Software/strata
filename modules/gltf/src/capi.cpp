// The C API of libstrata_gltf (strata_gltf.h) over load/plan/upload.
#include <algorithm>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "strata/gltf/model.hpp"
#include "strata/gltf/plan.hpp"
#include "strata/gltf/strata_gltf.h"
#include "strata/gltf/upload.hpp"

struct StrataGltfScene {
    strata::gltf::Uploaded uploaded;
    StrataGltfInfo info{};
    std::vector<StrataGltfMeshInfo> meshes;
    std::string report;
    std::string reportJson;
};

namespace {

thread_local std::string lastError;

StrataStatus failWith(StrataStatus status, const std::string& message) {
    lastError = message;
    return status;
}

StrataStatus statusOf(strata::gltf::Error::Kind kind) {
    switch (kind) {
        case strata::gltf::Error::Kind::Io: return STRATA_ERROR_IO;
        case strata::gltf::Error::Kind::Unsupported: return STRATA_ERROR_UNSUPPORTED;
        default: return STRATA_ERROR_INVALID_ARGUMENT;
    }
}

int positive(int32_t value, int fallback) { return value > 0 ? value : fallback; }

constexpr uint32_t kKnownFlags = STRATA_GLTF_ALWAYS_MERGE | STRATA_GLTF_NO_BACK_FACES | STRATA_GLTF_NO_MIPMAPS;

}  // namespace

extern "C" {

uint32_t strata_gltf_api_version(void) { return STRATA_GLTF_API_VERSION; }

const char* strata_gltf_last_error(void) { return lastError.c_str(); }

StrataStatus strata_gltf_load(StrataEngine* engine, const char* path, const StrataGltfOptions* options,
                              StrataGltfScene** out_scene) {
    if (out_scene) *out_scene = nullptr;
    if (!engine || !path || !out_scene) {
        return failWith(STRATA_ERROR_INVALID_ARGUMENT, "engine, path and out_scene are required");
    }
    strata::gltf::LoadOptions load;
    strata::gltf::BudgetOptions budget;
    if (options) {
        if (options->struct_size < sizeof(StrataGltfOptions)) {
            return failWith(STRATA_ERROR_INVALID_ARGUMENT, "options->struct_size is too small");
        }
        if (options->flags & ~kKnownFlags) return failWith(STRATA_ERROR_INVALID_ARGUMENT, "unknown options->flags bit");
        if (options->color_policy < 0 || options->color_policy > 2 || options->mask_policy < 0 ||
            options->mask_policy > 1 || options->emissive_policy < 0 || options->emissive_policy > 1) {
            return failWith(STRATA_ERROR_INVALID_ARGUMENT, "unknown color_policy, mask_policy or emissive_policy");
        }
        load.scene = options->scene;
        budget.maxMeshes = positive(options->max_meshes, STRATA_MAX_MESHES);
        budget.maxTextures = positive(options->max_textures, STRATA_MAX_TEXTURES);
        budget.maxTextureSize = std::min(positive(options->max_texture_size, STRATA_MAX_TEXTURE_SIZE), STRATA_MAX_TEXTURE_SIZE);
        budget.atlasSize = std::min(positive(options->atlas_size, 4096), budget.maxTextureSize);
        budget.alwaysMerge = (options->flags & STRATA_GLTF_ALWAYS_MERGE) != 0;
        budget.backFaces = (options->flags & STRATA_GLTF_NO_BACK_FACES) == 0;
        budget.mipmaps = (options->flags & STRATA_GLTF_NO_MIPMAPS) == 0;
        budget.colors = static_cast<strata::gltf::ColorPolicy>(options->color_policy);
        budget.mask = static_cast<strata::gltf::MaskPolicy>(options->mask_policy);
        budget.emissive = static_cast<strata::gltf::EmissivePolicy>(options->emissive_policy);
    }
    try {
        const strata::gltf::Model model = strata::gltf::loadFile(path, load);
        const strata::gltf::Plan plan = strata::gltf::plan(model, budget);
        auto scene = std::make_unique<StrataGltfScene>();
        StrataStatus status = STRATA_OK;
        std::string error;
        if (!strata::gltf::upload(engine, plan, scene->uploaded, &status, &error)) return failWith(status, error);

        const strata::gltf::PlanReport& r = plan.report;
        StrataGltfInfo& info = scene->info;
        info.primitives_in = r.primitivesIn;
        info.primitives_skipped = model.primitivesSkipped;
        info.mesh_count = r.meshesOut;
        info.texture_count = r.texturesOut;
        info.material_count = static_cast<int32_t>(model.materials.size());
        info.merged = r.merged ? 1 : 0;
        info.atlased = r.atlased ? 1 : 0;
        info.atlas_pages = r.atlasPages;
        info.warning_count = static_cast<int32_t>(r.notes.size());
        info.vertices = static_cast<int64_t>(r.vertices);
        info.triangles = static_cast<int64_t>(r.triangles);
        for (int k = 0; k < 3; ++k) {
            info.bounds_min[k] = model.boundsMin[k];
            info.bounds_max[k] = model.boundsMax[k];
        }
        for (size_t i = 0; i < plan.meshes.size(); ++i) {
            const strata::gltf::PlannedMesh& m = plan.meshes[i];
            StrataGltfMeshInfo mi{};
            mi.mesh_id = scene->uploaded.meshIds[i];
            mi.texture_id = m.texture >= 0 ? scene->uploaded.textureIds[static_cast<size_t>(m.texture)] : 0u;
            mi.flags = m.flags;
            mi.material = m.material;
            mi.alpha_mode = static_cast<int32_t>(m.alphaMode);
            mi.alpha_cutoff = m.alphaCutoff;
            mi.double_sided = m.doubleSided ? 1 : 0;
            mi.primitives = m.primitives;
            mi.vertex_count = static_cast<int32_t>(m.vertexCount());
            mi.index_count = static_cast<int32_t>(m.indices.size());
            for (int k = 0; k < 3; ++k) mi.emissive[k] = m.emissive[k];
            scene->meshes.push_back(mi);
        }
        scene->report = r.text();
        scene->reportJson = r.json();
        *out_scene = scene.release();
        return STRATA_OK;
    } catch (const strata::gltf::Error& e) {
        return failWith(statusOf(e.kind()), e.what());
    } catch (const std::bad_alloc&) {
        return failWith(STRATA_ERROR_INTERNAL, "out of memory");
    } catch (const std::exception& e) {
        return failWith(STRATA_ERROR_INTERNAL, e.what());
    }
}

StrataStatus strata_gltf_get_info(const StrataGltfScene* scene, StrataGltfInfo* out_info) {
    if (!scene || !out_info) return failWith(STRATA_ERROR_INVALID_ARGUMENT, "scene and out_info are required");
    *out_info = scene->info;
    return STRATA_OK;
}

StrataStatus strata_gltf_get_mesh(const StrataGltfScene* scene, int32_t index, StrataGltfMeshInfo* out_mesh) {
    if (!scene || !out_mesh) return failWith(STRATA_ERROR_INVALID_ARGUMENT, "scene and out_mesh are required");
    if (index < 0 || static_cast<size_t>(index) >= scene->meshes.size()) {
        return failWith(STRATA_ERROR_INVALID_ARGUMENT, "mesh index out of range");
    }
    *out_mesh = scene->meshes[static_cast<size_t>(index)];
    return STRATA_OK;
}

const char* strata_gltf_report(const StrataGltfScene* scene) { return scene ? scene->report.c_str() : ""; }

const char* strata_gltf_report_json(const StrataGltfScene* scene) { return scene ? scene->reportJson.c_str() : "{}"; }

void strata_gltf_unload(StrataEngine* engine, StrataGltfScene* scene) {
    if (!scene) return;
    strata::gltf::remove(engine, scene->uploaded);
    delete scene;
}

}  // extern "C"
