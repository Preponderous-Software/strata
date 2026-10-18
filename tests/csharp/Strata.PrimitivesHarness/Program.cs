// Draws a Strata scene file from C#, the way Beyond Nations will.
//
//   Strata.PrimitivesHarness <scene-file> <out.png> [--native-dir DIR] [--surface headless|window]
//
// The items live in an array of BnRenderItem, a struct declared exactly like
// BN's RenderItem (an int enum, two System.Numerics.Vector3, an Rgba of four
// floats), and the camera is built with System.Numerics as BN's PlayerCamera
// builds it. Nothing is converted on the way across: Strata receives a
// pointer to the managed array.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using Strata.Interop;

namespace Strata.PrimitivesHarness {

    // Mirrors beyondnations.PrimitiveKind.
    public enum PrimitiveKind { Cube, Capsule, Cylinder, Sphere }

    // Mirrors beyondnations.Rgba.
    public readonly struct Rgba {
        public readonly float r, g, b, a;
        public Rgba(float r, float g, float b, float a) { this.r = r; this.g = g; this.b = b; this.a = a; }
    }

    // Mirrors beyondnations.RenderItem, with the explicit layout BN adds in B1.
    [StructLayout(LayoutKind.Sequential)]
    public struct BnRenderItem {
        public PrimitiveKind kind;
        public Vector3 position;
        public Vector3 scale;
        public Rgba color;
    }

    internal sealed class Scene {
        public int Width = 640, Height = 360;
        public Vector3 Eye = new Vector3(0, 10, 20), Target = Vector3.Zero;
        public float FovY = 60, Near = 0.1f, Far = 300;
        public StrataLighting Lighting;
        public uint ClearFlags = StrataAbi.ClearColor | StrataAbi.ClearDepth;
        public float[] ClearColor = { 0.45f, 0.65f, 0.90f, 1f };
        public readonly List<BnRenderItem> Items = new List<BnRenderItem>();

        public static unsafe Scene Load(string path) {
            var scene = new Scene();
            StrataLighting lighting;
            StrataNative.Check(StrataNative.strata_lighting_defaults(StrataAbi.LightingLegacy, &lighting), "strata_lighting_defaults");
            scene.Lighting = lighting;
            scene.Parse(path, 0);
            return scene;
        }

        private unsafe void Parse(string path, int depth) {
            if (depth > 8) throw new InvalidDataException(path + ": includes nest too deeply");
            int lineNumber = 0;
            foreach (string raw in File.ReadAllLines(path)) {
                lineNumber++;
                string line = raw;
                int hash = line.IndexOf('#');
                if (hash >= 0) line = line.Substring(0, hash);
                string[] w = line.Split((char[]) null, StringSplitOptions.RemoveEmptyEntries);
                if (w.Length == 0) continue;
                string where = path + ":" + lineNumber;
                float F(int i) => float.Parse(w[i], CultureInfo.InvariantCulture);
                int I(int i) => int.Parse(w[i], CultureInfo.InvariantCulture);
                switch (w[0]) {
                    case "size": Width = I(1); Height = I(2); break;
                    case "camera":
                        Eye = new Vector3(F(1), F(2), F(3));
                        Target = new Vector3(F(4), F(5), F(6));
                        FovY = F(7); Near = F(8); Far = F(9);
                        break;
                    case "lighting":
                        if (w[1] == "legacy") {
                            Lighting.mode = StrataAbi.LightingLegacy;
                            Lighting.legacy_direction[0] = F(2);
                            Lighting.legacy_direction[1] = F(3);
                            Lighting.legacy_direction[2] = F(4);
                            Lighting.legacy_ambient = F(5);
                        } else if (w[1] == "sun") {
                            Lighting.mode = StrataAbi.LightingSun;
                            Lighting.time_of_day = F(2); Lighting.latitude_degrees = F(3);
                            Lighting.cascade_count = I(4); Lighting.shadow_map_size = I(5);
                            Lighting.shadow_distance = F(6); Lighting.split_lambda = F(7); Lighting.fog = I(8);
                        } else throw new InvalidDataException(where + ": unknown lighting mode " + w[1]);
                        break;
                    case "clear":
                        ClearColor = new[] { F(1), F(2), F(3), F(4) };
                        ClearFlags = StrataAbi.ClearColor | StrataAbi.ClearDepth | (w.Length > 5 && w[5] == "sky" ? StrataAbi.ClearSky : 0u);
                        break;
                    case "include":
                        Parse(Path.Combine(Path.GetDirectoryName(path) ?? ".", w[1]), depth + 1);
                        break;
                    case "item":
                        PrimitiveKind kind = w[1] switch {
                            "cube" => PrimitiveKind.Cube, "capsule" => PrimitiveKind.Capsule,
                            "cylinder" => PrimitiveKind.Cylinder, "sphere" => PrimitiveKind.Sphere,
                            _ => throw new InvalidDataException(where + ": unknown primitive " + w[1]),
                        };
                        Items.Add(new BnRenderItem {
                            kind = kind,
                            position = new Vector3(F(2), F(3), F(4)),
                            scale = new Vector3(F(5), F(6), F(7)),
                            color = new Rgba(F(8), F(9), F(10), w.Length > 11 ? F(11) : 1f),
                        });
                        break;
                    default: throw new InvalidDataException(where + ": unknown directive " + w[0]);
                }
            }
        }
    }

    internal static unsafe class Program {
        private static int Main(string[] args) {
            if (args.Length < 2) {
                Console.Error.WriteLine("usage: Strata.PrimitivesHarness <scene> <out.png> [--native-dir DIR] [--surface headless|window]");
                return 2;
            }
            int surface = StrataAbi.SurfaceHeadless;
            for (int i = 2; i < args.Length; i++) {
                if (args[i] == "--native-dir" && i + 1 < args.Length) StrataNative.UseLibraryDirectory(Path.GetFullPath(args[++i]));
                else if (args[i] == "--surface" && i + 1 < args.Length) surface = args[++i] == "window" ? StrataAbi.SurfaceWindow : StrataAbi.SurfaceHeadless;
                else { Console.Error.WriteLine("unknown argument " + args[i]); return 2; }
            }

            // The whole point: BN's struct and Strata's must agree byte for byte.
            int managed = Unsafe.SizeOf<BnRenderItem>(), native = StrataNative.strata_struct_size(StrataAbi.StructRenderItem);
            string[] fields = { "kind", "position", "scale", "color" };
            for (int f = 0; f < fields.Length; f++) {
                int m = (int) Marshal.OffsetOf<BnRenderItem>(fields[f]), n = StrataNative.strata_struct_field_offset(StrataAbi.StructRenderItem, f);
                if (m != n) { Console.Error.WriteLine($"layout mismatch: {fields[f]} at {m} managed, {n} native"); return 3; }
            }
            if (managed != native) { Console.Error.WriteLine($"layout mismatch: {managed} B managed, {native} B native"); return 3; }
            Console.WriteLine($"Strata {Marshal.PtrToStringUTF8(StrataNative.strata_version_string())}, ABI {StrataNative.strata_abi_version()}; RenderItem {managed} B on both sides");

            Scene scene = Scene.Load(args[0]);
            IntPtr title = Marshal.StringToCoTaskMemUTF8("Strata C# primitives harness");
            var desc = new StrataEngineDesc {
                struct_size = (uint) sizeof(StrataEngineDesc), abi_version = StrataAbi.Version, surface = surface,
                width = scene.Width, height = scene.Height, title = title,
                flags = surface == StrataAbi.SurfaceWindow ? StrataAbi.FlagHiddenWindow : 0u,
            };
            IntPtr engine;
            StrataNative.Check(StrataNative.strata_create(&desc, &engine), "strata_create");
            Marshal.FreeCoTaskMem(title);
            try {
                Console.WriteLine("GPU: " + Marshal.PtrToStringUTF8(StrataNative.strata_gpu_description(engine)));
                int width, height;
                StrataNative.Check(StrataNative.strata_get_drawable_size(engine, &width, &height), "strata_get_drawable_size");

                // As BN's PlayerCamera builds it: System.Numerics, right-handed,
                // uploaded untransposed (its rows are GL's columns).
                Matrix4x4 view = Matrix4x4.CreateLookAt(scene.Eye, scene.Target, Vector3.UnitY);
                Matrix4x4 projection = Matrix4x4.CreatePerspectiveFieldOfView(
                    scene.FovY * MathF.PI / 180f, width / (float) height, scene.Near, scene.Far);
                var camera = new StrataCamera { near_plane = scene.Near, far_plane = scene.Far };
                Buffer.MemoryCopy(&view, camera.view, 64, 64);
                Buffer.MemoryCopy(&projection, camera.projection, 64, 64);
                camera.eye[0] = scene.Eye.X; camera.eye[1] = scene.Eye.Y; camera.eye[2] = scene.Eye.Z;
                StrataNative.Check(StrataNative.strata_set_camera(engine, &camera), "strata_set_camera");
                StrataLighting lighting = scene.Lighting;
                StrataNative.Check(StrataNative.strata_set_lighting(engine, &lighting), "strata_set_lighting");

                var frame = new StrataFrameDesc { clear_flags = scene.ClearFlags };
                for (int i = 0; i < 4; i++) frame.clear_color[i] = scene.ClearColor[i];
                BnRenderItem[] items = scene.Items.ToArray();
                for (int pass = 0; pass < 2; pass++) {
                    StrataNative.Check(StrataNative.strata_begin_frame(engine, &frame), "strata_begin_frame");
                    fixed (BnRenderItem* p = items) {
                        StrataNative.Check(StrataNative.strata_submit_primitives(engine, p, items.Length), "strata_submit_primitives");
                    }
                    StrataNative.Check(StrataNative.strata_end_frame(engine), "strata_end_frame");
                }
                StrataFrameStats stats;
                StrataNative.Check(StrataNative.strata_get_frame_stats(engine, &stats), "strata_get_frame_stats");
                Console.WriteLine($"{stats.instances} instances (cube {stats.instances_per_kind[0]}, capsule {stats.instances_per_kind[1]}, " +
                    $"cylinder {stats.instances_per_kind[2]}, sphere {stats.instances_per_kind[3]}), {stats.draw_calls} draws, {stats.items_rejected} rejected");

                byte[] path = Encoding.UTF8.GetBytes(Path.GetFullPath(args[1]) + "\0");
                fixed (byte* p = path) StrataNative.Check(StrataNative.strata_save_screenshot_png(engine, p), "strata_save_screenshot_png");
                Console.WriteLine("wrote " + args[1]);
            } finally {
                StrataNative.strata_destroy(engine);
            }
            return 0;
        }
    }
}
