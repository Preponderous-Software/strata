// C# view of Strata's C ABI (include/strata/strata.h, ABI version 1).
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace Strata.Interop {

    public static class StrataAbi {
        public const uint Version = 1;

        public const int SurfaceWindow = 0, SurfaceHeadless = 1, SurfaceHostContext = 2;
        public const uint FlagVsync = 1u << 0, FlagDebugContext = 1u << 1, FlagHiddenWindow = 1u << 2, FlagGpuTiming = 1u << 3;
        public const int LightingLegacy = 0, LightingSun = 1;
        public const uint ClearColor = 1u << 0, ClearDepth = 1u << 1, ClearSky = 1u << 2;

        public const int StructRenderItem = 0, StructEngineDesc = 1, StructCamera = 2, StructLighting = 3,
            StructFrameDesc = 4, StructFrameStats = 5, StructEvent = 6;

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
        [DllImport(Library)] public static extern int strata_begin_frame(IntPtr engine, StrataFrameDesc* desc);
        /// <summary>items points at count 44-byte records with the StrataRenderItem layout.</summary>
        [DllImport(Library)] public static extern int strata_submit_primitives(IntPtr engine, void* items, int count);
        [DllImport(Library)] public static extern int strata_end_frame(IntPtr engine);
        [DllImport(Library)] public static extern int strata_present(IntPtr engine);
        [DllImport(Library)] public static extern int strata_get_frame_stats(IntPtr engine, StrataFrameStats* stats);
        [DllImport(Library)] public static extern int strata_read_pixels_rgba8(IntPtr engine, byte* dst, int capacity, int* width, int* height);
        [DllImport(Library)] public static extern int strata_save_screenshot_png(IntPtr engine, byte* utf8Path);

        public static string LastError() => Marshal.PtrToStringUTF8(strata_last_error()) ?? "";

        /// <summary>Throws with strata_last_error() text when status is not OK.</summary>
        public static void Check(int status, string what) {
            if (status != StrataAbi.Ok) throw new InvalidOperationException($"{what} failed ({status}): {LastError()}");
        }
    }
}
