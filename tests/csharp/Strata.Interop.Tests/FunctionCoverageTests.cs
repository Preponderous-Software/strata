using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using Strata.Interop;
using Xunit;

namespace Strata.Interop.Tests {

    /// <summary>
    /// Every function in strata.h is declared in StrataNative (#83), and the
    /// ones that were missing work.
    /// </summary>
    public class FunctionCoverageTests : IClassFixture<NativeLibraryFixture> {
        public FunctionCoverageTests(NativeLibraryFixture fixture) { }

        private static string FindHeader() {
            for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent) {
                string header = Path.Combine(dir.FullName, "include", "strata", "strata.h");
                if (File.Exists(header)) return header;
            }
            throw new FileNotFoundException("include/strata/strata.h not found above " + AppContext.BaseDirectory);
        }

        private static HashSet<string> EntryPoints() {
            var names = new HashSet<string>();
            const BindingFlags all = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static;
            foreach (MethodInfo method in typeof(StrataNative).GetMethods(all)) {
                if ((method.Attributes & MethodAttributes.PinvokeImpl) == 0) continue;
                var import = method.GetCustomAttribute<DllImportAttribute>();
                names.Add(string.IsNullOrEmpty(import?.EntryPoint) ? method.Name : import.EntryPoint);
            }
            return names;
        }

        [Fact]
        public void EveryFunctionInStrataHHasAnEntryPoint() {
            string header = File.ReadAllText(FindHeader());
            var inHeader = Regex.Matches(header, @"STRATA_API [^(]*?\b(strata_[a-z0-9_]+)\(")
                .Select(m => m.Groups[1].Value).ToHashSet();
            Assert.True(inHeader.Count > 40, $"only {inHeader.Count} STRATA_API functions found in strata.h");
            var declared = EntryPoints();
            Assert.Empty(inHeader.Except(declared).OrderBy(n => n));
            Assert.Empty(declared.Except(inHeader).OrderBy(n => n));
        }

        [Fact]
        public void StatusStringNamesEachStatus() {
            Assert.Equal("ok", Marshal.PtrToStringUTF8(StrataNative.strata_status_string(StrataAbi.Ok)));
            Assert.Equal("invalid argument", Marshal.PtrToStringUTF8(StrataNative.strata_status_string(1)));
            Assert.Equal("unknown status", Marshal.PtrToStringUTF8(StrataNative.strata_status_string(12345)));
        }

        [Fact]
        public unsafe void CameraLookAtFillsTheCamera() {
            float* eye = stackalloc float[3] { 0, 0, 5 };
            float* target = stackalloc float[3] { 0, 0, 0 };
            float* up = stackalloc float[3] { 0, 1, 0 };
            var camera = new StrataCamera();
            Assert.Equal(StrataAbi.Ok, StrataNative.strata_camera_look_at(eye, target, up, 60, 16f / 9f, 0.1f, 100, &camera));
            Assert.Equal(5f, camera.eye[2]);
            Assert.Equal(0.1f, camera.near_plane);
            Assert.Equal(100f, camera.far_plane);
            Assert.Equal(-5f, camera.view[14], 4);  // the eye is 5 units back along +z
            Assert.Equal(1, StrataNative.strata_camera_look_at(eye, target, up, 60, 16f / 9f, 0.1f, 100, null));
        }

        [Fact]
        public unsafe void LogCallbackReceivesEngineMessagesAndIsKeptAlive() {
            var messages = new List<string>();
            StrataNative.strata_set_log_callback((level, message, user) => messages.Add(Marshal.PtrToStringUTF8(message)), IntPtr.Zero);
            try {
                Assert.NotNull(StrataNative.InstalledLogCallback);
                GC.Collect();
                GC.WaitForPendingFinalizers();
                var desc = new StrataEngineDesc {
                    struct_size = (uint) sizeof(StrataEngineDesc), abi_version = StrataAbi.Version,
                    surface = StrataAbi.SurfaceHeadless, width = 64, height = 64,
                };
                IntPtr engine;
                StrataNative.Check(StrataNative.strata_create(&desc, &engine), "strata_create");
                StrataNative.strata_destroy(engine);
                Assert.Contains(messages, m => m.Contains("headless surface"));
            } finally {
                StrataNative.strata_set_log_callback(null, IntPtr.Zero);
            }
            Assert.Null(StrataNative.InstalledLogCallback);
        }
    }
}
