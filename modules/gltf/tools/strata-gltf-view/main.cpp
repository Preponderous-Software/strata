// strata-gltf-view: loads a .gltf/.glb with the glTF module and renders it
// headless to a PNG, framed by a camera preset and lit by a sun preset.
//
//   strata-gltf-view <in.gltf|in.glb> <out.png> [options]
//
// See docs/GLTF.md for every option.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "image/image.hpp"
#include "json.hpp"
#include "strata/gltf/model.hpp"
#include "strata/gltf/plan.hpp"
#include "strata/gltf/upload.hpp"
#include "strata/strata.h"

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage: strata-gltf-view <in.gltf|in.glb> <out.png> [options]\n"
                 "  --size WxH              image size (default 640x360)\n"
                 "  --camera PRESET         iso | front | back | left | right | top (default iso)\n"
                 "  --eye X,Y,Z             explicit eye (with --target; overrides --camera)\n"
                 "  --target X,Y,Z          explicit target (default: the bounds' centre)\n"
                 "  --fov DEGREES           vertical field of view (default 45)\n"
                 "  --fit PERCENT|bounds    frame the box holding the central PERCENT of vertices on each\n"
                 "                          axis plus a 5%% margin (default 95), or the whole bounds\n"
                 "  --cameras FILE          JSON with named cameras: {\"cameras\": {NAME: {\"eye\": [x,y,z],\n"
                 "                          \"target\": [x,y,z], \"fov_y_deg\": F}}} and optionally\n"
                 "                          {\"sun\": {\"times\": {NAME: 0..1}, \"latitude_deg\": L}}\n"
                 "  --view NAME             the camera to use from --cameras (default: the first by name)\n"
                 "  --shadow-distance M     sun shadow distance (default: fitted, or the engine's 150)\n"
                 "  --zoom F                distance multiplier on the framed camera (default 1)\n"
                 "  --sun PRESET            morning | noon | afternoon | evening | legacy | 0..1 time of day\n"
                 "                          (default afternoon)\n"
                 "  --latitude DEGREES      sun path latitude (default 40)\n"
                 "  --no-shadows            disable sun shadows\n"
                 "  --fog                   enable distance fog\n"
                 "  --scene N               glTF scene index (default: the file's default)\n"
                 "  --max-meshes N          mesh budget (default 256)\n"
                 "  --max-textures N        texture budget (default 256)\n"
                 "  --max-texture-size N    largest texture side (default 8192)\n"
                 "  --atlas-size N          atlas page side (default 4096)\n"
                 "  --merge                 merge by material even when not needed\n"
                 "  --no-back-faces         do not add back faces for doubleSided materials\n"
                 "  --colors POLICY         display | linear | stored (default display)\n"
                 "  --mask POLICY           blend | opaque (default blend)\n"
                 "  --emissive POLICY       unlit | ignore (default unlit)\n"
                 "  --json FILE             write the load report as JSON\n"
                 "  --quiet                 do not print the report\n"
                 "  --surface S             headless | window (default headless)\n");
    return 2;
}

bool parseFloats(const char* text, float* out, int count) {
    const char* p = text;
    for (int i = 0; i < count; ++i) {
        char* end = nullptr;
        out[i] = std::strtof(p, &end);
        if (end == p || !std::isfinite(out[i])) return false;
        p = end;
        if (i + 1 < count) {
            if (*p != ',') return false;
            ++p;
        }
    }
    return *p == '\0';
}

bool parseInt(const char* text, int& out) {
    char* end = nullptr;
    const long v = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || v < -1000000000L || v > 1000000000L) return false;
    out = static_cast<int>(v);
    return true;
}

bool check(StrataStatus status, const char* what) {
    if (status == STRATA_OK) return true;
    std::fprintf(stderr, "%s failed: %s (%s)\n", what, strata_status_string(status), strata_last_error());
    return false;
}

struct Options {
    int width = 640, height = 360;
    std::string camera = "iso";
    bool hasEye = false, hasTarget = false;
    float eye[3] = {0, 0, 0}, target[3] = {0, 0, 0};
    float fov = 45.0f;
    float zoom = 1.0f;
    std::string sun = "afternoon";
    float latitude = 40.0f;
    bool shadows = true, fog = false, quiet = false;
    std::string json;
    float fitPercent = 95.0f;  // 100 or more: the whole bounds
    float shadowDistance = 0.0f;
    std::map<std::string, float> sunTimes;  // named times of day from a cameras file
    std::string camerasFile, view;
    bool latitudeGiven = false;
    int32_t surface = STRATA_SURFACE_HEADLESS;
    strata::gltf::LoadOptions load;
    strata::gltf::BudgetOptions budget;
};

// The direction from the target to the eye, and the camera's up, per preset.
bool presetDirection(const std::string& name, float dir[3], float up[3]) {
    up[0] = 0, up[1] = 1, up[2] = 0;
    if (name == "iso") dir[0] = 0.75f, dir[1] = 0.65f, dir[2] = 1.0f;          // from the south-east, above
    else if (name == "front") dir[0] = 0, dir[1] = 0.25f, dir[2] = 1;          // from +Z
    else if (name == "back") dir[0] = 0, dir[1] = 0.25f, dir[2] = -1;          // from -Z
    else if (name == "right") dir[0] = 1, dir[1] = 0.25f, dir[2] = 0;          // from +X
    else if (name == "left") dir[0] = -1, dir[1] = 0.25f, dir[2] = 0;          // from -X
    else if (name == "top") dir[0] = 0, dir[1] = 1, dir[2] = 0, up[0] = 0, up[1] = 0, up[2] = -1;  // north up
    else return false;
    const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    for (int k = 0; k < 3; ++k) dir[k] /= len;
    return true;
}

bool readVec3(const gltfview::Json* j, float out[3]) {
    if (!j || j->type != gltfview::Json::Type::Array || j->array.size() != 3) return false;
    for (int k = 0; k < 3; ++k) {
        if (j->array[static_cast<size_t>(k)].type != gltfview::Json::Type::Number) return false;
        out[k] = static_cast<float>(j->array[static_cast<size_t>(k)].number);
    }
    return true;
}

// Reads --cameras: picks --view (or the first camera by name) and any named sun times.
bool readCameras(Options& o) {
    std::ifstream in(o.camerasFile, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    if (!in) {
        std::fprintf(stderr, "strata-gltf-view: cannot read %s\n", o.camerasFile.c_str());
        return false;
    }
    gltfview::Json root;
    try {
        root = gltfview::JsonReader(text.str()).parse();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "strata-gltf-view: %s: %s\n", o.camerasFile.c_str(), e.what());
        return false;
    }
    const gltfview::Json* cameras = root.find("cameras");
    if (!cameras || cameras->type != gltfview::Json::Type::Object || cameras->object.empty()) {
        std::fprintf(stderr, "strata-gltf-view: %s has no \"cameras\" object\n", o.camerasFile.c_str());
        return false;
    }
    const std::string name = o.view.empty() ? cameras->object.begin()->first : o.view;
    const gltfview::Json* cam = cameras->find(name);
    if (!cam || !readVec3(cam->find("eye"), o.eye) || !readVec3(cam->find("target"), o.target)) {
        std::fprintf(stderr, "strata-gltf-view: no usable camera '%s' in %s; it has:", name.c_str(), o.camerasFile.c_str());
        for (const auto& entry : cameras->object) std::fprintf(stderr, " %s", entry.first.c_str());
        std::fprintf(stderr, "\n");
        return false;
    }
    o.hasEye = o.hasTarget = true;
    if (const gltfview::Json* fov = cam->find("fov_y_deg")) {
        if (fov->type == gltfview::Json::Type::Number) o.fov = static_cast<float>(fov->number);
    }
    if (const gltfview::Json* projection = cam->find("projection")) {
        if (projection->type == gltfview::Json::Type::String && projection->string != "perspective") {
            std::fprintf(stderr, "strata-gltf-view: camera '%s' is %s; drawn as perspective\n", name.c_str(),
                         projection->string.c_str());
        }
    }
    if (const gltfview::Json* sun = root.find("sun")) {
        if (const gltfview::Json* times = sun->find("times")) {
            for (const auto& entry : times->object) {
                if (entry.second.type == gltfview::Json::Type::Number) {
                    o.sunTimes[entry.first] = static_cast<float>(entry.second.number);
                }
            }
        }
        const gltfview::Json* lat = sun->find("latitude_deg");
        if (lat && lat->type == gltfview::Json::Type::Number && !o.latitudeGiven) {
            o.latitude = static_cast<float>(lat->number);
        }
    }
    return true;
}

// The box holding the central `percent` of vertex positions on each axis
// (sampled to at most about two million vertices), or the whole bounds.
void framingBox(const strata::gltf::Model& model, float percent, float outMin[3], float outMax[3]) {
    for (int k = 0; k < 3; ++k) outMin[k] = model.boundsMin[k], outMax[k] = model.boundsMax[k];
    if (percent >= 100.0f) return;
    const size_t total = model.vertexCount();
    if (total < 8) return;
    const size_t stride = std::max<size_t>(1, total / 2000000);
    std::vector<float> axis[3];
    size_t n = 0;
    for (const strata::gltf::Primitive& p : model.primitives) {
        for (size_t v = 0; v < p.vertexCount(); ++v, ++n) {
            if (n % stride) continue;
            for (int k = 0; k < 3; ++k) axis[k].push_back(p.positions[v * 3 + static_cast<size_t>(k)]);
        }
    }
    const double tail = (100.0 - percent) / 200.0;
    for (int k = 0; k < 3; ++k) {
        std::vector<float>& a = axis[k];
        const size_t lo = static_cast<size_t>(tail * static_cast<double>(a.size() - 1));
        const size_t hi = a.size() - 1 - lo;
        std::nth_element(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(lo), a.end());
        outMin[k] = a[lo];
        std::nth_element(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(hi), a.end());
        outMax[k] = a[hi];
        // A margin of a twentieth of the box each side, so the edge of what
        // was kept is not cut, but never past the whole bounds.
        const float margin = 0.05f * (outMax[k] - outMin[k]);
        outMin[k] = std::max(outMin[k] - margin, model.boundsMin[k]);
        outMax[k] = std::min(outMax[k] + margin, model.boundsMax[k]);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string input = argv[1], output = argv[2];
    Options o;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        const bool v = i + 1 < argc;
        int n = 0;
        if (a == "--size" && v) {
            if (std::sscanf(argv[++i], "%dx%d", &o.width, &o.height) != 2 || o.width < 1 || o.height < 1) return usage();
        } else if (a == "--camera" && v) {
            o.camera = argv[++i];
        } else if (a == "--eye" && v) {
            if (!parseFloats(argv[++i], o.eye, 3)) return usage();
            o.hasEye = true;
        } else if (a == "--target" && v) {
            if (!parseFloats(argv[++i], o.target, 3)) return usage();
            o.hasTarget = true;
        } else if (a == "--fov" && v) {
            o.fov = std::strtof(argv[++i], nullptr);
        } else if (a == "--zoom" && v) {
            o.zoom = std::strtof(argv[++i], nullptr);
        } else if (a == "--sun" && v) {
            o.sun = argv[++i];
        } else if (a == "--latitude" && v) {
            o.latitude = std::strtof(argv[++i], nullptr);
            o.latitudeGiven = true;
        } else if (a == "--fit" && v) {
            const std::string fit = argv[++i];
            o.fitPercent = fit == "bounds" ? 100.0f : std::strtof(fit.c_str(), nullptr);
            if (!(o.fitPercent > 0.0f)) return usage();
        } else if (a == "--cameras" && v) {
            o.camerasFile = argv[++i];
        } else if (a == "--view" && v) {
            o.view = argv[++i];
        } else if (a == "--shadow-distance" && v) {
            o.shadowDistance = std::strtof(argv[++i], nullptr);
        } else if (a == "--emissive" && v) {
            const std::string p = argv[++i];
            if (p == "unlit") o.budget.emissive = strata::gltf::EmissivePolicy::Unlit;
            else if (p == "ignore") o.budget.emissive = strata::gltf::EmissivePolicy::Ignore;
            else return usage();
        } else if (a == "--no-shadows") {
            o.shadows = false;
        } else if (a == "--fog") {
            o.fog = true;
        } else if (a == "--scene" && v && parseInt(argv[i + 1], n)) {
            o.load.scene = n, ++i;
        } else if (a == "--max-meshes" && v && parseInt(argv[i + 1], n)) {
            o.budget.maxMeshes = n, ++i;
        } else if (a == "--max-textures" && v && parseInt(argv[i + 1], n)) {
            o.budget.maxTextures = n, ++i;
        } else if (a == "--max-texture-size" && v && parseInt(argv[i + 1], n)) {
            o.budget.maxTextureSize = n, ++i;
            o.budget.atlasSize = std::min(o.budget.atlasSize, n);
        } else if (a == "--atlas-size" && v && parseInt(argv[i + 1], n)) {
            o.budget.atlasSize = n, ++i;
        } else if (a == "--merge") {
            o.budget.alwaysMerge = true;
        } else if (a == "--no-back-faces") {
            o.budget.backFaces = false;
        } else if (a == "--colors" && v) {
            const std::string p = argv[++i];
            if (p == "display") o.budget.colors = strata::gltf::ColorPolicy::Display;
            else if (p == "linear") o.budget.colors = strata::gltf::ColorPolicy::Linear;
            else if (p == "stored") o.budget.colors = strata::gltf::ColorPolicy::AsStored;
            else return usage();
        } else if (a == "--mask" && v) {
            const std::string p = argv[++i];
            if (p == "blend") o.budget.mask = strata::gltf::MaskPolicy::Blend;
            else if (p == "opaque") o.budget.mask = strata::gltf::MaskPolicy::Opaque;
            else return usage();
        } else if (a == "--json" && v) {
            o.json = argv[++i];
        } else if (a == "--quiet") {
            o.quiet = true;
        } else if (a == "--surface" && v) {
            const std::string s = argv[++i];
            if (s == "headless") o.surface = STRATA_SURFACE_HEADLESS;
            else if (s == "window") o.surface = STRATA_SURFACE_WINDOW;
            else return usage();
        } else {
            return usage();
        }
    }
    if (!o.camerasFile.empty() && !readCameras(o)) return 2;
    if (!(o.fov > 1.0f && o.fov < 179.0f) || !(o.zoom > 0.0f)) return usage();

    strata::gltf::Model model;
    strata::gltf::Plan plan;
    try {
        model = strata::gltf::loadFile(input, o.load);
        plan = strata::gltf::plan(model, o.budget);
    } catch (const strata::gltf::Error& e) {
        std::fprintf(stderr, "strata-gltf-view: %s\n", e.what());
        return 1;
    }
    if (!o.quiet) std::printf("%s", plan.report.text().c_str());
    if (!o.json.empty()) {
        std::ofstream out(o.json);
        out << plan.report.json() << "\n";
        if (!out) {
            std::fprintf(stderr, "strata-gltf-view: cannot write %s\n", o.json.c_str());
            return 1;
        }
    }

    StrataEngineDesc desc{};
    desc.struct_size = sizeof desc;
    desc.abi_version = STRATA_ABI_VERSION;
    desc.surface = o.surface;
    desc.width = o.width;
    desc.height = o.height;
    desc.title = "strata-gltf-view";
    desc.flags = o.surface == STRATA_SURFACE_WINDOW ? static_cast<uint32_t>(STRATA_FLAG_HIDDEN_WINDOW) : 0u;
    StrataEngine* engine = nullptr;
    if (!check(strata_create(&desc, &engine), "strata_create")) return 1;
    if (strata_abi_minor() < 15) {
        std::fprintf(stderr, "strata-gltf-view: libstrata ABI 1.%u is too old (1.15 or later needed)\n", strata_abi_minor());
        strata_destroy(engine);
        return 1;
    }

    strata::gltf::Uploaded uploaded;
    StrataStatus status = STRATA_OK;
    std::string error;
    if (!strata::gltf::upload(engine, plan, uploaded, &status, &error)) {
        std::fprintf(stderr, "strata-gltf-view: %s\n", error.c_str());
        strata_destroy(engine);
        return 1;
    }

    // The region to frame: the box holding the central `fit` percent of
    // vertices on each axis (outliers such as a huge water plane or a stray
    // far-off object do not count), or the whole bounds.
    float fitMin[3], fitMax[3];
    framingBox(model, o.fitPercent, fitMin, fitMax);
    float centre[3], radius = 0.0f, fullRadius = 0.0f, fullCentre[3];
    for (int k = 0; k < 3; ++k) {
        centre[k] = 0.5f * (fitMin[k] + fitMax[k]);
        const float half = 0.5f * (fitMax[k] - fitMin[k]);
        radius += half * half;
        fullCentre[k] = 0.5f * (model.boundsMin[k] + model.boundsMax[k]);
        const float fullHalf = 0.5f * (model.boundsMax[k] - model.boundsMin[k]);
        fullRadius += fullHalf * fullHalf;
    }
    radius = std::max(std::sqrt(radius), 1e-3f);
    fullRadius = std::max(std::sqrt(fullRadius), radius);
    int32_t width = o.width, height = o.height;
    check(strata_get_drawable_size(engine, &width, &height), "strata_get_drawable_size");
    const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
    const float halfFov = 0.5f * o.fov * 3.14159265f / 180.0f;
    float dir[3], up[3];
    if (!presetDirection(o.camera, dir, up)) {
        std::fprintf(stderr, "strata-gltf-view: unknown camera preset '%s'\n", o.camera.c_str());
        strata_destroy(engine);
        return 2;
    }
    float target[3];
    for (int k = 0; k < 3; ++k) target[k] = o.hasTarget ? o.target[k] : centre[k];
    // The nearest eye along dir that keeps all eight corners of the framing
    // box in view (with a small margin): for a corner p (relative to the
    // target), at eye distance d its depth is d - p.dir, and it fits when its
    // sideways and upward offsets are within depth * tan(half field of view).
    const float f[3] = {-dir[0], -dir[1], -dir[2]};
    float right[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
    const float rl = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    for (float& c : right) c /= rl;
    const float camUp[3] = {right[1] * f[2] - right[2] * f[1], right[2] * f[0] - right[0] * f[2],
                            right[0] * f[1] - right[1] * f[0]};
    const float tanY = std::tan(halfFov), tanX = tanY * aspect;
    float distance = 0.0f;
    for (int corner = 0; corner < 8; ++corner) {
        float p[3];
        for (int k = 0; k < 3; ++k) p[k] = ((corner >> k) & 1 ? fitMax[k] : fitMin[k]) - target[k];
        const float x = p[0] * right[0] + p[1] * right[1] + p[2] * right[2];
        const float y = p[0] * camUp[0] + p[1] * camUp[1] + p[2] * camUp[2];
        const float along = p[0] * dir[0] + p[1] * dir[1] + p[2] * dir[2];
        distance = std::max(distance, along + 1.06f * std::max(std::fabs(x) / tanX, std::fabs(y) / tanY));
    }
    distance = o.zoom * std::max(distance, 1e-3f);
    float eye[3];
    for (int k = 0; k < 3; ++k) eye[k] = o.hasEye ? o.eye[k] : target[k] + dir[k] * distance;
    if (o.hasEye) up[0] = 0, up[1] = 1, up[2] = 0;
    // Far reaches the furthest point of the whole scene; near is as far out
    // as the framed region allows (a framed preset), or close for a placed
    // eye that may stand among the geometry.
    float toFull = 0.0f, toFit = 0.0f;
    for (int k = 0; k < 3; ++k) {
        toFull += (eye[k] - fullCentre[k]) * (eye[k] - fullCentre[k]);
        toFit += (eye[k] - centre[k]) * (eye[k] - centre[k]);
    }
    toFull = std::sqrt(toFull);
    toFit = std::sqrt(toFit);
    const float farPlane = toFull + fullRadius;
    const float nearPlane = o.hasEye ? std::max(0.05f, farPlane * 2e-5f)
                                     : std::max(toFit - 1.5f * radius, std::max(0.05f, farPlane * 2e-5f));

    bool ok = true;
    StrataCamera camera{};
    ok = ok && check(strata_camera_look_at(eye, target, up, o.fov, aspect, nearPlane, farPlane, &camera), "strata_camera_look_at");
    ok = ok && check(strata_set_camera(engine, &camera), "strata_set_camera");

    StrataLighting lighting{};
    StrataFrameDesc frame{};
    if (o.sun == "legacy") {
        ok = ok && check(strata_lighting_defaults(STRATA_LIGHTING_LEGACY, &lighting), "strata_lighting_defaults");
        frame.clear_flags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH;
        frame.clear_color[0] = 0.45f, frame.clear_color[1] = 0.62f, frame.clear_color[2] = 0.85f, frame.clear_color[3] = 1;
    } else {
        float time = 0.0f;
        auto named = o.sunTimes.find(o.sun);
        if (named != o.sunTimes.end()) time = named->second;
        else if (o.sun == "morning") time = 0.32f;
        else if (o.sun == "noon") time = 0.5f;
        else if (o.sun == "afternoon") time = 0.6f;
        else if (o.sun == "evening") time = 0.7f;
        else {
            char* end = nullptr;
            time = std::strtof(o.sun.c_str(), &end);
            if (end == o.sun.c_str() || *end != '\0' || !(time >= 0.0f && time < 1.0f)) {
                std::fprintf(stderr, "strata-gltf-view: unknown sun preset '%s'\n", o.sun.c_str());
                strata_destroy(engine);
                return 2;
            }
        }
        ok = ok && check(strata_lighting_defaults(STRATA_LIGHTING_SUN, &lighting), "strata_lighting_defaults");
        lighting.time_of_day = time;
        lighting.latitude_degrees = o.latitude;
        lighting.cascade_count = o.shadows ? 4 : 0;
        lighting.shadow_map_size = 2048;
        if (o.shadowDistance > 0.0f) lighting.shadow_distance = std::min(o.shadowDistance, farPlane);
        else if (!o.hasEye) lighting.shadow_distance = std::min(toFit + radius, farPlane);
        else lighting.shadow_distance = std::min(lighting.shadow_distance, farPlane);  // the engine's default
        lighting.fog = o.fog ? 1 : 0;
        frame.clear_flags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH | STRATA_CLEAR_SKY;
    }
    ok = ok && check(strata_set_lighting(engine, &lighting), "strata_set_lighting");
    ok = ok && check(strata_begin_frame(engine, &frame), "strata_begin_frame");
    ok = ok && check(strata_end_frame(engine), "strata_end_frame");

    strata::image::Image image;
    image.width = width;
    image.height = height;
    image.rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    int32_t rw = 0, rh = 0;
    ok = ok && check(strata_read_pixels_rgba8(engine, image.rgba.data(), static_cast<int32_t>(image.rgba.size()), &rw, &rh),
                     "strata_read_pixels_rgba8");
    strata::gltf::remove(engine, uploaded);
    strata_destroy(engine);
    if (!ok) return 1;
    std::string werr;
    if (!strata::image::writePng(output, image, &werr)) {
        std::fprintf(stderr, "strata-gltf-view: %s\n", werr.c_str());
        return 1;
    }
    if (!o.quiet) std::printf("wrote %s (%dx%d)\n", output.c_str(), width, height);
    return 0;
}
