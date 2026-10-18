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
