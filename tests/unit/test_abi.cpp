// The C ABI's struct layouts are a promise to every binding. These checks pin
// them, so a change to strata.h that moves a field fails here first.
#include <cmath>
#include <cstddef>
#include <cstring>

#include "strata/strata.h"
#include "test.hpp"

static_assert(sizeof(StrataRenderItem) == 44, "Beyond Nations' RenderItem is 44 bytes");
static_assert(offsetof(StrataRenderItem, kind) == 0, "");
static_assert(offsetof(StrataRenderItem, position) == 4, "");
static_assert(offsetof(StrataRenderItem, scale) == 16, "");
static_assert(offsetof(StrataRenderItem, color) == 28, "");
static_assert(sizeof(StrataCamera) == 148, "");
static_assert(sizeof(StrataLighting) == 48, "");
static_assert(sizeof(StrataFrameDesc) == 20, "");
static_assert(sizeof(StrataFrameStats) == 52, "");
static_assert(sizeof(StrataEvent) == 76, "");
// ABI 1.1
static_assert(sizeof(StrataPointLight) == 36, "");
static_assert(offsetof(StrataPointLight, position) == 0, "");
static_assert(offsetof(StrataPointLight, range) == 12, "");
static_assert(offsetof(StrataPointLight, color) == 16, "");
static_assert(offsetof(StrataPointLight, intensity) == 28, "");
static_assert(offsetof(StrataPointLight, flags) == 32, "");
static_assert(STRATA_POINT_LIGHT_CAST_SHADOWS == 0x1u, "ABI 1.2");
// ABI 1.3
static_assert(offsetof(StrataTerrainTile, origin) == 0, "");
static_assert(offsetof(StrataTerrainTile, cell_size) == 12, "");
static_assert(offsetof(StrataTerrainTile, samples_x) == 16, "");
static_assert(offsetof(StrataTerrainTile, samples_z) == 20, "");
static_assert(offsetof(StrataTerrainTile, heights) == 24, "");
#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
static_assert(offsetof(StrataTerrainTile, colors) == 32, "64-bit targets");
static_assert(offsetof(StrataTerrainTile, color) == 40, "64-bit targets");
static_assert(sizeof(StrataTerrainTile) == 56, "64-bit targets");
#endif
// ABI 1.4
static_assert(offsetof(StrataMesh, positions) == 0, "");
#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
static_assert(offsetof(StrataMesh, normals) == 8, "64-bit targets");
static_assert(offsetof(StrataMesh, colors) == 16, "64-bit targets");
static_assert(offsetof(StrataMesh, indices) == 24, "64-bit targets");
static_assert(offsetof(StrataMesh, vertex_count) == 32, "64-bit targets");
static_assert(offsetof(StrataMesh, index_count) == 36, "64-bit targets");
static_assert(offsetof(StrataMesh, color) == 40, "64-bit targets");
static_assert(sizeof(StrataMesh) == 56, "64-bit targets");
#endif
static_assert(STRATA_MAX_MESHES == 256, "ABI 1.4");
// ABI 1.5
static_assert(sizeof(StrataLineVertex) == 28, "");
static_assert(offsetof(StrataLineVertex, color) == 12, "");
static_assert(sizeof(StrataPoint) == 40, "");
static_assert(offsetof(StrataPoint, size_pixels) == 12, "");
static_assert(offsetof(StrataPoint, world_diameter) == 16, "");
static_assert(offsetof(StrataPoint, color) == 20, "");
static_assert(offsetof(StrataPoint, flags) == 36, "");
static_assert(offsetof(StrataText, pixel_size) == 8, "");
static_assert(offsetof(StrataText, flags) == 12, "");
static_assert(offsetof(StrataText, color) == 16, "");
#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
static_assert(offsetof(StrataText, utf8) == 32, "64-bit targets");
static_assert(sizeof(StrataText) == 40, "64-bit targets");
#endif
static_assert(STRATA_LIGHTING_SPACE == 2, "ABI 1.5");
// ABI 1.6
static_assert(sizeof(StrataSolarPosition) == 32, "");
static_assert(offsetof(StrataSolarPosition, elevation_degrees) == 12, "");
static_assert(offsetof(StrataSolarPosition, azimuth_degrees) == 16, "");
static_assert(offsetof(StrataSolarPosition, declination_degrees) == 20, "");
static_assert(offsetof(StrataSolarPosition, hour_angle_degrees) == 24, "");
static_assert(offsetof(StrataSolarPosition, equation_of_time_minutes) == 28, "");
// ABI 1.7
static_assert(offsetof(StrataTexturedMesh, mesh) == 0, "");
#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
static_assert(offsetof(StrataTexturedMesh, uvs) == 56, "64-bit targets");
static_assert(offsetof(StrataTexturedMesh, texture) == 64, "64-bit targets");
static_assert(offsetof(StrataTexturedMesh, flags) == 68, "64-bit targets");
static_assert(sizeof(StrataTexturedMesh) == 72, "64-bit targets");
#endif
static_assert(STRATA_TEXTURE_MIPMAPS == 0x1u && STRATA_TEXTURE_CLAMP_U == 0x2u && STRATA_TEXTURE_CLAMP_V == 0x4u, "");
static_assert(STRATA_TEXTURE_NEAREST == 0x8u, "ABI 1.10");
static_assert(sizeof(StrataRect) == 32, "ABI 1.11");
static_assert(offsetof(StrataRect, width) == 8 && offsetof(StrataRect, color) == 16, "ABI 1.11");
static_assert(STRATA_MESH_TRANSLUCENT == 0x2u && STRATA_POINT_ON_TOP == 0x2u && STRATA_LINES_ON_TOP == 0x1u, "ABI 1.11");
static_assert(STRATA_MESH_UNLIT == 0x1u, "");
// ABI 1.8
static_assert(sizeof(StrataRayHit) == 36, "");
static_assert(offsetof(StrataRayHit, mesh_id) == 0, "");
static_assert(offsetof(StrataRayHit, triangle) == 4, "");
static_assert(offsetof(StrataRayHit, distance) == 8, "");
static_assert(offsetof(StrataRayHit, point) == 12, "");
static_assert(offsetof(StrataRayHit, normal) == 24, "");
static_assert(STRATA_STRUCT_RAY_HIT == 15, "");
// ABI 1.13
static_assert(sizeof(StrataWideLineVertex) == 36, "");
static_assert(offsetof(StrataWideLineVertex, color) == 12, "");
static_assert(offsetof(StrataWideLineVertex, width_pixels) == 28, "");
static_assert(offsetof(StrataWideLineVertex, world_width) == 32, "");
static_assert(STRATA_STRUCT_WIDE_LINE_VERTEX == 17, "");
static_assert(STRATA_LINES_ROUND_CAPS == 0x2u, "");
static_assert(STRATA_ABI_VERSION == 1u, "ABI 1 callers must keep working: additions bump STRATA_ABI_MINOR");
#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
static_assert(sizeof(StrataEngineDesc) == 48, "64-bit targets");
#endif

TEST(abi_version_is_reported) {
    CHECK(strata_abi_version() == STRATA_ABI_VERSION);
    CHECK(std::strlen(strata_version_string()) > 0);
    CHECK(strata_abi_minor() == STRATA_ABI_MINOR);
    CHECK(strata_abi_minor() >= 14u);
}

TEST(point_light_layout_is_queryable) {
    CHECK(strata_struct_size(STRATA_STRUCT_POINT_LIGHT) == 36);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_POINT_LIGHT, 0) == 0);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_POINT_LIGHT, 1) == 12);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_POINT_LIGHT, 2) == 16);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_POINT_LIGHT, 3) == 28);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_POINT_LIGHT, 4) == 32);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_POINT_LIGHT, 5) == -1);
}

TEST(overlay_layouts_are_queryable) {
    CHECK(strata_struct_size(STRATA_STRUCT_LINE_VERTEX) == 28);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_LINE_VERTEX, 1) == 12);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_LINE_VERTEX, 2) == -1);
    CHECK(strata_struct_size(STRATA_STRUCT_POINT) == 40);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_POINT, 4) == 36);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_POINT, 5) == -1);
    CHECK(strata_struct_size(STRATA_STRUCT_TEXT) == static_cast<int32_t>(sizeof(StrataText)));
    CHECK(strata_struct_field_offset(STRATA_STRUCT_TEXT, 5) == static_cast<int32_t>(offsetof(StrataText, utf8)));
    CHECK(strata_struct_field_offset(STRATA_STRUCT_TEXT, 6) == -1);
}

TEST(overlay_submits_reject_bad_arguments_without_a_context) {
    StrataLineVertex v[2]{};
    StrataPoint p{};
    StrataText t{};
    CHECK(strata_submit_lines(nullptr, v, 2) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_submit_points(nullptr, &p, 1) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_submit_text(nullptr, &t, 1) == STRATA_ERROR_INVALID_ARGUMENT);
    StrataEngine* engine = reinterpret_cast<StrataEngine*>(0x1);  // never dereferenced: count is checked first
    CHECK(strata_submit_lines(engine, v, -2) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_submit_points(engine, nullptr, 3) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_submit_text(engine, nullptr, 1) == STRATA_ERROR_INVALID_ARGUMENT);
}

TEST(space_lighting_defaults_and_validation) {
    StrataLighting l{};
    REQUIRE(strata_lighting_defaults(STRATA_LIGHTING_SPACE, &l) == STRATA_OK);
    CHECK(l.mode == STRATA_LIGHTING_SPACE);
    CHECK(l.legacy_ambient >= 0.0f && l.legacy_ambient <= 0.1f);
    CHECK(l.fog == 0);
}

TEST(solar_position_is_queryable_and_validated) {
    CHECK(strata_struct_size(STRATA_STRUCT_SOLAR_POSITION) == 32);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_SOLAR_POSITION, 5) == 28);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_SOLAR_POSITION, 6) == -1);
    StrataSolarPosition p{};
    CHECK(strata_solar_position(1782043200.0, 40.0, 0.0, &p) == STRATA_OK);  // 2026-06-21 12:00 UTC
    CHECK(p.elevation_degrees > 73.0f && p.elevation_degrees < 73.8f);
    CHECK(strata_solar_position(0.0, 91.0, 0.0, &p) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_solar_position(0.0, 0.0, 181.0, &p) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_solar_position(NAN, 0.0, 0.0, &p) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_solar_position(0.0, 0.0, 0.0, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
    const float up[3] = {0, 1, 0};
    CHECK(strata_set_sun_direction(nullptr, up) == STRATA_ERROR_INVALID_ARGUMENT);
}

TEST(texture_calls_reject_bad_arguments_without_a_context) {
    CHECK(strata_struct_size(STRATA_STRUCT_TEXTURED_MESH) == static_cast<int32_t>(sizeof(StrataTexturedMesh)));
    CHECK(strata_struct_field_offset(STRATA_STRUCT_TEXTURED_MESH, 3) ==
          static_cast<int32_t>(offsetof(StrataTexturedMesh, flags)));
    CHECK(strata_struct_field_offset(STRATA_STRUCT_TEXTURED_MESH, 4) == -1);
    const uint8_t texel[4] = {255, 255, 255, 255};
    uint32_t id = 0;
    CHECK(strata_create_texture_rgba8(nullptr, 1, 1, texel, 0, &id) == STRATA_ERROR_INVALID_ARGUMENT);
    StrataEngine* engine = reinterpret_cast<StrataEngine*>(0x1);  // never dereferenced: pointers are checked first
    CHECK(strata_create_texture_rgba8(engine, 1, 1, nullptr, 0, &id) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_create_texture_rgba8(engine, 1, 1, texel, 0, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(id == 0);
    CHECK(strata_destroy_texture(nullptr, 1) == STRATA_ERROR_INVALID_ARGUMENT);
    StrataTexturedMesh mesh{};
    CHECK(strata_add_textured_mesh(nullptr, &mesh, &id) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_add_textured_mesh(engine, nullptr, &id) == STRATA_ERROR_INVALID_ARGUMENT);
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    CHECK(strata_set_mesh_transform(nullptr, 1, identity) == STRATA_ERROR_INVALID_ARGUMENT);
}

TEST(abi_1_8_calls_reject_bad_arguments_without_a_context) {
    CHECK(strata_struct_size(STRATA_STRUCT_RAY_HIT) == 36);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_RAY_HIT, 0) == 0);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_RAY_HIT, 1) == 4);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_RAY_HIT, 2) == 8);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_RAY_HIT, 3) == 12);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_RAY_HIT, 4) == 24);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_RAY_HIT, 5) == -1);
    CHECK(strata_struct_size(18) == -1);  // one past the last struct id (ABI 1.13)
    const uint8_t texel[4] = {255, 255, 255, 255};
    StrataEngine* engine = reinterpret_cast<StrataEngine*>(0x1);  // never dereferenced: pointers are checked first
    CHECK(strata_update_texture_rgba8(nullptr, 1, 0, 0, 1, 1, texel) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_update_texture_rgba8(engine, 1, 0, 0, 1, 1, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
    const float origin[3] = {0, 0, 0}, direction[3] = {0, 0, -1};
    StrataRayHit hit{};
    CHECK(strata_raycast_meshes(nullptr, origin, direction, 10.0f, &hit) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_raycast_meshes(engine, nullptr, direction, 10.0f, &hit) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_raycast_meshes(engine, origin, nullptr, 10.0f, &hit) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_raycast_meshes(engine, origin, direction, 10.0f, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_set_relative_mouse_mode(nullptr, 1) == STRATA_ERROR_INVALID_ARGUMENT);
}

TEST(abi_1_9_depth_readback_rejects_bad_arguments_without_a_context) {
    float depth[4] = {};
    int32_t width = 0, height = 0;
    StrataEngine* engine = reinterpret_cast<StrataEngine*>(0x1);  // never dereferenced: pointers are checked first
    CHECK(strata_read_depth_f32(nullptr, depth, sizeof depth, &width, &height) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_read_depth_f32(engine, nullptr, sizeof depth, &width, &height) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_read_depth_f32(engine, depth, sizeof depth, nullptr, &height) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_read_depth_f32(engine, depth, sizeof depth, &width, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
}

TEST(set_terrain_rejects_a_null_engine) {
    CHECK(strata_set_terrain(nullptr, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
}

TEST(mesh_calls_reject_a_null_engine) {
    StrataMesh mesh{};
    uint32_t id = 0;
    CHECK(strata_add_mesh(nullptr, &mesh, &id) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(id == 0);
    CHECK(strata_remove_mesh(nullptr, 1) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_clear_meshes(nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
}

TEST(set_point_lights_rejects_bad_arguments_without_a_context) {
    StrataPointLight light{};
    light.range = 5.0f;
    CHECK(strata_set_point_lights(nullptr, &light, 1) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(std::strlen(strata_last_error()) > 0);
}

TEST(render_item_layout_is_queryable) {
    CHECK(strata_struct_size(STRATA_STRUCT_RENDER_ITEM) == 44);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_RENDER_ITEM, STRATA_RENDER_ITEM_KIND) == 0);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_RENDER_ITEM, STRATA_RENDER_ITEM_POSITION) == 4);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_RENDER_ITEM, STRATA_RENDER_ITEM_SCALE) == 16);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_RENDER_ITEM, STRATA_RENDER_ITEM_COLOR) == 28);
}

TEST(every_struct_has_a_size_and_bad_ids_are_rejected) {
    CHECK(strata_struct_size(STRATA_STRUCT_ENGINE_DESC) == static_cast<int32_t>(sizeof(StrataEngineDesc)));
    CHECK(strata_struct_size(STRATA_STRUCT_CAMERA) == static_cast<int32_t>(sizeof(StrataCamera)));
    CHECK(strata_struct_size(STRATA_STRUCT_LIGHTING) == static_cast<int32_t>(sizeof(StrataLighting)));
    CHECK(strata_struct_size(STRATA_STRUCT_FRAME_DESC) == static_cast<int32_t>(sizeof(StrataFrameDesc)));
    CHECK(strata_struct_size(STRATA_STRUCT_FRAME_STATS) == static_cast<int32_t>(sizeof(StrataFrameStats)));
    CHECK(strata_struct_size(STRATA_STRUCT_EVENT) == static_cast<int32_t>(sizeof(StrataEvent)));
    CHECK(strata_struct_size(STRATA_STRUCT_POINT_LIGHT) == static_cast<int32_t>(sizeof(StrataPointLight)));
    CHECK(strata_struct_size(STRATA_STRUCT_TERRAIN_TILE) == static_cast<int32_t>(sizeof(StrataTerrainTile)));
    CHECK(strata_struct_field_offset(STRATA_STRUCT_TERRAIN_TILE, 6) ==
          static_cast<int32_t>(offsetof(StrataTerrainTile, color)));
    CHECK(strata_struct_field_offset(STRATA_STRUCT_TERRAIN_TILE, 7) == -1);
    CHECK(strata_struct_size(STRATA_STRUCT_MESH) == static_cast<int32_t>(sizeof(StrataMesh)));
    CHECK(strata_struct_field_offset(STRATA_STRUCT_MESH, 4) == static_cast<int32_t>(offsetof(StrataMesh, vertex_count)));
    CHECK(strata_struct_field_offset(STRATA_STRUCT_MESH, 6) == static_cast<int32_t>(offsetof(StrataMesh, color)));
    CHECK(strata_struct_field_offset(STRATA_STRUCT_MESH, 7) == -1);
    CHECK(strata_struct_size(-1) == -1);
    CHECK(strata_struct_size(99) == -1);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_RENDER_ITEM, 4) == -1);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_CAMERA, 2) == static_cast<int32_t>(offsetof(StrataCamera, eye)));
    CHECK(strata_struct_field_offset(STRATA_STRUCT_EVENT, 11) == static_cast<int32_t>(offsetof(StrataEvent, text)));
}

TEST(null_and_bad_arguments_fail_cleanly_without_a_context) {
    StrataEngine* engine = reinterpret_cast<StrataEngine*>(0x1);
    CHECK(strata_create(nullptr, &engine) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(engine == nullptr);
    CHECK(std::strlen(strata_last_error()) > 0);

    StrataEngineDesc desc{};
    desc.struct_size = sizeof desc;
    desc.abi_version = STRATA_ABI_VERSION + 1;
    CHECK(strata_create(&desc, &engine) == STRATA_ERROR_ABI_MISMATCH);
    desc.abi_version = STRATA_ABI_VERSION;
    desc.struct_size = 12;
    CHECK(strata_create(&desc, &engine) == STRATA_ERROR_ABI_MISMATCH);
    desc.struct_size = sizeof desc;
    desc.surface = STRATA_SURFACE_HOST_CONTEXT;
    desc.width = 64;
    desc.height = 64;
    CHECK(strata_create(&desc, &engine) == STRATA_ERROR_INVALID_ARGUMENT);  // no get_proc_address
    desc.surface = 42;
    CHECK(strata_create(&desc, &engine) == STRATA_ERROR_INVALID_ARGUMENT);
    desc.surface = STRATA_SURFACE_HEADLESS;
    desc.width = 0;
    CHECK(strata_create(&desc, &engine) == STRATA_ERROR_INVALID_ARGUMENT);

    CHECK(strata_submit_primitives(nullptr, nullptr, 0) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_end_frame(nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_poll_event(nullptr, nullptr) == 0);
    strata_destroy(nullptr);  // must be a no-op
}

TEST(surface_and_frame_calls_reject_bad_arguments_without_a_context) {
    StrataEngine* engine = reinterpret_cast<StrataEngine*>(0x1);  // never dereferenced: pointers are checked first
    int32_t width = -1, height = -1;
    CHECK(strata_get_drawable_size(nullptr, &width, &height) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_get_drawable_size(engine, nullptr, &height) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_get_drawable_size(engine, &width, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(width == -1 && height == -1);
    CHECK(strata_resize(nullptr, 64, 64) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(std::strlen(strata_last_error()) > 0);
    CHECK(std::strcmp(strata_gpu_description(nullptr), "") == 0);

    CHECK(strata_set_camera(nullptr, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_set_camera(engine, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_set_lighting(nullptr, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_set_lighting(engine, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);

    StrataFrameDesc frame{};
    CHECK(strata_begin_frame(nullptr, &frame) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_begin_frame(engine, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_present(nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
    StrataFrameStats stats{};
    CHECK(strata_get_frame_stats(nullptr, &stats) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_get_frame_stats(engine, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);

    uint8_t pixel[4] = {};
    CHECK(strata_read_pixels_rgba8(nullptr, pixel, 4, &width, &height) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_read_pixels_rgba8(engine, nullptr, 4, &width, &height) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_read_pixels_rgba8(engine, pixel, 4, nullptr, &height) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_read_pixels_rgba8(engine, pixel, 4, &width, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_save_screenshot_png(nullptr, "out.png") == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_save_screenshot_png(engine, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_save_screenshot_png(engine, "") == STRATA_ERROR_INVALID_ARGUMENT);
}

TEST(every_status_has_a_name) {
    CHECK(std::strcmp(strata_status_string(STRATA_OK), "ok") == 0);
    CHECK(std::strcmp(strata_status_string(STRATA_ERROR_INVALID_ARGUMENT), "invalid argument") == 0);
    CHECK(std::strcmp(strata_status_string(STRATA_ERROR_ABI_MISMATCH), "ABI mismatch") == 0);
    CHECK(std::strcmp(strata_status_string(STRATA_ERROR_PLATFORM), "platform error") == 0);
    CHECK(std::strcmp(strata_status_string(STRATA_ERROR_GPU), "GPU error") == 0);
    CHECK(std::strcmp(strata_status_string(STRATA_ERROR_IO), "I/O error") == 0);
    CHECK(std::strcmp(strata_status_string(STRATA_ERROR_INVALID_STATE), "invalid state") == 0);
    CHECK(std::strcmp(strata_status_string(STRATA_ERROR_UNSUPPORTED), "unsupported") == 0);
    CHECK(std::strcmp(strata_status_string(STRATA_ERROR_INTERNAL), "internal error") == 0);
    CHECK(std::strcmp(strata_status_string(99), "unknown status") == 0);
}

TEST(lighting_defaults_are_beyond_nations_constants) {
    StrataLighting l{};
    REQUIRE(strata_lighting_defaults(STRATA_LIGHTING_LEGACY, &l) == STRATA_OK);
    CHECK(l.mode == STRATA_LIGHTING_LEGACY);
    CHECK_NEAR(l.legacy_ambient, 0.35, 1e-6);
    // normalize(-0.35, -0.85, -0.4)
    const double len = std::sqrt(0.35 * 0.35 + 0.85 * 0.85 + 0.4 * 0.4);
    CHECK_NEAR(l.legacy_direction[0], -0.35 / len, 1e-6);
    CHECK_NEAR(l.legacy_direction[1], -0.85 / len, 1e-6);
    CHECK_NEAR(l.legacy_direction[2], -0.40 / len, 1e-6);
    CHECK(strata_lighting_defaults(7, &l) == STRATA_ERROR_INVALID_ARGUMENT);
    REQUIRE(strata_lighting_defaults(STRATA_LIGHTING_SUN, &l) == STRATA_OK);
    CHECK(l.cascade_count == 4);
    CHECK(l.shadow_map_size == 2048);
    CHECK_NEAR(l.split_lambda, 0.6, 1e-6);
}

TEST(camera_look_at_builds_an_opengl_camera) {
    const float eye[3] = {0, 0, 10}, target[3] = {0, 0, 0}, up[3] = {0, 1, 0};
    StrataCamera c{};
    REQUIRE(strata_camera_look_at(eye, target, up, 90.0f, 1.0f, 1.0f, 100.0f, &c) == STRATA_OK);
    CHECK_NEAR(c.view[14], -10.0, 1e-5);         // translation along -z
    CHECK_NEAR(c.projection[5], 1.0, 1e-5);      // 1 / tan(45 deg)
    CHECK_NEAR(c.projection[11], -1.0, 1e-6);    // perspective divide
    CHECK_NEAR(c.near_plane, 1.0, 0.0);
    CHECK(strata_camera_look_at(eye, target, up, 0.0f, 1.0f, 1.0f, 100.0f, &c) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_camera_look_at(eye, target, up, 60.0f, 1.0f, 5.0f, 1.0f, &c) == STRATA_ERROR_INVALID_ARGUMENT);
}

TEST(abi_1_11_overlay_calls_reject_bad_arguments_without_a_context) {
    CHECK(strata_struct_size(STRATA_STRUCT_RECT) == 32);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_RECT, 0) == 0);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_RECT, 1) == 4);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_RECT, 2) == 8);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_RECT, 3) == 12);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_RECT, 4) == 16);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_RECT, 5) == -1);
    const StrataRect rect{0, 0, 10, 10, {0, 0, 0, 0.5f}};
    const StrataLineVertex v[2] = {};
    StrataEngine* engine = reinterpret_cast<StrataEngine*>(0x1);  // never dereferenced: pointers are checked first
    CHECK(strata_submit_rects(nullptr, &rect, 1) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_submit_rects(engine, nullptr, 1) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_submit_rects(engine, &rect, -1) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_submit_lines_ex(nullptr, v, 2, STRATA_LINES_ON_TOP) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_submit_lines_ex(engine, nullptr, 2, 0) == STRATA_ERROR_INVALID_ARGUMENT);
}

TEST(abi_1_13_wide_lines_reject_bad_arguments_without_a_context) {
    CHECK(strata_struct_size(STRATA_STRUCT_WIDE_LINE_VERTEX) == 36);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_WIDE_LINE_VERTEX, 0) == 0);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_WIDE_LINE_VERTEX, 1) == 12);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_WIDE_LINE_VERTEX, 2) == 28);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_WIDE_LINE_VERTEX, 3) == 32);
    CHECK(strata_struct_field_offset(STRATA_STRUCT_WIDE_LINE_VERTEX, 4) == -1);
    const StrataWideLineVertex v[2] = {};
    StrataEngine* engine = reinterpret_cast<StrataEngine*>(0x1);  // never dereferenced: pointers are checked first
    CHECK(strata_submit_wide_lines(nullptr, v, 2, 0) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_submit_wide_lines(engine, nullptr, 2, STRATA_LINES_ROUND_CAPS) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_submit_wide_lines(engine, v, -2, 0) == STRATA_ERROR_INVALID_ARGUMENT);
}

TEST(abi_1_14_projection_and_world_text_reject_bad_arguments_without_a_context) {
    const float world[3] = {0, 0, 0};
    float px[2] = {0, 0};
    StrataText t{};
    CHECK(strata_project_point(nullptr, world, px, nullptr, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
    StrataEngine* engine = reinterpret_cast<StrataEngine*>(0x1);  // never dereferenced: pointers are checked first
    CHECK(strata_project_point(engine, nullptr, px, nullptr, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_submit_world_text(nullptr, &t, world, 1) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_submit_world_text(engine, nullptr, world, 1) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_submit_world_text(engine, &t, nullptr, 1) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_submit_world_text(engine, &t, world, -1) == STRATA_ERROR_INVALID_ARGUMENT);
}
