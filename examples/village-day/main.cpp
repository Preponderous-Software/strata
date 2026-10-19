// strata-village-day: the L2 village through a whole day, in a window or a
// browser tab. It uses only the C ABI (strata.h), so the same file builds
// natively (an SDL3 window, OpenGL 4.5) and with Emscripten (a canvas,
// WebGL 2); the only difference is who runs the frame loop.
//
//   strata-village-day [village.scene]
//
// Keys: Space pauses the clock, Left/Right scrub it, Up/Down change its speed.
// The village is tests/scenes/_village_items.scene (in the browser build it
// is packaged as /village.scene).
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include "scene_file.hpp"
#include "strata/strata.h"

namespace {

constexpr int kWidth = 960;
constexpr int kHeight = 540;
// SDL scancodes (USB HID usage IDs).
constexpr int kScancodeSpace = 44, kScancodeRight = 79, kScancodeLeft = 80, kScancodeDown = 81, kScancodeUp = 82;

struct Demo {
    StrataEngine* engine = nullptr;
    std::vector<StrataRenderItem> items;
    double timeOfDay = 0.30;  // morning
    double daysPerSecond = 1.0 / 40.0;
    bool paused = false;
    bool running = true;
    double lastSeconds = -1.0;
    double cameraAngle = 0.0;
    double fpsSeconds = 0.0;
    int fpsFrames = 0;
    double fps = 0.0;
};

double nowSeconds() {
#if defined(__EMSCRIPTEN__)
    return emscripten_get_now() / 1000.0;
#else
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}

bool ok(StrataStatus status, const char* what) {
    if (status == STRATA_OK) return true;
    std::fprintf(stderr, "%s failed: %s (%s)\n", what, strata_status_string(status), strata_last_error());
    return false;
}

// 0 by day, 1 at night, ramping across dusk and dawn (as the README's GIF).
float nightAmount(double t) {
    auto ramp = [](double a, double b, double x) { return std::fmin(std::fmax((x - a) / (b - a), 0.0), 1.0); };
    return static_cast<float>(std::fmax(ramp(0.72, 0.80, t), 1.0 - ramp(0.20, 0.28, t)));
}

void handleEvents(Demo& d) {
    StrataEvent e;
    while (strata_poll_event(d.engine, &e)) {
        if (e.type == STRATA_EVENT_QUIT) d.running = false;
        if (e.type != STRATA_EVENT_KEY_DOWN) continue;
        switch (e.scancode) {
            case kScancodeSpace:
                if (!e.repeat) d.paused = !d.paused;
                break;
            case kScancodeLeft: d.timeOfDay -= 0.01; break;
            case kScancodeRight: d.timeOfDay += 0.01; break;
            case kScancodeUp: d.daysPerSecond = std::fmin(d.daysPerSecond * 2.0, 1.0); break;
            case kScancodeDown: d.daysPerSecond = std::fmax(d.daysPerSecond / 2.0, 1.0 / 640.0); break;
            default: break;
        }
    }
}

void frame(Demo& d) {
    handleEvents(d);
    const double now = nowSeconds();
    const double dt = d.lastSeconds < 0.0 ? 0.0 : std::fmin(now - d.lastSeconds, 0.25);
    d.lastSeconds = now;
    if (!d.paused) d.timeOfDay += dt * d.daysPerSecond;
    d.timeOfDay -= std::floor(d.timeOfDay);
    d.cameraAngle += dt * 0.05;
    d.fpsSeconds += dt;
    ++d.fpsFrames;
    if (d.fpsSeconds >= 0.5) {
        d.fps = d.fpsFrames / d.fpsSeconds;
        d.fpsSeconds = 0.0;
        d.fpsFrames = 0;
    }

    int32_t width = 0, height = 0;
    strata_get_drawable_size(d.engine, &width, &height);
    if (width <= 0 || height <= 0) return;

    // A slow orbit around the village square.
    const float radius = 50.0f;
    const float angle = static_cast<float>(2.356 + 0.35 * std::sin(d.cameraAngle));
    const float eye[3] = {radius * std::cos(angle), 19.0f, radius * std::sin(angle) - 4.0f};
    const float target[3] = {0.0f, 0.0f, -4.0f};
    const float up[3] = {0.0f, 1.0f, 0.0f};
    StrataCamera camera{};
    strata_camera_look_at(eye, target, up, 60.0f, static_cast<float>(width) / static_cast<float>(height), 0.3f, 400.0f,
                          &camera);
    strata_set_camera(d.engine, &camera);

    StrataLighting lighting{};
    strata_lighting_defaults(STRATA_LIGHTING_SUN, &lighting);
    lighting.time_of_day = static_cast<float>(d.timeOfDay);
    lighting.latitude_degrees = 35.0f;
    lighting.cascade_count = 4;
    lighting.shadow_map_size = 2048;
    lighting.shadow_distance = 150.0f;
    strata_set_lighting(d.engine, &lighting);

    // The campfire (shadowed) and two lamps, lit after dusk.
    const float n = nightAmount(d.timeOfDay);
    StrataPointLight lights[3] = {
        {{-2.0f, 1.6f, 4.5f}, 16.0f, {1.0f, 0.55f, 0.2f}, 6.0f * n, STRATA_POINT_LIGHT_CAST_SHADOWS},
        {{-11.0f, 2.8f, -4.0f}, 10.0f, {1.0f, 0.8f, 0.5f}, 4.0f * n, 0u},
        {{7.5f, 2.8f, -4.5f}, 10.0f, {1.0f, 0.8f, 0.5f}, 4.0f * n, 0u},
    };
    strata_set_point_lights(d.engine, lights, n > 0.0f ? 3 : 0);

    StrataFrameDesc desc{};
    desc.clear_flags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH | STRATA_CLEAR_SKY;
    desc.clear_color[3] = 1.0f;
    if (!ok(strata_begin_frame(d.engine, &desc), "strata_begin_frame")) {
        d.running = false;
        return;
    }
    strata_submit_primitives(d.engine, d.items.data(), static_cast<int32_t>(d.items.size()));

    const int minutes = static_cast<int>(d.timeOfDay * 24.0 * 60.0);
    char line[160];
    std::snprintf(line, sizeof line, "%02d:%02d  %s  %.0f fps", minutes / 60, minutes % 60,
                  d.paused ? "paused" : "", d.fps);
    StrataText texts[2] = {};
    texts[0].x = 16.0f;
    texts[0].y = 16.0f;
    texts[0].pixel_size = 16.0f;
    texts[0].flags = STRATA_TEXT_SHADOW;
    texts[0].color[0] = texts[0].color[1] = texts[0].color[2] = texts[0].color[3] = 1.0f;
    texts[0].utf8 = line;
    texts[1] = texts[0];
    texts[1].y = static_cast<float>(height) - 32.0f;
    texts[1].pixel_size = 8.0f;
    texts[1].color[3] = 0.8f;
    texts[1].utf8 = "Strata - space: pause   left/right: scrub   up/down: speed";
    strata_submit_text(d.engine, texts, 2);

    if (!ok(strata_end_frame(d.engine), "strata_end_frame")) {
        d.running = false;
        return;
    }
    strata_present(d.engine);
}

#if defined(__EMSCRIPTEN__)
void browserFrame(void* user) {
    Demo& d = *static_cast<Demo*>(user);
    frame(d);
    if (!d.running) emscripten_cancel_main_loop();
}
#endif

}  // namespace

int main(int argc, char** argv) {
#if defined(__EMSCRIPTEN__)
    const std::string path = argc > 1 ? argv[1] : "/village.scene";
#else
    const std::string path = argc > 1 ? argv[1] : "tests/scenes/_village_items.scene";
#endif
    static Demo d;
    try {
        d.items = strata::scene::loadScene(path).items;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "could not load %s: %s\n", path.c_str(), e.what());
        return 1;
    }

    StrataEngineDesc desc{};
    desc.struct_size = sizeof desc;
    desc.abi_version = STRATA_ABI_VERSION;
    desc.surface = STRATA_SURFACE_WINDOW;
    desc.width = kWidth;
    desc.height = kHeight;
    desc.flags = STRATA_FLAG_VSYNC;
    desc.title = "Strata - village day";
    if (!ok(strata_create(&desc, &d.engine), "strata_create")) return 1;
    std::printf("Strata %s on %s\n", strata_version_string(), strata_gpu_description(d.engine));

#if defined(__EMSCRIPTEN__)
    // The browser runs the loop (requestAnimationFrame); main returns at once
    // and the engine lives on.
    emscripten_set_main_loop_arg(browserFrame, &d, 0, false);
#else
    while (d.running) frame(d);
    strata_destroy(d.engine);
#endif
    return 0;
}
