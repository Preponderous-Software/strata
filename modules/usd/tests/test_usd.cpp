// strata::usd world I/O tests: layout, delta minimality, determinism, and
// hydrate's refusal of malformed input.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/primSpec.h>

#include "strata/usd/world_io.h"

using namespace strata;
using world::FieldType;
using world::World;

static int failures = 0;
#define CHECK_MSG(c, m)                                                                                \
    do {                                                                                               \
        if (!(c)) {                                                                                    \
            std::printf("  FAIL %s:%d: %s -- %s\n", __FILE__, __LINE__, #c, std::string(m).c_str()); \
            ++failures;                                                                                \
        }                                                                                              \
    } while (0)
#define CHECK(c) CHECK_MSG(c, "")

static std::string out;
static const std::string kContent = std::string(STRATA_USD_TEST_DATA) + "/prefabs.usda";

static World schema() {
    World w;
    w.grid.init(0, 0, 8, {"t:a", "t:b"});
    world::Table t;
    t.collection = "Things";
    t.idPrefix = "T_";
    t.fields = {{"t:i", FieldType::Int}, {"t:l", FieldType::Int64}, {"t:d", FieldType::Double}};
    t.initColumns();
    w.tables = {t};
    w.variants["/Prefabs/Thing|look"] = "";
    return w;
}

static World sample() {
    World w = schema();
    w.grid.init(20, 13, 8, w.grid.channels);  // ragged edge chunks
    for (size_t i = 0; i < w.grid.data[0].size(); ++i) {
        w.grid.data[0][i] = static_cast<int32_t>(i);
        w.grid.data[1][i] = -static_cast<int32_t>(i);
    }
    auto& t = w.tables[0];
    for (int k = 0; k < 5; ++k) {
        const size_t r = t.addRow(k * 10, "/Prefabs/Thing");
        t.ints[0][r] = k;
        t.ints[1][r] = int64_t(k) << 40;
        t.dbls[2][r] = k / 3.0;
    }
    w.globals["t:tick"] = 7;
    w.variants["/Prefabs/Thing|look"] = "plain";
    return w;
}

static World load(const std::vector<std::string>& stack, std::string* err) {
    World w = world::emptyLike(schema());
    auto st = usd::compose(stack, err);
    if (!st || !usd::hydrate(st, w, err)) w.globals["__failed"] = 1;
    return w;
}

static size_t primSpecs(const pxr::SdfLayerRefPtr& l) {
    size_t n = 0;
    l->Traverse(pxr::SdfPath::AbsoluteRootPath(), [&](const pxr::SdfPath& p) { n += p.IsPrimPath(); });
    return n;
}

int main(int argc, char** argv) {
    out = argc > 1 ? argv[1] : (std::filesystem::temp_directory_path() / "strata-usd-tests").u8string();
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::u8path(out), ec);
    std::filesystem::create_directories(std::filesystem::u8path(out), ec);
    std::printf("- full layer round trip, ragged chunks\n");
    const World base = sample();
    CHECK(usd::writeFull(base)->Export(out + "/base.usdc"));
    std::string err, why;
    World got = load({kContent, out + "/base.usdc"}, &err);
    CHECK_MSG(world::equal(got, base, &why), err + why);

    std::printf("- delta holds only what changed; deletions deactivate; variants flip\n");
    World cur = base;
    cur.tables[0].ints[0][1] = 99;          // one field
    cur.tables[0].removeRow(3);             // one deletion
    const size_t r = cur.tables[0].addRow(70, "/Prefabs/Thing");  // one new entity
    cur.tables[0].dbls[2][r] = -0.0;
    cur.grid.data[1][0] = 5;                // one chunk
    cur.globals["t:tick"] = 8;
    cur.variants["/Prefabs/Thing|look"] = "fancy";
    usd::Manifest m;
    m.gameId = "test";
    m.baseHash = std::string(64, '0');
    auto delta = usd::writeDelta(base, cur, m);
    // Root, World, Grid, one chunk, Globals, Entities, Things, 3 entities, Prefabs, Thing
    const size_t specs = primSpecs(delta);
    CHECK_MSG(specs <= 12, std::to_string(specs) + " prim specs in the delta");
    CHECK(delta->Export(out + "/save.usdc"));
    got = load({kContent, out + "/base.usdc", out + "/save.usdc"}, &err);
    CHECK_MSG(world::equal(got, cur, &why), err + why);
    usd::Manifest back;
    CHECK(usd::readManifest(pxr::SdfLayer::OpenAsAnonymous(out + "/save.usdc"), back, &err));
    CHECK(back.gameId == "test" && back.baseHash == m.baseHash && back.formatVersion == usd::kFormatVersion);

    std::printf("- deterministic bytes\n");
    CHECK(usd::writeDelta(base, cur, m)->Export(out + "/save2.usdc"));
    std::ifstream a(out + "/save.usdc", std::ios::binary), b(out + "/save2.usdc", std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(a), {}) == std::string(std::istreambuf_iterator<char>(b), {}));

    std::printf("- hydrate refuses malformed input\n");
    auto bad = [&](const char* name, const std::string& usda) {
        const std::string p = out + "/bad.usda";
        std::ofstream(p) << usda;
        load({kContent, out + "/base.usdc", p}, &err);
        World w = world::emptyLike(schema());
        auto st = usd::compose({kContent, out + "/base.usdc", p}, &err);
        const bool refused = !st || !usd::hydrate(st, w, &err);
        CHECK_MSG(refused, name);
    };
    bad("bad entity name", "#usda 1.0\nover \"World\" {\nover \"Entities\" {\nover \"Things\" {\ndef Xform \"T_12x\" {}\n}\n}\n}\n");
    bad("stray prim", "#usda 1.0\nover \"World\" {\nover \"Entities\" {\nover \"Things\" {\ndef Xform \"Q_000001\" {}\n}\n}\n}\n");
    bad("missing field", "#usda 1.0\nover \"World\" {\nover \"Entities\" {\nover \"Things\" {\ndef Xform \"T_000999\" {}\n}\n}\n}\n");
    bad("short chunk", "#usda 1.0\nover \"World\" {\nover \"Grid\" {\nover \"C_000_000\" {\nint[] t:a = [1, 2]\n}\n}\n}\n");
    bad("non-int64 global", "#usda 1.0\nover \"World\" {\nover \"Globals\" {\nstring t:extra = \"x\"\n}\n}\n");
    bad("unparseable", "#usda 1.0\nover \"World\" {\nthis is not usda\n");
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
