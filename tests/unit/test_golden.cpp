// strata-golden's core with fakes: the check and render policies and their
// exact report text, with no GPU and no files. The file adapters are checked
// against the same contracts on a temporary directory at the end.
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>

#include "adapters/directory_scene_source.hpp"
#include "adapters/png_file_sink.hpp"
#include "adapters/png_golden_directory.hpp"
#include "fakes/golden/fake_artifact_sink.hpp"
#include "fakes/golden/fake_console.hpp"
#include "fakes/golden/fake_golden_store.hpp"
#include "fakes/golden/fake_scene_renderer.hpp"
#include "fakes/golden/fake_scene_source.hpp"
#include "golden.hpp"
#include "test.hpp"

using namespace strata::golden;
namespace fs = std::filesystem;

namespace {

strata::scene::Scene sceneOfWidth(int width) {
    strata::scene::Scene s;
    s.width = width;
    s.height = 2;
    return s;
}

strata::image::Image solid(int width, int height, uint8_t grey) {
    strata::image::Image img;
    img.width = width;
    img.height = height;
    img.rgba.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, grey);
    for (size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
    return img;
}

struct Rig {
    FakeSceneSource scenes;
    FakeSceneRenderer renderer;
    FakeGoldenStore goldens;
    FakeArtifactSink artifacts;
    FakeConsole console;
    CheckOptions options;
    int check() { return runCheck(options, scenes, renderer, goldens, artifacts, console); }
};

}  // namespace

TEST(golden_check_reports_pass_fail_size_and_missing_goldens) {
    Rig r;
    r.scenes.scenes["a_pass"] = sceneOfWidth(4);
    r.scenes.scenes["b_fail"] = sceneOfWidth(4);
    r.scenes.scenes["c_size"] = sceneOfWidth(4);
    r.scenes.scenes["d_missing"] = sceneOfWidth(4);
    r.scenes.scenes["_include"] = sceneOfWidth(4);
    r.goldens.images["a_pass"] = solid(4, 2, 128);
    r.goldens.images["b_fail"] = solid(4, 2, 0);
    r.goldens.images["c_size"] = solid(3, 2, 128);
    CHECK(r.check() == 1);
    CHECK(r.artifacts.prepared == 1);
    CHECK(r.console.text ==
          "a_pass\n"
          "  pass: 0 of 8 pixels over Delta E 4.0 (0.0000%, limit 0.5000%), max 0.00, mean 0.0000\n"
          "b_fail\n"
          "  FAIL: 8 of 8 pixels over Delta E 4.0 (100.0000%, limit 0.5000%), max 53.59, mean 53.5850\n"
          "  wrote out/b_fail.actual.png and its diff\n"
          "c_size\n"
          "  FAIL: size 4x2, golden is 3x2\n"
          "  wrote out/c_size.actual.png and its diff\n"
          "d_missing\n"
          "!  FAIL: no golden at golden/d_missing.png (no such file)\n"
          "GPU: fake GPU\n4 scene(s), 3 failure(s)\n");
    CHECK(r.artifacts.files.count("b_fail.actual.png") == 1);
    CHECK(r.artifacts.files.count("b_fail.diff.png") == 1);
    CHECK(r.artifacts.files.count("d_missing.actual.png") == 1);
    CHECK(r.artifacts.files.count("d_missing.diff.png") == 0);
    CHECK(r.artifacts.files.count("a_pass.actual.png") == 0);
}

TEST(golden_check_thresholds_come_from_the_options) {
    Rig r;
    r.scenes.scenes["s"] = sceneOfWidth(4);
    r.goldens.images["s"] = solid(4, 2, 0);
    r.options.compare.maxFraction = 1.0;
    CHECK(r.check() == 0);
    CHECK(r.console.text ==
          "s\n"
          "  pass: 8 of 8 pixels over Delta E 4.0 (100.0000%, limit 100.0000%), max 53.59, mean 53.5850\n"
          "GPU: fake GPU\n1 scene(s), 0 failure(s)\n");
}

TEST(golden_check_bless_writes_goldens_and_reports_refusals) {
    Rig r;
    r.scenes.scenes["a"] = sceneOfWidth(4);
    r.scenes.scenes["b"] = sceneOfWidth(4);
    r.goldens.unwritable.insert("b");
    r.options.bless = true;
    CHECK(r.check() == 1);
    CHECK(r.goldens.images.count("a") == 1);
    CHECK(r.artifacts.prepared == 1);  // the out dir is made even when blessing
    CHECK(r.artifacts.files.empty());
    CHECK(r.console.text ==
          "a\n  blessed golden/a.png\n"
          "b\n!  could not write golden/b.png: read-only\n"
          "GPU: fake GPU\n2 scene(s), 1 failure(s)\n");
}

TEST(golden_check_counts_load_and_render_failures_and_keeps_the_last_gpu) {
    Rig r;
    r.scenes.broken["a_broken"] = "a_broken.scene:2: unknown directive 'this'";
    r.scenes.scenes["b_ok"] = sceneOfWidth(4);
    r.scenes.scenes["c_no_engine"] = sceneOfWidth(7);
    r.goldens.images["b_ok"] = solid(4, 2, 128);
    r.renderer.noEngineWidths.push_back(7);
    CHECK(r.check() == 1);
    CHECK(r.console.text ==
          "a_broken\n!  a_broken.scene:2: unknown directive 'this'\n"
          "b_ok\n"
          "  pass: 0 of 8 pixels over Delta E 4.0 (0.0000%, limit 0.5000%), max 0.00, mean 0.0000\n"
          "c_no_engine\n"
          "GPU: fake GPU\n3 scene(s), 2 failure(s)\n");
}

TEST(golden_check_with_no_engine_at_all_reports_an_empty_gpu) {
    Rig r;
    r.scenes.scenes["a"] = sceneOfWidth(7);
    r.renderer.noEngineWidths.push_back(7);
    CHECK(r.check() == 1);
    CHECK(r.console.text == "a\nGPU: \n1 scene(s), 1 failure(s)\n");
}

TEST(golden_check_only_and_no_scenes) {
    Rig r;
    r.scenes.scenes["a"] = sceneOfWidth(4);
    r.scenes.scenes["b"] = sceneOfWidth(4);
    r.goldens.images["b"] = solid(4, 2, 128);
    r.options.only = "b";
    CHECK(r.check() == 0);
    CHECK(r.renderer.renders == 1);
    Rig none;
    none.options.only = "zzz";
    none.scenes.scenes["a"] = sceneOfWidth(4);
    CHECK(none.check() == 1);
    CHECK(none.console.text == "!no scenes found in mem:\n");
    CHECK(none.artifacts.prepared == 0);  // nothing is made before scenes are found
    CHECK(none.renderer.renders == 0);
}

TEST(golden_render_writes_the_image_or_says_why_not) {
    FakeSceneSource scenes;
    scenes.scenes["s"] = sceneOfWidth(4);
    scenes.broken["bad"] = "bad.scene:1: oops";
    FakeSceneRenderer renderer;
    FakeArtifactSink artifacts;
    {
        FakeConsole console;
        CHECK(runRender("s", "x.png", STRATA_SURFACE_HEADLESS, scenes, renderer, artifacts, console) == 0);
        CHECK(console.text == "s\n  GPU: fake GPU\n  wrote out/x.png\n");
        CHECK(artifacts.files.count("x.png") == 1);
        CHECK(artifacts.prepared == 0);
    }
    {
        FakeConsole console;
        CHECK(runRender("bad", "x.png", STRATA_SURFACE_HEADLESS, scenes, renderer, artifacts, console) == 1);
        CHECK(console.text == "!bad.scene:1: oops\n");  // nothing is printed before the scene loads
    }
    {
        FakeConsole console;
        artifacts.failWrites = true;
        CHECK(runRender("s", "x.png", STRATA_SURFACE_HEADLESS, scenes, renderer, artifacts, console) == 1);
        CHECK(console.text == "s\n  GPU: fake GPU\n!could not write out/x.png: disk full\n");
    }
    {
        FakeConsole console;
        renderer.failWidths.push_back(4);
        CHECK(runRender("s", "x.png", STRATA_SURFACE_HEADLESS, scenes, renderer, artifacts, console) == 1);
        CHECK(console.text == "s\n");
    }
}

// ---- the file adapters meet the same contracts as the fakes

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("strata-golden-test-" + std::to_string(std::random_device{}()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void write(const fs::path& p, const std::string& text) {
    std::FILE* f = std::fopen(p.string().c_str(), "wb");
    if (!f) return;
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

}  // namespace

TEST(directory_scene_source_lists_tests_sorted_and_skips_includes) {
    TempDir t;
    write(t.path / "b.scene", "size 8 4\n");
    write(t.path / "a.scene", "size 4 2\n");
    write(t.path / "_inc.scene", "size 4 2\n");
    write(t.path / "notes.txt", "x");
    write(t.path / "bad.scene", "size 4 2\nfrobnicate\n");
    DirectorySceneSource source(t.path.string());
    const auto all = source.list("");
    REQUIRE(all.size() == 3);
    CHECK(all[0].name == "a");
    CHECK(all[1].name == "b");
    CHECK(all[2].name == "bad");
    CHECK(source.list("b").size() == 1);
    CHECK(source.list("_inc").empty());
    CHECK(source.location() == t.path.string());
    const Loaded b = source.load(all[1].ref);
    CHECK(b.ok);
    CHECK(b.scene.width == 8);
    const Loaded bad = source.load(all[2].ref);
    CHECK(!bad.ok);
    CHECK(!bad.error.empty());
    bool threw = false;
    try {
        DirectorySceneSource(t.path.string() + "/missing").list("");
    } catch (const fs::filesystem_error&) {
        threw = true;
    }
    CHECK(threw);  // an unreadable directory still stops the tool
}

TEST(png_golden_directory_and_file_sink_round_trip) {
    TempDir t;
    PngGoldenDirectory goldens((t.path / "golden").string(), "linux-llvmpipe");
    CHECK(goldens.locate("s") == (t.path / "golden" / "s" / "linux-llvmpipe.png").string());
    CHECK(!goldens.read("s").ok);
    CHECK(!goldens.read("s").error.empty());
    CHECK(goldens.write("s", solid(4, 2, 77)).ok);  // makes golden/s/
    const ImageRead back = goldens.read("s");
    REQUIRE(back.ok);
    CHECK(back.image.width == 4);
    CHECK(back.image.rgba[0] == 77);

    PngFileSink sink((t.path / "out" / "deep").string());
    sink.prepare();
    CHECK(fs::is_directory(t.path / "out" / "deep"));
    CHECK(sink.write("x.png", solid(2, 2, 1)).ok);
    CHECK(fs::exists(t.path / "out" / "deep" / "x.png"));
    PngFileSink bare("");
    CHECK(bare.locate("some/where.png") == "some/where.png");  // render's <out.png>, as given
    const ImageWritten nowhere = bare.write((t.path / "no" / "dir" / "x.png").string(), solid(2, 2, 1));
    CHECK(!nowhere.ok);
    CHECK(!nowhere.error.empty());
}
