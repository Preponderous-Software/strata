/* strata_gltf: a glTF 2.0 loader for Strata, as a small C library of its own
 * (libstrata_gltf.so / strata_gltf.dll) for hosts that bind C: Python
 * (bindings/python/strata/gltf.py) and C# (StrataGltfNative). It is not part
 * of libstrata's ABI: it calls libstrata only through strata.h, so it works
 * with any libstrata of ABI 1.15 or later, and its own version is
 * STRATA_GLTF_API_VERSION.
 *
 * A load reads the file, fits it into Strata's limits (docs/GLTF.md: one mesh
 * per primitive, else merged by material, else atlased) and creates the
 * textures and meshes on the engine. The returned scene owns the ids; unload
 * removes them. Single-threaded, like the engine. Statuses are StrataStatus
 * values; strata_gltf_last_error() describes the most recent failure on the
 * calling thread. */
#ifndef STRATA_GLTF_H
#define STRATA_GLTF_H

#include <stdint.h>

#include "strata/strata.h"

#if defined(_WIN32)
#  if defined(STRATA_GLTF_BUILDING_LIBRARY)
#    define STRATA_GLTF_API __declspec(dllexport)
#  elif defined(STRATA_GLTF_STATIC)
#    define STRATA_GLTF_API
#  else
#    define STRATA_GLTF_API __declspec(dllimport)
#  endif
#else
#  define STRATA_GLTF_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define STRATA_GLTF_API_VERSION 1u

/* StrataGltfOptions.flags */
#define STRATA_GLTF_ALWAYS_MERGE 0x1u  /* merge primitives by material even when one mesh each would fit */
#define STRATA_GLTF_NO_BACK_FACES 0x2u /* do not add reversed triangles for doubleSided opaque materials */
#define STRATA_GLTF_NO_MIPMAPS 0x4u    /* never build mip chains */

/* StrataGltfOptions.color_policy: how glTF colours become Strata colours. */
#define STRATA_GLTF_COLORS_DISPLAY 0   /* texels as stored, linear factors/vertex colours sRGB-encoded (default) */
#define STRATA_GLTF_COLORS_LINEAR 1    /* texels decoded to linear, factors/vertex colours as given */
#define STRATA_GLTF_COLORS_AS_STORED 2 /* no conversion at all */

/* StrataGltfOptions.mask_policy: what alphaMode MASK becomes. */
#define STRATA_GLTF_MASK_BLEND 0  /* translucent mesh with the cutoff baked into texel alpha (default) */
#define STRATA_GLTF_MASK_OPAQUE 1 /* opaque mesh: cutouts show solid */

/* StrataGltfOptions.emissive_policy: what an emissive material becomes. */
#define STRATA_GLTF_EMISSIVE_UNLIT 0  /* unlit mesh coloured base + emissive (default) */
#define STRATA_GLTF_EMISSIVE_IGNORE 1 /* lit from the base colour; emissive only recorded */

/* StrataGltfMeshInfo.alpha_mode: the source material's glTF alphaMode. */
#define STRATA_GLTF_ALPHA_OPAQUE 0
#define STRATA_GLTF_ALPHA_MASK 1
#define STRATA_GLTF_ALPHA_BLEND 2

/* Zero fields take the defaults shown. 40 bytes. */
typedef struct StrataGltfOptions {
    uint32_t struct_size;     /* sizeof(StrataGltfOptions) */
    uint32_t flags;           /* STRATA_GLTF_* */
    int32_t scene;            /* -1: the file's default scene (0 is scene 0) */
    int32_t max_meshes;       /* 0: STRATA_MAX_MESHES. Lower it to leave room for the host's own meshes */
    int32_t max_textures;     /* 0: STRATA_MAX_TEXTURES */
    int32_t max_texture_size; /* 0: STRATA_MAX_TEXTURE_SIZE; larger images are scaled down */
    int32_t atlas_size;       /* 0: 4096; the side of an atlas page */
    int32_t color_policy;     /* STRATA_GLTF_COLORS_* */
    int32_t mask_policy;      /* STRATA_GLTF_MASK_* */
    int32_t emissive_policy;  /* STRATA_GLTF_EMISSIVE_* */
} StrataGltfOptions;

/* What a load made. 80 bytes. */
typedef struct StrataGltfInfo {
    int32_t primitives_in;    /* triangle primitives read from the file's scene */
    int32_t primitives_skipped; /* points, lines, compressed or malformed */
    int32_t mesh_count;       /* Strata meshes created */
    int32_t texture_count;    /* Strata textures created */
    int32_t material_count;   /* glTF materials in the file */
    int32_t merged;           /* non-zero: primitives were merged by material */
    int32_t atlased;          /* non-zero: textures were packed into atlas pages */
    int32_t atlas_pages;
    int32_t warning_count;    /* lines in the report's notes */
    int32_t reserved;
    int64_t vertices;         /* over every mesh created */
    int64_t triangles;
    float bounds_min[3];      /* world-space bounds of the scene */
    float bounds_max[3];
} StrataGltfInfo;

/* One created mesh. 52 bytes. */
typedef struct StrataGltfMeshInfo {
    uint32_t mesh_id;         /* the engine's mesh id */
    uint32_t texture_id;      /* the engine's texture id, or 0 */
    uint32_t flags;           /* the STRATA_MESH_* flags it was added with */
    int32_t material;         /* glTF material index; -1 the default material; -2 several merged */
    int32_t alpha_mode;       /* STRATA_GLTF_ALPHA_* of the source material(s) */
    float alpha_cutoff;       /* the source alphaCutoff (MASK) */
    int32_t double_sided;     /* non-zero when the source material is doubleSided */
    int32_t primitives;       /* glTF primitives in this mesh */
    int32_t vertex_count;
    int32_t index_count;
    float emissive[3];        /* the source emissiveFactor x strength, linear (the brightest when merged) */
} StrataGltfMeshInfo;

typedef struct StrataGltfScene StrataGltfScene;

STRATA_GLTF_API uint32_t strata_gltf_api_version(void);
STRATA_GLTF_API const char* strata_gltf_last_error(void);

/* Loads path (.gltf or .glb) onto engine. options may be NULL. On failure
 * nothing stays on the engine and *out_scene is NULL. Statuses:
 * STRATA_ERROR_IO (unreadable file or buffer), STRATA_ERROR_INVALID_ARGUMENT
 * (not glTF 2.0, bad options), STRATA_ERROR_UNSUPPORTED (cannot be fitted
 * into the limits), or the failing engine call's status. */
STRATA_GLTF_API StrataStatus strata_gltf_load(StrataEngine* engine, const char* path, const StrataGltfOptions* options,
                                              StrataGltfScene** out_scene);
STRATA_GLTF_API StrataStatus strata_gltf_get_info(const StrataGltfScene* scene, StrataGltfInfo* out_info);
/* index is 0..mesh_count-1. */
STRATA_GLTF_API StrataStatus strata_gltf_get_mesh(const StrataGltfScene* scene, int32_t index,
                                                  StrataGltfMeshInfo* out_mesh);
/* The plan's report: one fact per line, then any notes. Valid until unload. */
STRATA_GLTF_API const char* strata_gltf_report(const StrataGltfScene* scene);
/* The same as one JSON object. Valid until unload. */
STRATA_GLTF_API const char* strata_gltf_report_json(const StrataGltfScene* scene);
/* Removes the scene's meshes and textures from engine and frees the scene.
 * engine may be NULL (already destroyed): then only the scene is freed.
 * scene NULL does nothing. */
STRATA_GLTF_API void strata_gltf_unload(StrataEngine* engine, StrataGltfScene* scene);

#ifdef __cplusplus
}
#endif

#endif
