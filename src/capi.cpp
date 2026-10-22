// The C ABI: a thin shim over strata::Engine.
//
// Every entry point validates its pointers, catches every exception and turns
// it into a status code plus thread-local error text. Nothing throws across
// this boundary and nothing here holds on to a caller's pointer.
#include <cstddef>
#include <cstring>
#include <exception>
#include <new>
#include <string>

#include "core/error.hpp"
#include "core/log.hpp"
#include "strata/strata.h"
#include "strata/strata.hpp"

#ifndef STRATA_VERSION_STRING
#define STRATA_VERSION_STRING "0.0.0"
#endif

struct StrataEngine {
    std::unique_ptr<strata::Engine> engine;
};

namespace {

thread_local std::string t_lastError;

void setError(const std::string& message) { t_lastError = message; }

template <typename Fn>
StrataStatus guard(Fn&& fn) {
    try {
        fn();
        return STRATA_OK;
    } catch (const strata::Error& e) {
        setError(e.what());
        return e.status();
    } catch (const std::bad_alloc&) {
        setError("out of memory");
        return STRATA_ERROR_INTERNAL;
    } catch (const std::exception& e) {
        setError(e.what());
        return STRATA_ERROR_INTERNAL;
    } catch (...) {
        setError("unknown internal error");
        return STRATA_ERROR_INTERNAL;
    }
}

StrataStatus invalid(const char* message) {
    setError(message);
    return STRATA_ERROR_INVALID_ARGUMENT;
}

// One table drives both strata_struct_size and strata_struct_field_offset.
struct FieldTable {
    int32_t size;
    const int32_t* offsets;
    int32_t count;
};

#define STRATA_OFFSET(type, field) static_cast<int32_t>(offsetof(type, field))

const int32_t kRenderItemFields[] = {
    STRATA_OFFSET(StrataRenderItem, kind), STRATA_OFFSET(StrataRenderItem, position),
    STRATA_OFFSET(StrataRenderItem, scale), STRATA_OFFSET(StrataRenderItem, color)};
const int32_t kEngineDescFields[] = {
    STRATA_OFFSET(StrataEngineDesc, struct_size), STRATA_OFFSET(StrataEngineDesc, abi_version),
    STRATA_OFFSET(StrataEngineDesc, surface),     STRATA_OFFSET(StrataEngineDesc, width),
    STRATA_OFFSET(StrataEngineDesc, height),      STRATA_OFFSET(StrataEngineDesc, flags),
    STRATA_OFFSET(StrataEngineDesc, title),       STRATA_OFFSET(StrataEngineDesc, get_proc_address),
    STRATA_OFFSET(StrataEngineDesc, get_proc_address_user)};
const int32_t kCameraFields[] = {STRATA_OFFSET(StrataCamera, view), STRATA_OFFSET(StrataCamera, projection),
                                 STRATA_OFFSET(StrataCamera, eye), STRATA_OFFSET(StrataCamera, near_plane),
                                 STRATA_OFFSET(StrataCamera, far_plane)};
const int32_t kLightingFields[] = {
    STRATA_OFFSET(StrataLighting, mode),           STRATA_OFFSET(StrataLighting, legacy_direction),
    STRATA_OFFSET(StrataLighting, legacy_ambient), STRATA_OFFSET(StrataLighting, time_of_day),
    STRATA_OFFSET(StrataLighting, latitude_degrees), STRATA_OFFSET(StrataLighting, cascade_count),
    STRATA_OFFSET(StrataLighting, shadow_map_size), STRATA_OFFSET(StrataLighting, shadow_distance),
    STRATA_OFFSET(StrataLighting, split_lambda),   STRATA_OFFSET(StrataLighting, fog)};
const int32_t kFrameDescFields[] = {STRATA_OFFSET(StrataFrameDesc, clear_flags),
                                    STRATA_OFFSET(StrataFrameDesc, clear_color)};
const int32_t kFrameStatsFields[] = {
    STRATA_OFFSET(StrataFrameStats, draw_calls),      STRATA_OFFSET(StrataFrameStats, instances),
    STRATA_OFFSET(StrataFrameStats, instances_per_kind), STRATA_OFFSET(StrataFrameStats, items_rejected),
    STRATA_OFFSET(StrataFrameStats, shadow_cascades), STRATA_OFFSET(StrataFrameStats, cpu_ms_shadow),
    STRATA_OFFSET(StrataFrameStats, cpu_ms_opaque),   STRATA_OFFSET(StrataFrameStats, cpu_ms_sky),
    STRATA_OFFSET(StrataFrameStats, cpu_ms_frame),    STRATA_OFFSET(StrataFrameStats, gpu_finish_ms)};
const int32_t kEventFields[] = {
    STRATA_OFFSET(StrataEvent, type),   STRATA_OFFSET(StrataEvent, scancode), STRATA_OFFSET(StrataEvent, modifiers),
    STRATA_OFFSET(StrataEvent, repeat), STRATA_OFFSET(StrataEvent, button),   STRATA_OFFSET(StrataEvent, width),
    STRATA_OFFSET(StrataEvent, height), STRATA_OFFSET(StrataEvent, x),        STRATA_OFFSET(StrataEvent, y),
    STRATA_OFFSET(StrataEvent, dx),     STRATA_OFFSET(StrataEvent, dy),       STRATA_OFFSET(StrataEvent, text)};
const int32_t kPointLightFields[] = {
    STRATA_OFFSET(StrataPointLight, position), STRATA_OFFSET(StrataPointLight, range),
    STRATA_OFFSET(StrataPointLight, color),    STRATA_OFFSET(StrataPointLight, intensity),
    STRATA_OFFSET(StrataPointLight, flags)};
const int32_t kTerrainTileFields[] = {
    STRATA_OFFSET(StrataTerrainTile, origin),    STRATA_OFFSET(StrataTerrainTile, cell_size),
    STRATA_OFFSET(StrataTerrainTile, samples_x), STRATA_OFFSET(StrataTerrainTile, samples_z),
    STRATA_OFFSET(StrataTerrainTile, heights),   STRATA_OFFSET(StrataTerrainTile, colors),
    STRATA_OFFSET(StrataTerrainTile, color)};
const int32_t kMeshFields[] = {
    STRATA_OFFSET(StrataMesh, positions),    STRATA_OFFSET(StrataMesh, normals),
    STRATA_OFFSET(StrataMesh, colors),       STRATA_OFFSET(StrataMesh, indices),
    STRATA_OFFSET(StrataMesh, vertex_count), STRATA_OFFSET(StrataMesh, index_count),
    STRATA_OFFSET(StrataMesh, color)};
const int32_t kLineVertexFields[] = {STRATA_OFFSET(StrataLineVertex, position), STRATA_OFFSET(StrataLineVertex, color)};
const int32_t kPointFields[] = {STRATA_OFFSET(StrataPoint, position), STRATA_OFFSET(StrataPoint, size_pixels),
                                STRATA_OFFSET(StrataPoint, world_diameter), STRATA_OFFSET(StrataPoint, color),
                                STRATA_OFFSET(StrataPoint, flags)};
const int32_t kTextFields[] = {STRATA_OFFSET(StrataText, x),     STRATA_OFFSET(StrataText, y),
                               STRATA_OFFSET(StrataText, pixel_size), STRATA_OFFSET(StrataText, flags),
                               STRATA_OFFSET(StrataText, color), STRATA_OFFSET(StrataText, utf8)};

const int32_t kSolarPositionFields[] = {
    STRATA_OFFSET(StrataSolarPosition, to_sun),           STRATA_OFFSET(StrataSolarPosition, elevation_degrees),
    STRATA_OFFSET(StrataSolarPosition, azimuth_degrees),  STRATA_OFFSET(StrataSolarPosition, declination_degrees),
    STRATA_OFFSET(StrataSolarPosition, hour_angle_degrees),
    STRATA_OFFSET(StrataSolarPosition, equation_of_time_minutes)};

const int32_t kRayHitFields[] = {STRATA_OFFSET(StrataRayHit, mesh_id), STRATA_OFFSET(StrataRayHit, triangle),
                                 STRATA_OFFSET(StrataRayHit, distance), STRATA_OFFSET(StrataRayHit, point),
                                 STRATA_OFFSET(StrataRayHit, normal)};

const int32_t kTexturedMeshFields[] = {STRATA_OFFSET(StrataTexturedMesh, mesh), STRATA_OFFSET(StrataTexturedMesh, uvs),
                                       STRATA_OFFSET(StrataTexturedMesh, texture),
                                       STRATA_OFFSET(StrataTexturedMesh, flags)};

#define STRATA_TABLE(type, fields) \
    FieldTable { static_cast<int32_t>(sizeof(type)), fields, static_cast<int32_t>(sizeof(fields) / sizeof(fields[0])) }

const FieldTable* fieldTable(int32_t structId) {
    static const FieldTable tables[] = {
        STRATA_TABLE(StrataRenderItem, kRenderItemFields), STRATA_TABLE(StrataEngineDesc, kEngineDescFields),
        STRATA_TABLE(StrataCamera, kCameraFields),         STRATA_TABLE(StrataLighting, kLightingFields),
        STRATA_TABLE(StrataFrameDesc, kFrameDescFields),   STRATA_TABLE(StrataFrameStats, kFrameStatsFields),
        STRATA_TABLE(StrataEvent, kEventFields),           STRATA_TABLE(StrataPointLight, kPointLightFields),
        STRATA_TABLE(StrataTerrainTile, kTerrainTileFields), STRATA_TABLE(StrataMesh, kMeshFields),
        STRATA_TABLE(StrataLineVertex, kLineVertexFields), STRATA_TABLE(StrataPoint, kPointFields),
        STRATA_TABLE(StrataText, kTextFields),             STRATA_TABLE(StrataSolarPosition, kSolarPositionFields),
        STRATA_TABLE(StrataTexturedMesh, kTexturedMeshFields), STRATA_TABLE(StrataRayHit, kRayHitFields)};
    if (structId < 0 || structId >= static_cast<int32_t>(sizeof tables / sizeof tables[0])) return nullptr;
    return &tables[structId];
}

}  // namespace

extern "C" {

uint32_t strata_abi_version(void) { return STRATA_ABI_VERSION; }

uint32_t strata_abi_minor(void) { return STRATA_ABI_MINOR; }

const char* strata_version_string(void) { return STRATA_VERSION_STRING; }

int32_t strata_struct_size(int32_t struct_id) {
    const FieldTable* table = fieldTable(struct_id);
    return table ? table->size : -1;
}

int32_t strata_struct_field_offset(int32_t struct_id, int32_t field_id) {
    const FieldTable* table = fieldTable(struct_id);
    if (!table || field_id < 0 || field_id >= table->count) return -1;
    return table->offsets[field_id];
}

const char* strata_last_error(void) { return t_lastError.c_str(); }

const char* strata_status_string(StrataStatus status) {
    switch (status) {
        case STRATA_OK: return "ok";
        case STRATA_ERROR_INVALID_ARGUMENT: return "invalid argument";
        case STRATA_ERROR_ABI_MISMATCH: return "ABI mismatch";
        case STRATA_ERROR_PLATFORM: return "platform error";
        case STRATA_ERROR_GPU: return "GPU error";
        case STRATA_ERROR_IO: return "I/O error";
        case STRATA_ERROR_INVALID_STATE: return "invalid state";
        case STRATA_ERROR_UNSUPPORTED: return "unsupported";
        case STRATA_ERROR_INTERNAL: return "internal error";
        default: return "unknown status";
    }
}

void strata_set_log_callback(StrataLogFn fn, void* user) { strata::setLogSink(fn, user); }

StrataStatus strata_create(const StrataEngineDesc* desc, StrataEngine** out_engine) {
    if (!out_engine) return invalid("out_engine is NULL");
    *out_engine = nullptr;
    if (!desc) return invalid("desc is NULL");
    if (desc->abi_version != STRATA_ABI_VERSION) {
        setError("the host was built against Strata ABI " + std::to_string(desc->abi_version) +
                 " but this library implements ABI " + std::to_string(STRATA_ABI_VERSION));
        return STRATA_ERROR_ABI_MISMATCH;
    }
    if (desc->struct_size != sizeof(StrataEngineDesc)) {
        setError("StrataEngineDesc.struct_size is " + std::to_string(desc->struct_size) + ", expected " +
                 std::to_string(sizeof(StrataEngineDesc)));
        return STRATA_ERROR_ABI_MISMATCH;
    }
    return guard([&] {
        strata::EngineDesc d;
        d.surface = desc->surface;
        d.width = desc->width;
        d.height = desc->height;
        d.flags = desc->flags;
        d.title = desc->title ? desc->title : "Strata";
        d.getProcAddress = desc->get_proc_address;
        d.getProcAddressUser = desc->get_proc_address_user;
        auto handle = std::make_unique<StrataEngine>();
        handle->engine = strata::Engine::create(d);
        *out_engine = handle.release();
    });
}

void strata_destroy(StrataEngine* engine) {
    if (!engine) return;
    try {
        delete engine;
    } catch (...) {
        // A destructor that throws has nowhere to report to; swallow it rather
        // than take the host down.
    }
}

const char* strata_gpu_description(const StrataEngine* engine) {
    if (!engine) return "";
    return engine->engine->gpuDescription().c_str();
}

int32_t strata_poll_event(StrataEngine* engine, StrataEvent* out) {
    if (!engine || !out) return 0;
    int32_t result = 0;
    guard([&] { result = engine->engine->pollEvent(*out) ? 1 : 0; });
    return result;
}

StrataStatus strata_get_drawable_size(const StrataEngine* engine, int32_t* width, int32_t* height) {
    if (!engine || !width || !height) return invalid("engine, width and height must not be NULL");
    return guard([&] { engine->engine->drawableSize(*width, *height); });
}

StrataStatus strata_resize(StrataEngine* engine, int32_t width, int32_t height) {
    if (!engine) return invalid("engine is NULL");
    return guard([&] { engine->engine->resize(width, height); });
}

StrataStatus strata_set_camera(StrataEngine* engine, const StrataCamera* camera) {
    if (!engine || !camera) return invalid("engine and camera must not be NULL");
    return guard([&] { engine->engine->setCamera(*camera); });
}

StrataStatus strata_camera_look_at(const float eye[3], const float target[3], const float up[3], float fov_y_degrees,
                                   float aspect, float near_plane, float far_plane, StrataCamera* out) {
    if (!eye || !target || !up || !out) return invalid("eye, target, up and out must not be NULL");
    if (!(fov_y_degrees > 0.0f && fov_y_degrees < 180.0f) || !(aspect > 0.0f) || !(near_plane > 0.0f) ||
        !(far_plane > near_plane)) {
        return invalid("need 0 < fov < 180, aspect > 0 and 0 < near < far");
    }
    return guard([&] { *out = strata::cameraLookAt(eye, target, up, fov_y_degrees, aspect, near_plane, far_plane); });
}

StrataStatus strata_lighting_defaults(int32_t mode, StrataLighting* out) {
    if (!out) return invalid("out is NULL");
    if (mode != STRATA_LIGHTING_LEGACY && mode != STRATA_LIGHTING_SUN && mode != STRATA_LIGHTING_SPACE) {
        return invalid("unknown lighting mode");
    }
    return guard([&] { *out = strata::lightingDefaults(mode); });
}

StrataStatus strata_set_lighting(StrataEngine* engine, const StrataLighting* lighting) {
    if (!engine || !lighting) return invalid("engine and lighting must not be NULL");
    return guard([&] { engine->engine->setLighting(*lighting); });
}

StrataStatus strata_solar_position(double unix_seconds_utc, double latitude_degrees, double longitude_degrees,
                                   StrataSolarPosition* out) {
    if (!out) return invalid("out must not be NULL");
    return guard([&] { *out = strata::solarPosition(unix_seconds_utc, latitude_degrees, longitude_degrees); });
}

StrataStatus strata_set_sun_direction(StrataEngine* engine, const float to_sun[3]) {
    if (!engine) return invalid("engine must not be NULL");
    return guard([&] { engine->engine->setSunDirection(to_sun); });
}

StrataStatus strata_set_point_lights(StrataEngine* engine, const StrataPointLight* lights, int32_t count) {
    if (!engine) return invalid("engine is NULL");
    if (count < 0) return invalid("count is negative");
    if (count > 0 && !lights) return invalid("lights is NULL with a non-zero count");
    return guard([&] { engine->engine->setPointLights(lights, static_cast<size_t>(count)); });
}

StrataStatus strata_set_terrain(StrataEngine* engine, const StrataTerrainTile* tile) {
    if (!engine) return invalid("engine is NULL");
    return guard([&] { engine->engine->setTerrain(tile); });
}

StrataStatus strata_add_mesh(StrataEngine* engine, const StrataMesh* mesh, uint32_t* out_id) {
    if (!engine || !mesh || !out_id) return invalid("engine, mesh and out_id must not be NULL");
    return guard([&] { *out_id = engine->engine->addMesh(*mesh); });
}

StrataStatus strata_add_textured_mesh(StrataEngine* engine, const StrataTexturedMesh* mesh, uint32_t* out_id) {
    if (!engine || !mesh || !out_id) return invalid("engine, mesh and out_id must not be NULL");
    return guard([&] { *out_id = engine->engine->addTexturedMesh(*mesh); });
}

StrataStatus strata_set_mesh_transform(StrataEngine* engine, uint32_t id, const float model[16]) {
    if (!engine) return invalid("engine must not be NULL");
    return guard([&] { engine->engine->setMeshTransform(id, model); });
}

StrataStatus strata_create_texture_rgba8(StrataEngine* engine, int32_t width, int32_t height, const uint8_t* rgba,
                                         uint32_t flags, uint32_t* out_id) {
    if (!engine || !rgba || !out_id) return invalid("engine, rgba and out_id must not be NULL");
    return guard([&] { *out_id = engine->engine->createTextureRgba8(width, height, rgba, flags); });
}

StrataStatus strata_update_texture_rgba8(StrataEngine* engine, uint32_t id, int32_t x, int32_t y, int32_t width,
                                         int32_t height, const uint8_t* rgba) {
    if (!engine || !rgba) return invalid("engine and rgba must not be NULL");
    return guard([&] { engine->engine->updateTextureRgba8(id, x, y, width, height, rgba); });
}

StrataStatus strata_raycast_meshes(StrataEngine* engine, const float origin[3], const float direction[3],
                                   float max_distance, StrataRayHit* out_hit) {
    if (!engine || !origin || !direction || !out_hit) {
        return invalid("engine, origin, direction and out_hit must not be NULL");
    }
    return guard([&] { *out_hit = engine->engine->raycastMeshes(origin, direction, max_distance); });
}

StrataStatus strata_set_relative_mouse_mode(StrataEngine* engine, int32_t enabled) {
    if (!engine) return invalid("engine must not be NULL");
    return guard([&] { engine->engine->setRelativeMouseMode(enabled != 0); });
}

StrataStatus strata_destroy_texture(StrataEngine* engine, uint32_t id) {
    if (!engine) return invalid("engine must not be NULL");
    return guard([&] { engine->engine->destroyTexture(id); });
}

StrataStatus strata_remove_mesh(StrataEngine* engine, uint32_t id) {
    if (!engine) return invalid("engine is NULL");
    return guard([&] { engine->engine->removeMesh(id); });
}

StrataStatus strata_clear_meshes(StrataEngine* engine) {
    if (!engine) return invalid("engine is NULL");
    return guard([&] { engine->engine->clearMeshes(); });
}

StrataStatus strata_begin_frame(StrataEngine* engine, const StrataFrameDesc* desc) {
    if (!engine || !desc) return invalid("engine and desc must not be NULL");
    return guard([&] { engine->engine->beginFrame(*desc); });
}

StrataStatus strata_submit_primitives(StrataEngine* engine, const StrataRenderItem* items, int32_t count) {
    if (!engine) return invalid("engine is NULL");
    if (count < 0) return invalid("count is negative");
    if (count > 0 && !items) return invalid("items is NULL with a non-zero count");
    return guard([&] { engine->engine->submitPrimitives(items, static_cast<size_t>(count)); });
}

StrataStatus strata_submit_lines(StrataEngine* engine, const StrataLineVertex* vertices, int32_t count) {
    if (!engine) return invalid("engine is NULL");
    if (count < 0) return invalid("count is negative");
    if (count > 0 && !vertices) return invalid("vertices is NULL with a non-zero count");
    return guard([&] { engine->engine->submitLines(vertices, static_cast<size_t>(count)); });
}

StrataStatus strata_submit_points(StrataEngine* engine, const StrataPoint* points, int32_t count) {
    if (!engine) return invalid("engine is NULL");
    if (count < 0) return invalid("count is negative");
    if (count > 0 && !points) return invalid("points is NULL with a non-zero count");
    return guard([&] { engine->engine->submitPoints(points, static_cast<size_t>(count)); });
}

StrataStatus strata_submit_text(StrataEngine* engine, const StrataText* texts, int32_t count) {
    if (!engine) return invalid("engine is NULL");
    if (count < 0) return invalid("count is negative");
    if (count > 0 && !texts) return invalid("texts is NULL with a non-zero count");
    return guard([&] { engine->engine->submitText(texts, static_cast<size_t>(count)); });
}

StrataStatus strata_end_frame(StrataEngine* engine) {
    if (!engine) return invalid("engine is NULL");
    return guard([&] { engine->engine->endFrame(); });
}

StrataStatus strata_present(StrataEngine* engine) {
    if (!engine) return invalid("engine is NULL");
    return guard([&] { engine->engine->present(); });
}

StrataStatus strata_get_frame_stats(const StrataEngine* engine, StrataFrameStats* out) {
    if (!engine || !out) return invalid("engine and out must not be NULL");
    *out = engine->engine->frameStats();
    return STRATA_OK;
}

StrataStatus strata_read_pixels_rgba8(StrataEngine* engine, uint8_t* dst, int32_t capacity_bytes, int32_t* out_width,
                                      int32_t* out_height) {
    if (!engine || !dst || !out_width || !out_height) return invalid("engine, dst, out_width and out_height must not be NULL");
    return guard([&] {
        std::vector<uint8_t> rgba;
        int32_t width = 0, height = 0;
        engine->engine->readPixels(rgba, width, height);
        if (capacity_bytes < 0 || rgba.size() > static_cast<size_t>(capacity_bytes)) {
            strata::fail(STRATA_ERROR_INVALID_ARGUMENT, "dst holds %d bytes but the frame needs %zu", capacity_bytes,
                         rgba.size());
        }
        std::memcpy(dst, rgba.data(), rgba.size());
        *out_width = width;
        *out_height = height;
    });
}

StrataStatus strata_save_screenshot_png(StrataEngine* engine, const char* utf8_path) {
    if (!engine || !utf8_path || !*utf8_path) return invalid("engine and a non-empty path are required");
    return guard([&] { engine->engine->saveScreenshotPng(utf8_path); });
}

}  // extern "C"
