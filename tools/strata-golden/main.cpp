// strata-golden: renders the golden-image scenes and checks them.
//
//   strata-golden render <scene-file> <out.png> [--surface headless|window]
//   strata-golden check  <scenes-dir> <golden-dir> <out-dir> [options]
//       --platform NAME      golden file name inside each test directory (default linux-llvmpipe)
//       --only NAME          check one scene
//       --bless              write the rendered images as the new goldens
//       --threshold DE       per-pixel Delta E threshold (default 4.0)
//       --max-fraction F     share of pixels allowed over it (default 0.005)
//       --surface KIND       headless (default) or window
//
// A scene named foo.scene is checked against <golden-dir>/foo/<platform>.png.
// Scene files whose names start with '_' are includes, not tests. On a
// failure the rendered image and a diff are written to <out-dir>.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "image/compare.hpp"
#include "image/image.hpp"
#include "scene_file.hpp"
#include "strata/strata.h"

namespace fs = std::filesystem;

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage: strata-golden render <scene> <out.png> [--surface headless|window]\n"
                 "       strata-golden check <scenes-dir> <golden-dir> <out-dir> [--platform NAME] [--only NAME]\n"
                 "                           [--bless] [--threshold DE] [--max-fraction F] [--surface KIND]\n");
    return 2;
}

bool check(StrataStatus status, const char* what) {
    if (status == STRATA_OK) return true;
    std::fprintf(stderr, "%s failed: %s (%s)\n", what, strata_status_string(status), strata_last_error());
    return false;
}

// Renders a scene with a fresh engine and returns its pixels.
bool renderScene(const strata::scene::Scene& scene, int32_t surface, strata::image::Image& out, std::string& gpu) {
    StrataEngineDesc desc{};
    desc.struct_size = sizeof desc;
    desc.abi_version = STRATA_ABI_VERSION;
    desc.surface = surface;
    desc.width = scene.width;
    desc.height = scene.height;
    desc.title = "strata-golden";
    desc.flags = surface == STRATA_SURFACE_WINDOW ? static_cast<uint32_t>(STRATA_FLAG_HIDDEN_WINDOW) : 0u;
    StrataEngine* engine = nullptr;
    if (!check(strata_create(&desc, &engine), "strata_create")) return false;
    gpu = strata_gpu_description(engine);

    bool ok = true;
    int32_t width = 0, height = 0;
    ok = ok && check(strata_get_drawable_size(engine, &width, &height), "strata_get_drawable_size");
    StrataCamera camera{};
    const float up[3] = {0, 1, 0};
    const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
    ok = ok && check(strata_camera_look_at(scene.eye, scene.target, up, scene.fovY, aspect, scene.nearPlane,
                                           scene.farPlane, &camera),
                     "strata_camera_look_at");
    ok = ok && check(strata_set_camera(engine, &camera), "strata_set_camera");
    ok = ok && check(strata_set_lighting(engine, &scene.lighting), "strata_set_lighting");
    if (scene.hasSunDate) {
        StrataSolarPosition sun{};
        ok = ok && check(strata_solar_position(scene.sunUnixSeconds, scene.sunLatitude, scene.sunLongitude, &sun),
                         "strata_solar_position");
        ok = ok && check(strata_set_sun_direction(engine, sun.to_sun), "strata_set_sun_direction");
    }
    if (scene.hasTerrain) {
        StrataTerrainTile tile = scene.terrain;  // point at this scene's arrays, whatever copies were made
        tile.heights = scene.terrainHeights.data();
        tile.colors = scene.terrainColors.empty() ? nullptr : scene.terrainColors.data();
        ok = ok && check(strata_set_terrain(engine, &tile), "strata_set_terrain");
    }
    std::vector<std::pair<std::string, uint32_t>> textures;
    for (const auto& t : scene.textures) {
        uint32_t id = 0;
        ok = ok && check(strata_create_texture_rgba8(engine, t.width, t.height, t.rgba.data(), t.flags, &id),
                         "strata_create_texture_rgba8");
        textures.emplace_back(t.name, id);
    }
    std::vector<uint32_t> meshIds;
    for (const auto& mesh : scene.meshes) {
        const StrataMesh view = mesh.view();
        uint32_t id = 0;
        if (mesh.textured()) {
            StrataTexturedMesh textured{};
            textured.mesh = view;
            textured.uvs = mesh.uvs.empty() ? nullptr : mesh.uvs.data();
            for (const auto& t : textures) {
                if (t.first == mesh.texture) textured.texture = t.second;
            }
            if (!mesh.texture.empty() && textured.texture == 0) {
                std::fprintf(stderr, "unknown texture '%s'\n", mesh.texture.c_str());
                ok = false;
            }
            textured.flags = mesh.unlit ? STRATA_MESH_UNLIT : 0u;
            ok = ok && check(strata_add_textured_mesh(engine, &textured, &id), "strata_add_textured_mesh");
        } else {
            ok = ok && check(strata_add_mesh(engine, &view, &id), "strata_add_mesh");
        }
        if (ok && mesh.hasTransform) {
            ok = check(strata_set_mesh_transform(engine, id, mesh.transform), "strata_set_mesh_transform");
        }
        meshIds.push_back(id);
    }
    for (const auto& r : scene.rayExpectations) {
        if (!ok) break;
        StrataRayHit hit{};
        ok = check(strata_raycast_meshes(engine, r.origin, r.direction, r.maxDistance, &hit), "strata_raycast_meshes");
        const uint32_t want = r.mesh < 0 ? 0u : meshIds[static_cast<size_t>(r.mesh)];
        if (ok && (hit.mesh_id != want ||
                   (want != 0 && r.hasDistance && std::fabs(hit.distance - r.distance) > 1e-3f))) {
            std::fprintf(stderr, "expect_ray on line %d: wanted mesh id %u at %.4f, got mesh id %u at %.4f\n", r.line,
                         want, static_cast<double>(r.distance), hit.mesh_id, static_cast<double>(hit.distance));
            ok = false;
        }
    }
    if (!scene.pointLights.empty()) {
        ok = ok && check(strata_set_point_lights(engine, scene.pointLights.data(),
                                                 static_cast<int32_t>(scene.pointLights.size())),
                         "strata_set_point_lights");
    }
    // Two frames: the second proves a frame can follow a frame, and is the one captured.
    for (int frame = 0; ok && frame < 2; ++frame) {
        if (frame == 1) {
            // Texture updates land between the frames, after the texture has
            // been drawn once, so the captured frame shows them.
            for (const auto& u : scene.textureUpdates) {
                uint32_t id = 0;
                for (const auto& t : textures) {
                    if (t.first == u.name) id = t.second;
                }
                ok = ok && check(strata_update_texture_rgba8(engine, id, u.x, u.y, u.width, u.height, u.rgba.data()),
                                 "strata_update_texture_rgba8");
            }
            if (!ok) break;
        }
        ok = ok && check(strata_begin_frame(engine, &scene.frame), "strata_begin_frame");
        ok = ok && check(strata_submit_primitives(engine, scene.items.data(), static_cast<int32_t>(scene.items.size())),
                         "strata_submit_primitives");
        if (!scene.lines.empty()) {
            ok = ok && check(strata_submit_lines(engine, scene.lines.data(), static_cast<int32_t>(scene.lines.size())),
                             "strata_submit_lines");
        }
        if (!scene.points.empty()) {
            ok = ok && check(strata_submit_points(engine, scene.points.data(),
                                                  static_cast<int32_t>(scene.points.size())),
                             "strata_submit_points");
        }
        if (!scene.texts.empty()) {
            std::vector<StrataText> texts;
            for (const auto& t : scene.texts) {
                texts.push_back(t.text);
                texts.back().utf8 = t.value.c_str();
            }
            ok = ok && check(strata_submit_text(engine, texts.data(), static_cast<int32_t>(texts.size())),
                             "strata_submit_text");
        }
        ok = ok && check(strata_end_frame(engine), "strata_end_frame");
    }
    if (ok) {
        out.width = width;
        out.height = height;
        out.rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        int32_t w = 0, h = 0;
        ok = check(strata_read_pixels_rgba8(engine, out.rgba.data(), static_cast<int32_t>(out.rgba.size()), &w, &h),
                   "strata_read_pixels_rgba8");
        for (size_t i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = 255;
        StrataFrameStats stats{};
        strata_get_frame_stats(engine, &stats);
        std::printf("  %d instances (cube %d, capsule %d, cylinder %d, sphere %d), %d draws, %d cascades, %.2f ms CPU\n",
                    stats.instances, stats.instances_per_kind[0], stats.instances_per_kind[1],
                    stats.instances_per_kind[2], stats.instances_per_kind[3], stats.draw_calls, stats.shadow_cascades,
                    static_cast<double>(stats.cpu_ms_frame));
    }
    if (surface == STRATA_SURFACE_WINDOW) strata_present(engine);
    strata_destroy(engine);
    return ok;
}

int32_t parseSurface(const std::string& name) {
    if (name == "headless") return STRATA_SURFACE_HEADLESS;
    if (name == "window") return STRATA_SURFACE_WINDOW;
    std::fprintf(stderr, "unknown surface '%s' (headless or window)\n", name.c_str());
    std::exit(2);
}

int commandRender(int argc, char** argv) {
    if (argc < 4) return usage();
    int32_t surface = STRATA_SURFACE_HEADLESS;
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "--surface") == 0 && i + 1 < argc) surface = parseSurface(argv[++i]);
        else return usage();
    }
    strata::scene::Scene scene;
    try {
        scene = strata::scene::loadScene(argv[2]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    strata::image::Image image;
    std::string gpu;
    std::printf("%s\n", argv[2]);
    if (!renderScene(scene, surface, image, gpu)) return 1;
    std::printf("  GPU: %s\n", gpu.c_str());
    std::string error;
    if (!strata::image::writePng(argv[3], image, &error)) {
        std::fprintf(stderr, "could not write %s: %s\n", argv[3], error.c_str());
        return 1;
    }
    std::printf("  wrote %s\n", argv[3]);
    return 0;
}

int commandCheck(int argc, char** argv) {
    if (argc < 5) return usage();
    const fs::path scenesDir = argv[2], goldenDir = argv[3], outDir = argv[4];
    std::string platform = "linux-llvmpipe", only;
    bool bless = false;
    strata::image::CompareOptions options;
    int32_t surface = STRATA_SURFACE_HEADLESS;
    for (int i = 5; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--platform" && hasValue) platform = argv[++i];
        else if (arg == "--only" && hasValue) only = argv[++i];
        else if (arg == "--bless") bless = true;
        else if (arg == "--threshold" && hasValue) options.pixelThreshold = std::atof(argv[++i]);
        else if (arg == "--max-fraction" && hasValue) options.maxFraction = std::atof(argv[++i]);
        else if (arg == "--surface" && hasValue) surface = parseSurface(argv[++i]);
        else return usage();
    }

    std::vector<fs::path> scenes;
    for (const auto& entry : fs::directory_iterator(scenesDir)) {
        const std::string name = entry.path().filename().string();
        if (entry.path().extension() == ".scene" && name[0] != '_') {
            if (only.empty() || entry.path().stem() == only) scenes.push_back(entry.path());
        }
    }
    std::sort(scenes.begin(), scenes.end());
    if (scenes.empty()) {
        std::fprintf(stderr, "no scenes found in %s\n", scenesDir.string().c_str());
        return 1;
    }
    fs::create_directories(outDir);

    int failures = 0;
    std::string gpu;
    for (const fs::path& scenePath : scenes) {
        const std::string name = scenePath.stem().string();
        std::printf("%s\n", name.c_str());
        strata::scene::Scene scene;
        try {
            scene = strata::scene::loadScene(scenePath.string());
        } catch (const std::exception& e) {
            std::fprintf(stderr, "  %s\n", e.what());
            ++failures;
            continue;
        }
        strata::image::Image actual;
        if (!renderScene(scene, surface, actual, gpu)) {
            ++failures;
            continue;
        }
        const fs::path golden = goldenDir / name / (platform + ".png");
        std::string error;
        if (bless) {
            fs::create_directories(golden.parent_path());
            if (!strata::image::writePng(golden.string(), actual, &error)) {
                std::fprintf(stderr, "  could not write %s: %s\n", golden.string().c_str(), error.c_str());
                ++failures;
            } else {
                std::printf("  blessed %s\n", golden.string().c_str());
            }
            continue;
        }
        strata::image::Image expected;
        const fs::path actualPath = outDir / (name + ".actual.png");
        if (!strata::image::readPng(golden.string(), expected, &error)) {
            std::fprintf(stderr, "  FAIL: no golden at %s (%s)\n", golden.string().c_str(), error.c_str());
            strata::image::writePng(actualPath.string(), actual, nullptr);
            ++failures;
            continue;
        }
        const auto result = strata::image::compare(expected, actual, options);
        if (!result.sameSize) {
            std::printf("  FAIL: size %dx%d, golden is %dx%d\n", actual.width, actual.height, expected.width,
                        expected.height);
        } else {
            std::printf("  %s: %lld of %lld pixels over Delta E %.1f (%.4f%%, limit %.4f%%), max %.2f, mean %.4f\n",
                        result.passed ? "pass" : "FAIL", static_cast<long long>(result.pixelsOver),
                        static_cast<long long>(result.pixels), options.pixelThreshold, result.fractionOver * 100.0,
                        options.maxFraction * 100.0, result.maxDeltaE, result.meanDeltaE);
        }
        if (!result.passed) {
            ++failures;
            strata::image::writePng(actualPath.string(), actual, nullptr);
            strata::image::writePng((outDir / (name + ".diff.png")).string(),
                                    strata::image::diffImage(expected, actual, options.pixelThreshold), nullptr);
            std::printf("  wrote %s and its diff\n", actualPath.string().c_str());
        }
    }
    std::printf("GPU: %s\n%zu scene(s), %d failure(s)\n", gpu.c_str(), scenes.size(), failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    std::printf("strata %s (ABI %u.%u)\n", strata_version_string(), strata_abi_version(), strata_abi_minor());
    if (std::strcmp(argv[1], "render") == 0) return commandRender(argc, argv);
    if (std::strcmp(argv[1], "check") == 0) return commandCheck(argc, argv);
    return usage();
}
