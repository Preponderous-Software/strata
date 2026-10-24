// C# view of Strata's C ABI (include/strata/strata.h, ABI version 1.8).
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace Strata.Interop {

    public static class StrataAbi {
        public const uint Version = 1;
        /// <summary>The minor version these declarations cover (strata_abi_minor). 1.1 added point lights, 1.2 their shadows, 1.3 a terrain tile, 1.4 static meshes, 1.5 overlays and space lighting, 1.6 a sun placed by date, time and place, 1.7 textures and textured, transformable meshes, 1.8 texture sub-region updates, mesh ray picking and relative mouse mode, 1.9 depth readback, 1.10 nearest-texel texture filtering, 1.11 translucent meshes, overlays on top of the scene and filled screen rectangles.</summary>
        public const uint Minor = 11;

        public const int SurfaceWindow = 0, SurfaceHeadless = 1, SurfaceHostContext = 2;
        public const uint FlagVsync = 1u << 0, FlagDebugContext = 1u << 1, FlagHiddenWindow = 1u << 2, FlagGpuTiming = 1u << 3;
        public const int LightingLegacy = 0, LightingSun = 1;
        public const int LightingSpace = 2;  // ABI 1.5
        public const uint ClearColor = 1u << 0, ClearDepth = 1u << 1, ClearSky = 1u << 2;

        public const int StructRenderItem = 0, StructEngineDesc = 1, StructCamera = 2, StructLighting = 3,
            StructFrameDesc = 4, StructFrameStats = 5, StructEvent = 6,
            StructPointLight = 7,  // ABI 1.1
            StructTerrainTile = 8, // ABI 1.3
            StructMesh = 9,        // ABI 1.4
            StructLineVertex = 10, StructPoint = 11, StructText = 12,  // ABI 1.5
            StructSolarPosition = 13,  // ABI 1.6
            StructTexturedMesh = 14,   // ABI 1.7
            StructRayHit = 15,         // ABI 1.8
            StructRect = 16;           // ABI 1.11
        public const uint TextureMipmaps = 0x1u, TextureClampU = 0x2u, TextureClampV = 0x4u;  // ABI 1.7
        public const uint TextureNearest = 0x8u;  // ABI 1.10
        public const uint MeshUnlit = 0x1u;  // ABI 1.7
        public const uint MeshTranslucent = 0x2u;  // ABI 1.11

        public const int MaxPointLights = 32;  // ABI 1.1
        public const uint PointLightCastShadows = 0x1u;  // ABI 1.2
        public const int MaxShadowedPointLights = 4;     // ABI 1.2
        public const int MaxMeshes = 256;                // ABI 1.4
        public const int MaxMeshVertices = 16777216;     // ABI 1.4

        public const uint PointSoft = 0x1u;   // ABI 1.5
        public const uint PointOnTop = 0x2u;  // ABI 1.11
        public const uint LinesOnTop = 0x1u;  // ABI 1.11, strata_submit_lines_ex
        public const uint TextShadow = 0x1u;  // ABI 1.5

        public const int Ok = 0;
    }

    /// <summary>StrataRenderItem: 44 bytes, the layout of Beyond Nations' RenderItem.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StrataRenderItem {
        public int kind;
        public fixed float position[3];
        public fixed float scale[3];
        public fixed float color[4];
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct StrataEngineDesc {
        public uint struct_size;
        public uint abi_version;
        public int surface;
        public int width;
        public int height;
        public uint flags;
        public IntPtr title;                 // const char*, UTF-8
        public IntPtr get_proc_address;      // StrataGetProcAddressFn
        public IntPtr get_proc_address_user;
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StrataCamera {
        public fixed float view[16];
        public fixed float projection[16];
        public fixed float eye[3];
        public float near_plane;
        public float far_plane;
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StrataLighting {
        public int mode;
        public fixed float legacy_direction[3];
        public float legacy_ambient;
        public float time_of_day;
        public float latitude_degrees;
        public int cascade_count;
        public int shadow_map_size;
        public float shadow_distance;
        public float split_lambda;
        public int fog;
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StrataFrameDesc {
        public uint clear_flags;
        public fixed float clear_color[4];
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StrataFrameStats {
        public int draw_calls;
        public int instances;
        public fixed int instances_per_kind[4];
        public int items_rejected;
        public int shadow_cascades;
        public float cpu_ms_shadow;
        public float cpu_ms_opaque;
        public float cpu_ms_sky;
        public float cpu_ms_frame;
        public float gpu_finish_ms;
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StrataEvent {
        public int type;
        public int scancode;
        public int modifiers;
        public int repeat;
        public int button;
        public int width;
        public int height;
        public float x, y;
        public float dx, dy;
        public fixed byte text[32];
    }

    /// <summary>StrataPointLight (ABI 1.1): 36 bytes.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StrataPointLight {
        public fixed float position[3];
        public float range;
        public fixed float color[3];
        public float intensity;
        public uint flags;
    }

    /// <summary>StrataTerrainTile (ABI 1.3): 56 bytes on 64-bit. The arrays are copied during strata_set_terrain.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StrataTerrainTile {
        public fixed float origin[3];
        public float cell_size;
        public int samples_x;
        public int samples_z;
        public float* heights;   // samples_x * samples_z, x fastest
        public float* colors;    // null, or 3 floats per sample
        public fixed float color[4];
    }

    /// <summary>StrataMesh (ABI 1.4): 56 bytes on 64-bit. The arrays are copied during strata_add_mesh.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StrataMesh {
        public float* positions;  // vertex_count xyz, world space
        public float* normals;    // null (computed), or vertex_count xyz
        public float* colors;     // null (use color), or vertex_count linear RGB
        public uint* indices;     // index_count; counter-clockwise seen from outside
        public int vertex_count;
        public int index_count;
        public fixed float color[4];
    }

    /// <summary>StrataLineVertex (ABI 1.5): 28 bytes; pairs make segments.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StrataLineVertex {
        public fixed float position[3];
        public fixed float color[4];
    }

    /// <summary>StrataPoint (ABI 1.5): 40 bytes.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StrataPoint {
        public fixed float position[3];
        public float size_pixels;
        public float world_diameter;
        public fixed float color[4];
        public uint flags;
    }

    /// <summary>StrataText (ABI 1.5): 40 bytes on 64-bit. utf8 is copied during strata_submit_text.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StrataText {
        public float x, y;
        public float pixel_size;
        public uint flags;
        public fixed float color[4];
        public byte* utf8;  // NUL-terminated UTF-8
    }

    /// <summary>StrataSolarPosition (ABI 1.6): 32 bytes. to_sun is in Strata's world frame (+X east, +Y up, +Z south).</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StrataSolarPosition {
        public fixed float to_sun[3];
        public float elevation_degrees;
        public float azimuth_degrees;
        public float declination_degrees;
        public float hour_angle_degrees;
        public float equation_of_time_minutes;
    }

    /// <summary>StrataTexturedMesh (ABI 1.7): 72 bytes on 64-bit.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StrataTexturedMesh {
        public StrataMesh mesh;
        public float* uvs;  // null or vertex_count (u, v) pairs
        public uint texture;
        public uint flags;
    }

    /// <summary>StrataRayHit (ABI 1.8): 36 bytes. mesh_id 0 means nothing was hit.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StrataRayHit {
        public uint mesh_id;
        public int triangle;         // index in the mesh's index list / 3, or -1
        public float distance;       // along the normalised direction
        public fixed float point[3];  // world space
        public fixed float normal[3]; // unit, world space, facing the ray's origin
    }

    /// <summary>StrataRect (ABI 1.11): 32 bytes. A filled screen rectangle in pixels from the top left.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StrataRect {
        public float x, y;
        public float width, height;
        public fixed float color[4];
    }

    public static unsafe class StrataNative {
        public const string Library = "strata";

        /// <summary>
        /// Loads libstrata from a directory rather than the default probing
        /// path. Call before the first P/Invoke.
        /// </summary>
        public static void UseLibraryDirectory(string directory) {
            NativeLibrary.SetDllImportResolver(typeof(StrataNative).Assembly, (name, assembly, path) => {
                if (name != Library) return IntPtr.Zero;
                string file = OperatingSystem.IsWindows() ? "strata.dll"
                    : OperatingSystem.IsMacOS() ? "libstrata.dylib" : "libstrata.so";
                return NativeLibrary.Load(Path.Combine(directory, file));
            });
        }

        [DllImport(Library)] public static extern uint strata_abi_version();
        [DllImport(Library)] public static extern IntPtr strata_version_string();
        /// <summary>ABI 1.1. Absent from 1.0 libraries (EntryPointNotFoundException means minor 0).</summary>
        [DllImport(Library)] public static extern uint strata_abi_minor();
        [DllImport(Library)] public static extern int strata_struct_size(int structId);
        [DllImport(Library)] public static extern int strata_struct_field_offset(int structId, int fieldId);
        [DllImport(Library)] public static extern IntPtr strata_last_error();
        [DllImport(Library)] public static extern int strata_create(StrataEngineDesc* desc, IntPtr* engine);
        [DllImport(Library)] public static extern void strata_destroy(IntPtr engine);
        [DllImport(Library)] public static extern IntPtr strata_gpu_description(IntPtr engine);
        [DllImport(Library)] public static extern int strata_poll_event(IntPtr engine, StrataEvent* evt);
        [DllImport(Library)] public static extern int strata_get_drawable_size(IntPtr engine, int* width, int* height);
        [DllImport(Library)] public static extern int strata_resize(IntPtr engine, int width, int height);
        [DllImport(Library)] public static extern int strata_set_camera(IntPtr engine, StrataCamera* camera);
        [DllImport(Library)] public static extern int strata_lighting_defaults(int mode, StrataLighting* lighting);
        [DllImport(Library)] public static extern int strata_set_lighting(IntPtr engine, StrataLighting* lighting);
        /// <summary>ABI 1.6. Seconds since 1970-01-01 UTC; degrees, north and east positive. Needs no engine.</summary>
        [DllImport(Library)] public static extern int strata_solar_position(double unixSecondsUtc, double latitudeDegrees, double longitudeDegrees, StrataSolarPosition* position);
        /// <summary>ABI 1.6. toSun points at three floats; null goes back to the lighting's time of day and latitude.</summary>
        [DllImport(Library)] public static extern int strata_set_sun_direction(IntPtr engine, float* toSun);
        /// <summary>ABI 1.1.</summary>
        [DllImport(Library)] public static extern int strata_set_point_lights(IntPtr engine, StrataPointLight* lights, int count);
        /// <summary>ABI 1.3. tile null removes the terrain.</summary>
        [DllImport(Library)] public static extern int strata_set_terrain(IntPtr engine, StrataTerrainTile* tile);
        /// <summary>ABI 1.4. Writes the new mesh's id (never 0).</summary>
        [DllImport(Library)] public static extern int strata_add_mesh(IntPtr engine, StrataMesh* mesh, uint* id);
        /// <summary>ABI 1.4.</summary>
        [DllImport(Library)] public static extern int strata_remove_mesh(IntPtr engine, uint id);
        /// <summary>ABI 1.4.</summary>
        [DllImport(Library)] public static extern int strata_clear_meshes(IntPtr engine);
        /// <summary>ABI 1.7. rgba is width * height * 4 bytes, top row first. Writes the texture's id (never 0).</summary>
        [DllImport(Library)] public static extern int strata_create_texture_rgba8(IntPtr engine, int width, int height, byte* rgba, uint flags, uint* id);
        /// <summary>ABI 1.7.</summary>
        [DllImport(Library)] public static extern int strata_destroy_texture(IntPtr engine, uint id);
        /// <summary>ABI 1.7. Writes the new mesh's id (never 0).</summary>
        [DllImport(Library)] public static extern int strata_add_textured_mesh(IntPtr engine, StrataTexturedMesh* mesh, uint* id);
        /// <summary>ABI 1.7. model points at 16 floats, column-major; null restores the identity.</summary>
        [DllImport(Library)] public static extern int strata_set_mesh_transform(IntPtr engine, uint id, float* model);
        /// <summary>ABI 1.8. Replaces a width x height rectangle at (x, y) of a texture; rgba is width * height * 4 bytes, top row first.</summary>
        [DllImport(Library)] public static extern int strata_update_texture_rgba8(IntPtr engine, uint id, int x, int y, int width, int height, byte* rgba);
        /// <summary>ABI 1.8. origin and direction point at three floats each; writes the nearest mesh hit (mesh_id 0 for none).</summary>
        [DllImport(Library)] public static extern int strata_raycast_meshes(IntPtr engine, float* origin, float* direction, float maxDistance, StrataRayHit* hit);
        /// <summary>ABI 1.8. Window surface only; headless and host-context engines return STRATA_ERROR_UNSUPPORTED (7).</summary>
        [DllImport(Library)] public static extern int strata_set_relative_mouse_mode(IntPtr engine, int enabled);
        [DllImport(Library)] public static extern int strata_begin_frame(IntPtr engine, StrataFrameDesc* desc);
        /// <summary>items points at count 44-byte records with the StrataRenderItem layout.</summary>
        [DllImport(Library)] public static extern int strata_submit_primitives(IntPtr engine, void* items, int count);
        /// <summary>ABI 1.5. count is a number of vertices and must be even.</summary>
        [DllImport(Library)] public static extern int strata_submit_lines(IntPtr engine, StrataLineVertex* vertices, int count);
        /// <summary>ABI 1.5.</summary>
        [DllImport(Library)] public static extern int strata_submit_points(IntPtr engine, StrataPoint* points, int count);
        /// <summary>ABI 1.5.</summary>
        [DllImport(Library)] public static extern int strata_submit_text(IntPtr engine, StrataText* texts, int count);
        /// <summary>ABI 1.11. strata_submit_lines with STRATA_LINES_* flags (LinesOnTop skips the depth test).</summary>
        [DllImport(Library)] public static extern int strata_submit_lines_ex(IntPtr engine, StrataLineVertex* vertices, int count, uint flags);
        /// <summary>ABI 1.11. Filled screen rectangles, drawn before this frame's text.</summary>
        [DllImport(Library)] public static extern int strata_submit_rects(IntPtr engine, StrataRect* rects, int count);
        [DllImport(Library)] public static extern int strata_end_frame(IntPtr engine);
        [DllImport(Library)] public static extern int strata_present(IntPtr engine);
        [DllImport(Library)] public static extern int strata_get_frame_stats(IntPtr engine, StrataFrameStats* stats);
        [DllImport(Library)] public static extern int strata_read_pixels_rgba8(IntPtr engine, byte* dst, int capacity, int* width, int* height);
        /// <summary>ABI 1.9. Planar view distance per pixel, top row first; +infinity where nothing was drawn. Headless only.</summary>
        [DllImport(Library)] public static extern int strata_read_depth_f32(IntPtr engine, float* dst, int capacity, int* width, int* height);
        [DllImport(Library)] public static extern int strata_save_screenshot_png(IntPtr engine, byte* utf8Path);

        public static string LastError() => Marshal.PtrToStringUTF8(strata_last_error()) ?? "";

        /// <summary>Throws with strata_last_error() text when status is not OK.</summary>
        public static void Check(int status, string what) {
            if (status != StrataAbi.Ok) throw new InvalidOperationException($"{what} failed ({status}): {LastError()}");
        }
    }
}
