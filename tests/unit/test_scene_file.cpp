// The scene format (tools/common/scene_file.*): what each directive leaves in
// the Scene, and that a malformed file is refused with "file:line: message".
// The goldens read the same parser, but only ever with valid scenes, so nothing
// else reaches its errors. No GL: scenes are written to the working directory.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "scene_file.hpp"
#include "strata/strata.h"
#include "test.hpp"

using strata::scene::loadScene;
using strata::scene::Scene;
using strata::scene::SceneMesh;

namespace {

constexpr double kEps = 1e-5;
const char* const kPath = "strata-unit-test.scene";

// A scene file that is gone again when this leaves scope.
class TempScene {
public:
    TempScene(std::string path, const std::string& text) : path_(std::move(path)) {
        std::ofstream out(path_);
        out << text;
    }
    ~TempScene() { std::remove(path_.c_str()); }
    TempScene(const TempScene&) = delete;
    TempScene& operator=(const TempScene&) = delete;
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

// A directory for scenes that include one another, gone again the same way.
class TempDir {
public:
    TempDir() { std::filesystem::create_directory(path_); }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    std::string file(const char* name) const { return path_ + "/" + name; }

private:
    std::string path_ = "strata-unit-test-scenes";
};

Scene parsed(const std::string& text) {
    const TempScene file(kPath, text);
    return loadScene(file.path());
}

std::string messageOf(const std::string& path) {
    try {
        loadScene(path);
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return "";
}

// The error a scene is refused with, or an empty string if it loads.
std::string failure(const std::string& text) {
    const TempScene file(kPath, text);
    return messageOf(file.path());
}

std::string at(int line, const std::string& message) {
    return std::string(kPath) + ":" + std::to_string(line) + ": " + message;
}

const uint8_t* pixel(const Scene::Texture& t, int x, int y) {
    return &t.rgba[(static_cast<size_t>(y) * static_cast<size_t>(t.width) + static_cast<size_t>(x)) * 4];
}

bool rgba(const uint8_t* p, int r, int g, int b, int a) {
    return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
}

}  // namespace

// ---------------------------------------------------------------- file structure

TEST(scene_defaults_when_a_file_sets_nothing) {
    const Scene s = parsed("# only a comment\n\n   \n");
    CHECK(s.width == 640 && s.height == 360);
    CHECK_NEAR(s.fovY, 60.0, kEps);
    CHECK(s.lighting.mode == STRATA_LIGHTING_LEGACY);
    CHECK(s.frame.clear_flags == (STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH));
    CHECK_NEAR(s.frame.clear_color[0], 0.45, kEps);
    CHECK_NEAR(s.frame.clear_color[3], 1.0, kEps);
    CHECK(!s.hasTerrain && !s.hasSunDate && !s.hasAmbient && !s.hasSunIntensity);
    CHECK(s.items.empty() && s.meshes.empty() && s.textures.empty());
}

TEST(scene_a_hash_ends_any_line_text_included) {
    const Scene s = parsed("size 10 20 # the frame\n#size 1 2\ntext 1 2 8 1 1 1 1 : a # b\n");
    CHECK(s.width == 10 && s.height == 20);
    REQUIRE(s.texts.size() == 1);
    CHECK(s.texts[0].value == "a ");
}

TEST(scene_includes_read_relative_to_the_including_file) {
    const TempDir dir;
    const TempScene part(dir.file("part.scene"), "size 7 8\nitem cube 0 0 0 1 1 1 1 1 1\n");
    const TempScene top(dir.file("top.scene"), "size 1 2\ninclude part.scene\nitem sphere 0 0 0 1 1 1 1 1 1\n");
    const Scene s = loadScene(top.path());
    CHECK(s.width == 7 && s.height == 8);
    REQUIRE(s.items.size() == 2);  // in file order, the include where it was named
    CHECK(s.items[0].kind == STRATA_PRIMITIVE_CUBE);
    CHECK(s.items[1].kind == STRATA_PRIMITIVE_SPHERE);
}

TEST(scene_include_errors_name_the_included_file) {
    const TempDir dir;
    const TempScene bad(dir.file("bad.scene"), "size 1 2\nnonsense\n");
    const TempScene top(dir.file("top.scene"), "include bad.scene\n");
    CHECK(messageOf(top.path()) == dir.file("bad.scene") + ":2: unknown directive 'nonsense'");

    const TempScene dangling(dir.file("dangling.scene"), "include gone.scene\n");
    CHECK(messageOf(dangling.path()) == dir.file("gone.scene") + ": cannot open");

    const TempScene loop(dir.file("loop.scene"), "include loop.scene\n");
    CHECK(messageOf(loop.path()) == dir.file("loop.scene") + ": includes nest too deeply");
}

TEST(scene_a_missing_file_cannot_be_opened) {
    CHECK(messageOf("strata-unit-test-absent.scene") == "strata-unit-test-absent.scene: cannot open");
}

// ---------------------------------------------------------------- camera and light

TEST(scene_reads_the_camera) {
    const Scene s = parsed("camera 1 2 3 4 5 6 45 0.5 100\n");
    CHECK_NEAR(s.eye[2], 3.0, kEps);
    CHECK_NEAR(s.target[0], 4.0, kEps);
    CHECK_NEAR(s.fovY, 45.0, kEps);
    CHECK_NEAR(s.nearPlane, 0.5, kEps);
    CHECK_NEAR(s.farPlane, 100.0, kEps);
}

TEST(scene_reads_each_lighting_mode) {
    const Scene legacy = parsed("lighting legacy 0 -1 0.5 0.25\n");
    CHECK(legacy.lighting.mode == STRATA_LIGHTING_LEGACY);
    CHECK_NEAR(legacy.lighting.legacy_direction[1], -1.0, kEps);
    CHECK_NEAR(legacy.lighting.legacy_direction[2], 0.5, kEps);
    CHECK_NEAR(legacy.lighting.legacy_ambient, 0.25, kEps);

    const Scene sun = parsed("lighting sun 0.5 40 3 1024 80 0.6 1\n");
    CHECK(sun.lighting.mode == STRATA_LIGHTING_SUN);
    CHECK_NEAR(sun.lighting.time_of_day, 0.5, kEps);
    CHECK_NEAR(sun.lighting.latitude_degrees, 40.0, kEps);
    CHECK(sun.lighting.cascade_count == 3);
    CHECK(sun.lighting.shadow_map_size == 1024);
    CHECK_NEAR(sun.lighting.shadow_distance, 80.0, kEps);
    CHECK_NEAR(sun.lighting.split_lambda, 0.6, kEps);
    CHECK(sun.lighting.fog == 1);

    const Scene space = parsed("lighting space 0.1\n");
    CHECK(space.lighting.mode == STRATA_LIGHTING_SPACE);
    CHECK_NEAR(space.lighting.legacy_ambient, 0.1, kEps);
}

TEST(scene_sun_date_becomes_unix_seconds) {
    const struct {
        const char* line;
        double seconds;
    } cases[] = {
        {"sun date 1970-01-01 00:00 0 0 0\n", 0.0},
        {"sun date 1969-12-31 23:59 0 0 0\n", -60.0},           // before the epoch
        {"sun date 2000-01-01 12:00 0 0 0\n", 946728000.0},     // J2000
        {"sun date 2000-01-01 12:00 2 0 0\n", 946720800.0},     // east of Greenwich is earlier
        {"sun date 2000-01-01 00:00 -5.5 0 0\n", 946704600.0},  // west is later, and offsets may be fractional
        {"sun date 2024-03-01 00:00 0 0 0\n", 1709251200.0},    // past a leap day
    };
    for (const auto& c : cases) {
        const Scene s = parsed(c.line);
        CHECK(s.hasSunDate);
        CHECK_NEAR(s.sunUnixSeconds, c.seconds, 1e-6);
    }
    const Scene place = parsed("sun date 2000-01-01 12:00 0 40.5 -74.25\n");
    CHECK_NEAR(place.sunLatitude, 40.5, 1e-9);
    CHECK_NEAR(place.sunLongitude, -74.25, 1e-9);
}

TEST(scene_reads_ambient_and_sun_intensity) {
    const Scene s = parsed("ambient 0.5 0.6 0.7 0.1 0.2 0.3\nsun_intensity 2 1 0.9 0.8\n");
    CHECK(s.hasAmbient);
    CHECK_NEAR(s.ambientTint[2], 0.7, kEps);
    CHECK_NEAR(s.ambientLift[0], 0.1, kEps);
    CHECK(s.hasSunIntensity);
    CHECK_NEAR(s.sunIntensity, 2.0, kEps);
    CHECK_NEAR(s.sunTint[2], 0.8, kEps);
}

TEST(scene_clear_can_ask_for_the_sky) {
    const Scene plain = parsed("clear 0.1 0.2 0.3 0.4\n");
    CHECK(plain.frame.clear_flags == (STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH));
    CHECK_NEAR(plain.frame.clear_color[3], 0.4, kEps);
    const Scene sky = parsed("clear 0 0 0 1 sky\n");
    CHECK(sky.frame.clear_flags == (STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH | STRATA_CLEAR_SKY));
}

TEST(scene_reads_point_lights) {
    const Scene s = parsed("light point 1 2 3 10 1 0.5 0.25 2\nlight point 0 0 0 1 1 1 1 1 shadow\n");
    REQUIRE(s.pointLights.size() == 2);
    CHECK_NEAR(s.pointLights[0].position[2], 3.0, kEps);
    CHECK_NEAR(s.pointLights[0].range, 10.0, kEps);
    CHECK_NEAR(s.pointLights[0].color[2], 0.25, kEps);
    CHECK_NEAR(s.pointLights[0].intensity, 2.0, kEps);
    CHECK(s.pointLights[0].flags == 0);
    CHECK(s.pointLights[1].flags == STRATA_POINT_LIGHT_CAST_SHADOWS);
}

// ---------------------------------------------------------------- terrain and meshes

TEST(scene_terrain_points_at_its_own_samples) {
    const Scene s = parsed(
        "terrain 1 2 3 0.5 2 2 0.2 0.3 0.4\n"
        "heights 1 2\nheights 3 4\n"
        "colors 1 0 0 0 1 0\ncolors 0 0 1 1 1 1\n");
    CHECK(s.hasTerrain);
    CHECK_NEAR(s.terrain.origin[1], 2.0, kEps);
    CHECK_NEAR(s.terrain.cell_size, 0.5, kEps);
    CHECK(s.terrain.samples_x == 2 && s.terrain.samples_z == 2);
    CHECK_NEAR(s.terrain.color[2], 0.4, kEps);
    CHECK_NEAR(s.terrain.color[3], 1.0, kEps);
    REQUIRE(s.terrainHeights.size() == 4);
    CHECK_NEAR(s.terrainHeights[3], 4.0, kEps);
    CHECK(s.terrain.heights == s.terrainHeights.data());
    CHECK(s.terrain.colors == s.terrainColors.data());
}

TEST(scene_terrain_colours_are_optional_and_a_second_tile_replaces_the_first) {
    const Scene s = parsed(
        "terrain 0 0 0 1 2 2 1 1 1\nheights 1 2 3 4\ncolors 1 0 0 0 1 0 0 0 1 1 1 1\n"
        "terrain 0 0 0 1 2 3 1 1 1\nheights 1 2 3 4 5 6\n");
    CHECK(s.terrain.samples_z == 3);
    CHECK(s.terrainHeights.size() == 6);
    CHECK(s.terrainColors.empty());
    CHECK(s.terrain.colors == nullptr);
}

TEST(scene_mesh_directives_apply_to_the_latest_mesh) {
    const Scene s = parsed(
        "mesh 0.5 0.5 0.5\nvertices 9 9 9 9 9 9 9 9 9\ntriangles 0 1 2\n"
        "mesh 0.1 0.2 0.3\n"
        "vertices 0 0 0 1 0\nvertices 0 0 1 0\n"  // values may run across lines
        "vcolors 1 0 0 0 1 0 0 0 1\nuvs 0 0 1 0 0 1\ntriangles 0 1\ntriangles 2\n");
    REQUIRE(s.meshes.size() == 2);
    const SceneMesh& plain = s.meshes[0];
    CHECK(plain.view().colors == nullptr);
    CHECK(!plain.textured());

    const SceneMesh& m = s.meshes[1];
    CHECK(m.positions.size() == 9);
    CHECK(m.uvs.size() == 6);
    CHECK(m.indices.size() == 3 && m.indices[2] == 2);
    CHECK(m.textured());
    const StrataMesh view = m.view();
    CHECK(view.positions == m.positions.data());
    CHECK(view.colors == m.colors.data());
    CHECK(view.indices == m.indices.data());
    CHECK(view.normals == nullptr);
    CHECK(view.vertex_count == 3);
    CHECK(view.index_count == 3);
    CHECK_NEAR(view.color[1], 0.2, kEps);
    CHECK_NEAR(view.color[3], 1.0, kEps);
}

TEST(scene_mesh_material_directives) {
    const Scene s = parsed("mesh 1 1 1\nmesh_texture brick\nmesh 1 1 1\nmesh_unlit\nmesh 1 1 1\nmesh_translucent 0.4\n");
    REQUIRE(s.meshes.size() == 3);
    CHECK(s.meshes[0].texture == "brick");
    CHECK(s.meshes[0].textured());
    CHECK(s.meshes[1].unlit);
    CHECK(s.meshes[1].textured());
    CHECK(s.meshes[2].translucent);
    CHECK_NEAR(s.meshes[2].color[3], 0.4, kEps);
    CHECK(s.meshes[2].textured());
}

TEST(scene_mesh_transform_is_translate_tilt_spin_scale) {
    // Spin +X to -Z about +Y, then tilt about +Z (the axis need not be unit
    // length), so +X stays at -Z. The other order would leave +X at +Y.
    const Scene s = parsed("mesh 1 1 1\nmesh_transform 7 8 9 0 0 3 90 90 2\n");
    REQUIRE(s.meshes.size() == 1);
    CHECK(s.meshes[0].hasTransform);
    const float expected[16] = {0, 0, -2, 0, -2, 0, 0, 0, 0, 2, 0, 0, 7, 8, 9, 1};
    for (int i = 0; i < 16; ++i) CHECK_NEAR(s.meshes[0].transform[i], expected[i], kEps);
}

TEST(scene_sphere_mesh_is_a_closed_outward_facing_unit_sphere) {
    const int slices = 8, stacks = 4, row = slices + 1;
    const Scene s = parsed("sphere_mesh 8 4 0.2 0.4 0.6\n");
    REQUIRE(s.meshes.size() == 1);
    const SceneMesh& m = s.meshes[0];
    const size_t vertices = static_cast<size_t>(row) * static_cast<size_t>(stacks + 1);
    REQUIRE(m.positions.size() == vertices * 3);
    REQUIRE(m.uvs.size() == vertices * 2);
    REQUIRE(m.indices.size() == static_cast<size_t>(slices * stacks * 6));
    CHECK_NEAR(m.color[2], 0.6, kEps);

    // v = 0 is the north pole; u runs east from +X towards -Z.
    CHECK_NEAR(m.positions[1], 1.0, kEps);
    CHECK_NEAR(m.uvs[1], 0.0, kEps);
    const size_t onEquator = static_cast<size_t>(2 * row + 2);  // a quarter of the way round
    CHECK_NEAR(m.positions[onEquator * 3 + 0], 0.0, kEps);
    CHECK_NEAR(m.positions[onEquator * 3 + 1], 0.0, kEps);
    CHECK_NEAR(m.positions[onEquator * 3 + 2], -1.0, kEps);
    CHECK_NEAR(m.uvs[onEquator * 2 + 0], 0.25, kEps);
    CHECK_NEAR(m.uvs[onEquator * 2 + 1], 0.5, kEps);

    for (size_t i = 0; i < vertices; ++i) {
        const float* p = &m.positions[i * 3];
        CHECK_NEAR(p[0] * p[0] + p[1] * p[1] + p[2] * p[2], 1.0, 1e-4);
    }

    // Counter-clockwise from outside: the face normal leans away from the
    // centre. The triangles that meet a pole's duplicated vertices have no area.
    int facing = 0;
    for (size_t i = 0; i < m.indices.size(); i += 3) {
        const float* a = &m.positions[size_t{m.indices[i]} * 3];
        const float* b = &m.positions[size_t{m.indices[i + 1]} * 3];
        const float* c = &m.positions[size_t{m.indices[i + 2]} * 3];
        const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const float v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const float n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        if (n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < 1e-8f) continue;
        CHECK(n[0] * a[0] + n[1] * a[1] + n[2] * a[2] > 0.0f);
        ++facing;
    }
    CHECK(facing == slices * (stacks - 1) * 2);
}

// ---------------------------------------------------------------- textures

TEST(scene_checker_texture_alternates_and_clamps_colours_to_bytes) {
    const Scene s = parsed("texture t checker 2 2 2 2 2 -1 0.5 0 1 0\n");
    REQUIRE(s.textures.size() == 1);
    const Scene::Texture& t = s.textures[0];
    CHECK(t.name == "t");
    CHECK(t.width == 2 && t.height == 2);
    CHECK(t.flags == 0);
    REQUIRE(t.rgba.size() == 16);
    CHECK(rgba(pixel(t, 0, 0), 255, 0, 128, 255));
    CHECK(rgba(pixel(t, 1, 0), 0, 255, 0, 255));
    CHECK(rgba(pixel(t, 0, 1), 0, 255, 0, 255));
    CHECK(rgba(pixel(t, 1, 1), 255, 0, 128, 255));
}

TEST(scene_checker_cells_span_the_texture) {
    const Scene s = parsed("texture t checker 4 2 2 1 1 0 0 0 0 1\n");
    REQUIRE(s.textures.size() == 1);
    const Scene::Texture& t = s.textures[0];
    CHECK(rgba(pixel(t, 1, 1), 255, 0, 0, 255));
    CHECK(rgba(pixel(t, 2, 0), 0, 0, 255, 255));
}

TEST(scene_texture_options_set_flags) {
    const Scene s = parsed(
        "texture a checker 1 1 1 1 0 0 0 1 1 1 mipmaps\n"
        "texture b checker 1 1 1 1 0 0 0 1 1 1 nearest clamp_v mipmaps\n");
    REQUIRE(s.textures.size() == 2);
    CHECK(s.textures[0].flags == STRATA_TEXTURE_MIPMAPS);
    CHECK(s.textures[1].flags == (STRATA_TEXTURE_MIPMAPS | STRATA_TEXTURE_CLAMP_V | STRATA_TEXTURE_NEAREST));
}

TEST(scene_globe_texture_shows_its_caps_and_prime_meridian) {
    const Scene s = parsed("texture g globe 96 48 nearest\n");
    REQUIRE(s.textures.size() == 1);
    const Scene::Texture& t = s.textures[0];
    CHECK(t.flags == (STRATA_TEXTURE_CLAMP_V | STRATA_TEXTURE_NEAREST));  // always clamp_v
    CHECK(rgba(pixel(t, 48, 0), 242, 242, 242, 255));                     // a white cap
    const uint8_t* meridian = pixel(t, 0, 24);
    CHECK(meridian[0] > 200 && meridian[1] < 40 && meridian[2] < 40);
    const uint8_t* land = pixel(t, 48, 24);
    CHECK(land[0] < land[1]);  // neither cap nor meridian
    CHECK(land[3] == 255);
}

TEST(scene_texture_updates_keep_file_order_and_are_filled_with_one_colour) {
    const Scene s = parsed(
        "texture t checker 2 2 1 1 0 0 0 1 1 1\n"
        "texture_update t 1 1 2 3 1 0.5 0\n"
        "texture_update t 0 0 1 1 0 0 0\n");
    REQUIRE(s.textureUpdates.size() == 2);
    const Scene::TextureUpdate& u = s.textureUpdates[0];
    CHECK(u.name == "t");
    CHECK(u.x == 1 && u.y == 1 && u.width == 2 && u.height == 3);
    REQUIRE(u.rgba.size() == 24);
    CHECK(rgba(&u.rgba[0], 255, 128, 0, 255));
    CHECK(rgba(&u.rgba[20], 255, 128, 0, 255));
    CHECK(rgba(&s.textureUpdates[1].rgba[0], 0, 0, 0, 255));
}

// ---------------------------------------------------------------- per-frame submissions

TEST(scene_items_name_their_kind_and_default_to_opaque) {
    const Scene s = parsed(
        "item cube 1 2 3 4 5 6 0.1 0.2 0.3\n"
        "item capsule 0 0 0 1 1 1 1 1 1 0.5\n"
        "item cylinder 0 0 0 1 1 1 1 1 1\n"
        "item sphere 0 0 0 1 1 1 1 1 1\n");
    REQUIRE(s.items.size() == 4);
    CHECK(s.items[0].kind == STRATA_PRIMITIVE_CUBE);
    CHECK(s.items[1].kind == STRATA_PRIMITIVE_CAPSULE);
    CHECK(s.items[2].kind == STRATA_PRIMITIVE_CYLINDER);
    CHECK(s.items[3].kind == STRATA_PRIMITIVE_SPHERE);
    CHECK_NEAR(s.items[0].position[2], 3.0, kEps);
    CHECK_NEAR(s.items[0].scale[2], 6.0, kEps);
    CHECK_NEAR(s.items[0].color[2], 0.3, kEps);
    CHECK_NEAR(s.items[0].color[3], 1.0, kEps);
    CHECK_NEAR(s.items[1].color[3], 0.5, kEps);
}

TEST(scene_lines_share_one_colour_and_on_top_goes_to_its_own_list) {
    const Scene s = parsed("line 0 0 0 1 2 3 1 0 0 0.5\nline 0 0 0 1 1 1 0 1 0 1 on_top\n");
    REQUIRE(s.lines.size() == 2);
    REQUIRE(s.linesOnTop.size() == 2);
    CHECK_NEAR(s.lines[1].position[2], 3.0, kEps);
    CHECK_NEAR(s.lines[0].color[3], 0.5, kEps);
    CHECK_NEAR(s.lines[1].color[0], 1.0, kEps);
    CHECK_NEAR(s.lines[1].color[3], 0.5, kEps);
    CHECK_NEAR(s.linesOnTop[1].color[1], 1.0, kEps);
}

TEST(scene_wide_lines_are_binned_by_their_flags) {
    const Scene s = parsed(
        "wide_line 0 0 0 1 0 0 1 1 1 0.5 3 0.25\n"
        "wide_line 0 0 0 1 0 0 1 1 1 1 3 0.25 on_top\n"
        "wide_line 0 0 0 1 0 0 1 1 1 1 3 0.25 round\n"
        "wide_line 0 0 0 1 0 0 1 1 1 1 3 0.25 to 0 0 0 0.5 6 1 round on_top\n");
    const size_t onTop = STRATA_LINES_ON_TOP, roundCaps = STRATA_LINES_ROUND_CAPS, both = onTop | roundCaps;
    REQUIRE(s.wideLines[0].size() == 2);
    REQUIRE(s.wideLines[onTop].size() == 2);
    REQUIRE(s.wideLines[roundCaps].size() == 2);
    REQUIRE(s.wideLines[both].size() == 2);

    // Without 'to' the end copies the start.
    const StrataWideLineVertex& end = s.wideLines[0][1];
    CHECK_NEAR(end.color[3], 0.5, kEps);
    CHECK_NEAR(end.width_pixels, 3.0, kEps);
    CHECK_NEAR(end.world_width, 0.25, kEps);

    // With it, the end has its own colour and widths and the start keeps its.
    const StrataWideLineVertex& start = s.wideLines[both][0];
    const StrataWideLineVertex& to = s.wideLines[both][1];
    CHECK_NEAR(start.color[0], 1.0, kEps);
    CHECK_NEAR(start.width_pixels, 3.0, kEps);
    CHECK_NEAR(to.color[0], 0.0, kEps);
    CHECK_NEAR(to.color[3], 0.5, kEps);
    CHECK_NEAR(to.width_pixels, 6.0, kEps);
    CHECK_NEAR(to.world_width, 1.0, kEps);
}

TEST(scene_circle_is_a_closed_loop_of_segments_in_the_xz_plane) {
    const Scene s = parsed("circle 1 2 3 2 4 1 0 0 1\n");
    REQUIRE(s.lines.size() == 8);
    CHECK_NEAR(s.lines[0].position[0], 3.0, kEps);
    CHECK_NEAR(s.lines[0].position[1], 2.0, kEps);
    CHECK_NEAR(s.lines[0].position[2], 3.0, kEps);
    CHECK_NEAR(s.lines[1].position[0], 1.0, kEps);
    CHECK_NEAR(s.lines[1].position[2], 5.0, kEps);
    for (size_t i = 1; i + 1 < s.lines.size(); i += 2) {  // each segment starts where the last ended
        CHECK_NEAR(s.lines[i].position[0], s.lines[i + 1].position[0], kEps);
        CHECK_NEAR(s.lines[i].position[2], s.lines[i + 1].position[2], kEps);
    }
    CHECK_NEAR(s.lines[7].position[0], s.lines[0].position[0], kEps);
    CHECK_NEAR(s.lines[7].position[2], s.lines[0].position[2], kEps);
    CHECK_NEAR(s.lines[7].color[0], 1.0, kEps);
}

TEST(scene_points_and_rects) {
    const Scene s = parsed(
        "point 1 2 3 4 0.5 1 0 0 0.25\n"
        "point 0 0 0 1 0 1 1 1 1 soft\n"
        "point 0 0 0 1 0 1 1 1 1 on_top soft\n"
        "rect 10 20 30 40 0 0 1 0.5\n");
    REQUIRE(s.points.size() == 3);
    CHECK_NEAR(s.points[0].position[1], 2.0, kEps);
    CHECK_NEAR(s.points[0].size_pixels, 4.0, kEps);
    CHECK_NEAR(s.points[0].world_diameter, 0.5, kEps);
    CHECK_NEAR(s.points[0].color[3], 0.25, kEps);
    CHECK(s.points[0].flags == 0);
    CHECK(s.points[1].flags == STRATA_POINT_SOFT);
    CHECK(s.points[2].flags == (STRATA_POINT_SOFT | STRATA_POINT_ON_TOP));
    REQUIRE(s.rects.size() == 1);
    CHECK_NEAR(s.rects[0].x, 10.0, kEps);
    CHECK_NEAR(s.rects[0].height, 40.0, kEps);
    CHECK_NEAR(s.rects[0].color[2], 1.0, kEps);
}

TEST(scene_text_runs_from_the_colon_to_the_end_of_the_line) {
    const Scene s = parsed(
        "text 10 20 16 1 0.5 0 1 : Hello  world : again\n"
        "text 0 0 8 1 1 1 1 shadow : shaded\n"
        "text 0 0 8 1 1 1 1 :\n");
    REQUIRE(s.texts.size() == 3);
    CHECK(s.texts[0].value == "Hello  world : again");
    CHECK_NEAR(s.texts[0].text.y, 20.0, kEps);
    CHECK_NEAR(s.texts[0].text.pixel_size, 16.0, kEps);
    CHECK_NEAR(s.texts[0].text.color[1], 0.5, kEps);
    CHECK(s.texts[0].text.flags == 0);
    CHECK(s.texts[1].value == "shaded");
    CHECK(s.texts[1].text.flags == STRATA_TEXT_SHADOW);
    CHECK(s.texts[2].value.empty());
}

TEST(scene_labels_add_an_anchor_to_text) {
    const Scene s = parsed("label 1 2 3 4 -5 8 1 1 1 1 shadow : over there\n");
    CHECK(s.texts.empty());
    REQUIRE(s.labels.size() == 1);
    const Scene::Label& l = s.labels[0];
    CHECK_NEAR(l.anchor[2], 3.0, kEps);
    CHECK_NEAR(l.text.text.x, 4.0, kEps);
    CHECK_NEAR(l.text.text.y, -5.0, kEps);
    CHECK(l.text.text.flags == STRATA_TEXT_SHADOW);
    CHECK(l.text.value == "over there");
}

TEST(scene_expectations_remember_their_lines) {
    const Scene s = parsed(
        "mesh 1 1 1\n"
        "expect_ray 0 0 5 0 0 -1 10 0 1.5\n"
        "expect_ray 0 0 5 0 1 0 10 none\n"
        "expect_depth 1 2 3.5\n"
        "expect_depth 0 0 inf\n");
    REQUIRE(s.rayExpectations.size() == 2);
    const Scene::RayExpectation& hit = s.rayExpectations[0];
    CHECK_NEAR(hit.origin[2], 5.0, kEps);
    CHECK_NEAR(hit.direction[2], -1.0, kEps);
    CHECK_NEAR(hit.maxDistance, 10.0, kEps);
    CHECK(hit.mesh == 0);
    CHECK(hit.hasDistance);
    CHECK_NEAR(hit.distance, 1.5, kEps);
    CHECK(hit.line == 2);
    CHECK(s.rayExpectations[1].mesh == -1);
    CHECK(!s.rayExpectations[1].hasDistance);
    CHECK(s.rayExpectations[1].line == 3);

    REQUIRE(s.depthExpectations.size() == 2);
    CHECK(s.depthExpectations[0].x == 1 && s.depthExpectations[0].y == 2);
    CHECK_NEAR(s.depthExpectations[0].distance, 3.5, kEps);
    CHECK(s.depthExpectations[0].line == 4);
    CHECK(s.depthExpectations[1].distance == std::numeric_limits<float>::infinity());
    CHECK(s.depthExpectations[1].line == 5);
}

// ---------------------------------------------------------------- errors

TEST(scene_errors_name_the_line_and_what_was_wrong) {
    const struct {
        const char* scene;
        int line;
        const char* message;
    } cases[] = {
        {"\n# comment\nwhat 1\n", 3, "unknown directive 'what'"},
        {"size x 1\n", 1, "expected width"},
        {"size 640\n", 1, "expected height"},
        {"camera 0 0 0\n", 1, "expected target"},
        {"lighting\n", 1, "expected lighting mode"},
        {"lighting moon\n", 1, "unknown lighting mode 'moon'"},
        {"sun noon\n", 1, "expected 'sun date'"},
        {"sun date 2026-13-01 12:00 0 0 0\n", 1, "bad date '2026-13-01'"},
        {"sun date 2026-01-32 12:00 0 0 0\n", 1, "bad date '2026-01-32'"},
        {"sun date 2026-01-01 24:00 0 0 0\n", 1, "bad time '24:00'"},
        {"sun date 2026-01-01 12:60 0 0 0\n", 1, "bad time '12:60'"},
        {"clear 0 0 0 1 dusk\n", 1, "expected 'sky' or nothing after the clear colour"},
        {"light spot 0 0 0\n", 1, "unknown light type 'spot' (expected 'point')"},
        {"light point 0 0 0 1 1 1 1 1 dim\n", 1, "expected 'shadow' or nothing after the intensity"},
        {"heights 1 2\n", 1, "'heights' before 'terrain'"},
        {"colors 1 2 3\n", 1, "'colors' before 'terrain'"},
        {"terrain 0 0 0 1 2 2 1 1 1\nheights 1 x\n", 2, "expected numbers after 'heights'"},
        {"terrain 0 0 0 1 2 2 1 1 1\ncolors x\n", 2, "expected numbers after 'colors'"},
        {"vertices 0 0 0\n", 1, "'vertices' before 'mesh'"},
        {"vcolors 0 0 0\n", 1, "'vcolors' before 'mesh'"},
        {"triangles 0 1 2\n", 1, "'triangles' before 'mesh'"},
        {"uvs 0 0\n", 1, "'uvs' before 'mesh'"},
        {"mesh_texture t\n", 1, "'mesh_texture' before 'mesh'"},
        {"mesh_unlit\n", 1, "'mesh_unlit' before 'mesh'"},
        {"mesh_translucent 0.5\n", 1, "'mesh_translucent' before 'mesh'"},
        {"mesh_transform 0 0 0 0 1 0 0 0 1\n", 1, "'mesh_transform' before 'mesh'"},
        {"mesh 1 1 1\nvertices a\n", 2, "expected numbers after 'vertices'"},
        {"mesh 1 1 1\nvcolors a\n", 2, "expected numbers after 'vcolors'"},
        {"mesh 1 1 1\ntriangles 0 x\n", 2, "expected indices after 'triangles'"},
        {"mesh 1 1 1\nuvs x\n", 2, "expected numbers after 'uvs'"},
        {"mesh 1 1 1\nmesh_transform 0 0 0 0 0 0 1 1 1\n", 2, "the tilt axis must be non-zero"},
        {"texture t checker 0 4 1 1 0 0 0 1 1 1\n", 1, "texture size out of range"},
        {"texture t checker 4097 4 1 1 0 0 0 1 1 1\n", 1, "texture size out of range"},
        {"texture t checker 4 4 0 1 0 0 0 1 1 1\n", 1, "checker cells must be positive"},
        {"texture t plasma 4 4\n", 1, "unknown texture kind 'plasma'"},
        {"texture t globe 4 4 wrap\n", 1, "unknown texture option 'wrap'"},
        {"sphere_mesh 2 2 1 1 1\n", 1, "a sphere needs at least 3 slices and 2 stacks"},
        {"sphere_mesh 3 1 1 1 1\n", 1, "a sphere needs at least 3 slices and 2 stacks"},
        {"texture_update t 0 0 0 1 1 1 1\n", 1, "update size out of range"},
        {"expect_ray 0 0 0 0 0 1 1 mesh\n", 1, "expected a mesh index or 'none'"},
        {"expect_ray 0 0 0 0 0 1 1 -1\n", 1, "the mesh index must be >= 0"},
        {"expect_depth 0 0 far\n", 1, "expected a distance or 'inf'"},
        {"item pyramid 0 0 0 1 1 1 1 1 1\n", 1, "unknown primitive 'pyramid'"},
        {"line 0 0 0 1 1 1 1 1 1 1 over\n", 1, "expected 'on_top' or nothing after the colour"},
        {"wide_line 0 0 0 1 1 1 1 1 1 1 2 0 fat\n", 1, "expected 'to', 'round' or 'on_top' after the widths"},
        {"circle 0 0 0 1 2 1 1 1 1\n", 1, "a circle needs at least 3 segments"},
        {"point 0 0 0 1 0 1 1 1 1 big\n", 1, "expected 'soft', 'on_top' or nothing after the colour"},
        {"text 0 0 8 1 1 1 1 hello\n", 1, "expected ':' before the text"},
        {"text 0 0 8 1 1 1 1\n", 1, "expected ':' before the text"},
        {"include\n", 1, "expected file name"},
    };
    for (const auto& c : cases) {
        const std::string got = failure(c.scene);
        if (got != at(c.line, c.message)) {
            strata::test::recordFailure(__FILE__, __LINE__, std::string(c.scene) + "  was refused with: " + got);
        }
    }
}

TEST(scene_checks_made_after_the_whole_file_is_read_name_the_file) {
    const struct {
        const char* scene;
        const char* message;
    } cases[] = {
        {"terrain 0 0 0 1 2 2 1 1 1\nheights 1 2 3\n", "terrain has 3 heights, expected 4"},
        {"terrain 0 0 0 1 2 2 1 1 1\nheights 1 2 3 4\ncolors 1 0 0\n", "terrain has 3 colour values, expected 12"},
        {"mesh 1 1 1\nvertices 0 0 0 1\n", "a mesh's vertex values are not a multiple of 3"},
        {"mesh 1 1 1\nvertices 0 0 0\nuvs 0\n", "a mesh has 1 uv values, expected 2"},
        {"mesh 1 1 1\nvertices 0 0 0\nvcolors 1 0\n", "a mesh has 2 colour values, expected 3"},
        {"texture_update ghost 0 0 1 1 1 1 1\n", "texture_update names unknown texture 'ghost'"},
    };
    for (const auto& c : cases) {
        const std::string got = failure(c.scene);
        if (got != std::string(kPath) + ": " + c.message) {
            strata::test::recordFailure(__FILE__, __LINE__, std::string(c.scene) + "  was refused with: " + got);
        }
    }
}

TEST(scene_expectations_are_checked_at_the_end_but_keep_their_line) {
    CHECK(failure("\nexpect_ray 0 0 0 0 0 1 1 0\n") == at(2, "expect_ray names mesh 0 of 0"));
    CHECK(failure("size 4 4\nexpect_depth 4 0 1\n") == at(2, "expect_depth pixel is outside the frame"));
}

TEST(scene_end_of_file_checks_let_a_name_or_size_come_later) {
    CHECK(failure("texture_update t 0 0 1 1 1 1 1\ntexture t globe 2 2\n").empty());
    CHECK(failure("expect_ray 0 0 0 0 0 1 1 0\nmesh 1 1 1\n").empty());
    CHECK(failure("expect_depth 700 0 1\nsize 800 600\n").empty());
}
