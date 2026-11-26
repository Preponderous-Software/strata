using System.Runtime.CompilerServices;
using Strata.Interop;
using Xunit;

namespace Strata.Interop.Tests {

    /// <summary>The glTF module's C API (strata_gltf.h) as StrataGltfNative declares it.</summary>
    public class GltfTests : IClassFixture<NativeLibraryFixture> {
        public GltfTests(NativeLibraryFixture fixture) { }

        [Fact]
        public void StructSizesMatchStrataGltfH() {
            Assert.Equal(40, Unsafe.SizeOf<StrataGltfOptions>());
            Assert.Equal(80, Unsafe.SizeOf<StrataGltfInfo>());
            Assert.Equal(52, Unsafe.SizeOf<StrataGltfMeshInfo>());
        }

        [Fact]
        public void LibraryLoadsBesideLibstrataAndReportsItsVersion() {
            Assert.Equal(StrataGltfAbi.ApiVersion, StrataGltfNative.strata_gltf_api_version());
        }

        [Fact]
        public unsafe void LoadWithoutAnEngineIsAnInvalidArgument() {
            System.IntPtr scene = (System.IntPtr) 1;
            Assert.Equal(1, StrataGltfNative.strata_gltf_load(System.IntPtr.Zero, "missing.glb", null, &scene));
            Assert.Equal(System.IntPtr.Zero, scene);
            Assert.Contains("required", StrataGltfNative.LastError());
        }
    }
}
