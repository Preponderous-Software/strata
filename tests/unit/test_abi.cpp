// The C ABI's struct layouts are a promise to every binding. These checks pin
// them, so a change to strata.h that moves a field fails here first.
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
static_assert(STRATA_ABI_VERSION == 1u, "ABI 1 callers must keep working: additions bump STRATA_ABI_MINOR");
#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
static_assert(sizeof(StrataEngineDesc) == 48, "64-bit targets");
#endif

TEST(abi_version_is_reported) {
    CHECK(strata_abi_version() == STRATA_ABI_VERSION);
    CHECK(std::strlen(strata_version_string()) > 0);
    CHECK(strata_abi_minor() == STRATA_ABI_MINOR);
    CHECK(strata_abi_minor() >= 2u);
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
