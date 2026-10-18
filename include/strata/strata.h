/*
 * strata.h -- the versioned C ABI of the Strata engine.
 *
 * Strata's real interface is the C++ API in strata.hpp. This header is a thin,
 * stable shim over it, so that any language with a C FFI (C# via P/Invoke in
 * particular) can drive the engine.
 *
 * Rules every function here follows:
 *   - Nothing throws across this boundary. Functions that can fail return a
 *     StrataStatus; strata_last_error() describes the most recent failure on
 *     the calling thread.
 *   - Every struct that crosses the boundary is plain data with fixed-size
 *     fields. Its size and field offsets can be queried at run time
 *     (strata_struct_size, strata_struct_field_offset) so that a binding can
 *     assert it agrees with the native layout before it passes a pointer.
 *   - The engine is main-thread only, and it never calls back into the host
 *     except through the optional log sink.
 *   - Matrices are 16 floats, column-major, as OpenGL reads them. That is also
 *     the memory layout of a System.Numerics.Matrix4x4 used the way OpenGL
 *     hosts in .NET use it (rows of the .NET matrix become GLSL columns).
 *
 * Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
 */
#ifndef STRATA_H
#define STRATA_H

#include <stddef.h>
#include <stdint.h>

/* Versioning.
 *
 * STRATA_ABI_VERSION (the major version) is bumped whenever a struct layout or
 * a function signature in this header changes incompatibly. Hosts pass the
 * value they were built against in StrataEngineDesc.abi_version and
 * strata_create refuses a mismatch.
 *
 * STRATA_ABI_MINOR is bumped for every compatible addition: a new function, a
 * new struct (with its own STRATA_STRUCT_* id), a new enum or flag value.
 * Existing structs and signatures never change within a major version, so a
 * host built against any 1.x keeps working with every later 1.y. A host that
 * uses an addition checks strata_abi_minor() at run time and falls back (or
 * reports the library as too old) when it is lower than the minor the
 * addition arrived in. Each addition below names that minor.
 *
 *   1.0  v0.1.0: primitives, legacy and sun lighting, cascaded shadows
 *   1.1  point lights (StrataPointLight, strata_set_point_lights) */
#define STRATA_ABI_VERSION 1u
#define STRATA_ABI_MINOR 1u

#if defined(_WIN32)
#  if defined(STRATA_BUILDING_LIBRARY)
#    define STRATA_API __declspec(dllexport)
#  elif defined(STRATA_STATIC)
#    define STRATA_API
#  else
#    define STRATA_API __declspec(dllimport)
#  endif
#else
#  define STRATA_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ status */

typedef int32_t StrataStatus;
enum {
    STRATA_OK = 0,
    STRATA_ERROR_INVALID_ARGUMENT = 1,
    STRATA_ERROR_ABI_MISMATCH = 2,
    STRATA_ERROR_PLATFORM = 3, /* window, context or display could not be created */
    STRATA_ERROR_GPU = 4,      /* GL version too old, shader or framebuffer failure */
    STRATA_ERROR_IO = 5,       /* a file could not be written */
    STRATA_ERROR_INVALID_STATE = 6, /* e.g. submit outside begin_frame/end_frame */
    STRATA_ERROR_UNSUPPORTED = 7,
    STRATA_ERROR_INTERNAL = 8
};

/* --------------------------------------------------------------- render data */

/* Primitive kinds. The values match Beyond Nations' PrimitiveKind enum, so a
 * BN RenderItem crosses the boundary without translation. */
enum {
    STRATA_PRIMITIVE_CUBE = 0,     /* 1 unit on every side, centred */
    STRATA_PRIMITIVE_CAPSULE = 1,  /* 1 unit across, 2 units tall, centred */
    STRATA_PRIMITIVE_CYLINDER = 2, /* 1 unit across, 2 units tall, centred */
    STRATA_PRIMITIVE_SPHERE = 3,   /* 1 unit across, centred */
    STRATA_PRIMITIVE_KIND_COUNT = 4
};

/* One primitive instance, already in world space: scaled, then translated.
 * 44 bytes, field for field the layout of Beyond Nations' RenderItem
 * (int enum, Vector3 position, Vector3 scale, Rgba of four floats). Items with
 * a kind outside [0, STRATA_PRIMITIVE_KIND_COUNT) are skipped and counted in
 * StrataFrameStats.items_rejected. */
typedef struct StrataRenderItem {
    int32_t kind;
    float position[3];
    float scale[3];
    float color[4]; /* linear 0..1 RGBA; alpha is carried but primitives are opaque */
} StrataRenderItem;

/* ------------------------------------------------------------------- engine */

typedef struct StrataEngine StrataEngine;

enum {
    STRATA_SURFACE_WINDOW = 0,      /* SDL3 window with its own GL 4.5 core context */
    STRATA_SURFACE_HEADLESS = 1,    /* no window: EGL surfaceless context + offscreen target */
    STRATA_SURFACE_HOST_CONTEXT = 2 /* draw into the GL context current on the calling thread */
};

enum {
    STRATA_FLAG_VSYNC = 1u << 0,         /* window surface only */
    STRATA_FLAG_DEBUG_CONTEXT = 1u << 1, /* request a GL debug context and log its messages */
    STRATA_FLAG_HIDDEN_WINDOW = 1u << 2, /* window surface only: create it hidden */
    STRATA_FLAG_GPU_TIMING = 1u << 3     /* glFinish at end of frame and report the wait */
};

/* Resolves a GL entry point by name for STRATA_SURFACE_HOST_CONTEXT. Called
 * only during strata_create, never per frame. */
typedef void* (*StrataGetProcAddressFn)(const char* name, void* user);

typedef struct StrataEngineDesc {
    uint32_t struct_size; /* sizeof(StrataEngineDesc) */
    uint32_t abi_version; /* STRATA_ABI_VERSION */
    int32_t surface;      /* STRATA_SURFACE_* */
    int32_t width;        /* drawable size in pixels; window: initial size */
    int32_t height;
    uint32_t flags;       /* STRATA_FLAG_* */
    const char* title;    /* window only; UTF-8; may be NULL */
    StrataGetProcAddressFn get_proc_address; /* host context only */
    void* get_proc_address_user;
} StrataEngineDesc;

/* ------------------------------------------------------------------- camera */

typedef struct StrataCamera {
    float view[16];       /* world -> view, column-major */
    float projection[16]; /* view -> clip, column-major; either depth convention */
    float eye[3];         /* world-space eye position */
    float near_plane;     /* the distances the projection was built with; */
    float far_plane;      /* shadow cascades are fitted between them */
} StrataCamera;

/* ----------------------------------------------------------------- lighting */

enum {
    /* One fixed directional light plus a flat ambient term, no shadows, no
     * fog: exactly the model of Beyond Nations' PrimitiveRenderer
     * (light = ambient + (1 - ambient) * max(dot(n, -dir), 0)). Exists so the
     * native renderer can be compared image-for-image with the old one. */
    STRATA_LIGHTING_LEGACY = 0,
    /* A sun (and a moon) driven by time of day, hemisphere ambient, distance
     * fog, an analytic sky and cascaded shadow maps. */
    STRATA_LIGHTING_SUN = 1
};

typedef struct StrataLighting {
    int32_t mode;               /* STRATA_LIGHTING_* */
    float legacy_direction[3];  /* LEGACY: direction the light travels */
    float legacy_ambient;       /* LEGACY: 0..1 */
    float time_of_day;          /* SUN: [0,1); 0.25 sunrise, 0.5 noon, 0.75 sunset. The game owns time. */
    float latitude_degrees;     /* SUN: tilts the sun's path; 0 = equator */
    int32_t cascade_count;      /* SUN: 0 disables shadows, 1..4 cascades */
    int32_t shadow_map_size;    /* SUN: texels per cascade side, 256..4096 */
    float shadow_distance;      /* SUN: furthest distance from the eye that receives shadows */
    float split_lambda;         /* SUN: 0 uniform .. 1 logarithmic cascade splits */
    int32_t fog;                /* SUN: non-zero enables distance fog */
} StrataLighting;

/* ------------------------------------------------------------- point lights */

/* ABI 1.1. The most point lights strata_set_point_lights accepts. */
#define STRATA_MAX_POINT_LIGHTS 32

/* ABI 1.1. A light that shines equally in every direction from a point. Point
 * lights shade STRATA_LIGHTING_SUN scenes, on top of the sun or moon and
 * before fog; STRATA_LIGHTING_LEGACY ignores them. With x = distance / range,
 * the light falls off as
 *   intensity * window^2 / (1 + 16 x^2),  window = clamp(1 - x^4, 0, 1)
 * so it is `intensity` at the light, about a fifth of that at half the range,
 * and exactly zero at `range`: nothing beyond it is touched.
 * 36 bytes. */
typedef struct StrataPointLight {
    float position[3]; /* world space */
    float range;       /* > 0: the distance at which the light reaches zero */
    float color[3];    /* linear RGB, 0..1 each */
    float intensity;   /* >= 0; at the light itself, 1 is about as bright as the noon sun */
    uint32_t flags;    /* must be 0 in ABI 1.1 */
} StrataPointLight;

/* ------------------------------------------------------------------- frames */

enum {
    STRATA_CLEAR_COLOR = 1u << 0,
    STRATA_CLEAR_DEPTH = 1u << 1,
    STRATA_CLEAR_SKY = 1u << 2 /* SUN lighting: draw the analytic sky behind the scene */
};

typedef struct StrataFrameDesc {
    uint32_t clear_flags;  /* STRATA_CLEAR_*; 0 draws over whatever is in the target */
    float clear_color[4];
} StrataFrameDesc;

typedef struct StrataFrameStats {
    int32_t draw_calls;
    int32_t instances;
    int32_t instances_per_kind[4]; /* indexed by STRATA_PRIMITIVE_* */
    int32_t items_rejected;        /* items whose kind was out of range */
    int32_t shadow_cascades;       /* cascades rendered this frame */
    float cpu_ms_shadow;
    float cpu_ms_opaque;
    float cpu_ms_sky;
    float cpu_ms_frame;            /* end_frame, all passes */
    float gpu_finish_ms;           /* only with STRATA_FLAG_GPU_TIMING */
} StrataFrameStats;

/* ------------------------------------------------------------------- events */

enum {
    STRATA_EVENT_NONE = 0,
    STRATA_EVENT_QUIT = 1,
    STRATA_EVENT_KEY_DOWN = 2,
    STRATA_EVENT_KEY_UP = 3,
    STRATA_EVENT_MOUSE_MOTION = 4,
    STRATA_EVENT_MOUSE_BUTTON_DOWN = 5,
    STRATA_EVENT_MOUSE_BUTTON_UP = 6,
    STRATA_EVENT_MOUSE_WHEEL = 7,
    STRATA_EVENT_RESIZED = 8,
    STRATA_EVENT_TEXT_INPUT = 9
};

typedef struct StrataEvent {
    int32_t type;      /* STRATA_EVENT_* */
    int32_t scancode;  /* keys: USB HID usage id (identical to SDL3 scancodes) */
    int32_t modifiers; /* keys: SDL3 SDL_Keymod bits */
    int32_t repeat;    /* keys: non-zero for auto-repeat */
    int32_t button;    /* mouse buttons: 1 left, 2 middle, 3 right, 4/5 extra */
    int32_t width;     /* RESIZED: new drawable size in pixels */
    int32_t height;
    float x, y;        /* mouse position; wheel: scroll amount */
    float dx, dy;      /* mouse motion: relative movement */
    char text[32];     /* TEXT_INPUT: NUL-terminated UTF-8 */
} StrataEvent;

/* ------------------------------------------------------------ introspection */

enum {
    STRATA_STRUCT_RENDER_ITEM = 0,
    STRATA_STRUCT_ENGINE_DESC = 1,
    STRATA_STRUCT_CAMERA = 2,
    STRATA_STRUCT_LIGHTING = 3,
    STRATA_STRUCT_FRAME_DESC = 4,
    STRATA_STRUCT_FRAME_STATS = 5,
    STRATA_STRUCT_EVENT = 6,
    STRATA_STRUCT_POINT_LIGHT = 7 /* ABI 1.1 */
};

/* Field ids for strata_struct_field_offset(STRATA_STRUCT_RENDER_ITEM, ...). */
enum {
    STRATA_RENDER_ITEM_KIND = 0,
    STRATA_RENDER_ITEM_POSITION = 1,
    STRATA_RENDER_ITEM_SCALE = 2,
    STRATA_RENDER_ITEM_COLOR = 3
};

/* ---------------------------------------------------------------- functions */

STRATA_API uint32_t strata_abi_version(void);
/* ABI 1.1. The library's STRATA_ABI_MINOR. A library older than 1.1 does not
 * export this function, so a host that needs 1.1 or later can treat a missing
 * symbol as minor 0. */
STRATA_API uint32_t strata_abi_minor(void);
STRATA_API const char* strata_version_string(void);

/* Size in bytes of a boundary struct, or -1 for an unknown id. */
STRATA_API int32_t strata_struct_size(int32_t struct_id);
/* Byte offset of a field. Field ids are the struct's members in declaration
 * order (0-based); STRATA_RENDER_ITEM_* names them for the render item.
 * Returns -1 for an unknown struct or field. */
STRATA_API int32_t strata_struct_field_offset(int32_t struct_id, int32_t field_id);

/* Text for the most recent failure on this thread. Never NULL. */
STRATA_API const char* strata_last_error(void);
STRATA_API const char* strata_status_string(StrataStatus status);

/* Optional log sink, process-wide. level: 0 debug, 1 info, 2 warning, 3 error.
 * NULL restores the default (stderr). Called on the thread that logs, which is
 * always the engine's thread. */
typedef void (*StrataLogFn)(int32_t level, const char* message, void* user);
STRATA_API void strata_set_log_callback(StrataLogFn fn, void* user);

STRATA_API StrataStatus strata_create(const StrataEngineDesc* desc, StrataEngine** out_engine);
STRATA_API void strata_destroy(StrataEngine* engine);

/* "<GL renderer> | <GL version>" of the engine's context. */
STRATA_API const char* strata_gpu_description(const StrataEngine* engine);

/* Pops one pending event. Returns 1 and fills *out, or 0 when the queue is
 * empty. Headless and host-context engines never produce events. */
STRATA_API int32_t strata_poll_event(StrataEngine* engine, StrataEvent* out);

STRATA_API StrataStatus strata_get_drawable_size(const StrataEngine* engine, int32_t* width, int32_t* height);
/* Headless and host-context surfaces: sets the drawable size. Window: resizes the window. */
STRATA_API StrataStatus strata_resize(StrataEngine* engine, int32_t width, int32_t height);

STRATA_API StrataStatus strata_set_camera(StrataEngine* engine, const StrataCamera* camera);
/* Convenience: fills *out with a right-handed look-at view and an OpenGL
 * (depth -1..1) perspective projection. Needs no engine. */
STRATA_API StrataStatus strata_camera_look_at(const float eye[3], const float target[3], const float up[3],
                                              float fov_y_degrees, float aspect, float near_plane,
                                              float far_plane, StrataCamera* out);

/* Fills *out with the defaults for a lighting mode. LEGACY defaults are Beyond
 * Nations' PrimitiveRenderer constants. */
STRATA_API StrataStatus strata_lighting_defaults(int32_t mode, StrataLighting* out);
STRATA_API StrataStatus strata_set_lighting(StrataEngine* engine, const StrataLighting* lighting);

/* ABI 1.1. Replaces the scene's point lights with a copy of `lights`; they
 * stay until the next call. count 0 (lights may then be NULL) removes them
 * all. At most STRATA_MAX_POINT_LIGHTS. Every light is checked first, and on
 * an invalid one nothing changes. May be called inside or outside a frame;
 * the lights in place at end_frame are the ones drawn. */
STRATA_API StrataStatus strata_set_point_lights(StrataEngine* engine, const StrataPointLight* lights, int32_t count);

/* A frame is begin_frame, any number of submits, end_frame, then present.
 * Submitted arrays are read during the call and never retained. */
STRATA_API StrataStatus strata_begin_frame(StrataEngine* engine, const StrataFrameDesc* desc);
STRATA_API StrataStatus strata_submit_primitives(StrataEngine* engine, const StrataRenderItem* items, int32_t count);
STRATA_API StrataStatus strata_end_frame(StrataEngine* engine);
/* Window: swaps buffers. Headless and host context: no-op. */
STRATA_API StrataStatus strata_present(StrataEngine* engine);

STRATA_API StrataStatus strata_get_frame_stats(const StrataEngine* engine, StrataFrameStats* out);

/* Reads the last rendered frame as tightly packed RGBA8, top row first.
 * capacity_bytes must be at least width * height * 4. */
STRATA_API StrataStatus strata_read_pixels_rgba8(StrataEngine* engine, uint8_t* dst, int32_t capacity_bytes,
                                                 int32_t* out_width, int32_t* out_height);
/* Writes the last rendered frame to a PNG file (RGB, top row first). */
STRATA_API StrataStatus strata_save_screenshot_png(StrataEngine* engine, const char* utf8_path);

#ifdef __cplusplus
}
#endif

#endif /* STRATA_H */
