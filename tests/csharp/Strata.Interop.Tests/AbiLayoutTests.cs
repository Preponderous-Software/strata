using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Strata.Interop;
using Xunit;

namespace Strata.Interop.Tests {

    public class NativeLibraryFixture {
        public NativeLibraryFixture() {
            string dir = Environment.GetEnvironmentVariable("STRATA_NATIVE_DIR");
            if (string.IsNullOrEmpty(dir)) throw new InvalidOperationException("set STRATA_NATIVE_DIR to the directory holding libstrata");
            StrataNative.UseLibraryDirectory(dir);
        }
    }

    public class AbiLayoutTests : IClassFixture<NativeLibraryFixture> {
        public AbiLayoutTests(NativeLibraryFixture fixture) { }

        private static void AssertLayout<T>(int structId, params string[] fields) where T : unmanaged {
            Assert.Equal(StrataNative.strata_struct_size(structId), Unsafe.SizeOf<T>());
            for (int i = 0; i < fields.Length; i++) {
                Assert.Equal(StrataNative.strata_struct_field_offset(structId, i), (int) Marshal.OffsetOf<T>(fields[i]));
            }
            Assert.Equal(-1, StrataNative.strata_struct_field_offset(structId, fields.Length));
        }

        [Fact]
        public void AbiVersionMatches() {
            Assert.Equal(StrataAbi.Version, StrataNative.strata_abi_version());
        }

        [Fact]
        public void AbiMinorIsAtLeastTheOneTheseDeclarationsCover() {
            Assert.True(StrataNative.strata_abi_minor() >= StrataAbi.Minor);
        }

        [Fact]
        public void PointLightIs36BytesAndMatchesNative() {
            Assert.Equal(36, Unsafe.SizeOf<StrataPointLight>());
            AssertLayout<StrataPointLight>(StrataAbi.StructPointLight, "position", "range", "color", "intensity", "flags");
        }

        [Fact]
        public void TerrainTileMatchesNative() {
            AssertLayout<StrataTerrainTile>(StrataAbi.StructTerrainTile, "origin", "cell_size", "samples_x", "samples_z",
                "heights", "colors", "color");
        }

        [Fact]
        public void MeshMatchesNative() {
            AssertLayout<StrataMesh>(StrataAbi.StructMesh, "positions", "normals", "colors", "indices",
                "vertex_count", "index_count", "color");
        }

        [Fact]
        public unsafe void MeshCallsRejectANullEngine() {
            var mesh = new StrataMesh();
            uint id = 0;
            Assert.Equal(1, StrataNative.strata_add_mesh(IntPtr.Zero, &mesh, &id));  // STRATA_ERROR_INVALID_ARGUMENT
            Assert.Equal(1, StrataNative.strata_remove_mesh(IntPtr.Zero, 1));
            Assert.Equal(1, StrataNative.strata_clear_meshes(IntPtr.Zero));
        }

        [Fact]
        public void OverlayStructsMatchNative() {
            Assert.Equal(28, Unsafe.SizeOf<StrataLineVertex>());
            AssertLayout<StrataLineVertex>(StrataAbi.StructLineVertex, "position", "color");
            Assert.Equal(40, Unsafe.SizeOf<StrataPoint>());
            AssertLayout<StrataPoint>(StrataAbi.StructPoint, "position", "size_pixels", "world_diameter", "color", "flags");
            AssertLayout<StrataText>(StrataAbi.StructText, "x", "y", "pixel_size", "flags", "color", "utf8");
        }

        [Fact]
        public unsafe void SolarPositionMatchesNative() {
            Assert.Equal(32, Unsafe.SizeOf<StrataSolarPosition>());
            AssertLayout<StrataSolarPosition>(StrataAbi.StructSolarPosition, "to_sun", "elevation_degrees", "azimuth_degrees",
                "declination_degrees", "hour_angle_degrees", "equation_of_time_minutes");
            // 2026-06-21 12:00 UTC on the prime meridian at 40 N: the June solstice noon sun stands about 73.4 degrees high.
            StrataSolarPosition p;
            Assert.Equal(0, StrataNative.strata_solar_position(1782043200.0, 40.0, 0.0, &p));
            Assert.InRange(p.elevation_degrees, 73.0f, 73.8f);
            Assert.Equal(1, StrataNative.strata_set_sun_direction(IntPtr.Zero, null));  // STRATA_ERROR_INVALID_ARGUMENT
        }

        [Fact]
        public unsafe void TexturedMeshMatchesNative() {
            if (IntPtr.Size == 8) Assert.Equal(72, Unsafe.SizeOf<StrataTexturedMesh>());
            AssertLayout<StrataTexturedMesh>(StrataAbi.StructTexturedMesh, "mesh", "uvs", "texture", "flags");
            byte* texel = stackalloc byte[4];
            uint id = 0;
            Assert.Equal(1, StrataNative.strata_create_texture_rgba8(IntPtr.Zero, 1, 1, texel, 0, &id));  // STRATA_ERROR_INVALID_ARGUMENT
            Assert.Equal(1, StrataNative.strata_destroy_texture(IntPtr.Zero, 1));
            var mesh = new StrataTexturedMesh();
            Assert.Equal(1, StrataNative.strata_add_textured_mesh(IntPtr.Zero, &mesh, &id));
            Assert.Equal(1, StrataNative.strata_set_mesh_transform(IntPtr.Zero, 1, null));
        }

        [Fact]
        public unsafe void RayHitMatchesNative() {
            Assert.Equal(36, Unsafe.SizeOf<StrataRayHit>());
            AssertLayout<StrataRayHit>(StrataAbi.StructRayHit, "mesh_id", "triangle", "distance", "point", "normal");
            Assert.Equal(0, (int) Marshal.OffsetOf<StrataRayHit>("mesh_id"));
            Assert.Equal(4, (int) Marshal.OffsetOf<StrataRayHit>("triangle"));
            Assert.Equal(8, (int) Marshal.OffsetOf<StrataRayHit>("distance"));
            Assert.Equal(12, (int) Marshal.OffsetOf<StrataRayHit>("point"));
            Assert.Equal(24, (int) Marshal.OffsetOf<StrataRayHit>("normal"));
            float* origin = stackalloc float[3];
            float* direction = stackalloc float[3];
            direction[2] = -1;
            StrataRayHit hit;
            Assert.Equal(1, StrataNative.strata_raycast_meshes(IntPtr.Zero, origin, direction, 10, &hit));  // STRATA_ERROR_INVALID_ARGUMENT
            byte* texel = stackalloc byte[4];
            Assert.Equal(1, StrataNative.strata_update_texture_rgba8(IntPtr.Zero, 1, 0, 0, 1, 1, texel));
            Assert.Equal(1, StrataNative.strata_set_relative_mouse_mode(IntPtr.Zero, 1));
        }

        [Fact]
        public unsafe void RectMatchesNativeAndAbi111CallsRejectANullEngine() {
            Assert.Equal(32, Unsafe.SizeOf<StrataRect>());
            AssertLayout<StrataRect>(StrataAbi.StructRect, "x", "y", "width", "height", "color");
            var rect = new StrataRect { width = 10, height = 10 };
            Assert.Equal(1, StrataNative.strata_submit_rects(IntPtr.Zero, &rect, 1));  // STRATA_ERROR_INVALID_ARGUMENT
            Assert.Equal(1, StrataNative.strata_submit_lines_ex(IntPtr.Zero, null, 0, StrataAbi.LinesOnTop));
        }

        [Fact]
        public unsafe void OverlaySubmitsRejectANullEngine() {
            var point = new StrataPoint { size_pixels = 2 };
            Assert.Equal(1, StrataNative.strata_submit_points(IntPtr.Zero, &point, 1));  // STRATA_ERROR_INVALID_ARGUMENT
            Assert.Equal(1, StrataNative.strata_submit_lines(IntPtr.Zero, null, 0));
            Assert.Equal(1, StrataNative.strata_submit_text(IntPtr.Zero, null, 0));
        }

        [Fact]
        public unsafe void SetTerrainRejectsANullEngine() {
            Assert.Equal(1, StrataNative.strata_set_terrain(IntPtr.Zero, null));  // STRATA_ERROR_INVALID_ARGUMENT
        }

        [Fact]
        public unsafe void SetPointLightsRejectsANullEngine() {
            var light = new StrataPointLight { range = 5, intensity = 1 };
            Assert.Equal(1, StrataNative.strata_set_point_lights(IntPtr.Zero, &light, 1));  // STRATA_ERROR_INVALID_ARGUMENT
        }

        [Fact]
        public void RenderItemIs44BytesAndMatchesNative() {
            Assert.Equal(44, Unsafe.SizeOf<StrataRenderItem>());
            AssertLayout<StrataRenderItem>(StrataAbi.StructRenderItem, "kind", "position", "scale", "color");
        }

        [Fact]
        public void EngineDescMatchesNative() {
            AssertLayout<StrataEngineDesc>(StrataAbi.StructEngineDesc, "struct_size", "abi_version", "surface", "width",
                "height", "flags", "title", "get_proc_address", "get_proc_address_user");
        }

        [Fact]
        public void CameraMatchesNative() {
            AssertLayout<StrataCamera>(StrataAbi.StructCamera, "view", "projection", "eye", "near_plane", "far_plane");
        }

        [Fact]
        public void LightingMatchesNative() {
            AssertLayout<StrataLighting>(StrataAbi.StructLighting, "mode", "legacy_direction", "legacy_ambient",
                "time_of_day", "latitude_degrees", "cascade_count", "shadow_map_size", "shadow_distance",
                "split_lambda", "fog");
        }

        [Fact]
        public void FrameDescAndStatsMatchNative() {
            AssertLayout<StrataFrameDesc>(StrataAbi.StructFrameDesc, "clear_flags", "clear_color");
            AssertLayout<StrataFrameStats>(StrataAbi.StructFrameStats, "draw_calls", "instances", "instances_per_kind",
                "items_rejected", "shadow_cascades", "cpu_ms_shadow", "cpu_ms_opaque", "cpu_ms_sky", "cpu_ms_frame",
                "gpu_finish_ms");
        }

        [Fact]
        public void EventMatchesNative() {
            AssertLayout<StrataEvent>(StrataAbi.StructEvent, "type", "scancode", "modifiers", "repeat", "button",
                "width", "height", "x", "y", "dx", "dy", "text");
        }

        [Fact]
        public unsafe void BadArgumentsReturnStatusInsteadOfCrashing() {
            IntPtr engine;
            Assert.Equal(1, StrataNative.strata_create(null, &engine));  // STRATA_ERROR_INVALID_ARGUMENT
            Assert.NotEqual("", StrataNative.LastError());
            var desc = new StrataEngineDesc { struct_size = (uint) sizeof(StrataEngineDesc), abi_version = 999 };
            Assert.Equal(2, StrataNative.strata_create(&desc, &engine));  // STRATA_ERROR_ABI_MISMATCH
        }
    }
}
