// The web kit's grid relay (web/kit/grid-relay/relay.h, WIRE.md): its wire
// format reader and the frame it builds, without GL.
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "relay.h"
#include "render/lights.hpp"
#include "render/overlay.hpp"
#include "test.hpp"

#include "grid_relay_wire_1_0.inc"

namespace {

using Bytes = std::vector<uint8_t>;

struct Writer {
    Bytes b;
    explicit Writer(uint8_t kind, uint8_t major = grid_relay::kMajor, uint8_t minor = grid_relay::kMinor) {
        b = {'G', '3', major, minor, kind, 0, 0, 0};
    }
    Writer& u8(uint8_t v) {
        b.push_back(v);
        return *this;
    }
    Writer& u16(uint16_t v) { return u8(static_cast<uint8_t>(v)).u8(static_cast<uint8_t>(v >> 8)); }
    Writer& u32(uint32_t v) { return u16(static_cast<uint16_t>(v)).u16(static_cast<uint16_t>(v >> 16)); }
    Writer& f(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        return u32(bits);
    }
    Writer& shape(uint8_t kind, uint8_t attach, uint16_t a, uint16_t bb, float x, float y, float z, float sx,
                  float sy, float sz) {
        u8(kind).u8(attach).u16(a).u16(bb).u16(0);
        f(x).f(y).f(z).f(sx).f(sy).f(sz);
        return f(0.5f).f(0.5f).f(0.5f).f(1.0f);
    }
    Writer& light(uint8_t attach, uint8_t flags, uint16_t a, float x, float y, float z) {
        u8(attach).u8(flags).u16(a).f(x).f(y).f(z);
        return f(1.0f).f(0.8f).f(0.6f).f(3.0f).f(1.5f);
    }
    int into(grid_relay::Relay& r) const { return r.submit(b.data(), b.size()); }
};

Writer board1() {
    Writer w(grid_relay::kBoard);
    w.u32(1).u32(0).shape(STRATA_PRIMITIVE_CUBE, grid_relay::kWorld, 0, 0, 0.0f, -0.09f, 0.0f, 0.97f, 0.18f, 0.97f);
    return w;
}

// Two pieces sliding east by one cell over a 0.5 s tick, a bridge between
// them, a shape on the first and a light on the second.
Writer pieces2(float tick = 0.5f) {
    Writer w(grid_relay::kPieces);
    w.f(tick).u32(2).u32(3).u32(1);
    w.f(0.0f).f(0.0f).f(1.0f).f(0.0f);  // anchor 0: (0, 0) -> (1, 0)
    w.f(-1.0f).f(0.0f).f(0.0f).f(0.0f); // anchor 1: (-1, 0) -> (0, 0)
    w.shape(STRATA_PRIMITIVE_CAPSULE, grid_relay::kPiece, 0, 0, 0.25f, 0.3f, 0.0f, 0.8f, 0.55f, 0.8f);
    w.shape(STRATA_PRIMITIVE_SPHERE, grid_relay::kBridge, 0, 1, 0.0f, 0.2f, 0.0f, 0.6f, 0.4f, 0.6f);
    w.shape(STRATA_PRIMITIVE_CUBE, grid_relay::kWorld, 0, 0, 3.0f, 0.0f, 3.0f, 1.0f, 1.0f, 1.0f);
    w.light(grid_relay::kPiece, 1, 1, 0.0f, 1.0f, 0.0f);
    return w;
}

}  // namespace

TEST(grid_relay_refuses_other_majors_and_foreign_bytes) {
    grid_relay::Relay r;
    CHECK(Writer(grid_relay::kView, 2, 0).into(r) == grid_relay::kBadMajor);
    CHECK(Writer(grid_relay::kView, 0, 9).into(r) == grid_relay::kBadMajor);
    const uint8_t png[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    CHECK(r.submit(png, sizeof png) == grid_relay::kBadMagic);
    CHECK(r.submit(png, 3) == grid_relay::kBadMagic);
    CHECK(Writer(9).into(r) == grid_relay::kBadKind);
    CHECK(r.messages() == 0);
}

TEST(grid_relay_reads_a_newer_minor_and_ignores_what_it_appends) {
    grid_relay::Relay r;
    Writer w = board1();
    w.b[3] = 7;          // minor 7
    w.u32(0xdeadbeef);   // a field a later minor appended
    CHECK(w.into(r) == grid_relay::kOk);
    CHECK(r.hasBoard());
}

TEST(grid_relay_refuses_truncated_and_bad_messages_and_keeps_the_scene) {
    grid_relay::Relay r;
    CHECK(board1().into(r) == grid_relay::kOk);
    Writer cut = pieces2();
    cut.b.resize(cut.b.size() - 4);
    CHECK(cut.into(r) == grid_relay::kTruncated);
    Writer badIndex(grid_relay::kPieces);
    badIndex.f(0.0f).u32(0).u32(1).u32(0).shape(0, grid_relay::kPiece, 0, 0, 0, 0, 0, 1, 1, 1);  // no anchor 0
    CHECK(badIndex.into(r) == grid_relay::kBadValue);
    Writer badKind = board1();
    badKind.b[16] = 9;  // the shape's kind
    CHECK(badKind.into(r) == grid_relay::kBadValue);
    Writer nan = board1();
    nan.b.resize(24);
    nan.f(std::nanf("")).f(0).f(0).f(1).f(1).f(1).f(1).f(1).f(1).f(1);
    CHECK(nan.into(r) == grid_relay::kBadValue);
    Writer boardOnPiece(grid_relay::kBoard);
    boardOnPiece.u32(1).u32(0).shape(0, grid_relay::kPiece, 0, 0, 0, 0, 0, 1, 1, 1);  // the board has no anchors
    CHECK(boardOnPiece.into(r) == grid_relay::kBadValue);
    grid_relay::Frame f;
    r.build(0.0, false, 1.0f, f);
    CHECK(f.items.size() == 1);
}

TEST(grid_relay_slides_pieces_over_a_tick_from_the_first_frame_after_it) {
    grid_relay::Relay r;
    CHECK(board1().into(r) == grid_relay::kOk);
    CHECK(pieces2().into(r) == grid_relay::kOk);
    grid_relay::Frame f;
    r.build(10.0, false, 1.0f, f);  // the tick starts here
    CHECK(f.items.size() == 4);
    CHECK_NEAR(f.items[1].position[0], 0.25, 1e-6);  // anchor 0 at its start, plus the offset
    CHECK(f.items[1].kind == STRATA_PRIMITIVE_CAPSULE);
    r.build(10.25, false, 1.0f, f);  // half way
    CHECK_NEAR(f.items[1].position[0], 0.75, 1e-6);
    CHECK_NEAR(f.items[1].position[1], 0.3, 1e-6);  // y is absolute
    // The bridge: midway between anchors 0 (0.5) and 1 (-0.5), spanning them.
    CHECK_NEAR(f.items[2].position[0], 0.0, 1e-6);
    CHECK_NEAR(f.items[2].scale[0], 1.0, 1e-6);
    CHECK_NEAR(f.items[2].scale[2], 0.6, 1e-6);
    CHECK_NEAR(f.items[3].position[0], 3.0, 1e-6);  // a world shape does not move
    CHECK(f.lights.size() == 1);
    CHECK_NEAR(f.lights[0].position[0], -0.5, 1e-6);
    CHECK(f.lights[0].flags == STRATA_POINT_LIGHT_CAST_SHADOWS);
    r.build(11.0, false, 1.0f, f);  // past the tick: at the end
    CHECK_NEAR(f.items[1].position[0], 1.25, 1e-6);
}

TEST(grid_relay_jumps_under_reduced_motion_and_for_a_zero_tick) {
    grid_relay::Relay r;
    CHECK(board1().into(r) == grid_relay::kOk);
    CHECK(pieces2().into(r) == grid_relay::kOk);
    grid_relay::Frame f;
    r.build(10.0, true, 1.0f, f);
    CHECK_NEAR(f.items[1].position[0], 1.25, 1e-6);
    CHECK(pieces2(0.0f).into(r) == grid_relay::kOk);
    r.build(20.0, false, 1.0f, f);
    CHECK_NEAR(f.items[1].position[0], 1.25, 1e-6);
}

TEST(grid_relay_scales_the_hud_to_the_drawing_buffer) {
    grid_relay::Relay r;
    CHECK(board1().into(r) == grid_relay::kOk);
    Writer hud(grid_relay::kHud);
    hud.u32(1).u32(1);
    hud.f(10).f(20).f(300).f(40).f(0).f(0).f(0).f(0.5f);
    hud.f(24).f(30).f(16).f(1).f(1).f(1).f(1).u16(5).u16(0);
    for (char c : std::string("SCORE")) hud.u8(static_cast<uint8_t>(c));
    hud.u8(0).u8(0).u8(0);  // padded to 4
    CHECK(hud.into(r) == grid_relay::kOk);
    grid_relay::Frame f;
    r.build(0.0, false, 2.0f, f);
    CHECK(f.rects.size() == 1 && f.texts.size() == 1);
    CHECK_NEAR(f.rects[0].width, 600.0, 1e-6);
    CHECK_NEAR(f.texts[0].x, 48.0, 1e-6);
    CHECK_NEAR(f.texts[0].pixel_size, 32.0, 1e-6);
    CHECK(std::string(f.texts[0].utf8) == "SCORE");
    Writer longText(grid_relay::kHud);
    longText.u32(0).u32(1).f(0).f(0).f(8).f(1).f(1).f(1).f(1).u16(200).u16(0);  // 200 bytes promised, none sent
    CHECK(longText.into(r) == grid_relay::kTruncated);
}

TEST(grid_relay_turns_a_view_into_the_kits_orbit) {
    grid_relay::Relay r;
    Writer v(grid_relay::kView);
    v.f(0).f(13.5f).f(9.5f).f(0).f(-0.3f).f(-0.4f).f(42).f(0.3f).f(200).f(0.1f).f(0.12f).f(0.16f).f(1)
        .f(-0.45f).f(0.85f).f(0.35f);
    CHECK(v.into(r) == grid_relay::kOk);
    const grid_relay::Orbit o = grid_relay::orbitOf(r.view());
    // Back from the orbit to the eye, as strata_kit's sk_frame does.
    const float cp = std::cos(o.pitch);
    CHECK_NEAR(o.target[0] + o.dist * cp * std::cos(o.yaw), 0.0, 1e-4);
    CHECK_NEAR(o.target[1] + o.dist * std::sin(o.pitch), 13.5, 1e-4);
    CHECK_NEAR(o.target[2] + o.dist * cp * std::sin(o.yaw), 9.5, 1e-4);
    Writer bad(grid_relay::kView);
    bad.f(0).f(1).f(1).f(0).f(0).f(0).f(0).f(0.3f).f(200).f(0).f(0).f(0).f(1).f(0).f(1).f(0);  // fov 0
    CHECK(bad.into(r) == grid_relay::kBadValue);
}

// -- hardening: what the relay accepts, the engine accepts too

namespace {

// True when the engine's own checks (strata_set_point_lights,
// strata_submit_rects, strata_submit_text) take everything this frame holds.
bool engineTakes(const grid_relay::Frame& f) {
    try {
        strata::render::validatePointLights(f.lights.data(), std::min<size_t>(f.lights.size(), STRATA_MAX_POINT_LIGHTS));
        strata::render::validateRects(f.rects.data(), f.rects.size());
        strata::render::validateTexts(f.texts.data(), f.texts.size());
    } catch (...) {
        return false;
    }
    for (const StrataRenderItem& it : f.items) {
        if (it.kind < 0 || it.kind >= STRATA_PRIMITIVE_KIND_COUNT) return false;
        for (int i = 0; i < 3; ++i)
            if (!std::isfinite(it.position[i]) || !std::isfinite(it.scale[i])) return false;
    }
    return true;
}

Writer hud1(float alpha = 0.5f) {
    Writer w(grid_relay::kHud);
    w.u32(1).u32(1);
    w.f(10).f(20).f(300).f(40).f(0).f(0).f(0).f(alpha);
    w.f(24).f(30).f(16).f(1).f(1).f(1).f(1).u16(5).u16(0);
    for (char c : std::string("SCORE")) w.u8(static_cast<uint8_t>(c));
    return w.u8(0).u8(0).u8(0);
}

Writer view1() {
    Writer v(grid_relay::kView);
    v.f(0).f(13.5f).f(9.5f).f(0).f(-0.3f).f(-0.4f).f(42).f(0.3f).f(200).f(0.1f).f(0.12f).f(0.16f).f(1)
        .f(-0.45f).f(0.85f).f(0.35f);
    return v;
}

Writer boardLights(int n, int shadowed) {
    Writer w(grid_relay::kBoard);
    w.u32(0).u32(static_cast<uint32_t>(n));
    for (int i = 0; i < n; ++i) w.light(grid_relay::kWorld, i < shadowed ? 1 : 0, 0, float(i), 2.0f, 0.0f);
    return w;
}

struct Rng {  // xorshift64*: the same bytes on every platform and run
    uint64_t s;
    uint32_t next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return static_cast<uint32_t>((s * 2685821657736338717ull) >> 32);
    }
    uint32_t below(uint32_t n) { return n ? next() % n : 0; }
};

}  // namespace

TEST(grid_relay_says_how_far_a_short_header_got) {
    grid_relay::Relay r;
    const uint8_t g3[8] = {'G', '3', 1, 0, 1, 0, 0, 0};
    CHECK(r.submit(g3, 1) == grid_relay::kBadMagic);
    CHECK(r.submit(g3, 2) == grid_relay::kTruncated);
    CHECK(r.submit(g3, 7) == grid_relay::kTruncated);
    const uint8_t major2[4] = {'G', '3', 2, 0};
    CHECK(r.submit(major2, 4) == grid_relay::kBadMajor);
    CHECK(r.submit(nullptr, 8) == grid_relay::kBadMagic);
    CHECK(r.messages() == 0);
}

TEST(grid_relay_refuses_lights_the_engine_would_refuse) {
    grid_relay::Relay r;
    Writer zeroRange(grid_relay::kBoard);
    zeroRange.u32(0).u32(1).u8(0).u8(0).u16(0).f(0).f(1).f(0).f(1).f(1).f(1).f(0.0f).f(1.0f);
    CHECK(zeroRange.into(r) == grid_relay::kBadValue);
    Writer negative(grid_relay::kBoard);
    negative.u32(0).u32(1).u8(0).u8(0).u16(0).f(0).f(1).f(0).f(-1).f(1).f(1).f(3.0f).f(1.0f);
    CHECK(negative.into(r) == grid_relay::kBadValue);
    Writer dim(grid_relay::kBoard);
    dim.u32(0).u32(1).u8(0).u8(0).u16(0).f(0).f(1).f(0).f(1).f(1).f(1).f(3.0f).f(-0.5f);
    CHECK(dim.into(r) == grid_relay::kBadValue);
    CHECK(boardLights(5, 5).into(r) == grid_relay::kTooLarge);  // five shadow casters
    CHECK(boardLights(32, 4).into(r) == grid_relay::kOk);
    CHECK(boardLights(33, 0).into(r) == grid_relay::kTooLarge);
}

TEST(grid_relay_keeps_a_frame_to_four_shadow_casters_across_board_and_pieces) {
    grid_relay::Relay r;
    CHECK(boardLights(3, 3).into(r) == grid_relay::kOk);
    Writer pieces(grid_relay::kPieces);
    pieces.f(0).u32(1).u32(0).u32(3).f(0).f(0).f(0).f(0);
    for (int i = 0; i < 3; ++i) pieces.light(grid_relay::kPiece, 1, 0, 0.0f, 1.0f, 0.0f);
    CHECK(pieces.into(r) == grid_relay::kOk);
    grid_relay::Frame f;
    r.build(0.0, false, 1.0f, f);
    CHECK(f.lights.size() == 6);
    int casters = 0;
    for (const StrataPointLight& l : f.lights) casters += (l.flags & STRATA_POINT_LIGHT_CAST_SHADOWS) ? 1 : 0;
    CHECK(casters == STRATA_MAX_SHADOWED_POINT_LIGHTS);
    CHECK(f.lights[3].flags == STRATA_POINT_LIGHT_CAST_SHADOWS);  // the board's three, then the first piece's
    CHECK(f.lights[4].flags == 0u);
    CHECK(engineTakes(f));
}

TEST(grid_relay_refuses_hud_colours_the_engine_would_refuse_and_bad_views) {
    grid_relay::Relay r;
    CHECK(hud1(1.5f).into(r) == grid_relay::kBadValue);  // alpha over 1
    Writer negative = hud1();
    std::memcpy(&negative.b[16 + 16], "\x00\x00\x80\xbf", 4);  // the panel's red: -1
    CHECK(negative.into(r) == grid_relay::kBadValue);
    CHECK(hud1().into(r) == grid_relay::kOk);
    Writer onTarget(grid_relay::kView);
    onTarget.f(1).f(2).f(3).f(1).f(2).f(3).f(42).f(0.3f).f(200).f(0).f(0).f(0).f(1).f(0).f(1).f(0);
    CHECK(onTarget.into(r) == grid_relay::kBadValue);
    Writer far(grid_relay::kView);
    far.f(0).f(2e7f).f(0).f(0).f(0).f(0).f(42).f(0.3f).f(200).f(0).f(0).f(0).f(1).f(0).f(1).f(0);
    CHECK(far.into(r) == grid_relay::kBadValue);  // past kMaxCoordinate
}

TEST(grid_relay_refuses_coordinates_whose_sums_would_overflow) {
    grid_relay::Relay r;
    Writer anchors(grid_relay::kPieces);
    anchors.f(1).u32(1).u32(0).u32(0).f(-3e38f).f(0).f(3e38f).f(0);  // the slide's length is not a float
    CHECK(anchors.into(r) == grid_relay::kBadValue);
    Writer shape(grid_relay::kBoard);
    shape.u32(1).u32(0).shape(0, grid_relay::kWorld, 0, 0, 2e6f, 0, 0, 1, 1, 1);
    CHECK(shape.into(r) == grid_relay::kBadValue);
    CHECK(r.messages() == 0);
}

TEST(grid_relay_leaves_out_hud_items_scaled_past_a_float_and_ignores_a_bad_ui_scale) {
    grid_relay::Relay r;
    CHECK(board1().into(r) == grid_relay::kOk);
    Writer hud(grid_relay::kHud);
    hud.u32(2).u32(0);
    hud.f(3e38f).f(0).f(10).f(10).f(0).f(0).f(0).f(1);  // fine at scale 1, infinite at 2
    hud.f(1).f(1).f(10).f(10).f(0).f(0).f(0).f(1);
    CHECK(hud.into(r) == grid_relay::kOk);
    grid_relay::Frame f;
    r.build(0.0, false, 2.0f, f);
    CHECK(f.rects.size() == 1);
    CHECK(engineTakes(f));
    r.build(0.0, false, std::numeric_limits<float>::quiet_NaN(), f);
    CHECK(f.rects.size() == 2);
    CHECK_NEAR(f.rects[1].x, 1.0, 1e-6);
}

TEST(grid_relay_reads_graphiks_golden_messages_as_their_scene) {
    grid_relay::Relay r;
    CHECK(r.submit(kWireBoard, sizeof kWireBoard) == grid_relay::kOk);
    CHECK(r.submit(kWireView, sizeof kWireView) == grid_relay::kOk);
    CHECK(r.submit(kWirePieces, sizeof kWirePieces) == grid_relay::kOk);
    CHECK(r.submit(kWireHud, sizeof kWireHud) == grid_relay::kOk);
    CHECK(r.messages() == 4);
    CHECK(r.anchorCount() == 3);
    // camera: GridBoard(3, 2).camera(4 / 3) in graphik (fitCamera)
    CHECK_NEAR(r.view().eye[1], 5.859, 1e-3);
    CHECK_NEAR(r.view().eye[2], 4.123, 1e-3);
    CHECK_NEAR(r.view().target[1], -0.3, 1e-6);
    CHECK_NEAR(r.view().fov, 42.0, 1e-6);
    grid_relay::Frame f;
    r.build(0.0, false, 1.0f, f);  // the tick starts: every anchor at its start
    // ten board shapes (six tiles, four frame sides), then five piece shapes
    CHECK(f.items.size() == 15);
    CHECK(f.lights.size() == 2);
    CHECK(f.lights[0].flags == STRATA_POINT_LIGHT_CAST_SHADOWS);
    CHECK_NEAR(f.items[10].position[0], -1.0, 1e-6);   // the head, on anchor 0's start
    CHECK_NEAR(f.items[11].position[0], -0.85, 1e-6);  // its eye, 0.15 east of it
    CHECK_NEAR(f.items[11].position[2], -0.2, 1e-6);   // and 0.3 south
    CHECK_NEAR(f.lights[1].position[0], 1.0, 1e-6);    // the gem's glow stands on anchor 2
    r.build(0.125, false, 1.0f, f);                    // half of the 0.25 s tick
    CHECK_NEAR(f.items[10].position[0], -0.5, 1e-6);
    CHECK_NEAR(f.items[14].position[0], -0.5, 1e-6);   // the bridge, between head and body
    CHECK_NEAR(f.items[14].scale[2], 1.0, 1e-6);       // spanning them north to south
    CHECK(f.texts.size() == 1 && f.rects.size() == 1);
    CHECK(std::string(f.texts[0].utf8) == "Score 3 \xe2\x80\x94 \xc3\xa9t\xc3\xa9");
    CHECK(engineTakes(f));
}

// Malformed input under ASan and UBSan (the Sanitizers CI job runs this
// suite): every truncation of every valid message, random byte flips, wild
// counts and plain noise. A refused message must change nothing, and every
// frame built from what was accepted must pass the engine's own checks.
TEST(grid_relay_survives_malformed_messages_and_only_keeps_what_the_engine_takes) {
    const std::vector<Bytes> seeds = {
        board1().b, pieces2().b, hud1().b, view1().b, boardLights(4, 4).b,
        Bytes(kWireBoard, kWireBoard + sizeof kWireBoard), Bytes(kWireView, kWireView + sizeof kWireView),
        Bytes(kWirePieces, kWirePieces + sizeof kWirePieces), Bytes(kWireHud, kWireHud + sizeof kWireHud),
    };
    grid_relay::Relay r;
    grid_relay::Frame f;
    Rng rng{0x9e3779b97f4a7c15ull};
    double t = 0.0;
    auto feed = [&](const Bytes& m) {
        const uint32_t before = r.messages();
        // a copy of exactly m.size() bytes, so ASan sees any read past the end
        std::vector<uint8_t> exact(m.begin(), m.end());
        const int rc = r.submit(exact.empty() ? nullptr : exact.data(), exact.size());
        CHECK(rc <= 0 && rc >= grid_relay::kTooLarge);
        CHECK(r.messages() == before + (rc == grid_relay::kOk ? 1u : 0u));
        t += 0.05;
        if (r.hasBoard()) {
            r.build(t, (rng.next() & 7) == 0, 1.0f + float(rng.below(3)), f);
            CHECK(engineTakes(f));
        }
    };
    for (const Bytes& seed : seeds) {
        for (size_t n = 0; n <= seed.size(); ++n) feed(Bytes(seed.begin(), seed.begin() + n));
    }
    for (int i = 0; i < 20000; ++i) {
        Bytes m = seeds[rng.below(static_cast<uint32_t>(seeds.size()))];
        switch (rng.below(5)) {
        case 0:  // flip a few bytes past the header
            for (uint32_t k = 0, n = 1 + rng.below(4); k < n; ++k)
                m[8 + rng.below(static_cast<uint32_t>(m.size() - 8))] = static_cast<uint8_t>(rng.next());
            break;
        case 1: {  // a wild count where the body's counts are
            const size_t at = 8 + 4 * rng.below(4);
            if (at + 4 <= m.size()) {
                const uint32_t v = rng.below(2) ? rng.next() : rng.below(70000);
                std::memcpy(&m[at], &v, 4);
            }
            break;
        }
        case 2:  // cut short, or padded with noise
            if (rng.below(2)) m.resize(rng.below(static_cast<uint32_t>(m.size())));
            else for (uint32_t k = 0, n = rng.below(64); k < n; ++k) m.push_back(static_cast<uint8_t>(rng.next()));
            break;
        case 3:  // a float set to something awkward
            if (m.size() >= 12) {
                static const float awkward[] = {0.0f, -0.0f, -1.0f, 1e30f, -1e30f, 3e38f, 1e-40f,
                                                std::numeric_limits<float>::infinity(),
                                                std::numeric_limits<float>::quiet_NaN(), 2.0f};
                const float v = awkward[rng.below(10)];
                std::memcpy(&m[8 + 4 * rng.below(static_cast<uint32_t>((m.size() - 8) / 4))], &v, 4);
            }
            break;
        default:  // noise behind a valid header
            m.resize(8 + rng.below(256));
            for (size_t k = 8; k < m.size(); ++k) m[k] = static_cast<uint8_t>(rng.next());
            m[4] = static_cast<uint8_t>(1 + rng.below(4));
            break;
        }
        feed(m);
    }
    // and nothing anchored or attached is left pointing past the anchors
    CHECK(pieces2().into(r) == grid_relay::kOk);
    r.build(t + 1.0, false, 1.0f, f);
    CHECK(engineTakes(f));
}
