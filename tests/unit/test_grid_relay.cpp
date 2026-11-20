// The web kit's grid relay (web/kit/grid-relay/relay.h, WIRE.md): its wire
// format reader and the frame it builds, without GL.
#include <cstring>
#include <string>
#include <vector>

#include "relay.h"
#include "test.hpp"

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
