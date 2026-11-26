// C# view of the glTF module's C API (modules/gltf/include/strata/gltf/strata_gltf.h,
// API version 1): libstrata_gltf.so / strata_gltf.dll, which loads .gltf/.glb
// files onto a StrataEngine through libstrata's own C ABI.
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
using System;
using System.Runtime.InteropServices;

namespace Strata.Interop {

    public static class StrataGltfAbi {
        public const uint ApiVersion = 1;
        public const uint AlwaysMerge = 0x1u, NoBackFaces = 0x2u, NoMipmaps = 0x4u;
        public const int ColorsDisplay = 0, ColorsLinear = 1, ColorsAsStored = 2;
        public const int MaskBlend = 0, MaskOpaque = 1;
        public const int EmissiveUnlit = 0, EmissiveIgnore = 1;
        public const int AlphaOpaque = 0, AlphaMask = 1, AlphaBlend = 2;
    }

    /// <summary>StrataGltfOptions: 40 bytes. Zero fields take the defaults.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct StrataGltfOptions {
        public uint struct_size;
        public uint flags;
        public int scene;            // -1: the file's default scene
        public int max_meshes;       // 0: STRATA_MAX_MESHES
        public int max_textures;     // 0: STRATA_MAX_TEXTURES
        public int max_texture_size; // 0: STRATA_MAX_TEXTURE_SIZE
        public int atlas_size;       // 0: 4096
        public int color_policy;
        public int mask_policy;
        public int emissive_policy;
    }

    /// <summary>StrataGltfInfo: 80 bytes.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StrataGltfInfo {
        public int primitives_in, primitives_skipped, mesh_count, texture_count, material_count;
        public int merged, atlased, atlas_pages, warning_count, reserved;
        public long vertices, triangles;
        public fixed float bounds_min[3];
        public fixed float bounds_max[3];
    }

    /// <summary>StrataGltfMeshInfo: 52 bytes.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct StrataGltfMeshInfo {
        public uint mesh_id, texture_id, flags;
        public int material, alpha_mode;
        public float alpha_cutoff;
        public int double_sided, primitives, vertex_count, index_count;
        public fixed float emissive[3];
    }

    /// <summary>
    /// P/Invoke declarations for libstrata_gltf. StrataNative.UseLibraryDirectory
    /// resolves it from the same directory as libstrata.
    /// </summary>
    public static unsafe class StrataGltfNative {
        public const string Library = "strata_gltf";

        [DllImport(Library)] public static extern uint strata_gltf_api_version();
        [DllImport(Library)] public static extern IntPtr strata_gltf_last_error();
        [DllImport(Library)] public static extern int strata_gltf_load(IntPtr engine, [MarshalAs(UnmanagedType.LPUTF8Str)] string path,
                                                                       StrataGltfOptions* options, IntPtr* outScene);
        [DllImport(Library)] public static extern int strata_gltf_get_info(IntPtr scene, StrataGltfInfo* outInfo);
        [DllImport(Library)] public static extern int strata_gltf_get_mesh(IntPtr scene, int index, StrataGltfMeshInfo* outMesh);
        [DllImport(Library)] public static extern IntPtr strata_gltf_report(IntPtr scene);
        [DllImport(Library)] public static extern IntPtr strata_gltf_report_json(IntPtr scene);
        [DllImport(Library)] public static extern void strata_gltf_unload(IntPtr engine, IntPtr scene);

        public static string LastError() => Marshal.PtrToStringUTF8(strata_gltf_last_error());
    }
}
