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
 *   1.1  point lights (StrataPointLight, strata_set_point_lights)
 *   1.2  shadowed point lights (STRATA_POINT_LIGHT_CAST_SHADOWS)
 *   1.3  a terrain tile (StrataTerrainTile, strata_set_terrain)
 *   1.4  static triangle meshes (StrataMesh, strata_add_mesh,
 *        strata_remove_mesh, strata_clear_meshes)
 *   1.5  unlit overlays (StrataLineVertex, StrataPoint, StrataText,
 *        strata_submit_lines/_points/_text) and STRATA_LIGHTING_SPACE
 *   1.6  a sun placed by date, time and place (StrataSolarPosition,
 *        strata_solar_position, strata_set_sun_direction)
 *   1.7  textures and textured, transformable meshes (StrataTexturedMesh,
 *        strata_create_texture_rgba8, strata_destroy_texture,
 *        strata_add_textured_mesh, strata_set_mesh_transform,
 *        STRATA_MESH_UNLIT)
 *   1.8  texture sub-region updates, mesh ray picking and relative mouse
 *        mode (strata_update_texture_rgba8, StrataRayHit,
 *        strata_raycast_meshes, strata_set_relative_mouse_mode) */
#define STRATA_ABI_VERSION 1u
#define STRATA_ABI_MINOR 8u

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
    STRATA_SURFACE_WINDOW = 0,      /* SDL3 window with its own GL 4.5 core context (browser: WebGL 2 on the canvas) */
    STRATA_SURFACE_HEADLESS = 1,    /* no window: EGL surfaceless context + offscreen target (browser: hidden window) */
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
    STRATA_LIGHTING_SUN = 1,
    /* ABI 1.5. Point lights only, over a flat ambient term (legacy_ambient,
     * 0..1): no sun or moon, no shadows from them, no fog and no sky. For
     * scenes lit by lights placed in them, such as a star at the origin of a
     * planetary system. Point lights with STRATA_POINT_LIGHT_CAST_SHADOWS
     * still cast shadows. Every other StrataLighting field is ignored. */
    STRATA_LIGHTING_SPACE = 2
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

/* ABI 1.6. Where the sun is in the sky at one moment, seen from one place on
 * Earth, as strata_solar_position computes it (NOAA's solar position
 * equations, after Meeus: within about a minute of arc for 1800-2100). The
 * position is geometric: atmospheric refraction, which lifts a sun near the
 * horizon by up to about half a degree, is not applied. 32 bytes. */
typedef struct StrataSolarPosition {
    float to_sun[3];                /* unit vector towards the sun in Strata's world frame: +X east, +Y up, +Z south */
    float elevation_degrees;        /* above the horizon; negative at night */
    float azimuth_degrees;          /* clockwise from north: 90 east, 180 south, 270 west */
    float declination_degrees;      /* -23.44 at the December solstice .. +23.44 at the June solstice */
    float hour_angle_degrees;       /* 0 at local solar noon, negative before it */
    float equation_of_time_minutes; /* apparent minus mean solar time */
} StrataSolarPosition;

/* ------------------------------------------------------------- point lights */

/* ABI 1.1. The most point lights strata_set_point_lights accepts. */
#define STRATA_MAX_POINT_LIGHTS 32

/* ABI 1.2. StrataPointLight.flags. A shadow-casting light renders the scene
 * into a cube shadow map (six 512 x 512 faces) every frame. At most
 * STRATA_MAX_SHADOWED_POINT_LIGHTS lights in one call may set it. */
#define STRATA_POINT_LIGHT_CAST_SHADOWS 0x1u
#define STRATA_MAX_SHADOWED_POINT_LIGHTS 4

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
    uint32_t flags;    /* STRATA_POINT_LIGHT_* (1.2); any other bit is rejected */
} StrataPointLight;

/* ------------------------------------------------------------------ terrain */

/* ABI 1.3. One static heightmapped terrain tile. Samples form a samples_x by
 * samples_z grid (x fastest) spaced cell_size apart from `origin` along +X and
 * +Z; each height is added to origin[1]. The engine copies the data into a
 * mesh (normals from central differences), so the arrays need not outlive the
 * call. The tile is drawn in the opaque pass under both lighting modes, and in
 * STRATA_LIGHTING_SUN it casts and receives sun and point-light shadows.
 * 56 bytes on 64-bit targets. */
typedef struct StrataTerrainTile {
    float origin[3];      /* world position of sample (0, 0) */
    float cell_size;      /* > 0: distance between neighbouring samples */
    int32_t samples_x;    /* 2..1025 */
    int32_t samples_z;    /* 2..1025 */
    const float* heights; /* samples_x * samples_z heights, row by row along +Z */
    const float* colors;  /* NULL, or samples_x * samples_z linear RGB triples */
    float color[4];       /* linear RGBA used when colors is NULL */
} StrataTerrainTile;

/* ------------------------------------------------------------ static meshes */

/* ABI 1.4. The most meshes a scene holds at once. */
#define STRATA_MAX_MESHES 256
/* ABI 1.4. The most vertices one mesh may have. */
#define STRATA_MAX_MESH_VERTICES 16777216

/* ABI 1.4. Arbitrary static geometry already in world space: an indexed
 * triangle list with a normal and a colour per vertex. The engine copies the
 * data into GPU buffers, so the arrays need not outlive the call. Meshes are
 * drawn in the opaque pass exactly like the terrain tile (same vertex format,
 * same shaders): under both lighting modes, and in STRATA_LIGHTING_SUN they
 * cast and receive sun and point-light shadows. Back faces are culled in the
 * opaque pass, so triangles must wind counter-clockwise seen from outside (the
 * side the normal points to); computed normals follow the same winding. Shadow
 * passes draw both faces, so an open surface still casts. Colours are linear RGB,
 * the same space as render-item and terrain colours. Flat shading is had by
 * not sharing vertices between faces. 56 bytes on 64-bit targets. */
typedef struct StrataMesh {
    const float* positions;  /* vertex_count xyz, world space */
    const float* normals;    /* NULL (the engine computes area-weighted smooth normals) or vertex_count xyz */
    const float* colors;     /* NULL (use color) or vertex_count linear RGB triples */
    const uint32_t* indices; /* index_count indices; triangles wind counter-clockwise seen from outside */
    int32_t vertex_count;    /* 3..STRATA_MAX_MESH_VERTICES */
    int32_t index_count;     /* > 0, a multiple of 3, every index < vertex_count */
    float color[4];          /* linear RGBA used when colors is NULL */
} StrataMesh;

/* ---------------------------------------------------------------- textures */

/* ABI 1.7. Textures are RGBA8 images a host uploads once and textured meshes
 * sample. Texels are used as stored, in the same space as every other Strata
 * colour (no sRGB decoding), and multiply the mesh's vertex colour; alpha is
 * ignored (meshes are opaque). Rows run top to bottom, so v = 0 is the
 * image's first row; u runs left to right. */
#define STRATA_MAX_TEXTURES 256
#define STRATA_MAX_TEXTURE_SIZE 8192 /* largest width or height */

/* strata_create_texture_rgba8 flags. Wrapping is the default on both axes. */
#define STRATA_TEXTURE_MIPMAPS 0x1u /* build a mip chain and filter trilinearly: for anything seen small or at a slant */
#define STRATA_TEXTURE_CLAMP_U 0x2u /* clamp u to the edge instead of wrapping */
#define STRATA_TEXTURE_CLAMP_V 0x4u /* clamp v to the edge instead of wrapping (a planet map's poles) */

/* StrataTexturedMesh.flags. */
#define STRATA_MESH_UNLIT 0x1u /* drawn as its colour times its texture, with no lighting or fog, and casting no
                                  shadows: for things that glow, such as a star's surface */

/* ABI 1.7. A static mesh (as strata_add_mesh) with texture coordinates and a
 * texture. A sphere for a planet map is a latitude-longitude grid with u =
 * longitude / 360 and v = (90 - latitude) / 180, with the seam's column of
 * vertices duplicated at u = 0 and u = 1. 72 bytes on 64-bit targets. */
typedef struct StrataTexturedMesh {
    StrataMesh mesh;      /* positions, normals, colours and indices; colours multiply the texture */
    const float* uvs;     /* NULL (every vertex at 0, 0) or vertex_count (u, v) pairs */
    uint32_t texture;     /* 0 for none, or an id from strata_create_texture_rgba8 */
    uint32_t flags;       /* STRATA_MESH_*; any other bit is rejected */
} StrataTexturedMesh;

/* ------------------------------------------------------------------ picking */

/* ABI 1.8. What strata_raycast_meshes found: the nearest static mesh triangle
 * along a ray. 36 bytes. */
typedef struct StrataRayHit {
    uint32_t mesh_id;   /* the mesh hit, or 0 when nothing was hit (every other field is then 0, triangle -1) */
    int32_t triangle;   /* index of the hit triangle in the mesh's index list, divided by 3; -1 for no hit */
    float distance;     /* from the origin to the hit, along the normalised direction (world units) */
    float point[3];     /* the hit, world space */
    float normal[3];    /* unit world-space geometric normal of the hit triangle, on the side the ray came from */
} StrataRayHit;

/* ----------------------------------------------------------------- overlays */

/* ABI 1.5. Overlays are drawn after the lit scene and the sky, unlit, with
 * alpha blending, and never cast or receive shadows. Like primitives they are
 * submitted between begin_frame and end_frame, read during the call and
 * never retained.
 *
 * Lines and points are in world space, tested against the scene's depth
 * (an opaque object in front hides them) but never write depth, and they are
 * not clipped by the far plane. Text is in pixels and drawn on top of
 * everything. */

/* One end of a line segment. 28 bytes. */
typedef struct StrataLineVertex {
    float position[3]; /* world space */
    float color[4];    /* linear RGBA; alpha blends */
} StrataLineVertex;

/* StrataPoint.flags. */
#define STRATA_POINT_SOFT 0x1u /* a soft glow that fades to nothing at the edge, instead of a solid disc */

/* A round, camera-facing point. Its diameter on screen is the larger of
 * size_pixels and world_diameter as projected at the point's distance, so a
 * point can be a fixed-size marker, a disc the size of a real object, or a
 * real-size disc that never shrinks below a few pixels. 40 bytes. */
typedef struct StrataPoint {
    float position[3];    /* world space, the centre */
    float size_pixels;    /* >= 0: smallest diameter on screen, in pixels */
    float world_diameter; /* >= 0: diameter in world units; 0 for a pure screen-size point */
    float color[4];       /* linear RGBA; alpha blends */
    uint32_t flags;       /* STRATA_POINT_*; any other bit is rejected */
} StrataPoint;

/* StrataText.flags. */
#define STRATA_TEXT_SHADOW 0x1u /* draw a black copy one font pixel down and right first, for legibility */

/* A run of text on screen in Strata's built-in 8 x 8 bitmap font (printable
 * ASCII; other bytes show as '?'; '\n' starts a new line). Every glyph is a
 * square cell pixel_size wide and tall and the font is monospaced, so a line
 * of n characters is n * pixel_size pixels wide. Multiples of 8 stay crisp.
 * 40 bytes on 64-bit targets. */
typedef struct StrataText {
    float x, y;          /* top-left of the first glyph, in pixels from the top-left of the drawable */
    float pixel_size;    /* 1..512: glyph cell size in pixels */
    uint32_t flags;      /* STRATA_TEXT_*; any other bit is rejected */
    float color[4];      /* linear RGBA */
    const char* utf8;    /* NUL-terminated; copied during the call */
} StrataText;

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
    STRATA_STRUCT_POINT_LIGHT = 7, /* ABI 1.1 */
    STRATA_STRUCT_TERRAIN_TILE = 8, /* ABI 1.3 */
    STRATA_STRUCT_MESH = 9,         /* ABI 1.4 */
    STRATA_STRUCT_LINE_VERTEX = 10, /* ABI 1.5 */
    STRATA_STRUCT_POINT = 11,       /* ABI 1.5 */
    STRATA_STRUCT_TEXT = 12,        /* ABI 1.5 */
    STRATA_STRUCT_SOLAR_POSITION = 13, /* ABI 1.6 */
    STRATA_STRUCT_TEXTURED_MESH = 14,  /* ABI 1.7 */
    STRATA_STRUCT_RAY_HIT = 15         /* ABI 1.8 */
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

/* ABI 1.6. The sun's position at unix_seconds_utc (seconds since 1970-01-01
 * 00:00 UTC, fractions allowed) seen from latitude_degrees (-90..90, north
 * positive) and longitude_degrees (-180..180, east positive). The seasons come
 * from the date: at 40 degrees north the noon sun stands about 73 degrees high
 * at the June solstice and 27 degrees at the December one. Needs no engine.
 * Strata's world has -Z north; a host whose map is turned rotates to_sun about
 * +Y itself before passing it to strata_set_sun_direction. */
STRATA_API StrataStatus strata_solar_position(double unix_seconds_utc, double latitude_degrees,
                                              double longitude_degrees, StrataSolarPosition* out);
/* ABI 1.6. Places the STRATA_LIGHTING_SUN sun: to_sun is a vector towards it
 * in world space (normalised by the engine), used instead of the lighting's
 * time_of_day and latitude_degrees for the sun, moon, sky, ambient and
 * shadows. It stays until the next call, across strata_set_lighting calls.
 * NULL goes back to time_of_day and latitude_degrees. */
STRATA_API StrataStatus strata_set_sun_direction(StrataEngine* engine, const float to_sun[3]);

/* ABI 1.1. Replaces the scene's point lights with a copy of `lights`; they
 * stay until the next call. count 0 (lights may then be NULL) removes them
 * all. At most STRATA_MAX_POINT_LIGHTS. Every light is checked first, and on
 * an invalid one nothing changes. A 1.1 library rejects
 * STRATA_POINT_LIGHT_CAST_SHADOWS as an unknown flag; check strata_abi_minor()
 * >= 2 before setting it. May be called inside or outside a frame;
 * the lights in place at end_frame are the ones drawn. */
STRATA_API StrataStatus strata_set_point_lights(StrataEngine* engine, const StrataPointLight* lights, int32_t count);

/* ABI 1.3. Replaces the scene's terrain with a mesh built from `tile`, or
 * removes it when tile is NULL. The tile is validated first; on an invalid one
 * the old terrain stays. May be called inside or outside a frame. */
STRATA_API StrataStatus strata_set_terrain(StrataEngine* engine, const StrataTerrainTile* tile);

/* ABI 1.4. Adds a static mesh built from a copy of `mesh` and writes its id
 * (never 0) to *out_id. The mesh is validated first; on an invalid one, or
 * when STRATA_MAX_MESHES are already present, nothing changes. Ids are not
 * reused within an engine. May be called inside or outside a frame. */
STRATA_API StrataStatus strata_add_mesh(StrataEngine* engine, const StrataMesh* mesh, uint32_t* out_id);
/* ABI 1.4. Removes one mesh; an unknown id is STRATA_ERROR_INVALID_ARGUMENT. */
STRATA_API StrataStatus strata_remove_mesh(StrataEngine* engine, uint32_t id);
/* ABI 1.4. Removes every mesh. */
STRATA_API StrataStatus strata_clear_meshes(StrataEngine* engine);

/* ABI 1.7. Uploads a width x height RGBA8 image (4 bytes per texel, rows top
 * to bottom, tightly packed; copied during the call) and writes its id (never
 * 0) to *out_id. Width and height are 1..STRATA_MAX_TEXTURE_SIZE; flags are
 * STRATA_TEXTURE_*. At most STRATA_MAX_TEXTURES at once. Ids are not reused
 * within an engine. */
STRATA_API StrataStatus strata_create_texture_rgba8(StrataEngine* engine, int32_t width, int32_t height,
                                                    const uint8_t* rgba, uint32_t flags, uint32_t* out_id);
/* ABI 1.8. Replaces the width x height rectangle at (x, y) of a texture made
 * by strata_create_texture_rgba8 (x to the right, y down from the first row)
 * with `rgba`: 4 bytes per texel, rows top to bottom, tightly packed, copied
 * during the call, the same conventions as creation. The rectangle must lie
 * inside the texture and be at least 1 x 1; an unknown id, a rectangle out of
 * bounds or a NULL rgba is STRATA_ERROR_INVALID_ARGUMENT and changes nothing.
 * A texture made with STRATA_TEXTURE_MIPMAPS has its whole mip chain rebuilt,
 * which costs about as much as the base level again: on a large mipmapped
 * texture, batch small updates into one call per frame. Meshes see the new
 * texels from the next frame drawn. */
STRATA_API StrataStatus strata_update_texture_rgba8(StrataEngine* engine, uint32_t id, int32_t x, int32_t y,
                                                    int32_t width, int32_t height, const uint8_t* rgba);
/* ABI 1.7. Frees a texture. Meshes that use it draw untextured from then on;
 * an unknown id is STRATA_ERROR_INVALID_ARGUMENT. */
STRATA_API StrataStatus strata_destroy_texture(StrataEngine* engine, uint32_t id);
/* ABI 1.7. Adds a textured static mesh, exactly as strata_add_mesh does (the
 * same limit, ids and validation), plus its uvs, texture and flags. The
 * texture must exist when the mesh is added. */
STRATA_API StrataStatus strata_add_textured_mesh(StrataEngine* engine, const StrataTexturedMesh* mesh,
                                                 uint32_t* out_id);
/* ABI 1.7. Sets a mesh's model matrix (16 floats, column-major), applied to
 * its positions and, through the inverse transpose, its normals when it is
 * drawn and when it casts shadows. Any mesh starts with the identity, so its
 * positions are world space until this is called; NULL restores the identity.
 * The matrix must be finite and invertible. Spinning a planet about a tilted
 * axis is one call a frame: model = translate(centre) * rotate(tilt) *
 * rotate(spin about +Y) * scale(radius), with the mesh built around the
 * origin. Changing the matrix re-uploads nothing but the matrix. */
STRATA_API StrataStatus strata_set_mesh_transform(StrataEngine* engine, uint32_t id, const float model[16]);

/* ABI 1.8. Casts a ray against every static mesh (strata_add_mesh and
 * strata_add_textured_mesh, unlit ones included) with its current model
 * matrix, and writes the nearest hit within max_distance to *out_hit, or
 * mesh_id 0 when there is none (still STRATA_OK). origin is world space;
 * direction must be finite and non-zero and is normalised by the engine;
 * max_distance must be > 0 (it may be +infinity). Both faces of a triangle
 * count, so a ray starting inside a closed mesh hits its inside. Primitives,
 * the terrain tile and overlays are not tested. Equally near hits go to the
 * mesh added first. Needs no frame and draws nothing.
 *
 * To answer this without reading the GPU back, the engine keeps a CPU copy of
 * every mesh's positions and indices from ABI 1.8 on: 12 bytes per vertex plus
 * 4 per index (the GPU copy is 44 per vertex plus 4 per index), freed with
 * the mesh. The
 * cost per call is one bounding-box test per mesh plus one ray-triangle test
 * per triangle of each mesh whose box the ray crosses. */
STRATA_API StrataStatus strata_raycast_meshes(StrataEngine* engine, const float origin[3], const float direction[3],
                                              float max_distance, StrataRayHit* out_hit);

/* ABI 1.8. Relative mouse mode for first-person look. Non-zero enabled hides
 * the cursor, confines it to the window and reports STRATA_EVENT_MOUSE_MOTION
 * as relative movement only: dx and dy carry it, and x and y are 0. Zero
 * shows the cursor and goes back to absolute positions. Window surface only;
 * headless and host-context engines return STRATA_ERROR_UNSUPPORTED and
 * change nothing (a host that owns the window captures the mouse itself). In
 * a browser this is pointer lock, which the browser grants only from a user
 * gesture such as a click; SDL requests it on the next one. */
STRATA_API StrataStatus strata_set_relative_mouse_mode(StrataEngine* engine, int32_t enabled);

/* A frame is begin_frame, any number of submits, end_frame, then present.
 * Submitted arrays are read during the call and never retained. */
STRATA_API StrataStatus strata_begin_frame(StrataEngine* engine, const StrataFrameDesc* desc);
STRATA_API StrataStatus strata_submit_primitives(StrataEngine* engine, const StrataRenderItem* items, int32_t count);
/* ABI 1.5. Overlays (see above). Each call adds to this frame's overlays.
 * Every element is checked first, and on an invalid one nothing from the
 * call is added. Lines: count is a number of vertices, even, and each pair is
 * one segment. */
STRATA_API StrataStatus strata_submit_lines(StrataEngine* engine, const StrataLineVertex* vertices, int32_t count);
STRATA_API StrataStatus strata_submit_points(StrataEngine* engine, const StrataPoint* points, int32_t count);
STRATA_API StrataStatus strata_submit_text(StrataEngine* engine, const StrataText* texts, int32_t count);
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
