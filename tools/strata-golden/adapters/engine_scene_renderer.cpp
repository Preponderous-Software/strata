#include "adapters/engine_scene_renderer.hpp"

#include <cmath>
#include <utility>
#include <vector>

#include "text.hpp"

namespace strata::golden {

EngineSceneRenderer::EngineSceneRenderer(Console& console) : console_(console) {}

bool EngineSceneRenderer::check(StrataStatus status, const char* what) {
    if (status == STRATA_OK) return true;
    console_.err(sformat("%s failed: %s (%s)\n", what, strata_status_string(status), strata_last_error()));
    return false;
}

// Renders a scene with a fresh engine and returns its pixels.
bool EngineSceneRenderer::render(const scene::Scene& scene, int32_t surface, image::Image& out, std::string& gpu) {
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
    if (scene.hasAmbient) {
        ok = ok && check(strata_set_ambient(engine, scene.ambientTint, scene.ambientLift), "strata_set_ambient");
    }
    if (scene.hasSunIntensity) {
        ok = ok && check(strata_set_sun_intensity(engine, scene.sunIntensity, scene.sunTint),
                         "strata_set_sun_intensity");
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
                console_.err(sformat("unknown texture '%s'\n", mesh.texture.c_str()));
                ok = false;
            }
            textured.flags = (mesh.unlit ? STRATA_MESH_UNLIT : 0u) | (mesh.translucent ? STRATA_MESH_TRANSLUCENT : 0u) |
                             (mesh.flat ? STRATA_MESH_FLAT_NORMALS : 0u);
            ok = ok && check(strata_add_textured_mesh(engine, &textured, &id), "strata_add_textured_mesh");
        } else {
            ok = ok && check(strata_add_mesh(engine, &view, &id), "strata_add_mesh");
        }
        if (ok && mesh.hasTransform) {
            ok = check(strata_set_mesh_transform(engine, id, mesh.transform), "strata_set_mesh_transform");
        }
        if (ok && mesh.hidden) ok = check(strata_set_mesh_visible(engine, id, 0), "strata_set_mesh_visible");
        meshIds.push_back(id);
    }
    for (const auto& r : scene.rayExpectations) {
        if (!ok) break;
        StrataRayHit hit{};
        ok = check(strata_raycast_meshes(engine, r.origin, r.direction, r.maxDistance, &hit), "strata_raycast_meshes");
        const uint32_t want = r.mesh < 0 ? 0u : meshIds[static_cast<size_t>(r.mesh)];
        if (ok && (hit.mesh_id != want ||
                   (want != 0 && r.hasDistance && std::fabs(hit.distance - r.distance) > 1e-3f))) {
            console_.err(sformat("expect_ray on line %d: wanted mesh id %u at %.4f, got mesh id %u at %.4f\n", r.line,
                         want, static_cast<double>(r.distance), hit.mesh_id, static_cast<double>(hit.distance)));
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
        if (!scene.linesOnTop.empty()) {
            ok = ok && check(strata_submit_lines_ex(engine, scene.linesOnTop.data(),
                                                    static_cast<int32_t>(scene.linesOnTop.size()), STRATA_LINES_ON_TOP),
                             "strata_submit_lines_ex");
        }
        for (uint32_t flags = 0; flags < 4; ++flags) {
            const auto& wide = scene.wideLines[flags];
            if (wide.empty()) continue;
            ok = ok && check(strata_submit_wide_lines(engine, wide.data(), static_cast<int32_t>(wide.size()), flags),
                             "strata_submit_wide_lines");
        }
        if (!scene.rects.empty()) {
            ok = ok && check(strata_submit_rects(engine, scene.rects.data(), static_cast<int32_t>(scene.rects.size())),
                             "strata_submit_rects");
        }
        if (!scene.points.empty()) {
            ok = ok && check(strata_submit_points(engine, scene.points.data(),
                                                  static_cast<int32_t>(scene.points.size())),
                             "strata_submit_points");
        }
        if (!scene.labels.empty()) {
            std::vector<StrataText> texts;
            std::vector<float> anchors;
            for (const auto& l : scene.labels) {
                texts.push_back(l.text.text);
                texts.back().utf8 = l.text.value.c_str();
                anchors.insert(anchors.end(), l.anchor, l.anchor + 3);
            }
            ok = ok && check(strata_submit_world_text(engine, texts.data(), anchors.data(),
                                                      static_cast<int32_t>(texts.size())),
                             "strata_submit_world_text");
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
        if (ok && !scene.depthExpectations.empty()) ok = checkDepth(engine, scene, width, height);
        StrataFrameStats stats{};
        strata_get_frame_stats(engine, &stats);
        console_.out(sformat("  %d instances (cube %d, capsule %d, cylinder %d, sphere %d), %d draws, %d cascades, %.2f ms CPU\n",
                    stats.instances, stats.instances_per_kind[0], stats.instances_per_kind[1],
                    stats.instances_per_kind[2], stats.instances_per_kind[3], stats.draw_calls, stats.shadow_cascades,
                    static_cast<double>(stats.cpu_ms_frame)));
    }
    if (surface == STRATA_SURFACE_WINDOW) strata_present(engine);
    strata_destroy(engine);
    return ok;
}

// ABI 1.9: expect_depth lines, read from the captured frame.
bool EngineSceneRenderer::checkDepth(StrataEngine* engine, const scene::Scene& scene, int32_t width, int32_t height) {
    std::vector<float> depth(static_cast<size_t>(width) * static_cast<size_t>(height));
    int32_t w = 0, h = 0;
    const StrataStatus status = strata_read_depth_f32(engine, depth.data(),
                                                      static_cast<int32_t>(depth.size() * sizeof(float)), &w, &h);
    if (status == STRATA_ERROR_UNSUPPORTED) {
        console_.out(sformat("  expect_depth skipped: %s\n", strata_last_error()));
        return true;
    }
    if (!check(status, "strata_read_depth_f32")) return false;
    if (w != width || h != height) {
        console_.err(sformat("strata_read_depth_f32 read %dx%d, the frame is %dx%d\n", w, h, width, height));
        return false;
    }
    bool ok = true;
    for (const auto& d : scene.depthExpectations) {
        const float got = depth[static_cast<size_t>(d.y) * static_cast<size_t>(width) + static_cast<size_t>(d.x)];
        const bool pass = std::isinf(d.distance) ? (std::isinf(got) && got > 0.0f)
                                                 : std::fabs(got - d.distance) <= d.distance * 1e-3f;
        if (!pass) {
            console_.err(sformat("expect_depth on line %d: wanted %.4f at (%d, %d), got %.4f\n", d.line,
                         static_cast<double>(d.distance), d.x, d.y, static_cast<double>(got)));
            ok = false;
        }
    }
    console_.out(sformat("  %zu depth expectation(s) %s\n", scene.depthExpectations.size(), ok ? "met" : "FAILED"));
    return ok;
}

}  // namespace strata::golden
