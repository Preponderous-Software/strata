// Save-core tests (RFC 0019 §6). Every guarantee in slot.h has a test here.
//
//   strata-save-tests <scratch dir>           fast suite
//   strata-save-tests <scratch dir> --crash   single-bit-flip probe + 25 x kill -9
//
// Corruption probes run in forked children capped at 4 GB of address space:
// without the checksum a flipped size field can make the USD crate reader try
// to allocate ~150 GB (the spike saw a 22 GB OOM kill). Never run them uncapped.
#include <cinttypes>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#ifndef _WIN32
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <pxr/base/vt/dictionary.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>

#include "check.h"
#include "strata/save/sha256.h"
#include "strata/save/slot.h"
#include "toy_game.h"

using namespace strata;
using strata::save::Crash;
using strata::save::Slot;
using strata::world::World;

namespace {

std::string root;

std::string readAll(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
void writeAll(const std::string& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}
std::string freshDir(const std::string& name) {
    const std::string d = root + "/" + name;
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::u8path(d), ec);
    std::filesystem::create_directories(std::filesystem::u8path(d), ec);
    return d;
}
std::vector<std::string> saveFiles(const std::string& dir) {
    std::vector<std::string> out;
    for (const auto& n : save::fs::list(dir))
        if (n.compare(0, 5, "save-") == 0) out.push_back(n);
    return out;
}
bool hasTemps(const std::string& dir) {
    for (const auto& n : save::fs::list(dir))
        if (n.compare(0, 5, ".tmp-") == 0 || n == "HEAD.tmp") return true;
    return false;
}
std::string headSave(const std::string& dir) {
    save::Head h;
    if (!save::parseHead(readAll(dir + "/HEAD"), h, nullptr)) return "";
    return h.saveFile;
}

std::unique_ptr<Slot> openSlot(const std::string& dir, int version = 2, save::Layers layers = toy::layers()) {
    std::string err;
    auto s = Slot::open(dir, toy::saveSchema(version), std::move(layers), &err);
    if (!s) std::printf("  open failed: %s\n", err.c_str());
    return s;
}

// The state a toy colony reaches after `ticks` steps from seed 42.
World replay(int ticks, int version = 2, int agents = 40) {
    World w = toy::generate(42, 40, 40, agents, version);
    for (int t = 0; t < ticks; ++t) toy::step(w);
    return w;
}

// New game in `dir`: create + load. Returns the slot; `w` gets the live state.
std::unique_ptr<Slot> newGame(const std::string& dir, World& w, int version = 2, save::Layers l = toy::layers()) {
    auto s = openSlot(dir, version, l);
    if (!s) std::exit(1);
    std::string err;
    w = toy::generate(42, 40, 40, 40, version);
    if (!s->create(w, &err)) {
        std::printf("  create failed: %s\n", err.c_str());
        std::exit(1);
    }
    auto r = s->load();
    if (!r.ok) {
        std::printf("  load after create failed: %s\n", r.error.c_str());
        std::exit(1);
    }
    std::string why;
    CHECK_MSG(world::equal(r.current, w, &why), "base round trip: " + why);
    return s;
}

int64_t tickOf(const World& w) { return w.globals.at("toy:tick"); }

// ---------------------------------------------------------------------------

void testSha256() {
    SECTION("SHA-256 matches the FIPS 180-4 test vectors");
    CHECK(save::sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(save::sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(save::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(save::sha256Hex(std::string(1000000, 'a')) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    std::string msg;
    for (int i = 0; i < 200; ++i) msg += static_cast<char>(i * 7 + 3);
    const std::string whole = save::sha256Hex(msg);
    bool allSame = true;
    for (size_t split = 0; split <= msg.size(); ++split) {
        save::Sha256 h;
        h.update(msg.data(), split);
        h.update(msg.data() + split, msg.size() - split);
        allSame &= save::toHex(h.finish()) == whole;
    }
    CHECK_MSG(allSame, "incremental updates must match one-shot hashing at every split");
    const std::string p = freshDir("sha") + "/f.bin";
    writeAll(p, msg);
    std::string hex;
    uint64_t size = 0;
    CHECK(save::sha256File(p, hex, &size) && hex == whole && size == msg.size());
    CHECK(!save::sha256File(p + ".missing", hex));
}

void testHead() {
    SECTION("HEAD: round trip, and every truncation or bit flip is rejected");
    save::Head h;
    h.baseFile = "base-" + std::string(64, 'a') + ".usdc";
    h.baseSha = std::string(64, 'a');
    h.baseSize = 1234;
    h.saveFile = "save-000000000042-" + std::string(64, 'b') + ".usdc";
    h.saveSha = std::string(64, 'b');
    h.saveSize = 99;
    h.history = {{h.saveFile, h.baseSha}, {"save-000000000041-" + std::string(64, 'c') + ".usdc", h.baseSha}};
    const std::string text = save::formatHead(h);
    save::Head back;
    std::string err;
    CHECK_MSG(save::parseHead(text, back, &err), err);
    CHECK(save::formatHead(back) == text);
    int accepted = 0;
    for (size_t cut = 0; cut < text.size(); ++cut) accepted += save::parseHead(text.substr(0, cut), back, nullptr);
    CHECK_MSG(accepted == 0, std::to_string(accepted) + " truncations accepted");
    accepted = 0;
    for (size_t i = 0; i < text.size(); ++i)
        for (int b = 0; b < 8; ++b) {
            std::string t = text;
            t[i] = static_cast<char>(t[i] ^ (1 << b));
            accepted += save::parseHead(t, back, nullptr);
        }
    CHECK_MSG(accepted == 0, std::to_string(accepted) + " single-bit flips accepted");
    save::Head never;
    never.baseFile = h.baseFile;
    never.baseSha = h.baseSha;
    CHECK(save::parseHead(save::formatHead(never), back, &err) && back.saveFile.empty());
}

void testRoundTrip() {
    SECTION("exact round trip (special doubles, int64 extremes, deletions), deterministic bytes");
    const std::string dir = freshDir("roundtrip");
    World w;
    auto slot = newGame(dir, w);
    for (int t = 0; t < 120; ++t) toy::step(w);
    auto& A = w.table("Agents");
    const double specials[] = {-0.0, std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::max(),
                               std::numeric_limits<double>::lowest(), 0.1, 1.0 / 3.0};
    for (size_t i = 0; i < sizeof specials / sizeof specials[0]; ++i) A.dbls[2][i] = specials[i];
    uint64_t nanBits = 0x7ff8000000001234ULL;  // a quiet NaN with a payload
    std::memcpy(&A.dbls[2][9], &nanBits, sizeof nanBits);
    A.ints[3][0] = std::numeric_limits<int64_t>::min();
    A.ints[3][1] = std::numeric_limits<int64_t>::max();
    A.ints[0][2] = std::numeric_limits<int32_t>::min();
    A.ints[1][2] = std::numeric_limits<int32_t>::max();
    w.globals["toy:big"] = std::numeric_limits<int64_t>::min();
    auto r1 = slot->save(w, tickOf(w));
    CHECK_MSG(r1.ok, r1.error);
    slot.reset();  // a restart: nothing in memory survives
    slot = openSlot(dir);
    auto loaded = slot->load();
    CHECK_MSG(loaded.ok, loaded.error);
    std::string why;
    CHECK_MSG(world::equal(loaded.current, w, &why), why);
    CHECK(!loaded.usedFallback && loaded.rejected.empty() && loaded.saveFile == r1.file);
    CHECK(loaded.manifest.tick == 120 && loaded.manifest.gameId == toy::kGameId);
    uint64_t gotNan = 0;
    std::memcpy(&gotNan, &loaded.current.table("Agents").dbls[2][9], sizeof gotNan);
    CHECK_MSG(gotNan == nanBits, "NaN payload must survive bit-exactly");
    // The reloaded game keeps simulating identically.
    World a = w, b = loaded.current;
    for (int t = 0; t < 100; ++t) {
        toy::step(a);
        toy::step(b);
    }
    CHECK_MSG(world::equal(a, b, &why), "after 100 more ticks: " + why);
    // Same state -> byte-identical save -> the same content-addressed name.
    auto r2 = slot->save(b, tickOf(b));
    auto r3 = slot->save(b, tickOf(b));
    CHECK(r2.ok && r3.ok && r2.file == r3.file && r2.sha256 == r3.sha256);
    // Save of a save: the newest is loaded.
    slot.reset();
    slot = openSlot(dir);
    loaded = slot->load();
    CHECK_MSG(loaded.ok && world::equal(loaded.current, b, &why), loaded.error + why);
    std::printf("  base %s bytes, save %" PRIu64 " bytes\n",
                std::to_string(readAll(dir + "/" + save::fs::list(dir)[0]).size()).c_str(), r2.bytes);
}

void testUnsupportedChangesRefused() {
    SECTION("unsupported changes are refused, leaving the slot untouched");
    const std::string dir = freshDir("refuse");
    World w;
    auto slot = newGame(dir, w);
    auto ok = slot->save(w, 0);
    CHECK(ok.ok);
    const std::string head = readAll(dir + "/HEAD");
    const auto files = save::fs::list(dir);
    World big = w;
    big.table("Agents").ints[0][0] = int64_t(1) << 40;  // does not fit an Int field
    auto r = slot->save(big, 1);
    CHECK_MSG(!r.ok && r.error.find("does not fit") != std::string::npos, r.error);
    World reshaped = w;
    reshaped.grid.init(41, 40, 16, reshaped.grid.channels);
    r = slot->save(reshaped, 1);
    CHECK_MSG(!r.ok && r.error.find("grid shape") != std::string::npos, r.error);
    World prefab = w;
    prefab.table("Agents").prefab[0] = "/Prefabs/Prop";
    r = slot->save(prefab, 1);
    CHECK_MSG(!r.ok && r.error.find("prefab") != std::string::npos, r.error);
    CHECK(readAll(dir + "/HEAD") == head && save::fs::list(dir) == files);
    // A slot that already holds a game cannot be re-created over.
    std::string err;
    CHECK(!slot->create(w, &err) && err.find("refusing") != std::string::npos);
}

void testCrashPoints() {
    SECTION("a crash at every protocol step leaves the previous whole save");
    const Crash points[] = {Crash::AfterTempWrite, Crash::AfterVerify, Crash::AfterRename, Crash::AfterHeadTemp,
                            Crash::AfterPrevLink};
    const char* names[] = {"after temp write", "after verify", "after rename", "after HEAD.tmp", "after HEAD.prev link"};
    for (size_t k = 0; k < 5; ++k) {
        const std::string dir = freshDir("crash" + std::to_string(k));
        World w;
        auto slot = newGame(dir, w);
        for (int t = 0; t < 50; ++t) toy::step(w);
        CHECK(slot->save(w, 50).ok);
        const World atA = w;
        for (int t = 0; t < 25; ++t) toy::step(w);
        save::SaveOptions o;
        o.crashAt = points[k];
        auto r = slot->save(w, 75, o);
        CHECK_MSG(!r.ok, names[k]);
        slot.reset();  // the process dies here
        slot = openSlot(dir);
        CHECK_MSG(!hasTemps(dir), std::string("temp files must be deleted on open: ") + names[k]);
        auto loaded = slot->load();
        std::string why;
        CHECK_MSG(loaded.ok && !loaded.usedFallback && world::equal(loaded.current, atA, &why),
                  std::string(names[k]) + ": " + loaded.error + why);
        // And the game can save normally afterwards.
        auto again = slot->save(w, 75);
        CHECK_MSG(again.ok, again.error);
        slot.reset();
        slot = openSlot(dir);
        loaded = slot->load();
        CHECK_MSG(loaded.ok && world::equal(loaded.current, w, &why), std::string(names[k]) + " resave: " + why);
    }
}

void testVerifyBeforeCommit() {
    SECTION("verify-before-commit refuses a truncated temp file and a valid-but-wrong one");
    const std::string dir = freshDir("verify");
    World w;
    auto slot = newGame(dir, w);
    for (int t = 0; t < 30; ++t) toy::step(w);
    CHECK(slot->save(w, 30).ok);
    const std::string head = readAll(dir + "/HEAD");
    const auto saves = saveFiles(dir);
    for (int t = 0; t < 30; ++t) toy::step(w);
    save::SaveOptions o;
    o.tamper = [](const std::string& tmp) {
        const std::string b = readAll(tmp);
        writeAll(tmp, b.substr(0, b.size() / 2));
    };
    auto r = slot->save(w, 60, o);
    CHECK_MSG(!r.ok && r.error.find("verify refused") != std::string::npos, r.error);
    o.tamper = [](const std::string& tmp) { pxr::SdfLayer::CreateAnonymous("x.usdc")->Export(tmp); };
    r = slot->save(w, 60, o);
    CHECK_MSG(!r.ok && r.error.find("verify refused") != std::string::npos, r.error);
    std::printf("  refused: %.110s\n", r.error.c_str());
    CHECK(readAll(dir + "/HEAD") == head && saveFiles(dir) == saves && !hasTemps(dir));
}

// Three saves at ticks 10, 20, 30 in a fresh slot.
std::unique_ptr<Slot> threeSaves(const std::string& dir, World states[3]) {
    World w;
    auto slot = newGame(dir, w);
    for (int k = 0; k < 3; ++k) {
        for (int t = 0; t < 10; ++t) toy::step(w);
        auto r = slot->save(w, tickOf(w));
        CHECK_MSG(r.ok, r.error);
        states[k] = w;
    }
    return slot;
}

void testDamagedSaves() {
    SECTION("a damaged or missing latest save falls back to the previous one, loudly");
    struct Damage {
        const char* name;
        void (*apply)(const std::string& path);
    };
    const Damage damages[] = {
        {"truncated to half", [](const std::string& p) { auto b = readAll(p); writeAll(p, b.substr(0, b.size() / 2)); }},
        {"truncated by 7 bytes", [](const std::string& p) { auto b = readAll(p); writeAll(p, b.substr(0, b.size() - 7)); }},
        {"truncated to 16 bytes", [](const std::string& p) { writeAll(p, readAll(p).substr(0, 16)); }},
        {"64 bytes flipped", [](const std::string& p) {
             auto b = readAll(p);
             for (size_t i = b.size() / 3; i < b.size() / 3 + 64; ++i) b[i] = static_cast<char>(b[i] ^ 0x5a);
             writeAll(p, b);
         }},
        {"deleted", [](const std::string& p) { std::remove(p.c_str()); }},
        {"empty", [](const std::string& p) { writeAll(p, ""); }},
    };
    for (const auto& d : damages) {
        const std::string dir = freshDir("damaged");
        World states[3];
        auto slot = threeSaves(dir, states);
        slot.reset();
        const std::string latest = headSave(dir);
        d.apply(dir + "/" + latest);
        slot = openSlot(dir);
        save::LoadOptions strict;
        strict.fallback = false;
        auto r = slot->load(strict);
        CHECK_MSG(!r.ok, std::string("must not load silently: ") + d.name);
        r = slot->load();
        std::string why;
        CHECK_MSG(r.ok && r.usedFallback && world::equal(r.current, states[1], &why),
                  std::string(d.name) + ": " + r.error + why);
        CHECK(!r.rejected.empty() && r.rejected[0].find(latest) == 0);
        CHECK(!r.warnings.empty());
    }
    {
        const std::string dir = freshDir("damaged2");
        World states[3];
        auto slot = threeSaves(dir, states);
        slot.reset();
        save::Head h;
        save::parseHead(readAll(dir + "/HEAD"), h, nullptr);
        writeAll(dir + "/" + h.history[0].first, "garbage");
        writeAll(dir + "/" + h.history[1].first, "garbage");
        slot = openSlot(dir);
        auto r = slot->load();
        std::string why;
        CHECK_MSG(r.ok && r.usedFallback && world::equal(r.current, states[0], &why), "two damaged: " + r.error + why);
        std::printf("  two damaged saves -> loaded tick %" PRId64 "; rejected: %zu\n", r.manifest.tick,
                    r.rejected.size());
    }
}

void testDamagedHead() {
    SECTION("a damaged or missing HEAD falls back to the newest intact save");
    const char* names[] = {"garbage", "truncated", "one bit flipped", "deleted"};
    for (int k = 0; k < 4; ++k) {
        const std::string dir = freshDir("head");
        World states[3];
        auto slot = threeSaves(dir, states);
        slot.reset();
        std::string h = readAll(dir + "/HEAD");
        if (k == 0) writeAll(dir + "/HEAD", "not a head\n");
        if (k == 1) writeAll(dir + "/HEAD", h.substr(0, h.size() / 2));
        if (k == 2) {
            h[h.size() / 2] = static_cast<char>(h[h.size() / 2] ^ 4);
            writeAll(dir + "/HEAD", h);
        }
        if (k == 3) std::remove((dir + "/HEAD").c_str());
        slot = openSlot(dir);
        auto r = slot->load();
        std::string why;
        CHECK_MSG(r.ok && r.usedFallback && world::equal(r.current, states[2], &why),
                  std::string(names[k]) + ": " + r.error + why);
        CHECK(!r.rejected.empty() && r.rejected[0].compare(0, 5, "HEAD:") == 0);
        World next = r.current;
        toy::step(next);
        CHECK(slot->save(next, tickOf(next)).ok);
        save::Head parsed;
        CHECK_MSG(save::parseHead(readAll(dir + "/HEAD"), parsed, nullptr), "the next save rewrites a valid HEAD");
    }
}

void testDamagedBase() {
    SECTION("a damaged or missing base is refused, never loaded as a fresh world");
    for (int k = 0; k < 2; ++k) {
        const std::string dir = freshDir("base");
        World states[3];
        auto slot = threeSaves(dir, states);
        slot.reset();
        std::string base;
        for (const auto& n : save::fs::list(dir))
            if (n.compare(0, 5, "base-") == 0) base = n;
        if (k == 0) {
            std::string b = readAll(dir + "/" + base);
            b[b.size() / 2] = static_cast<char>(b[b.size() / 2] ^ 1);
            writeAll(dir + "/" + base, b);
        } else {
            std::remove((dir + "/" + base).c_str());
        }
        slot = openSlot(dir);
        auto r = slot->load();
        CHECK_MSG(!r.ok && r.error.find("base") != std::string::npos, r.error);
    }
}

void testUsdSilentSkipHazard() {
    SECTION("USD alone silently skips a broken sublayer; strata::usd::compose refuses it");
    const std::string dir = freshDir("hazard");
    World states[3];
    auto slot = threeSaves(dir, states);
    slot.reset();
    std::string base;
    for (const auto& n : save::fs::list(dir))
        if (n.compare(0, 5, "base-") == 0) base = n;
    const std::string broken = dir + "/broken.usdc";
    const std::string good = readAll(dir + "/" + headSave(dir));
    writeAll(broken, good.substr(0, good.size() / 2));
    // What USD does by itself: the stage opens and the save is simply absent.
    auto rootLayer = pxr::SdfLayer::CreateAnonymous("root.usda");
    rootLayer->SetSubLayerPaths({broken, dir + "/" + base, toy::dataDir() + "/prefabs.usda"});
    auto stage = pxr::UsdStage::Open(rootLayer);
    int64_t tick = -1;
    if (stage) stage->GetPrimAtPath(pxr::SdfPath("/World/Globals")).GetAttribute(pxr::TfToken("toy:tick")).Get(&tick);
    std::printf("  raw UsdStage::Open with a truncated save: %s, tick=%" PRId64 " (the bare base)\n",
                stage ? "opened" : "failed", tick);
    CHECK_MSG(stage && tick == 0, "documents the hazard; if USD starts failing here, keep the guard anyway");
    std::string err;
    CHECK(!usd::compose({toy::dataDir() + "/prefabs.usda", dir + "/" + base, broken}, &err));
    CHECK_MSG(err.find("unreadable layer") != std::string::npos, err);
    CHECK(!usd::compose({toy::dataDir() + "/prefabs.usda", dir + "/" + base, dir + "/nope.usdc"}, &err));
    CHECK_MSG(err.find("missing layer") != std::string::npos, err);
}

void testMods() {
    SECTION("mods: load order, mismatch refused (no fallback), forced, content change warns");
    const std::string data = toy::dataDir();
    {   // Load order through composition: the later mod wins.
        const std::string md = freshDir("modorder");
        writeAll(md + "/slow.usda",
                 "#usda 1.0\nover \"Prefabs\" {\n over \"Agent\" {\n  double toy:stats:speed = 0.5\n }\n}\n");
        auto speed = [&](std::vector<std::string> stack) {
            std::string err;
            auto st = usd::compose(stack, &err);
            double v = -1;
            if (st) st->GetPrimAtPath(pxr::SdfPath("/Prefabs/Agent")).GetAttribute(pxr::TfToken("toy:stats:speed")).Get(&v);
            return v;
        };
        const std::string content = data + "/prefabs.usda";
        CHECK(speed({content}) == 1);
        CHECK(speed({content, data + "/mods/fast.usda"}) == 3);
        CHECK(speed({content, data + "/mods/fast.usda", md + "/slow.usda"}) == 0.5);
        CHECK(speed({content, md + "/slow.usda", data + "/mods/fast.usda"}) == 3);
    }
    const std::string dir = freshDir("mods");
    World w;
    auto slot = newGame(dir, w, 2, toy::layers(true, false));
    for (int t = 0; t < 20; ++t) toy::step(w);
    CHECK(slot->save(w, 20).ok);
    slot.reset();
    slot = openSlot(dir, 2, toy::layers(false, false));
    auto r = slot->load();
    CHECK_MSG(!r.ok && r.error.find("mod list differs") != std::string::npos, r.error);
    CHECK_MSG(r.rejected.size() == 1, "a policy refusal must not try older saves");
    save::LoadOptions force;
    force.allowModMismatch = true;
    r = slot->load(force);
    std::string why;
    CHECK_MSG(r.ok && world::equal(r.current, w, &why) && !r.warnings.empty(), r.error + why);
    slot.reset();
    slot = openSlot(dir, 2, toy::layers(true, true));
    r = slot->load();
    CHECK_MSG(!r.ok, "an added mod is a different mod list");
    slot.reset();
    // Content change: same mod id, different bytes -> warning, still loads.
    const std::string md = freshDir("modcontent");
    writeAll(md + "/fast.usda", readAll(data + "/mods/fast.usda"));
    save::Layers l = toy::layers();
    l.mods.push_back({"fast", {md + "/fast.usda"}});
    World w2;
    slot = newGame(freshDir("mods2"), w2, 2, l);
    const std::string dir2 = slot->dir();
    CHECK(slot->save(w2, 0).ok);
    slot.reset();
    writeAll(md + "/fast.usda", "#usda 1.0\nover \"Prefabs\" {\n over \"Agent\" {\n  double toy:stats:speed = 4\n }\n}\n");
    slot = openSlot(dir2, 2, l);
    r = slot->load();
    bool warned = false;
    for (const auto& x : r.warnings) warned |= x.find("content changed") != std::string::npos;
    CHECK_MSG(r.ok && warned, r.error);
    slot.reset();
    // A mod layer that cannot be read stops the slot from opening at all.
    std::remove((md + "/fast.usda").c_str());
    std::string err;
    CHECK(!Slot::open(dir2, toy::saveSchema(2), l, &err) && err.find("cannot read layer") != std::string::npos);
}

void testPolicyRefusals() {
    SECTION("another game, a newer schema and a newer format are refused without fallback");
    const std::string dir = freshDir("policy");
    World states[3];
    auto slot = threeSaves(dir, states);
    slot.reset();
    {
        save::Schema other = toy::saveSchema(2);
        other.gameId = "another-game";
        std::string err;
        auto s = Slot::open(dir, other, toy::layers(), &err);
        auto r = s->load();
        CHECK_MSG(!r.ok && r.error.find("belongs to game") != std::string::npos && r.rejected.size() == 1, r.error);
    }
    {
        auto s = openSlot(dir, 1);
        auto r = s->load();
        CHECK_MSG(!r.ok && r.error.find("newer game version") != std::string::npos && r.rejected.size() == 1, r.error);
    }
    {   // Forge a correctly hashed save from a newer engine format.
        const std::string latest = headSave(dir);
        auto layer = pxr::SdfLayer::OpenAsAnonymous(dir + "/" + latest);
        pxr::VtDictionary d = layer->GetCustomLayerData();
        d["eng:formatVersion"] = pxr::VtValue(99);
        layer->SetCustomLayerData(d);
        const std::string tmp = dir + "/forged.usdc";
        layer->Export(tmp);
        std::string sha;
        uint64_t size = 0;
        save::sha256File(tmp, sha, &size);
        const std::string name = "save-000000000099-" + sha + ".usdc";
        std::rename(tmp.c_str(), (dir + "/" + name).c_str());
        save::Head h;
        save::parseHead(readAll(dir + "/HEAD"), h, nullptr);
        h.saveFile = name;
        h.saveSha = sha;
        h.saveSize = size;
        writeAll(dir + "/HEAD", save::formatHead(h));
        auto s = openSlot(dir);
        auto r = s->load();
        CHECK_MSG(!r.ok && r.error.find("newer engine format") != std::string::npos && r.rejected.size() == 1,
                  r.error);
    }
}

void testMigration() {
    SECTION("an older schema is migrated on a copy; the original files are kept byte-identical");
    const std::string dir = freshDir("migrate");
    World w;
    auto slot = newGame(dir, w, 1);
    for (int t = 0; t < 30; ++t) toy::step(w);
    CHECK(slot->save(w, 30).ok);
    slot.reset();
    std::map<std::string, std::string> before;
    for (const auto& n : save::fs::list(dir))
        if (n != "LOCK") before[n] = readAll(dir + "/" + n);
    // The expected v2 state: the v1 state run through the game's migration.
    World expected = w;
    std::string err;
    CHECK(toy::saveSchema(2).upgrade.at(1)(expected, &err));
    slot = openSlot(dir, 2);
    auto r = slot->load();
    std::string why;
    CHECK_MSG(r.ok && r.migrated && world::equal(r.current, expected, &why), r.error + why);
    CHECK(r.manifest.gameSchemaVersion == 2);
    for (const auto& kv : before) {
        if (kv.first == "HEAD" || kv.first == "HEAD.prev") continue;
        CHECK_MSG(readAll(dir + "/" + kv.first) == kv.second, "original file changed: " + kv.first);
    }
    // Loading again needs no migration.
    slot.reset();
    slot = openSlot(dir, 2);
    auto again = slot->load();
    CHECK_MSG(again.ok && !again.migrated && world::equal(again.current, expected, &why), again.error + why);
    // The migrated game keeps playing and saving.
    World next = again.current;
    for (int t = 0; t < 10; ++t) toy::step(next);
    CHECK(slot->save(next, tickOf(next)).ok);
    slot.reset();
    // The old game version now refuses the slot instead of misreading it.
    slot = openSlot(dir, 1);
    auto old = slot->load();
    CHECK_MSG(!old.ok && old.error.find("newer game version") != std::string::npos, old.error);
}

void testAsync() {
    SECTION("async save: snapshot on the caller, busy while in flight, saves the snapshot");
    const std::string dir = freshDir("async");
    World w;
    auto slot = newGame(dir, w);
    for (int t = 0; t < 40; ++t) toy::step(w);
    const World snapshot = w;
    auto f = slot->saveAsync(w, 40);
    auto busy = slot->saveAsync(w, 40);
    const auto b = busy.get();
    CHECK_MSG(!b.ok && b.error == "busy", b.error);
    for (int t = 0; t < 10; ++t) toy::step(w);  // the game keeps running
    const auto r = f.get();
    CHECK_MSG(r.ok, r.error);
    std::printf("  snapshot %.3f ms (main thread); export %.2f ms, verify %.2f ms, commit %.2f ms (worker)\n",
                r.snapshotMs, r.exportMs, r.verifyMs, r.commitMs);
    slot.reset();
    slot = openSlot(dir);
    auto loaded = slot->load();
    std::string why;
    CHECK_MSG(loaded.ok && world::equal(loaded.current, snapshot, &why), loaded.error + why);
    // Destroying a slot with a save in flight finishes the save first.
    for (int t = 0; t < 5; ++t) toy::step(w);
    auto pending = slot->saveAsync(w, tickOf(w));
    slot.reset();
    CHECK(pending.get().ok);
}

void testLockAndPrune() {
    SECTION("one process per slot; old saves are pruned to the history");
    const std::string dir = freshDir("lock");
    World w;
    auto slot = newGame(dir, w);
    std::string err;
    CHECK(!Slot::open(dir, toy::saveSchema(2), toy::layers(), &err) && err.find("in use") != std::string::npos);
    slot->keep = 3;
    for (int k = 0; k < 8; ++k) {
        toy::step(w);
        CHECK(slot->save(w, tickOf(w)).ok);
    }
    save::Head h;
    CHECK(save::parseHead(readAll(dir + "/HEAD"), h, nullptr) && h.history.size() == 3);
    const auto files = saveFiles(dir);
    CHECK_MSG(files.size() <= 4, std::to_string(files.size()) + " save files kept");
    for (const auto& e : h.history) CHECK(save::fs::exists(dir + "/" + e.first));
}

// ---- crash suite -------------------------------------------------------------

#ifndef _WIN32
// The crash suite forks capped children and SIGKILLs writers; POSIX only.
void testBitFlipProbe() {
    SECTION("single-bit flips: with SHA-256 every one is rejected (probes capped at 4 GB)");
    const std::string dir = freshDir("bitflip");
    World w;
    auto slot = newGame(dir, w);
    for (int t = 0; t < 60; ++t) toy::step(w);
    CHECK(slot->save(w, 60).ok);
    slot.reset();
    const std::string name = headSave(dir);
    const std::string path = dir + "/" + name;
    const std::string cur = readAll(path);
    std::string base;
    for (const auto& n : save::fs::list(dir))
        if (n.compare(0, 5, "base-") == 0) base = n;
    for (bool withSum : {true, false}) {
        int rejected = 0, harmless = 0, silent = 0, crashed = 0, n = 0;
        const size_t stride = std::max<size_t>(1, cur.size() / 400);
        for (size_t i = 0; i < cur.size(); i += stride, ++n) {
            std::string f = cur;
            f[i] = static_cast<char>(f[i] ^ 0x01);
            writeAll(path, f);
            std::fflush(stdout);
            const pid_t pid = ::fork();
            if (pid == 0) {
                struct rlimit rl {
                    4UL << 30, 4UL << 30
                };
                ::setrlimit(RLIMIT_AS, &rl);
                if (!std::freopen("/dev/null", "w", stderr)) ::_exit(13);
                std::string e, why;
                World got;
                if (withSum) {
                    auto s = Slot::open(dir, toy::saveSchema(2), toy::layers(), &e);
                    if (!s) ::_exit(13);
                    save::LoadOptions o;
                    o.fallback = false;
                    auto r = s->load(o);
                    if (!r.ok) ::_exit(10);
                    got = r.current;
                } else {
                    // Bypass the checksum: what USD's parser alone would do.
                    got = world::emptyLike(toy::schema(2));
                    auto st = usd::compose({toy::dataDir() + "/prefabs.usda", dir + "/" + base, path}, &e);
                    if (!st || !usd::hydrate(st, got, &e)) ::_exit(10);
                }
                ::_exit(world::equal(got, w, &why) ? 11 : 12);
            }
            int st = 0;
            ::waitpid(pid, &st, 0);
            const int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
            if (code == 10) ++rejected;
            else if (code == 11) ++harmless;
            else if (code == 12) ++silent;
            else ++crashed;
        }
        std::printf("  %d single-bit flips of a %zu-byte save, checksum %s: rejected=%d harmless=%d "
                    "SILENTLY-WRONG=%d CRASHED/OOM=%d\n",
                    n, cur.size(), withSum ? "ON " : "OFF", rejected, harmless, silent, crashed);
        if (withSum) CHECK_MSG(rejected == n, "the checksum must reject every flip before parsing");
    }
    writeAll(path, cur);
}

void testKill9() {
    SECTION("kill -9 at random moments during saves: every save left behind loads and replays exactly");
    const std::string dir = freshDir("kill9");
    {
        World w;
        auto slot = newGame(dir, w);
        CHECK(slot->save(w, 0).ok);
    }
    std::srand(1234);
    int kills = 0, exact = 0, viaFallback = 0;
    int64_t maxTick = 0, prevTick = 0;
    for (int round = 0; round < 25; ++round) {
        std::fflush(stdout);
        const pid_t pid = ::fork();
        if (pid == 0) {
            std::string e;
            auto s = Slot::open(dir, toy::saveSchema(2), toy::layers(), &e);
            if (!s) ::_exit(3);
            auto r = s->load();
            if (!r.ok) ::_exit(4);
            World c = r.current;
            for (;;) {
                for (int k = 0; k < 3; ++k) toy::step(c);
                s->save(c, tickOf(c));
            }
        }
        ::usleep(static_cast<useconds_t>(20000 + std::rand() % 400000));
        ::kill(pid, SIGKILL);
        int status = 0;
        ::waitpid(pid, &status, 0);
        ++kills;
        auto s = openSlot(dir);
        if (!s) continue;
        CHECK_MSG(!hasTemps(dir), "temps removed after a kill");
        save::LoadOptions strict;
        strict.fallback = false;
        auto r = s->load(strict);
        if (!r.ok) {
            r = s->load();
            if (r.ok) ++viaFallback;
        }
        if (!r.ok) {
            std::printf("  round %d: UNLOADABLE: %s\n", round, r.error.c_str());
            continue;
        }
        const World expect = replay(static_cast<int>(r.manifest.tick));
        std::string why;
        if (world::equal(r.current, expect, &why)) ++exact;
        else std::printf("  round %d: tick %" PRId64 " does not match replay: %s\n", round, r.manifest.tick, why.c_str());
        CHECK_MSG(r.manifest.tick >= prevTick, "progress never goes backwards");
        prevTick = r.manifest.tick;
        maxTick = std::max(maxTick, r.manifest.tick);
    }
    std::printf("  kill -9 rounds: %d; loaded and replayed exactly: %d (%d via fallback); reached tick %" PRId64 "\n",
                kills, exact, viaFallback, maxTick);
    CHECK(exact == kills);
}

#endif  // _WIN32

}  // namespace

int main(int argc, char** argv) {
    root = argc > 1 ? argv[1] : (std::filesystem::temp_directory_path() / "strata-save-tests").u8string();
    const bool crash = argc > 2 && std::string(argv[2]) == "--crash";
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(root), ec);
    if (crash) {
#ifdef _WIN32
        std::printf("crash suite (fork, setrlimit, SIGKILL) skipped on Windows\n");
#else
        testBitFlipProbe();
        testKill9();
#endif
    } else {
        testSha256();
        testHead();
        testRoundTrip();
        testUnsupportedChangesRefused();
        testCrashPoints();
        testVerifyBeforeCommit();
        testDamagedSaves();
        testDamagedHead();
        testDamagedBase();
        testUsdSilentSkipHazard();
        testMods();
        testPolicyRefusals();
        testMigration();
        testAsync();
        testLockAndPrune();
    }
    std::printf("%s (%d failure%s)\n", check::failures() ? "FAILED" : "ALL PASSED", check::failures(),
                check::failures() == 1 ? "" : "s");
    return check::failures() ? 1 : 0;
}
