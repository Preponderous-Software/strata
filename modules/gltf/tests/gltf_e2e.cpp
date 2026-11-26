// libstrata_gltf's C API on a real headless engine: every synthetic asset
// loads, reports what it made, draws, and unloads; failures leave nothing
// behind. Exit codes: 0 pass, 1 fail, 77 skipped (no headless GL here).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "strata/gltf/strata_gltf.h"
#include "strata/strata.h"

namespace {

int failures = 0;

#define EXPECT(cond)                                                                \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::fprintf(stderr, "%s:%d: EXPECT(%s) failed\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                             \
        }                                                                           \
    } while (0)

std::string asset(const char* name) { return std::string(STRATA_GLTF_ASSETS) + "/" + name; }

// Draws one frame looking at the origin and returns the fraction of pixels
// that differ from the clear colour.
double coverage(StrataEngine* engine) {
    StrataCamera camera{};
    const float eye[3] = {3, 2.5f, 5}, target[3] = {0, 0.5f, 0}, up[3] = {0, 1, 0};
    strata_camera_look_at(eye, target, up, 45, 160.0f / 90.0f, 0.1f, 50, &camera);
    strata_set_camera(engine, &camera);
    StrataLighting lighting{};
    strata_lighting_defaults(STRATA_LIGHTING_LEGACY, &lighting);
    strata_set_lighting(engine, &lighting);
    StrataFrameDesc frame{};
    frame.clear_flags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH;
    strata_begin_frame(engine, &frame);
    strata_end_frame(engine);
    std::vector<uint8_t> rgba(160 * 90 * 4);
    int32_t w = 0, h = 0;
    if (strata_read_pixels_rgba8(engine, rgba.data(), static_cast<int32_t>(rgba.size()), &w, &h) != STRATA_OK) return -1;
    size_t drawn = 0;
    for (size_t i = 0; i < rgba.size(); i += 4) drawn += (rgba[i] | rgba[i + 1] | rgba[i + 2]) != 0;
    return static_cast<double>(drawn) / static_cast<double>(w * h);
}

}  // namespace

int main() {
    StrataEngineDesc desc{};
    desc.struct_size = sizeof desc;
    desc.abi_version = STRATA_ABI_VERSION;
    desc.surface = STRATA_SURFACE_HEADLESS;
    desc.width = 160;
    desc.height = 90;
    StrataEngine* engine = nullptr;
    if (strata_create(&desc, &engine) != STRATA_OK) {
        std::fprintf(stderr, "skipped: no headless GL here (%s)\n", strata_last_error());
        return 77;
    }
    EXPECT(strata_gltf_api_version() == STRATA_GLTF_API_VERSION);
    EXPECT(sizeof(StrataGltfOptions) == 40);
    EXPECT(sizeof(StrataGltfInfo) == 80);
    EXPECT(sizeof(StrataGltfMeshInfo) == 52);

    const char* assets[] = {"cube.gltf", "textured_quad.glb", "hierarchy.gltf", "vertex_colors.glb", "alpha_modes.gltf"};
    for (const char* name : assets) {
        StrataGltfScene* scene = nullptr;
        const StrataStatus s = strata_gltf_load(engine, asset(name).c_str(), nullptr, &scene);
        if (s != STRATA_OK) {
            std::fprintf(stderr, "%s: %s\n", name, strata_gltf_last_error());
            ++failures;
            continue;
        }
        StrataGltfInfo info{};
        EXPECT(strata_gltf_get_info(scene, &info) == STRATA_OK);
        EXPECT(info.mesh_count > 0);
        EXPECT(info.triangles > 0);
        EXPECT(info.bounds_max[1] > info.bounds_min[1]);
        for (int32_t i = 0; i < info.mesh_count; ++i) {
            StrataGltfMeshInfo mesh{};
            EXPECT(strata_gltf_get_mesh(scene, i, &mesh) == STRATA_OK);
            EXPECT(mesh.mesh_id != 0);
            EXPECT((mesh.flags & STRATA_MESH_COLORS_RGBA) != 0);
            EXPECT(mesh.index_count % 3 == 0);
        }
        StrataGltfMeshInfo none{};
        EXPECT(strata_gltf_get_mesh(scene, info.mesh_count, &none) == STRATA_ERROR_INVALID_ARGUMENT);
        EXPECT(std::strstr(strata_gltf_report(scene), "meshes out:") != nullptr);
        EXPECT(strata_gltf_report_json(scene)[0] == '{');
        const double drawn = coverage(engine);
        EXPECT(drawn > 0.02);
        std::printf("%-20s meshes %d textures %d triangles %lld coverage %.3f\n", name, info.mesh_count,
                    info.texture_count, static_cast<long long>(info.triangles), drawn);
        strata_gltf_unload(engine, scene);
        EXPECT(coverage(engine) < 1e-9);  // everything removed again
    }

    // Options: alpha_modes as MASK opaque and no back faces, the hierarchy at
    // one mesh (its repeating texture dropped).
    StrataGltfOptions options{};
    options.struct_size = sizeof options;
    options.scene = -1;
    options.mask_policy = STRATA_GLTF_MASK_OPAQUE;
    options.flags = STRATA_GLTF_NO_BACK_FACES;
    StrataGltfScene* scene = nullptr;
    EXPECT(strata_gltf_load(engine, asset("alpha_modes.gltf").c_str(), &options, &scene) == STRATA_OK);
    if (scene) {
        StrataGltfMeshInfo mask{};
        EXPECT(strata_gltf_get_mesh(scene, 1, &mask) == STRATA_OK);
        EXPECT(mask.alpha_mode == STRATA_GLTF_ALPHA_MASK);
        EXPECT(std::fabs(mask.alpha_cutoff - 0.5f) < 1e-6f);
        EXPECT((mask.flags & STRATA_MESH_TRANSLUCENT) == 0);
        strata_gltf_unload(engine, scene);
    }
    options = StrataGltfOptions{};
    options.struct_size = sizeof options;
    options.scene = -1;
    options.max_meshes = 1;
    scene = nullptr;
    EXPECT(strata_gltf_load(engine, asset("hierarchy.gltf").c_str(), &options, &scene) == STRATA_OK);
    if (scene) {
        StrataGltfInfo info{};
        strata_gltf_get_info(scene, &info);
        EXPECT(info.mesh_count == 1 && info.texture_count == 0 && info.atlased == 1);
        strata_gltf_unload(engine, scene);
    }

    // Failures: nothing left behind, a status and a message.
    scene = reinterpret_cast<StrataGltfScene*>(&options);
    EXPECT(strata_gltf_load(engine, asset("missing.gltf").c_str(), nullptr, &scene) == STRATA_ERROR_IO);
    EXPECT(scene == nullptr);
    EXPECT(std::strstr(strata_gltf_last_error(), "missing.gltf") != nullptr);
    options.max_meshes = 1;
    EXPECT(strata_gltf_load(engine, asset("alpha_modes.gltf").c_str(), &options, &scene) == STRATA_ERROR_UNSUPPORTED);
    options.struct_size = 8;
    EXPECT(strata_gltf_load(engine, asset("cube.gltf").c_str(), &options, &scene) == STRATA_ERROR_INVALID_ARGUMENT);
    EXPECT(strata_gltf_load(nullptr, asset("cube.gltf").c_str(), nullptr, &scene) == STRATA_ERROR_INVALID_ARGUMENT);
    EXPECT(coverage(engine) < 1e-9);
    strata_gltf_unload(nullptr, nullptr);

    // Unloading after the engine is gone frees the scene only.
    EXPECT(strata_gltf_load(engine, asset("cube.gltf").c_str(), nullptr, &scene) == STRATA_OK);
    strata_destroy(engine);
    strata_gltf_unload(nullptr, scene);

    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    else std::printf("ok\n");
    return failures ? 1 : 0;
}
