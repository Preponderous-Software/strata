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
 * Colour. Every colour below is documented as "linear": the lighting maths
 * treats it as linear, and nothing converts it on the way in (texels are not
 * sRGB-decoded either, unless made with STRATA_TEXTURE_SRGB, ABI 1.20).
 * Nothing converts it on the way out: by default there is no sRGB output
 * encode (strata_set_color_output, ABI 1.20, can add one and a tonemap for
 * the lit scene; what follows describes the default), and Strata neither enables nor disables GL_FRAMEBUFFER_SRGB
 * (on STRATA_SURFACE_HOST_CONTEXT the host's own setting applies). The
 * shaders write the result into the colour target unencoded, and a display, a
 * browser canvas or a PNG viewer shows those bytes as sRGB.
 * strata_read_pixels_rgba8 returns the same bytes.
 * So an opaque colour drawn without lighting (lines, solid points, text,
 * rects, untextured STRATA_MESH_UNLIT meshes) reaches the screen exactly as
 * it was passed: an sRGB colour from a design tool, passed in unchanged,
 * looks as picked there. Partial alpha and antialiased edges blend with what
 * is beneath in the same unencoded values, STRATA_POINT_SOFT glows are added
 * to it, and a texture multiplies the colour by its texels. A host that enables GL_FRAMEBUFFER_SRGB on an sRGB-capable
 * host-context framebuffer gets the GL encode on top, and none of this
 * holds. Lit surfaces multiply that value by the light. Their mid-tones come
 * out darker and more saturated than a linear pipeline with an sRGB encode
 * would show them. Decoding an sRGB colour to linear before passing it in
 * makes every colour darker still, because nothing encodes it again.
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
 *        strata_raycast_meshes, strata_set_relative_mouse_mode)
 *   1.9  depth readback (strata_read_depth_f32)
 *   1.10 nearest-texel texture filtering (STRATA_TEXTURE_NEAREST)
 *   1.11 translucent meshes (STRATA_MESH_TRANSLUCENT), overlays drawn on top
 *        of the scene (STRATA_LINES_ON_TOP, strata_submit_lines_ex,
 *        STRATA_POINT_ON_TOP) and filled screen rectangles (StrataRect,
 *        strata_submit_rects)
 *   1.12 injected input, a shared frame clock and the drive channel
 *        (strata_push_event, strata_frame_seconds, strata_is_driven,
 *        STRATA_DRIVE; see docs/DRIVE.md)
 *   1.13 wide lines (StrataWideLineVertex, strata_submit_wide_lines,
 *        STRATA_LINES_ROUND_CAPS)
 *   1.14 world-to-screen projection and world-anchored text
 *        (strata_project_point, strata_submit_world_text)
 *   1.15 per-vertex RGBA mesh colours (STRATA_MESH_COLORS_RGBA)
 *   1.16 ambient and sun levels (strata_set_ambient,
 *        strata_set_sun_intensity)
 *   1.17 window fullscreen (strata_set_fullscreen, strata_is_fullscreen)
 *   1.18 a host context whose GL state is not kept (STRATA_FLAG_NO_HOST_STATE)
 *   1.19 hidden meshes and engine-made flat normals (strata_set_mesh_visible,
 *        STRATA_MESH_FLAT_NORMALS)
 *   1.20 mesh materials: alpha cutout, emission, normal maps and two-sided
 *        faces (StrataMaterial, strata_set_mesh_material), sRGB textures
 *        (STRATA_TEXTURE_SRGB), spot lights (StrataSpotLight,
 *        strata_set_spot_lights), an orthographic camera
 *        (strata_camera_ortho) and an optional tonemap and sRGB output
 *        encode (strata_set_color_output)
 *   1.21 clustered light culling for many lights (strata_set_light_culling,
 *        STRATA_LIGHT_CULLING_*, STRATA_MAX_CLUSTERED_LIGHTS, StrataLightStats,
 *        strata_get_light_stats) */
#define STRATA_ABI_VERSION 1u
#define STRATA_ABI_MINOR 21u

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
    STRATA_SURFACE_HEADLESS = 1,    /* no window: EGL surfaceless context + offscreen target. Without EGL
                                       (Windows, macOS) a hidden SDL3 window holds the context and the engine
                                       still draws into the offscreen target at the requested size, with no
                                       platform events (browser: a hidden canvas, drawn directly).
                                       On WSL2 (/dev/dxg) with no Mesa driver chosen in the environment, the engine
                                       sets GALLIUM_DRIVER=llvmpipe first; GALLIUM_DRIVER=d3d12 opts back in */
    STRATA_SURFACE_HOST_CONTEXT = 2 /* draw into the GL context current on the calling thread */
};

enum {
    STRATA_FLAG_VSYNC = 1u << 0,         /* window surface only */
    STRATA_FLAG_DEBUG_CONTEXT = 1u << 1, /* request a GL debug context and log its messages */
    STRATA_FLAG_HIDDEN_WINDOW = 1u << 2, /* window surface only: create it hidden */
    STRATA_FLAG_GPU_TIMING = 1u << 3,    /* glFinish at end of frame and report the wait */
    /* ABI 1.18, host context only: the host keeps no GL state of its own
       across Strata's calls (a context made for Strata alone, such as the
       web kit's canvas), so the engine does not save the host's state before
       each call that draws or reads and restore it after. Saving it is some
       sixty glGet and glIsEnabled queries a frame; in a browser each one is a
       synchronous round trip to the GPU process that stalls the page's main
       thread. With the flag the engine leaves its own state bound, as it does
       on its own window. Ignored on other surfaces, and by libraries older
       than 1.18 (which save and restore as before). */
    STRATA_FLAG_NO_HOST_STATE = 1u << 4
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
    float shadow_distance;      /* SUN: furthest distance from the eye that receives shadows (capped at the
                                   camera's far plane). Scale it to the scene: the default 150 suits a
                                   village; for a scene a few units across use a few times its size,
                                   or small objects lose their shadows (see ARCHITECTURE.md) */
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
 * before fog, and (1.5) STRATA_LIGHTING_SPACE scenes, on top of their flat
 * ambient; STRATA_LIGHTING_LEGACY ignores them. With x = distance / range,
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

/* ------------------------------------------------------------ light culling */

/* ABI 1.21. strata_set_light_culling modes: how lit fragments find the point
 * and spot lights that reach them. */
enum {
    /* The default, and all a library before 1.21 does: every lit fragment
     * evaluates every light set, and each list holds at most
     * STRATA_MAX_POINT_LIGHTS / STRATA_MAX_SPOT_LIGHTS. */
    STRATA_LIGHT_CULLING_NONE = 0,
    /* Clustered forward shading: each frame the engine cuts the view frustum
     * into a grid of clusters (screen tiles times depth slices) and lists, on
     * the CPU, the lights whose range reaches each one; a lit fragment then
     * evaluates only its own cluster's lights. Each list holds up to
     * STRATA_MAX_CLUSTERED_LIGHTS, so a night scene can light every lamp it
     * has, and a frame costs about what the lights near each pixel cost, not
     * what every light in the scene does. A light reaches exactly what it
     * reaches without culling; a cluster that more than
     * STRATA_MAX_LIGHTS_PER_CLUSTER lights reach keeps the ones that light it
     * most (StrataLightStats.clusters_overflowed counts those). Shadowed
     * lights work as before (STRATA_MAX_SHADOWED_POINT_LIGHTS in all). */
    STRATA_LIGHT_CULLING_CLUSTERED = 1
};

/* ABI 1.21. The most point lights, and separately spot lights, a list may
 * hold under STRATA_LIGHT_CULLING_CLUSTERED. */
#define STRATA_MAX_CLUSTERED_LIGHTS 4096
/* ABI 1.21. The most lights one cluster lists. */
#define STRATA_MAX_LIGHTS_PER_CLUSTER 256

/* ABI 1.21. What the light culling did in the most recent strata_end_frame
 * (strata_get_light_stats). Under STRATA_LIGHT_CULLING_NONE, or in a frame
 * that lit nothing (LEGACY lighting), only mode and the two counts are set. */
typedef struct StrataLightStats {
    int32_t mode;                   /* STRATA_LIGHT_CULLING_* in force */
    int32_t point_lights;           /* lights set */
    int32_t spot_lights;
    int32_t lights_in_view;         /* point and spot lights listed in at least one cluster */
    int32_t clusters;               /* clusters in the grid */
    int32_t clusters_lit;           /* clusters listing at least one light */
    int32_t light_references;       /* list entries over all clusters */
    int32_t max_lights_per_cluster; /* the longest list, before the STRATA_MAX_LIGHTS_PER_CLUSTER cut */
    int32_t clusters_overflowed;    /* clusters that more than STRATA_MAX_LIGHTS_PER_CLUSTER lights reach */
    float cpu_ms_culling;           /* CPU time spent listing lights and uploading the lists */
} StrataLightStats;

/* -------------------------------------------------------------- spot lights */

/* ABI 1.20. The most spot lights strata_set_spot_lights accepts. They are a
 * list of their own, so a scene may hold STRATA_MAX_POINT_LIGHTS point lights
 * and this many spot lights at once. Every lit fragment evaluates every light
 * of both lists (a light out of range adds nothing but still costs its loop
 * iteration), so the cost of shading grows with the number of lights set. */
#define STRATA_MAX_SPOT_LIGHTS 32

/* ABI 1.20. StrataSpotLight.flags. A shadow-casting spot light renders a cube
 * shadow map exactly as a shadowed point light does, from the same pool:
 * STRATA_MAX_SHADOWED_POINT_LIGHTS counts the shadowed point and spot lights
 * together. */
#define STRATA_SPOT_LIGHT_CAST_SHADOWS 0x1u

/* ABI 1.20. A point light (the same falloff with distance, the same lighting
 * modes) whose light is confined to a cone: full strength within
 * inner_cone_degrees of `direction`, nothing beyond outer_cone_degrees, and
 * between them a smooth fade (with t running linearly in the cosine of the
 * angle from 0 at the outer edge to 1 at the inner one, the factor is t * t,
 * as glTF's KHR_lights_punctual suggests). Street lamps, headlights, a
 * torch. 56 bytes. */
typedef struct StrataSpotLight {
    float position[3];        /* world space */
    float range;              /* > 0: the distance at which the light reaches zero */
    float direction[3];       /* the way the light shines; finite and non-zero, normalised by the engine */
    float intensity;          /* >= 0, as StrataPointLight.intensity */
    float color[3];           /* linear RGB, 0..1 each */
    float inner_cone_degrees; /* 0..outer_cone_degrees: half-angle of the full-strength core */
    float outer_cone_degrees; /* (0, 90]: half-angle at which the light reaches zero */
    uint32_t flags;           /* STRATA_SPOT_LIGHT_*; any other bit is rejected */
} StrataSpotLight;

/* ------------------------------------------------------------------ terrain */

/* ABI 1.3. One static heightmapped terrain tile. Samples form a samples_x by
 * samples_z grid (x fastest) spaced cell_size apart from `origin` along +X and
 * +Z; each height is added to origin[1]. The engine copies the data into a
 * mesh (normals from central differences), so the arrays need not outlive the
 * call. The tile is drawn in the opaque pass under every lighting mode. In
 * STRATA_LIGHTING_SUN it casts and receives sun and point-light shadows, and
 * in STRATA_LIGHTING_SPACE point-light shadows.
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
 * drawn in the opaque pass like the terrain tile (its lighting, with a uv per
 * vertex since 1.7): under every lighting mode, casting and receiving sun and
 * point-light shadows in STRATA_LIGHTING_SUN and point-light shadows in
 * STRATA_LIGHTING_SPACE. Back faces are culled in the
 * opaque pass, so triangles must wind counter-clockwise seen from outside (the
 * side the normal points to); computed normals follow the same winding. Shadow
 * passes draw both faces, so an open surface still casts. Colours are linear RGB,
 * the same space as render-item and terrain colours. Flat shading is had by
 * not sharing vertices between faces, or, since 1.19, with
 * STRATA_MESH_FLAT_NORMALS. 56 bytes on 64-bit targets. */
typedef struct StrataMesh {
    const float* positions;  /* vertex_count xyz, world space */
    const float* normals;    /* NULL (the engine computes area-weighted smooth normals) or vertex_count xyz */
    const float* colors;     /* NULL (use color) or vertex_count linear RGB triples (RGB, not RGBA:
                                RGBA here is misread without an error; see STRATA_MESH_COLORS_RGBA) */
    const uint32_t* indices; /* index_count indices; triangles wind counter-clockwise seen from outside */
    int32_t vertex_count;    /* 3..STRATA_MAX_MESH_VERTICES */
    int32_t index_count;     /* > 0, a multiple of 3, every index < vertex_count */
    float color[4];          /* linear RGBA used when colors is NULL */
} StrataMesh;

/* ---------------------------------------------------------------- textures */

/* ABI 1.7. Textures are RGBA8 images a host uploads once and textured meshes
 * sample. Texels are used as stored, in the same space as every other Strata
 * colour (no sRGB decoding), and multiply the mesh's vertex colour. Texel
 * alpha is ignored on an opaque mesh; on a STRATA_MESH_TRANSLUCENT mesh
 * (ABI 1.11) it multiplies mesh.color[3]. Rows run top to bottom, so v = 0 is
 * the image's first row; u runs left to right. */
#define STRATA_MAX_TEXTURES 256
#define STRATA_MAX_TEXTURE_SIZE 8192 /* largest width or height */

/* strata_create_texture_rgba8 flags. Wrapping is the default on both axes. */
#define STRATA_TEXTURE_MIPMAPS 0x1u /* build a mip chain and filter trilinearly: for anything seen small or at a slant */
#define STRATA_TEXTURE_CLAMP_U 0x2u /* clamp u to the edge instead of wrapping */
#define STRATA_TEXTURE_CLAMP_V 0x4u /* clamp v to the edge instead of wrapping (a planet map's poles) */
/* ABI 1.10. Sample the nearest texel instead of blending the four nearest, so
 * each texel is a hard-edged square however far it is magnified: for cell
 * fields, pixel art and lookup images. With STRATA_TEXTURE_MIPMAPS the
 * nearest texel of each of the two nearest levels is taken and the levels are
 * blended (GL_NEAREST_MIPMAP_LINEAR). A 1.7 to 1.9 library rejects the flag
 * as unknown; check strata_abi_minor() >= 10 first. */
#define STRATA_TEXTURE_NEAREST 0x8u
/* ABI 1.20. The texels' RGB is sRGB-encoded (as a photo, a painted texture or
 * an image editor's colours are): the GPU decodes it to linear when the
 * texture is sampled, filtering and mipmaps included; alpha is linear either
 * way. Meant for hosts that also turn on STRATA_OUTPUT_SRGB, which encodes the
 * result again; without it the decoded texture looks darker than it was
 * drawn. Use it for colour (albedo and emissive) textures only, never for a
 * normal map. A 1.7 to 1.19 library rejects the flag as unknown. */
#define STRATA_TEXTURE_SRGB 0x10u

/* StrataTexturedMesh.flags. */
#define STRATA_MESH_UNLIT 0x1u /* drawn as its colour times its texture, with no lighting or fog, and casting no
                                  shadows: for things that glow, such as a star's surface */
/* ABI 1.11. A translucent mesh: alpha blended over the opaque scene and the
 * sky, with alpha = mesh.color[3] (0..1, also when per-vertex colours are
 * given) times the texel's alpha. It is depth-tested against opaque geometry
 * but writes no depth, both faces are drawn, and it casts no shadows (it
 * receives them when lit). Translucent meshes are drawn after the sky, back to
 * front by the distance from the eye to each mesh's bounding-box centre, which
 * is exact for separate objects and approximate where they interpenetrate.
 * Combines with STRATA_MESH_UNLIT. Not in strata_read_depth_f32's depth. */
#define STRATA_MESH_TRANSLUCENT 0x2u
/* ABI 1.15. mesh.colors holds vertex_count RGBA quadruples (16 bytes a vertex)
 * instead of RGB triples, for a host whose vertex colours are RGBA already.
 * The fourth component is validated like the others (finite, >= 0) and then
 * ignored: a mesh's alpha is still mesh.color[3] (STRATA_MESH_TRANSLUCENT).
 * strata_add_mesh has no flags, so it always reads RGB; pass the mesh through
 * strata_add_textured_mesh with texture 0 to use this. A 1.7 to 1.14 library
 * rejects the flag as unknown; check strata_abi_minor() >= 15 first. */
#define STRATA_MESH_COLORS_RGBA 0x4u
/* ABI 1.19. Flat shading without host-made normals: mesh.normals must be
 * NULL, and the engine gives each triangle three vertices of its own carrying
 * that triangle's face normal (counter-clockwise winding, as computed normals
 * always follow), so shared vertices are split and every face is shaded as one
 * flat colour. Colours and uvs are per source vertex as usual. A degenerate
 * triangle (two equal corners, no area) gets "up" and draws nothing, so a
 * host need not filter them out. The GPU copy then holds index_count
 * vertices, which must be at most STRATA_MAX_MESH_VERTICES; picking and
 * strata_raycast_meshes still see the mesh as given (the same triangle
 * numbers). As with STRATA_MESH_COLORS_RGBA, pass the mesh through
 * strata_add_textured_mesh (texture 0 for none). A 1.7 to 1.18 library
 * rejects the flag as unknown; check strata_abi_minor() >= 19 first. */
#define STRATA_MESH_FLAT_NORMALS 0x8u

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

/* --------------------------------------------------------------- materials */

/* ABI 1.20. StrataMaterial.flags. Both faces of every triangle are drawn in
 * the opaque pass (shadow passes always draw both), and a back face is lit
 * with its normal turned towards the viewer: for leaves, grass and fence
 * cards, signs and thin walls made of one layer of triangles. Translucent
 * meshes draw both faces already; the flag then only turns the normal. */
#define STRATA_MATERIAL_DOUBLE_SIDED 0x1u

/* ABI 1.20. What a static mesh's surface does beyond its colour and albedo
 * texture, set with strata_set_mesh_material. An all-zero StrataMaterial is
 * the default every mesh starts with and draws exactly as before 1.20.
 *
 * Alpha cutout (alpha_cutoff > 0): a fragment whose albedo texel alpha (the
 * mesh's texture; 1 where it has none) is below alpha_cutoff is discarded, in
 * the opaque and translucent passes and in every shadow pass, so the holes in
 * a leaf card or a fence let light and shadow through. Cutout edges are hard
 * (no blending), so the mesh stays in the opaque pass, depth-sorted for free.
 * A texture with STRATA_TEXTURE_MIPMAPS keeps distant cutouts from breaking
 * up; texels whose alpha straddles the cutoff then decide the edge.
 *
 * Emission: emissive * emissive_strength (times the emissive texture's RGB
 * where one is given) is added to the lit colour before fog, under every
 * lighting mode and on STRATA_MESH_UNLIT meshes too. It lights nothing else
 * (add a point or spot light for that) and needs no light to be seen: lit
 * windows and signs at night. Linear, like every other colour.
 *
 * Normal map (normal_texture != 0, lit meshes only): a tangent-space normal
 * map in the common OpenGL and glTF convention (red +X along +u, green +Y
 * towards the image's first row, blue out of the surface; 128 128 255 is
 * flat). The tangent frame is derived per pixel from the screen-space
 * derivatives of the position and uvs, so meshes need no tangents; mirrored
 * uvs therefore work too. normal_scale multiplies the map's x and y (glTF's
 * normalTexture.scale): 1 uses the map as stored, 0 flattens it away. Use a
 * texture made without STRATA_TEXTURE_SRGB. Shadows and picking use the
 * geometric normal.
 *
 * A texture named here must exist when the material is set; destroying it
 * later turns that part of the material off, as an albedo texture does.
 * 36 bytes. */
typedef struct StrataMaterial {
    float alpha_cutoff;         /* 0 (off) or (0, 1] */
    float emissive[3];          /* linear RGB, each >= 0; (0, 0, 0) for none */
    float emissive_strength;    /* >= 0: multiplies emissive, so it can exceed 1 for a light source */
    uint32_t emissive_texture;  /* 0 for none, or a texture id whose RGB multiplies the emission */
    uint32_t normal_texture;    /* 0 for none, or a texture id holding a tangent-space normal map */
    float normal_scale;         /* finite: scales the normal map's x and y; 1 as stored */
    uint32_t flags;             /* STRATA_MATERIAL_*; any other bit is rejected */
} StrataMaterial;

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
 * not clipped by the far plane; from ABI 1.11, STRATA_LINES_ON_TOP and
 * STRATA_POINT_ON_TOP skip the depth test. Filled rectangles (1.11) and text
 * are in pixels and drawn on top of everything, rectangles first. */

/* One end of a line segment. 28 bytes. */
typedef struct StrataLineVertex {
    float position[3]; /* world space */
    float color[4];    /* linear RGBA; alpha blends */
} StrataLineVertex;

/* StrataPoint.flags. */
#define STRATA_POINT_SOFT 0x1u /* a soft glow that fades to nothing at the edge, instead of a solid disc */
#define STRATA_POINT_ON_TOP 0x2u /* ABI 1.11: not depth-tested, so it shows through the scene; drawn after the
                                    depth-tested lines and points. Older libraries reject it as unknown */

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

/* strata_submit_lines_ex flags (ABI 1.11). */
#define STRATA_LINES_ON_TOP 0x1u /* not depth-tested: the lines show through the scene (an arc behind a hill) */
/* ABI 1.13, strata_submit_wide_lines only (strata_submit_lines_ex rejects it):
 * each segment ends in a half disc instead of a square end, so segments that
 * share an end join without a notch, at any angle. */
#define STRATA_LINES_ROUND_CAPS 0x2u

/* ABI 1.13. One end of a wide line segment: a StrataLineVertex with a width.
 * The segment is drawn as a camera-facing quad (two triangles), so its width
 * holds in a browser too, where WebGL draws GL lines one pixel wide. Its
 * width on screen at each end is the larger of width_pixels and world_width
 * as projected at that end's distance, and it varies linearly along the
 * segment, so a ribbon can taper. The colour varies the same way. Edges are
 * antialiased over about one pixel. 36 bytes. */
typedef struct StrataWideLineVertex {
    float position[3];  /* world space */
    float color[4];     /* linear RGBA; alpha blends */
    float width_pixels; /* >= 0: smallest width on screen, in pixels */
    float world_width;  /* >= 0: width in world units; 0 for a pure screen-width line */
} StrataWideLineVertex;

/* ABI 1.11. A filled rectangle on screen, in pixels from the top-left of the
 * drawable, alpha blended: a backing panel for text, a bar, a HUD block. A
 * frame's rectangles are drawn in submission order, after every world overlay
 * and before any text, so text sits on the panels. 32 bytes. */
typedef struct StrataRect {
    float x, y;          /* top-left corner, pixels */
    float width, height; /* >= 0, pixels */
    float color[4];      /* linear RGBA; alpha blends */
} StrataRect;

/* A run of text on screen in Strata's built-in 8 x 8 bitmap font (printable
 * ASCII; anything else shows as one '?' per UTF-8 character, so an accented
 * letter becomes '?'; '\n' starts a new line). From ABI 1.14 the same struct
 * also labels a world position (strata_submit_world_text). Every glyph is a
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
    STRATA_STRUCT_RAY_HIT = 15,        /* ABI 1.8 */
    STRATA_STRUCT_RECT = 16,           /* ABI 1.11 */
    STRATA_STRUCT_WIDE_LINE_VERTEX = 17, /* ABI 1.13 */
    STRATA_STRUCT_MATERIAL = 18,         /* ABI 1.20 */
    STRATA_STRUCT_SPOT_LIGHT = 19,       /* ABI 1.20 */
    STRATA_STRUCT_LIGHT_STATS = 20       /* ABI 1.21 */
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
 * empty. Platform events come first, then injected ones (strata_push_event,
 * the drive channel) first in, first out. Headless and host-context surfaces
 * produce no platform events of their own. A driven engine with a stepped
 * clock waits inside the first call of each frame until the harness grants
 * the frame (docs/DRIVE.md). */
STRATA_API int32_t strata_poll_event(StrataEngine* engine, StrataEvent* out);

/* ABI 1.12. Queues a copy of *event; strata_poll_event returns it after any
 * platform events already pending. Works on every surface. A type outside
 * the STRATA_EVENT_* values is STRATA_ERROR_INVALID_ARGUMENT; when 4096
 * injected events are already waiting it is STRATA_ERROR_INVALID_STATE and
 * nothing is queued. text is copied up to its 32 bytes and NUL-terminated.
 * While relative mouse mode is on, injected MOUSE_MOTION events have x and y
 * set to 0. For tests, replays and scripted input. */
STRATA_API StrataStatus strata_push_event(StrataEngine* engine, const StrataEvent* event);

/* ABI 1.12. Writes the seconds to advance this frame to *out_seconds: the
 * time since the previous call on a monotonic clock (0 on the first call), or,
 * when a harness drives the engine with a stepped clock, exactly the step it
 * granted (the call then waits for the grant if strata_poll_event has not).
 * Call it once a frame in place of the game's own clock; a game that does is
 * repeatable frame for frame under a stepped driver. */
STRATA_API StrataStatus strata_frame_seconds(StrataEngine* engine, double* out_seconds);

/* ABI 1.12. 1 if this engine was created with a drive channel connected
 * (the STRATA_DRIVE environment variable; docs/DRIVE.md), else 0. It stays 1
 * after the harness disconnects. A game uses it to keep a driven run away
 * from the player's saves and records. The engine only ever connects to the
 * socket STRATA_DRIVE names; it never listens. */
STRATA_API int32_t strata_is_driven(const StrataEngine* engine);

STRATA_API StrataStatus strata_get_drawable_size(const StrataEngine* engine, int32_t* width, int32_t* height);
/* Headless and host-context surfaces: sets the drawable size. Window: resizes the window. */
STRATA_API StrataStatus strata_resize(StrataEngine* engine, int32_t width, int32_t height);

STRATA_API StrataStatus strata_set_camera(StrataEngine* engine, const StrataCamera* camera);
/* Convenience: fills *out with a right-handed look-at view and an OpenGL
 * (depth -1..1) perspective projection. Needs no engine. */
STRATA_API StrataStatus strata_camera_look_at(const float eye[3], const float target[3], const float up[3],
                                              float fov_y_degrees, float aspect, float near_plane,
                                              float far_plane, StrataCamera* out);
/* ABI 1.20. Convenience: fills *out with the same right-handed look-at view
 * and an OpenGL (depth -1..1) orthographic projection, centred on the view
 * axis, half_height world units from the centre to the top edge and
 * half_height * aspect to the side: maps, isometric and top-down views, and
 * the parallel-projection renders a level editor shows. near_plane and
 * far_plane are distances along the view axis from the eye (0 < near < far,
 * as strata_set_camera requires), so place the eye behind everything that
 * should be seen. Needs no engine.
 *
 * strata_set_camera accepts any orthographic projection (column-major
 * elements 3, 7 and 11 zero, 15 one), and the engine then fits the shadow
 * cascades to the box the camera sees instead of to a perspective frustum,
 * splitting it by distance along the view axis between near_plane and
 * shadow_distance. The sky has no perspective to show under an orthographic
 * camera: every pixel looks the same way, so it is one colour (the one seen
 * along the view axis). Points, wide lines, world text, picking rays built by
 * a host, strata_project_point and strata_read_depth_f32 all work with it. */
STRATA_API StrataStatus strata_camera_ortho(const float eye[3], const float target[3], const float up[3],
                                            float half_height, float aspect, float near_plane, float far_plane,
                                            StrataCamera* out);
/* ABI 1.14. Projects a world-space point with the camera last set
 * (strata_set_camera) onto the drawable as it is now (strata_get_drawable_size),
 * the same mapping the scene and strata_submit_world_text use. Writes
 * *out_in_front = 1 when the point is in front of the camera (its distance
 * along the view axis is > 0 and its clip w is > 0), else 0. In front,
 * out_pixels gets its position in pixels from the top-left of the drawable,
 * the coordinates StrataText and StrataRect use; it may lie off screen.
 * Behind the camera out_pixels is set to (0, 0). *out_depth gets its distance
 * along the camera's view axis in world units (negative behind the camera),
 * the same measure as strata_read_depth_f32. out_pixels, out_depth and
 * out_in_front may each be NULL when not wanted. world must be finite. Needs
 * no frame and draws nothing. */
STRATA_API StrataStatus strata_project_point(const StrataEngine* engine, const float world[3], float out_pixels[2],
                                             float* out_depth, int32_t* out_in_front);

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

/* ABI 1.16. Adjusts the ambient term without moving the sun. In
 * STRATA_LIGHTING_SUN the hemisphere ambient (sky and ground) becomes
 * ambient * tint + lift; in STRATA_LIGHTING_SPACE the flat legacy_ambient does
 * the same. LEGACY ignores both. tint is linear RGB, each component 0..16
 * (NULL: 1, 1, 1); lift is linear RGB, each 0..1 (NULL: 0, 0, 0). A tint of
 * equal components scales the ambient; a lift raises the shadow side (an
 * interior, a deep valley, or fill lights a tier has dropped). The sky, fog
 * and point lights are unchanged. Both stay until the next call, across
 * strata_set_lighting calls; strata_set_ambient(engine, NULL, NULL) goes back
 * to the defaults, which draw exactly what a 1.12 library draws. On an
 * invalid value nothing changes. */
STRATA_API StrataStatus strata_set_ambient(StrataEngine* engine, const float tint[3], const float lift[3]);
/* ABI 1.16. Scales the STRATA_LIGHTING_SUN key light (the sun by day, the
 * moon at night) by intensity (0..16) and tint (linear RGB, each 0..16; NULL:
 * 1, 1, 1), keeping its direction, shadows and the sky: dim or warm the sun
 * for weather or mood. Ignored by LEGACY and SPACE. It stays until the next
 * call, across strata_set_lighting calls; strata_set_sun_intensity(engine, 1,
 * NULL) goes back to the default, which draws exactly what a 1.12 library
 * draws. On an invalid value nothing changes. */
STRATA_API StrataStatus strata_set_sun_intensity(StrataEngine* engine, float intensity, const float tint[3]);

/* ABI 1.1. Replaces the scene's point lights with a copy of `lights`; they
 * stay until the next call. count 0 (lights may then be NULL) removes them
 * all. At most STRATA_MAX_POINT_LIGHTS. Every light is checked first, and on
 * an invalid one nothing changes. A 1.1 library rejects
 * STRATA_POINT_LIGHT_CAST_SHADOWS as an unknown flag; check strata_abi_minor()
 * >= 2 before setting it. May be called inside or outside a frame;
 * the lights in place at end_frame are the ones drawn. */
STRATA_API StrataStatus strata_set_point_lights(StrataEngine* engine, const StrataPointLight* lights, int32_t count);
/* ABI 1.20. Replaces the scene's spot lights with a copy of `lights`, as
 * strata_set_point_lights does for point lights (which it leaves alone): count
 * 0 removes them all, at most STRATA_MAX_SPOT_LIGHTS, every light checked
 * first, and on an invalid one nothing changes. The shadow casters of both
 * lists together may not exceed STRATA_MAX_SHADOWED_POINT_LIGHTS; both calls
 * refuse a list that would. Spot lights shade exactly where point lights do
 * (STRATA_LIGHTING_SUN and STRATA_LIGHTING_SPACE). */
STRATA_API StrataStatus strata_set_spot_lights(StrataEngine* engine, const StrataSpotLight* lights, int32_t count);

/* ABI 1.21. Selects how lit fragments find their lights
 * (STRATA_LIGHT_CULLING_*); it stays until the next call. Switching to
 * STRATA_LIGHT_CULLING_CLUSTERED changes nothing else. Switching back to
 * STRATA_LIGHT_CULLING_NONE while either list holds more lights than it
 * allows (STRATA_MAX_POINT_LIGHTS, STRATA_MAX_SPOT_LIGHTS) is
 * STRATA_ERROR_INVALID_STATE and changes nothing: set fewer lights first. An
 * unknown mode is STRATA_ERROR_INVALID_ARGUMENT. Under clustered culling
 * strata_set_point_lights and strata_set_spot_lights accept up to
 * STRATA_MAX_CLUSTERED_LIGHTS each. The same lights draw the same picture
 * either way, to within rounding (the sum runs in another order). */
STRATA_API StrataStatus strata_set_light_culling(StrataEngine* engine, int32_t mode);
/* ABI 1.21. Fills *out with the light culling's numbers for the most recent
 * strata_end_frame (see StrataLightStats). */
STRATA_API StrataStatus strata_get_light_stats(const StrataEngine* engine, StrataLightStats* out);

/* ABI 1.20. STRATA_OUTPUT_* flags for strata_set_color_output. */
#define STRATA_OUTPUT_SRGB 0x1u             /* encode the result from linear to sRGB (see below) */
#define STRATA_OUTPUT_TONEMAP_REINHARD 0x2u /* c / (1 + c) per channel: gentle, never quite white */
#define STRATA_OUTPUT_TONEMAP_ACES 0x4u     /* Narkowicz's fit of the ACES filmic curve: contrasty, bright
                                               values roll off to white; at most one TONEMAP flag */

/* ABI 1.20. How the lit scene's colour is written out. The default, flags 0
 * and exposure 1, is what every earlier library does: nothing at all (see
 * "Colour" at the top of this header). Otherwise each lit pixel's linear
 * colour is multiplied by exposure (> 0, finite, at most 64), then tonemapped
 * if a TONEMAP flag asks, then, with STRATA_OUTPUT_SRGB, clamped to 0..1 and
 * encoded to sRGB, so a host can pass linear colours (and
 * STRATA_TEXTURE_SRGB textures) and see them as a linear renderer would show
 * them. It applies to primitives, the terrain, meshes (unlit and translucent
 * ones too; a translucent mesh blends in output space) and the sky. Under
 * STRATA_OUTPUT_SRGB the sky and fog colours are decoded first, so the sky
 * looks as it did unencoded. Overlays (lines, points, text, rectangles) are
 * never changed: their colours stay what the host passed. It stays until the
 * next call; an invalid value changes nothing. */
STRATA_API StrataStatus strata_set_color_output(StrataEngine* engine, uint32_t flags, float exposure);

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
/* ABI 1.19. Zero `visible` hides a mesh, non-zero shows it again. A hidden
 * mesh keeps its GPU buffers, model matrix and id, but no pass draws it (the
 * opaque, translucent and every shadow pass) and strata_raycast_meshes does
 * not hit it, so swapping between several large meshes costs no upload.
 * Meshes start visible; an unknown id is STRATA_ERROR_INVALID_ARGUMENT and
 * changes nothing. */
STRATA_API StrataStatus strata_set_mesh_visible(StrataEngine* engine, uint32_t id, int32_t visible);
/* ABI 1.20. Sets a mesh's material (see StrataMaterial) to a copy of
 * *material; NULL restores the default (all zero). Works on meshes from
 * strata_add_mesh and strata_add_textured_mesh alike; the alpha cutout reads
 * the mesh's own albedo texture, so an untextured mesh is never cut. The
 * material is checked first: an unknown mesh id, a texture id with no
 * texture, a value out of range or an unknown flag is
 * STRATA_ERROR_INVALID_ARGUMENT and changes nothing. Changing a material
 * uploads nothing. */
STRATA_API StrataStatus strata_set_mesh_material(StrataEngine* engine, uint32_t id, const StrataMaterial* material);

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
 * change nothing (a host that owns the window captures the mouse itself),
 * except that from ABI 1.12 a driven engine (strata_is_driven) returns
 * STRATA_OK on any surface and makes injected motion relative. In
 * a browser this is pointer lock, which the browser grants only from a user
 * gesture such as a click; SDL requests it on the next one. */
STRATA_API StrataStatus strata_set_relative_mouse_mode(StrataEngine* engine, int32_t enabled);

/* ABI 1.17. Window fullscreen. Non-zero enabled makes the window cover the
 * whole display it is on, borderless, at the desktop's resolution (no video
 * mode change); zero puts it back as a window at the size it had before.
 * Asking for the state it is already in is STRATA_OK and changes nothing.
 * The drawable size changes with it: strata_drawable_size reports the new
 * size once the switch has happened, and a STRATA_EVENT_RESIZED follows
 * through strata_poll_event, as for any resize. Window surface only:
 * headless and host-context engines return STRATA_ERROR_UNSUPPORTED and
 * change nothing (a host that owns the window makes it fullscreen itself).
 * In a browser this is the Fullscreen API, which the browser grants only
 * from a user gesture such as a key press or a click. */
STRATA_API StrataStatus strata_set_fullscreen(StrataEngine* engine, int32_t enabled);

/* ABI 1.17. 1 while the window is fullscreen (see strata_set_fullscreen),
 * 0 otherwise, including on headless and host-context surfaces and for a
 * NULL engine. */
STRATA_API int32_t strata_is_fullscreen(const StrataEngine* engine);

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
/* ABI 1.11. strata_submit_lines with STRATA_LINES_* flags (0 is the same as
 * strata_submit_lines); an unknown bit is STRATA_ERROR_INVALID_ARGUMENT. */
STRATA_API StrataStatus strata_submit_lines_ex(StrataEngine* engine, const StrataLineVertex* vertices, int32_t count,
                                               uint32_t flags);
/* ABI 1.11. Filled screen rectangles for this frame (see StrataRect). Every
 * rectangle is checked first; on an invalid one nothing from the call is
 * added. */
STRATA_API StrataStatus strata_submit_rects(StrataEngine* engine, const StrataRect* rects, int32_t count);
/* ABI 1.13. Wide lines (see StrataWideLineVertex): count is a number of
 * vertices, even, and each pair is one segment, as for strata_submit_lines.
 * flags are STRATA_LINES_ON_TOP and STRATA_LINES_ROUND_CAPS; an unknown bit
 * is STRATA_ERROR_INVALID_ARGUMENT. They are drawn with this frame's lines of
 * the same kind (depth-tested or on top), after the one-pixel ones. Overlapping
 * translucent segments (a round-capped joint) blend twice where they overlap. */
STRATA_API StrataStatus strata_submit_wide_lines(StrataEngine* engine, const StrataWideLineVertex* vertices,
                                                 int32_t count, uint32_t flags);
/* ABI 1.14. World-anchored text: labels over a building, a unit or a map pin.
 * anchors holds count world positions (3 floats each, x y z); text i is drawn
 * with its top-left at the pixel where anchors[i] lands on screen plus
 * (texts[i].x, texts[i].y) as a pixel offset, so (-n * pixel_size / 2,
 * -pixel_size) centres a one-line label of n characters just above its
 * anchor. Anchors are projected at end_frame with the camera and drawable
 * size the frame is drawn with, exactly as strata_project_point does; a label
 * whose anchor is not in front of the camera is skipped (not an error). Like
 * screen text, labels are not depth-tested: they show through the scene.
 * They are drawn after this frame's rectangles and before its screen text,
 * so a HUD drawn with strata_submit_text stays on top. Every text and anchor
 * is checked first (anchors must be finite); on an invalid one nothing from
 * the call is added. */
STRATA_API StrataStatus strata_submit_world_text(StrataEngine* engine, const StrataText* texts, const float* anchors,
                                                 int32_t count);
STRATA_API StrataStatus strata_end_frame(StrataEngine* engine);
/* Window: swaps buffers. Headless and host context: no-op. */
STRATA_API StrataStatus strata_present(StrataEngine* engine);

STRATA_API StrataStatus strata_get_frame_stats(const StrataEngine* engine, StrataFrameStats* out);

/* Reads the last rendered frame as tightly packed RGBA8, top row first.
 * capacity_bytes must be at least width * height * 4. */
STRATA_API StrataStatus strata_read_pixels_rgba8(StrataEngine* engine, uint8_t* dst, int32_t capacity_bytes,
                                                 int32_t* out_width, int32_t* out_height);
/* ABI 1.9. Reads the last rendered frame's depth as tightly packed float32,
 * top row first, the same pixels as strata_read_pixels_rgba8: the distance
 * along the camera's view axis in world units (planar, not radial: a plane
 * facing the camera reads the same value in every pixel), or +infinity where
 * nothing was drawn. It is computed from the depth buffer with the projection
 * the frame was drawn with (either depth convention), so its precision is the
 * depth buffer's (32-bit float). Only primitives, the terrain tile and static
 * meshes, unlit ones included, write depth; the sky and overlays never do, so
 * they read as whatever is behind them. capacity_bytes must be at least
 * width * height * 4. Headless surfaces only (EGL, or natively the hidden
 * window standing in for it): a window, a host context, and a browser's
 * headless request return STRATA_ERROR_UNSUPPORTED. */
STRATA_API StrataStatus strata_read_depth_f32(StrataEngine* engine, float* dst, int32_t capacity_bytes,
                                              int32_t* out_width, int32_t* out_height);
/* Writes the last rendered frame to a PNG file (RGB, top row first). */
STRATA_API StrataStatus strata_save_screenshot_png(StrataEngine* engine, const char* utf8_path);

#ifdef __cplusplus
}
#endif

#endif /* STRATA_H */
