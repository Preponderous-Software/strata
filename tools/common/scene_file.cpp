#include "scene_file.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace strata::scene {
namespace {

std::string directoryOf(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

[[noreturn]] void bad(const std::string& where, const std::string& message) {
    throw std::runtime_error(where + ": " + message);
}

template <typename T>
void read(std::istringstream& in, T& value, const std::string& where, const char* what) {
    if (!(in >> value)) bad(where, std::string("expected ") + what);
}

int kindFromName(const std::string& name, const std::string& where) {
    if (name == "cube") return STRATA_PRIMITIVE_CUBE;
    if (name == "capsule") return STRATA_PRIMITIVE_CAPSULE;
    if (name == "cylinder") return STRATA_PRIMITIVE_CYLINDER;
    if (name == "sphere") return STRATA_PRIMITIVE_SPHERE;
    bad(where, "unknown primitive '" + name + "'");
}

// Days from 1970-01-01 to a proleptic Gregorian date (Howard Hinnant's days_from_civil).
long long daysFromCivil(long long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

uint8_t toByte(float v) { return static_cast<uint8_t>(std::lround(std::fmin(std::fmax(v, 0.0f), 1.0f) * 255.0f)); }

// A test planet map: a checker of twelve hued columns by six rows, white polar
// caps and a red column at the prime meridian (u = 0), so spin and tilt show.
void fillGlobe(Scene::Texture& t) {
    for (int y = 0; y < t.height; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(t.height);
        const float latitude = 90.0f - v * 180.0f;
        for (int x = 0; x < t.width; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(t.width);
            float r, g, b;
            const int column = static_cast<int>(u * 12.0f), row = static_cast<int>(v * 6.0f);
            const bool dark = ((column + row) & 1) != 0;
            const float hue = static_cast<float>(column % 4) / 4.0f;  // blue, green, teal, sand
            const float palette[4][3] = {{0.15f, 0.35f, 0.80f}, {0.25f, 0.60f, 0.25f}, {0.15f, 0.60f, 0.60f},
                                         {0.80f, 0.70f, 0.45f}};
            const float* c = palette[static_cast<int>(hue * 4.0f)];
            const float k = dark ? 0.6f : 1.0f;
            r = c[0] * k;
            g = c[1] * k;
            b = c[2] * k;
            if (std::fabs(latitude) > 70.0f) r = g = b = 0.95f;
            if (u < 1.0f / 48.0f) {
                r = 0.9f;
                g = 0.1f;
                b = 0.1f;
            }
            uint8_t* px = &t.rgba[(static_cast<size_t>(y) * static_cast<size_t>(t.width) + static_cast<size_t>(x)) * 4];
            px[0] = toByte(r);
            px[1] = toByte(g);
            px[2] = toByte(b);
            px[3] = 255;
        }
    }
}

// Column-major 4x4 product a * b.
void multiply(const float* a, const float* b, float* out) {
    float r[16];
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = sum;
        }
    std::memcpy(out, r, sizeof r);
}

// Rotation by `degrees` about a unit axis (Rodrigues), column-major.
void rotation(const float axis[3], float degrees, float* out) {
    const float a = degrees * 3.14159265358979f / 180.0f, c = std::cos(a), s = std::sin(a), t = 1.0f - c;
    const float x = axis[0], y = axis[1], z = axis[2];
    const float m[16] = {t * x * x + c,     t * x * y + s * z, t * x * z - s * y, 0,
                         t * x * y - s * z, t * y * y + c,     t * y * z + s * x, 0,
                         t * x * z + s * y, t * y * z - s * x, t * z * z + c,     0,
                         0,                 0,                 0,                 1};
    std::memcpy(out, m, sizeof m);
}

void parse(const std::string& path, Scene& scene, int depth) {
    if (depth > 8) bad(path, "includes nest too deeply");
    std::ifstream file(path);
    if (!file) bad(path, "cannot open");
    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        const std::string where = path + ":" + std::to_string(lineNumber);
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream in(line);
        std::string word;
        if (!(in >> word)) continue;
        if (word == "size") {
            read(in, scene.width, where, "width");
            read(in, scene.height, where, "height");
        } else if (word == "camera") {
            for (float& v : scene.eye) read(in, v, where, "eye");
            for (float& v : scene.target) read(in, v, where, "target");
            read(in, scene.fovY, where, "fov");
            read(in, scene.nearPlane, where, "near");
            read(in, scene.farPlane, where, "far");
        } else if (word == "lighting") {
            std::string mode;
            read(in, mode, where, "lighting mode");
            StrataLighting& l = scene.lighting;
            if (mode == "legacy") {
                l.mode = STRATA_LIGHTING_LEGACY;
                for (float& v : l.legacy_direction) read(in, v, where, "direction");
                read(in, l.legacy_ambient, where, "ambient");
            } else if (mode == "sun") {
                l.mode = STRATA_LIGHTING_SUN;
                read(in, l.time_of_day, where, "time of day");
                read(in, l.latitude_degrees, where, "latitude");
                read(in, l.cascade_count, where, "cascade count");
                read(in, l.shadow_map_size, where, "shadow map size");
                read(in, l.shadow_distance, where, "shadow distance");
                read(in, l.split_lambda, where, "split lambda");
                read(in, l.fog, where, "fog");
            } else if (mode == "space") {
                l.mode = STRATA_LIGHTING_SPACE;
                read(in, l.legacy_ambient, where, "ambient");
            } else {
                bad(where, "unknown lighting mode '" + mode + "'");
            }
        } else if (word == "sun") {
            std::string kind, date, time;
            read(in, kind, where, "'date'");
            if (kind != "date") bad(where, "expected 'sun date'");
            read(in, date, where, "date (YYYY-MM-DD)");
            read(in, time, where, "local time (HH:MM)");
            double utcOffset = 0.0;
            read(in, utcOffset, where, "UTC offset in hours");
            read(in, scene.sunLatitude, where, "latitude");
            read(in, scene.sunLongitude, where, "longitude");
            int y = 0, mo = 0, d = 0, h = 0, mi = 0;
            if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &mo, &d) != 3 || mo < 1 || mo > 12 || d < 1 || d > 31) {
                bad(where, "bad date '" + date + "'");
            }
            if (std::sscanf(time.c_str(), "%d:%d", &h, &mi) != 2 || h < 0 || h > 23 || mi < 0 || mi > 59) {
                bad(where, "bad time '" + time + "'");
            }
            const long long days = daysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d));
            scene.sunUnixSeconds = static_cast<double>(days) * 86400.0 + h * 3600.0 + mi * 60.0 - utcOffset * 3600.0;
            scene.hasSunDate = true;
        } else if (word == "clear") {
            for (float& v : scene.frame.clear_color) read(in, v, where, "clear colour");
            scene.frame.clear_flags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH;
            std::string sky;
            if (in >> sky) {
                if (sky != "sky") bad(where, "expected 'sky' or nothing after the clear colour");
                scene.frame.clear_flags |= STRATA_CLEAR_SKY;
            }
        } else if (word == "light") {
            std::string type;
            read(in, type, where, "light type");
            if (type != "point") bad(where, "unknown light type '" + type + "' (expected 'point')");
            StrataPointLight light{};
            for (float& v : light.position) read(in, v, where, "position");
            read(in, light.range, where, "range");
            for (float& v : light.color) read(in, v, where, "colour");
            read(in, light.intensity, where, "intensity");
            std::string option;
            if (in >> option) {
                if (option != "shadow") bad(where, "expected 'shadow' or nothing after the intensity");
                light.flags |= STRATA_POINT_LIGHT_CAST_SHADOWS;
            }
            scene.pointLights.push_back(light);
        } else if (word == "terrain") {
            StrataTerrainTile& t = scene.terrain;
            for (float& v : t.origin) read(in, v, where, "origin");
            read(in, t.cell_size, where, "cell size");
            read(in, t.samples_x, where, "samples x");
            read(in, t.samples_z, where, "samples z");
            for (int i = 0; i < 3; ++i) read(in, t.color[i], where, "colour");
            t.color[3] = 1.0f;
            scene.hasTerrain = true;
            scene.terrainHeights.clear();
            scene.terrainColors.clear();
        } else if (word == "heights" || word == "colors") {
            if (!scene.hasTerrain) bad(where, "'" + word + "' before 'terrain'");
            std::vector<float>& out = word == "heights" ? scene.terrainHeights : scene.terrainColors;
            float v;
            while (in >> v) out.push_back(v);
            if (!in.eof()) bad(where, "expected numbers after '" + word + "'");
        } else if (word == "mesh") {
            SceneMesh mesh;
            for (int i = 0; i < 3; ++i) read(in, mesh.color[i], where, "colour");
            scene.meshes.push_back(mesh);
        } else if (word == "vertices" || word == "vcolors") {
            if (scene.meshes.empty()) bad(where, "'" + word + "' before 'mesh'");
            std::vector<float>& out = word == "vertices" ? scene.meshes.back().positions : scene.meshes.back().colors;
            float v;
            while (in >> v) out.push_back(v);
            if (!in.eof()) bad(where, "expected numbers after '" + word + "'");
        } else if (word == "triangles") {
            if (scene.meshes.empty()) bad(where, "'triangles' before 'mesh'");
            uint32_t v;
            while (in >> v) scene.meshes.back().indices.push_back(v);
            if (!in.eof()) bad(where, "expected indices after 'triangles'");
        } else if (word == "texture") {
            Scene::Texture t;
            std::string kind;
            read(in, t.name, where, "texture name");
            read(in, kind, where, "texture kind");
            read(in, t.width, where, "width");
            read(in, t.height, where, "height");
            if (t.width < 1 || t.height < 1 || t.width > 4096 || t.height > 4096) bad(where, "texture size out of range");
            t.rgba.assign(static_cast<size_t>(t.width) * static_cast<size_t>(t.height) * 4, 255);
            if (kind == "checker") {
                int cu = 0, cv = 0;
                float a[3], b[3];
                read(in, cu, where, "cells u");
                read(in, cv, where, "cells v");
                for (float& v : a) read(in, v, where, "first colour");
                for (float& v : b) read(in, v, where, "second colour");
                if (cu < 1 || cv < 1) bad(where, "checker cells must be positive");
                for (int y = 0; y < t.height; ++y)
                    for (int x = 0; x < t.width; ++x) {
                        const bool odd = (((x * cu) / t.width + (y * cv) / t.height) & 1) != 0;
                        const float* c = odd ? b : a;
                        uint8_t* px = &t.rgba[(static_cast<size_t>(y) * static_cast<size_t>(t.width) +
                                               static_cast<size_t>(x)) * 4];
                        for (int i = 0; i < 3; ++i) px[i] = toByte(c[i]);
                    }
            } else if (kind == "globe") {
                fillGlobe(t);
                t.flags |= STRATA_TEXTURE_CLAMP_V;
            } else {
                bad(where, "unknown texture kind '" + kind + "'");
            }
            std::string option;
            while (in >> option) {
                if (option == "mipmaps") t.flags |= STRATA_TEXTURE_MIPMAPS;
                else if (option == "clamp_v") t.flags |= STRATA_TEXTURE_CLAMP_V;
                else if (option == "nearest") t.flags |= STRATA_TEXTURE_NEAREST;
                else bad(where, "unknown texture option '" + option + "'");
            }
            scene.textures.push_back(std::move(t));
        } else if (word == "sphere_mesh") {
            int slices = 0, stacks = 0;
            SceneMesh mesh;
            read(in, slices, where, "slices");
            read(in, stacks, where, "stacks");
            for (int i = 0; i < 3; ++i) read(in, mesh.color[i], where, "colour");
            if (slices < 3 || stacks < 2) bad(where, "a sphere needs at least 3 slices and 2 stacks");
            for (int j = 0; j <= stacks; ++j) {
                const float v = static_cast<float>(j) / static_cast<float>(stacks);
                const float lat = (0.5f - v) * 3.14159265358979f;
                for (int i = 0; i <= slices; ++i) {
                    const float u = static_cast<float>(i) / static_cast<float>(slices);
                    const float lon = u * 2.0f * 3.14159265358979f;
                    // u is longitude, east from +X towards -Z (counter-clockwise seen from
                    // the +Y pole, so a positive spin about +Y turns the planet prograde);
                    // v = 0 is the north pole, as an equirectangular map's top row.
                    const float x = std::cos(lat) * std::cos(lon), y = std::sin(lat), z = -std::cos(lat) * std::sin(lon);
                    mesh.positions.insert(mesh.positions.end(), {x, y, z});
                    mesh.uvs.insert(mesh.uvs.end(), {u, v});
                }
            }
            const uint32_t row = static_cast<uint32_t>(slices + 1);
            for (uint32_t j = 0; j < static_cast<uint32_t>(stacks); ++j)
                for (uint32_t i = 0; i < static_cast<uint32_t>(slices); ++i) {
                    const uint32_t a = j * row + i, b = a + 1, c = a + row, d = c + 1;
                    // Counter-clockwise seen from outside.
                    mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
                }
            scene.meshes.push_back(std::move(mesh));
        } else if (word == "uvs") {
            if (scene.meshes.empty()) bad(where, "'uvs' before 'mesh'");
            float v;
            while (in >> v) scene.meshes.back().uvs.push_back(v);
            if (!in.eof()) bad(where, "expected numbers after 'uvs'");
        } else if (word == "mesh_texture") {
            if (scene.meshes.empty()) bad(where, "'mesh_texture' before 'mesh'");
            read(in, scene.meshes.back().texture, where, "texture name");
        } else if (word == "mesh_unlit") {
            if (scene.meshes.empty()) bad(where, "'mesh_unlit' before 'mesh'");
            scene.meshes.back().unlit = true;
        } else if (word == "mesh_translucent") {
            if (scene.meshes.empty()) bad(where, "'mesh_translucent' before 'mesh'");
            read(in, scene.meshes.back().color[3], where, "alpha");
            scene.meshes.back().translucent = true;
        } else if (word == "mesh_transform") {
            if (scene.meshes.empty()) bad(where, "'mesh_transform' before 'mesh'");
            float t[3], axis[3], tilt = 0, spin = 0, scale = 1;
            for (float& v : t) read(in, v, where, "translation");
            for (float& v : axis) read(in, v, where, "tilt axis");
            read(in, tilt, where, "tilt degrees");
            read(in, spin, where, "spin degrees");
            read(in, scale, where, "scale");
            const float length = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
            if (!(length > 0.0f)) bad(where, "the tilt axis must be non-zero");
            for (float& v : axis) v /= length;
            const float up[3] = {0, 1, 0};
            float tiltM[16], spinM[16], m[16];
            rotation(axis, tilt, tiltM);
            rotation(up, spin, spinM);
            multiply(tiltM, spinM, m);
            const float scaleTranslate[16] = {scale, 0, 0, 0, 0, scale, 0, 0, 0, 0, scale, 0, 0, 0, 0, 1};
            multiply(m, scaleTranslate, m);
            m[12] = t[0];
            m[13] = t[1];
            m[14] = t[2];
            SceneMesh& mesh = scene.meshes.back();
            std::memcpy(mesh.transform, m, sizeof m);
            mesh.hasTransform = true;
        } else if (word == "texture_update") {
            Scene::TextureUpdate u;
            float rgb[3];
            read(in, u.name, where, "texture name");
            read(in, u.x, where, "x");
            read(in, u.y, where, "y");
            read(in, u.width, where, "width");
            read(in, u.height, where, "height");
            for (float& v : rgb) read(in, v, where, "colour");
            if (u.width < 1 || u.height < 1 || u.width > 4096 || u.height > 4096) bad(where, "update size out of range");
            u.rgba.resize(static_cast<size_t>(u.width) * static_cast<size_t>(u.height) * 4, 255);
            for (size_t i = 0; i < u.rgba.size(); i += 4) {
                for (int c = 0; c < 3; ++c) u.rgba[i + static_cast<size_t>(c)] = toByte(rgb[c]);
            }
            scene.textureUpdates.push_back(std::move(u));
        } else if (word == "expect_ray") {
            Scene::RayExpectation r;
            std::string target;
            for (float& v : r.origin) read(in, v, where, "origin");
            for (float& v : r.direction) read(in, v, where, "direction");
            read(in, r.maxDistance, where, "max distance");
            read(in, target, where, "mesh index or 'none'");
            if (target != "none") {
                try {
                    r.mesh = std::stoi(target);
                } catch (const std::exception&) {
                    bad(where, "expected a mesh index or 'none'");
                }
                if (r.mesh < 0) bad(where, "the mesh index must be >= 0");
            }
            if (in >> r.distance) r.hasDistance = true;
            r.line = lineNumber;
            scene.rayExpectations.push_back(r);
        } else if (word == "expect_depth") {
            Scene::DepthExpectation d;
            std::string distance;
            read(in, d.x, where, "x");
            read(in, d.y, where, "y");
            read(in, distance, where, "distance or 'inf'");
            if (distance == "inf") {
                d.distance = std::numeric_limits<float>::infinity();
            } else {
                try {
                    d.distance = std::stof(distance);
                } catch (const std::exception&) {
                    bad(where, "expected a distance or 'inf'");
                }
            }
            d.line = lineNumber;
            scene.depthExpectations.push_back(d);
        } else if (word == "include") {
            std::string other;
            read(in, other, where, "file name");
            parse(directoryOf(path) + other, scene, depth + 1);
        } else if (word == "item") {
            std::string kind;
            read(in, kind, where, "primitive kind");
            StrataRenderItem item{};
            item.kind = kindFromName(kind, where);
            for (float& v : item.position) read(in, v, where, "position");
            for (float& v : item.scale) read(in, v, where, "scale");
            for (int i = 0; i < 3; ++i) read(in, item.color[i], where, "colour");
            item.color[3] = 1.0f;
            float alpha;
            if (in >> alpha) item.color[3] = alpha;
            scene.items.push_back(item);
        } else if (word == "line") {
            StrataLineVertex a{}, b{};
            for (float& v : a.position) read(in, v, where, "start");
            for (float& v : b.position) read(in, v, where, "end");
            for (float& v : a.color) read(in, v, where, "colour");
            std::memcpy(b.color, a.color, sizeof b.color);
            std::string option;
            bool onTop = false;
            if (in >> option) {
                if (option != "on_top") bad(where, "expected 'on_top' or nothing after the colour");
                onTop = true;
            }
            auto& lines = onTop ? scene.linesOnTop : scene.lines;
            lines.push_back(a);
            lines.push_back(b);
        } else if (word == "wide_line") {
            StrataWideLineVertex a{}, b{};
            for (float& v : a.position) read(in, v, where, "start");
            for (float& v : b.position) read(in, v, where, "end");
            for (float& v : a.color) read(in, v, where, "colour");
            read(in, a.width_pixels, where, "width_pixels");
            read(in, a.world_width, where, "world_width");
            std::memcpy(b.color, a.color, sizeof b.color);
            b.width_pixels = a.width_pixels;
            b.world_width = a.world_width;
            uint32_t flags = 0;
            std::string option;
            while (in >> option) {
                if (option == "to") {
                    for (float& v : b.color) read(in, v, where, "end colour");
                    read(in, b.width_pixels, where, "end width_pixels");
                    read(in, b.world_width, where, "end world_width");
                } else if (option == "round") {
                    flags |= STRATA_LINES_ROUND_CAPS;
                } else if (option == "on_top") {
                    flags |= STRATA_LINES_ON_TOP;
                } else {
                    bad(where, "expected 'to', 'round' or 'on_top' after the widths");
                }
            }
            scene.wideLines[flags].push_back(a);
            scene.wideLines[flags].push_back(b);
        } else if (word == "circle") {
            float centre[3], radius = 0.0f, color[4];
            int segments = 0;
            for (float& v : centre) read(in, v, where, "centre");
            read(in, radius, where, "radius");
            read(in, segments, where, "segments");
            for (float& v : color) read(in, v, where, "colour");
            if (segments < 3) bad(where, "a circle needs at least 3 segments");
            for (int i = 0; i < segments; ++i) {
                for (int end = 0; end < 2; ++end) {
                    const double angle = 6.283185307179586 * (i + end) / segments;
                    StrataLineVertex v{};
                    v.position[0] = centre[0] + radius * static_cast<float>(std::cos(angle));
                    v.position[1] = centre[1];
                    v.position[2] = centre[2] + radius * static_cast<float>(std::sin(angle));
                    std::memcpy(v.color, color, sizeof v.color);
                    scene.lines.push_back(v);
                }
            }
        } else if (word == "point") {
            StrataPoint p{};
            for (float& v : p.position) read(in, v, where, "position");
            read(in, p.size_pixels, where, "size in pixels");
            read(in, p.world_diameter, where, "world diameter");
            for (float& v : p.color) read(in, v, where, "colour");
            std::string option;
            while (in >> option) {
                if (option == "soft") p.flags |= STRATA_POINT_SOFT;
                else if (option == "on_top") p.flags |= STRATA_POINT_ON_TOP;
                else bad(where, "expected 'soft', 'on_top' or nothing after the colour");
            }
            scene.points.push_back(p);
        } else if (word == "rect") {
            StrataRect r{};
            read(in, r.x, where, "x");
            read(in, r.y, where, "y");
            read(in, r.width, where, "width");
            read(in, r.height, where, "height");
            for (float& v : r.color) read(in, v, where, "colour");
            scene.rects.push_back(r);
        } else if (word == "text") {
            Scene::Text t;
            read(in, t.text.x, where, "x");
            read(in, t.text.y, where, "y");
            read(in, t.text.pixel_size, where, "pixel size");
            for (float& v : t.text.color) read(in, v, where, "colour");
            std::string option;
            read(in, option, where, "':' before the text");
            if (option == "shadow") {
                t.text.flags |= STRATA_TEXT_SHADOW;
                read(in, option, where, "':' before the text");
            }
            if (option != ":") bad(where, "expected ':' before the text");
            std::getline(in, t.value);
            if (!t.value.empty() && t.value[0] == ' ') t.value.erase(0, 1);
            scene.texts.push_back(std::move(t));
        } else {
            bad(where, "unknown directive '" + word + "'");
        }
    }
}

}  // namespace

StrataMesh SceneMesh::view() const {
    StrataMesh m{};
    m.positions = positions.data();
    m.colors = colors.empty() ? nullptr : colors.data();
    m.indices = indices.data();
    m.vertex_count = static_cast<int32_t>(positions.size() / 3);
    m.index_count = static_cast<int32_t>(indices.size());
    for (int i = 0; i < 4; ++i) m.color[i] = color[i];
    return m;
}

Scene loadScene(const std::string& path) {
    Scene scene;
    strata_lighting_defaults(STRATA_LIGHTING_LEGACY, &scene.lighting);
    scene.frame.clear_flags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH;
    scene.frame.clear_color[0] = 0.45f;  // BN's clear colour
    scene.frame.clear_color[1] = 0.65f;
    scene.frame.clear_color[2] = 0.90f;
    scene.frame.clear_color[3] = 1.0f;
    parse(path, scene, 0);
    if (scene.hasTerrain) {
        const size_t samples = static_cast<size_t>(scene.terrain.samples_x) * static_cast<size_t>(scene.terrain.samples_z);
        if (scene.terrainHeights.size() != samples) {
            bad(path, "terrain has " + std::to_string(scene.terrainHeights.size()) + " heights, expected " +
                          std::to_string(samples));
        }
        if (!scene.terrainColors.empty() && scene.terrainColors.size() != samples * 3) {
            bad(path, "terrain has " + std::to_string(scene.terrainColors.size()) + " colour values, expected " +
                          std::to_string(samples * 3));
        }
        scene.terrain.heights = scene.terrainHeights.data();
        scene.terrain.colors = scene.terrainColors.empty() ? nullptr : scene.terrainColors.data();
    }
    for (const SceneMesh& mesh : scene.meshes) {
        if (mesh.positions.size() % 3 != 0) bad(path, "a mesh's vertex values are not a multiple of 3");
        if (!mesh.uvs.empty() && mesh.uvs.size() * 3 != mesh.positions.size() * 2) {
            bad(path, "a mesh has " + std::to_string(mesh.uvs.size()) + " uv values, expected " +
                          std::to_string(mesh.positions.size() / 3 * 2));
        }
        if (!mesh.colors.empty() && mesh.colors.size() != mesh.positions.size()) {
            bad(path, "a mesh has " + std::to_string(mesh.colors.size()) + " colour values, expected " +
                          std::to_string(mesh.positions.size()));
        }
    }
    for (const Scene::TextureUpdate& u : scene.textureUpdates) {
        bool known = false;
        for (const Scene::Texture& t : scene.textures) known = known || t.name == u.name;
        if (!known) bad(path, "texture_update names unknown texture '" + u.name + "'");
    }
    for (const Scene::RayExpectation& r : scene.rayExpectations) {
        if (r.mesh >= static_cast<int>(scene.meshes.size())) {
            bad(path + ":" + std::to_string(r.line), "expect_ray names mesh " + std::to_string(r.mesh) + " of " +
                                                         std::to_string(scene.meshes.size()));
        }
    }
    for (const Scene::DepthExpectation& d : scene.depthExpectations) {
        if (d.x < 0 || d.y < 0 || d.x >= scene.width || d.y >= scene.height) {
            bad(path + ":" + std::to_string(d.line), "expect_depth pixel is outside the frame");
        }
    }
    return scene;
}

}  // namespace strata::scene
