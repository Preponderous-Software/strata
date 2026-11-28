/*
 * embed-offscreen: Strata drawn into a buffer the host owns, with input the
 * host injects. This is the shape a program takes when it shows Strata's
 * frames inside something else (another engine's texture, a video encoder, a
 * remote viewer) instead of in a window of Strata's own:
 *
 *   create   strata_create with STRATA_SURFACE_HEADLESS at the target size
 *   input    strata_push_event for every key, button or pointer move the
 *            host saw; strata_poll_event hands them back in order, and a
 *            headless surface adds no platform events of its own
 *   draw     strata_begin_frame / submit / strata_end_frame
 *   read     strata_read_pixels_rgba8 into the host's buffer (RGBA8, top row
 *            first), and strata_read_depth_f32 if it wants depth
 *   resize   strata_resize; the next frame and readback use the new size
 *
 * Plain C against the C ABI only. Run with no arguments it prints what it
 * did; with --check it is a test and exits 0 (pass), 1 (fail) or 77 (skipped:
 * no headless surface can be created here, no GPU or display).
 *
 * Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "strata/strata.h"

static int g_failures = 0;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                 \
            fprintf(stderr, "\n");                        \
            ++g_failures;                                 \
        }                                                 \
    } while (0)

/* Draws one frame: a dark clear, a unit cube straight ahead of the camera and
 * a bright square panel in the top-left corner. The panel's colour comes from
 * the host's input, so the readback shows the injected event took effect. */
static StrataStatus draw(StrataEngine* engine, int width, int height, const float panel[4]) {
    StrataCamera camera;
    const float eye[3] = {0.0f, 0.0f, 5.0f};
    const float target[3] = {0.0f, 0.0f, 0.0f};
    const float up[3] = {0.0f, 1.0f, 0.0f};
    StrataStatus status = strata_camera_look_at(eye, target, up, 45.0f, (float)width / (float)height, 0.1f, 50.0f,
                                                &camera);
    if (status != STRATA_OK) return status;
    if ((status = strata_set_camera(engine, &camera)) != STRATA_OK) return status;

    StrataFrameDesc frame;
    memset(&frame, 0, sizeof frame);
    frame.clear_flags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH;
    frame.clear_color[0] = 0.05f;
    frame.clear_color[1] = 0.05f;
    frame.clear_color[2] = 0.08f;
    frame.clear_color[3] = 1.0f;
    if ((status = strata_begin_frame(engine, &frame)) != STRATA_OK) return status;

    StrataRenderItem cube;
    memset(&cube, 0, sizeof cube);
    cube.kind = STRATA_PRIMITIVE_CUBE;
    cube.scale[0] = cube.scale[1] = cube.scale[2] = 1.0f;
    cube.color[0] = 0.8f;
    cube.color[1] = 0.3f;
    cube.color[2] = 0.2f;
    cube.color[3] = 1.0f;
    if ((status = strata_submit_primitives(engine, &cube, 1)) != STRATA_OK) return status;

    StrataRect rect;
    memset(&rect, 0, sizeof rect);
    rect.x = 0.0f;
    rect.y = 0.0f;
    rect.width = (float)width / 4.0f;
    rect.height = (float)height / 4.0f;
    memcpy(rect.color, panel, sizeof rect.color);
    if ((status = strata_submit_rects(engine, &rect, 1)) != STRATA_OK) return status;

    if ((status = strata_end_frame(engine)) != STRATA_OK) return status;
    return strata_present(engine); /* a no-op on a headless surface */
}

static const uint8_t* pixel(const uint8_t* rgba, int width, int x, int y) {
    return rgba + ((size_t)y * (size_t)width + (size_t)x) * 4u;
}

static int near_byte(uint8_t value, float expected) {
    const int want = (int)lroundf(expected * 255.0f);
    return abs((int)value - want) <= 2;
}

/* Runs one size: injected input in, a frame drawn, pixels and depth out. */
static void run_at(StrataEngine* engine, int width, int height, int verbose) {
    int w = 0, h = 0;
    CHECK(strata_get_drawable_size(engine, &w, &h) == STRATA_OK, "drawable size: %s", strata_last_error());
    CHECK(w == width && h == height, "drawable is %dx%d, wanted exactly %dx%d", w, h, width, height);

    /* The host's input: a key press. The engine reports injected events and
     * nothing else (a headless surface has no platform events). */
    StrataEvent key;
    memset(&key, 0, sizeof key);
    key.type = STRATA_EVENT_KEY_DOWN;
    key.scancode = 44; /* space */
    CHECK(strata_push_event(engine, &key) == STRATA_OK, "push_event: %s", strata_last_error());

    float panel[4] = {0.2f, 0.2f, 0.2f, 1.0f};
    int seen = 0, others = 0;
    StrataEvent event;
    while (strata_poll_event(engine, &event)) {
        if (event.type == STRATA_EVENT_KEY_DOWN && event.scancode == 44) {
            ++seen;
            panel[0] = 0.0f; /* the key turns the panel green */
            panel[1] = 1.0f;
            panel[2] = 0.0f;
        } else {
            ++others;
            if (verbose) printf("  unexpected event type %d\n", event.type);
        }
    }
    CHECK(seen == 1, "the injected key came back %d times", seen);
    CHECK(others == 0, "a headless surface reported %d platform events", others);

    CHECK(draw(engine, width, height, panel) == STRATA_OK, "draw: %s", strata_last_error());

    const size_t bytes = (size_t)width * (size_t)height * 4u;
    uint8_t* rgba = (uint8_t*)malloc(bytes);
    float* depth = (float*)malloc(bytes);
    if (!rgba || !depth) {
        CHECK(0, "out of memory");
        free(rgba);
        free(depth);
        return;
    }
    int rw = 0, rh = 0;
    CHECK(strata_read_pixels_rgba8(engine, rgba, (int32_t)bytes, &rw, &rh) == STRATA_OK, "read_pixels: %s",
          strata_last_error());
    CHECK(rw == width && rh == height, "read %dx%d, wanted %dx%d", rw, rh, width, height);
    if (rw == width && rh == height) {
        /* Top row first: the panel is in the top-left corner. */
        const uint8_t* corner = pixel(rgba, width, 2, 2);
        CHECK(near_byte(corner[0], 0.0f) && near_byte(corner[1], 1.0f) && near_byte(corner[2], 0.0f),
              "top-left panel is %u,%u,%u, wanted 0,255,0", corner[0], corner[1], corner[2]);
        const uint8_t* far = pixel(rgba, width, width - 3, height - 3);
        CHECK(near_byte(far[0], 0.05f) && near_byte(far[1], 0.05f) && near_byte(far[2], 0.08f),
              "bottom-right is %u,%u,%u, wanted the clear colour", far[0], far[1], far[2]);
        const uint8_t* centre = pixel(rgba, width, width / 2, height / 2);
        CHECK(centre[0] > centre[2], "centre is %u,%u,%u, wanted the reddish cube", centre[0], centre[1], centre[2]);
        if (verbose) {
            printf("  %dx%d: corner %u,%u,%u  centre %u,%u,%u  edge %u,%u,%u\n", width, height, corner[0], corner[1],
                   corner[2], centre[0], centre[1], centre[2], far[0], far[1], far[2]);
        }
    }

    /* Depth: the cube's front face is 4.5 units from the eye; the clear is
     * +infinity. */
    const StrataStatus ds = strata_read_depth_f32(engine, depth, (int32_t)bytes, &rw, &rh);
    CHECK(ds == STRATA_OK, "read_depth: %s", strata_last_error());
    if (ds == STRATA_OK && rw == width && rh == height) {
        const float centre = depth[(size_t)(height / 2) * (size_t)width + (size_t)(width / 2)];
        const float edge = depth[(size_t)(height - 3) * (size_t)width + (size_t)(width - 3)];
        CHECK(fabsf(centre - 4.5f) < 0.05f, "centre depth %f, wanted 4.5", centre);
        CHECK(isinf(edge), "edge depth %f, wanted +inf", edge);
        if (verbose) printf("  depth centre %.3f edge %f\n", centre, edge);
    }
    free(rgba);
    free(depth);
}

int main(int argc, char** argv) {
    const int check = argc > 1 && strcmp(argv[1], "--check") == 0;
    const int verbose = !check || getenv("STRATA_EMBED_VERBOSE") != NULL;

    StrataEngineDesc desc;
    memset(&desc, 0, sizeof desc);
    desc.struct_size = sizeof desc;
    desc.abi_version = STRATA_ABI_VERSION;
    desc.surface = STRATA_SURFACE_HEADLESS;
    desc.width = 320;
    desc.height = 180;
    StrataEngine* engine = NULL;
    const StrataStatus created = strata_create(&desc, &engine);
    if (created != STRATA_OK) {
        fprintf(stderr, "no headless engine here (%s): %s\n", strata_status_string(created), strata_last_error());
        return created == STRATA_ERROR_PLATFORM || created == STRATA_ERROR_GPU || created == STRATA_ERROR_UNSUPPORTED
                   ? 77
                   : 1;
    }
    if (verbose) printf("headless engine on %s\n", strata_gpu_description(engine));

    run_at(engine, 320, 180, verbose);
    /* The host's target changed size (say, a bigger screen in its world). */
    CHECK(strata_resize(engine, 512, 288) == STRATA_OK, "resize: %s", strata_last_error());
    run_at(engine, 512, 288, verbose);

    strata_destroy(engine);
    if (g_failures) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    if (verbose) printf("ok\n");
    return 0;
}
