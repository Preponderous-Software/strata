// relay.h: the grid relay's wire format reader and scene state.
//
// Part of the web kit's grid relay (web/kit/grid-relay, WIRE.md). A page
// that cannot submit primitives itself (Python under pygbag, any script)
// describes a grid scene in versioned binary messages; this file checks and
// keeps them and, each frame, turns them into primitives, point lights,
// screen panels and screen text. It knows nothing about any game.
//
// Pure C++17 on strata.h's structs: no GL, no kit, so tests/unit checks it
// natively. main.cpp is the kit scene around it.
//
// MIT licensed, as Strata.
#ifndef STRATA_GRID_RELAY_H
#define STRATA_GRID_RELAY_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "strata/strata.h"

namespace grid_relay {

// Wire format version. A message whose major differs is refused; a newer
// minor of the same major is read (fields a newer minor appends at the end of
// a message are ignored).
constexpr uint8_t kMajor = 1;
constexpr uint8_t kMinor = 0;

enum Kind : uint8_t { kBoard = 1, kPieces = 2, kHud = 3, kView = 4 };

// submit() results. Negative values are errors; the scene is left as it was.
enum Status : int {
    kOk = 0,
    kBadMagic = -1,       // not a relay message
    kBadMajor = -2,       // another major version of the format
    kTruncated = -3,      // shorter than its counts say
    kBadKind = -4,        // unknown message kind
    kBadValue = -5,       // a shape kind, attachment or index out of range, a non-finite number, or a
                          // light or HUD colour the engine would refuse
    kTooLarge = -6,       // over the limits below
};

constexpr size_t kHeaderBytes = 8;
constexpr size_t kShapeBytes = 48;
constexpr size_t kLightBytes = 36;
constexpr size_t kAnchorBytes = 16;
constexpr size_t kPanelBytes = 32;
constexpr size_t kLabelFixedBytes = 32;
constexpr size_t kViewBytes = 64;

constexpr uint32_t kMaxShapes = 1u << 16;
constexpr uint32_t kMaxAnchors = 1u << 15;
constexpr uint32_t kMaxHudItems = 4096;
// Shadow-casting lights in one message (strata.h's STRATA_MAX_SHADOWED_POINT_LIGHTS). A frame
// also keeps to it across the board's and the pieces' lights together: see build().
constexpr uint32_t kMaxShadowedLights = STRATA_MAX_SHADOWED_POINT_LIGHTS;
// The largest world coordinate, offset, size or light range taken. A board
// is a few hundred units across at most; the bound keeps every sum the relay
// makes (an anchor plus an offset, a slide between two anchors) finite.
constexpr float kMaxCoordinate = 1.0e6f;

inline bool within(const float* v, int n) {
    for (int i = 0; i < n; ++i)
        if (!(std::fabs(v[i]) <= kMaxCoordinate)) return false;
    return true;
}

// Attachments: where a shape or light's position is measured from.
enum Attach : uint8_t {
    kWorld = 0,   // absolute
    kPiece = 1,   // offset from piece anchor a (x, z interpolated)
    kBridge = 2,  // offset from the midpoint of anchors a and b; x/z scale grows to span them
};

struct Shape {
    uint8_t kind = 0, attach = kWorld;
    uint16_t a = 0, b = 0;
    float pos[3] = {}, scale[3] = {}, color[4] = {};
};

struct Light {
    uint8_t attach = kWorld, flags = 0;
    uint16_t a = 0;
    float pos[3] = {}, color[3] = {}, radius = 0.0f, intensity = 0.0f;
};

struct Anchor {
    float start[2] = {}, end[2] = {};
};

struct Panel {
    float x = 0, y = 0, w = 0, h = 0, color[4] = {};
};

struct Label {
    float x = 0, y = 0, size = 16.0f, color[4] = {};
    std::string text;
};

struct View {
    float eye[3] = {0.0f, 10.0f, 10.0f}, target[3] = {0.0f, 0.0f, 0.0f};
    float fov = 42.0f, nearPlane = 0.3f, farPlane = 200.0f;
    float clear[4] = {0.10f, 0.12f, 0.16f, 1.0f};
    float sun[3] = {-0.45f, 0.85f, 0.35f};
};

// The orbit form of a camera: what strata_kit's sk_set_camera takes.
struct Orbit {
    float yaw, pitch, dist, target[3];
};

inline Orbit orbitOf(const View& v) {
    const double dx = double(v.eye[0]) - v.target[0], dy = double(v.eye[1]) - v.target[1],
                 dz = double(v.eye[2]) - v.target[2];
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    Orbit o{};
    o.dist = static_cast<float>(dist);
    o.pitch = dist > 0.0 ? static_cast<float>(std::asin(std::clamp(dy / dist, -1.0, 1.0))) : 0.0f;
    o.yaw = static_cast<float>(std::atan2(dz, dx));
    std::copy(v.target, v.target + 3, o.target);
    return o;
}

// Bounds-checked little-endian reader.
class Reader {
public:
    Reader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    bool has(size_t bytes) const { return n_ - at_ >= bytes; }
    size_t at() const { return at_; }
    uint8_t u8() { return p_[at_++]; }
    uint16_t u16() {
        const uint16_t v = static_cast<uint16_t>(p_[at_] | (p_[at_ + 1] << 8));
        at_ += 2;
        return v;
    }
    uint32_t u32() {
        const uint32_t v = static_cast<uint32_t>(p_[at_]) | (static_cast<uint32_t>(p_[at_ + 1]) << 8) |
                           (static_cast<uint32_t>(p_[at_ + 2]) << 16) | (static_cast<uint32_t>(p_[at_ + 3]) << 24);
        at_ += 4;
        return v;
    }
    float f32() {
        const uint32_t bits = u32();
        float f;
        std::memcpy(&f, &bits, sizeof f);
        if (!std::isfinite(f)) finite_ = false;
        return f;
    }
    void floats(float* out, int n) {
        for (int i = 0; i < n; ++i) out[i] = f32();
    }
    void skip(size_t bytes) { at_ += bytes; }
    const uint8_t* here() const { return p_ + at_; }
    bool finite() const { return finite_; }

private:
    const uint8_t* p_;
    size_t n_;
    size_t at_ = 0;
    bool finite_ = true;
};

// What a frame draws, in the relay's own terms (main.cpp hands it to the kit).
struct Frame {
    std::vector<StrataRenderItem> items;
    std::vector<StrataPointLight> lights;
    std::vector<StrataRect> rects;
    std::vector<StrataText> texts;  // utf8 points into the Relay's labels
};

class Relay {
public:
    // One message. Returns kOk or a negative Status; on an error nothing changes.
    // The header is read field by field, so a short message says what it is
    // as far as it goes: not a relay message, another major, or truncated
    // (WIRE.md, "Errors"; graphik's grid3d.web.readHeader agrees).
    int submit(const uint8_t* data, size_t size) {
        if (!data || size < 2 || data[0] != 'G' || data[1] != '3') return kBadMagic;
        if (size < 3) return kTruncated;
        if (data[2] != kMajor) return kBadMajor;
        if (size < kHeaderBytes) return kTruncated;
        Reader r(data, size);
        r.skip(kHeaderBytes);
        switch (data[4]) {
        case kBoard: return readBoard(r);
        case kPieces: return readPieces(r);
        case kHud: return readHud(r);
        case kView: return readView(r);
        default: return kBadKind;
        }
    }

    // The fraction of the current tick's slide done at scene time t. The tick
    // starts at the first frame drawn after its message arrived.
    float alpha(double t, bool reducedMotion) {
        if (tickPending_) {
            tickStart_ = t;
            tickPending_ = false;
        }
        if (reducedMotion || tickSeconds_ <= 0.0f) return 1.0f;
        return static_cast<float>(std::clamp((t - tickStart_) / tickSeconds_, 0.0, 1.0));
    }

    // Everything at scene time t. uiScale maps the HUD's CSS pixels to the
    // drawing buffer's pixels (the device pixel ratio the kit settled on).
    void build(double t, bool reducedMotion, float uiScale, Frame& out) {
        const float k = alpha(t, reducedMotion);
        anchorsNow_.resize(anchors_.size() * 2);
        for (size_t i = 0; i < anchors_.size(); ++i) {
            const Anchor& a = anchors_[i];
            anchorsNow_[i * 2] = a.start[0] + (a.end[0] - a.start[0]) * k;
            anchorsNow_[i * 2 + 1] = a.start[1] + (a.end[1] - a.start[1]) * k;
        }
        out.items.clear();
        out.lights.clear();
        out.rects.clear();
        out.texts.clear();
        for (const Shape& s : boardShapes_) emitShape(s, out);
        for (const Shape& s : pieceShapes_) emitShape(s, out);
        // Each message keeps to the engine's shadow-caster limit, but the
        // board's and the pieces' together may not: past the limit a light
        // still shines, without its shadow (the engine would refuse the
        // whole frame's lights otherwise).
        uint32_t casters = 0;
        for (const Light& l : boardLights_) emitLight(l, out, casters);
        for (const Light& l : pieceLights_) emitLight(l, out, casters);
        if (!(uiScale > 0.0f) || !std::isfinite(uiScale)) uiScale = 1.0f;
        for (const Panel& p : panels_) {
            StrataRect r{};
            r.x = p.x * uiScale;
            r.y = p.y * uiScale;
            r.width = std::max(p.w, 0.0f) * uiScale;
            r.height = std::max(p.h, 0.0f) * uiScale;
            std::copy(p.color, p.color + 4, r.color);
            // one panel scaled past a float's range would make the engine
            // refuse every panel this frame: that one is left out
            if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.width) || !std::isfinite(r.height))
                continue;
            out.rects.push_back(r);
        }
        for (const Label& l : labels_) {
            StrataText x{};
            x.x = l.x * uiScale;
            x.y = l.y * uiScale;
            if (!std::isfinite(x.x) || !std::isfinite(x.y)) continue;  // as for panels
            x.pixel_size = std::clamp(l.size * uiScale, 1.0f, 512.0f);
            x.flags = STRATA_TEXT_SHADOW;
            std::copy(l.color, l.color + 4, x.color);
            x.utf8 = l.text.c_str();
            out.texts.push_back(x);
        }
    }

    const View& view() const { return view_; }
    bool hasView() const { return hasView_; }
    bool hasBoard() const { return hasBoard_; }
    uint32_t messages() const { return messages_; }
    size_t anchorCount() const { return anchors_.size(); }

private:
    static bool readShape(Reader& r, Shape& s) {
        s.kind = r.u8();
        s.attach = r.u8();
        s.a = r.u16();
        s.b = r.u16();
        r.u16();  // reserved
        r.floats(s.pos, 3);
        r.floats(s.scale, 3);
        r.floats(s.color, 4);
        return s.kind < STRATA_PRIMITIVE_KIND_COUNT && s.attach <= kBridge && within(s.pos, 3) && within(s.scale, 3);
    }
    // What strata_set_point_lights takes (src/render/lights.cpp): a positive
    // range, a colour and intensity that are not negative. A light the engine
    // would refuse is refused here, where send() can say so, rather than
    // dropping every light of every frame later.
    static bool readLight(Reader& r, Light& l) {
        l.attach = r.u8();
        l.flags = r.u8();
        l.a = r.u16();
        r.floats(l.pos, 3);
        r.floats(l.color, 3);
        l.radius = r.f32();
        l.intensity = r.f32();
        return l.attach <= kPiece && (l.flags & ~1u) == 0 && within(l.pos, 3) && l.radius > 0.0f &&
               l.radius <= kMaxCoordinate && l.intensity >= 0.0f &&
               l.color[0] >= 0.0f && l.color[1] >= 0.0f && l.color[2] >= 0.0f;
    }
    // What strata_submit_rects and strata_submit_text take for a colour:
    // nothing negative, alpha at most 1 (NaN is caught by Reader::finite).
    static bool hudColorOk(const float c[4]) {
        return c[0] >= 0.0f && c[1] >= 0.0f && c[2] >= 0.0f && c[3] >= 0.0f && c[3] <= 1.0f;
    }
    static bool attachOk(uint8_t attach, uint16_t a, uint16_t b, size_t anchors) {
        if (attach == kWorld) return true;
        if (a >= anchors) return false;
        return attach != kBridge || b < anchors;
    }

    int readShapesLights(Reader& r, uint32_t nShapes, uint32_t nLights, size_t anchors, std::vector<Shape>& shapes,
                         std::vector<Light>& lights) {
        if (nShapes > kMaxShapes || nLights > STRATA_MAX_POINT_LIGHTS) return kTooLarge;
        if (!r.has(size_t(nShapes) * kShapeBytes + size_t(nLights) * kLightBytes)) return kTruncated;
        shapes.resize(nShapes);
        for (Shape& s : shapes)
            if (!readShape(r, s) || !attachOk(s.attach, s.a, s.b, anchors)) return kBadValue;
        lights.resize(nLights);
        uint32_t casters = 0;
        for (Light& l : lights) {
            if (!readLight(r, l) || !attachOk(l.attach, l.a, 0, anchors)) return kBadValue;
            casters += l.flags & 1u;
        }
        if (!r.finite()) return kBadValue;
        return casters > kMaxShadowedLights ? kTooLarge : kOk;
    }

    int readBoard(Reader& r) {
        if (!r.has(8)) return kTruncated;
        const uint32_t nShapes = r.u32(), nLights = r.u32();
        std::vector<Shape> shapes;
        std::vector<Light> lights;
        const int rc = readShapesLights(r, nShapes, nLights, 0, shapes, lights);  // board: world only
        if (rc != kOk) return rc;
        boardShapes_.swap(shapes);
        boardLights_.swap(lights);
        hasBoard_ = true;
        ++messages_;
        return kOk;
    }

    int readPieces(Reader& r) {
        if (!r.has(16)) return kTruncated;
        const float tick = r.f32();
        const uint32_t nAnchors = r.u32(), nShapes = r.u32(), nLights = r.u32();
        if (nAnchors > kMaxAnchors) return kTooLarge;
        if (!r.has(size_t(nAnchors) * kAnchorBytes)) return kTruncated;
        std::vector<Anchor> anchors(nAnchors);
        for (Anchor& a : anchors) {
            r.floats(a.start, 2);
            r.floats(a.end, 2);
            if (!within(a.start, 2) || !within(a.end, 2)) return kBadValue;
        }
        std::vector<Shape> shapes;
        std::vector<Light> lights;
        const int rc = readShapesLights(r, nShapes, nLights, nAnchors, shapes, lights);
        if (rc != kOk) return rc;
        if (!r.finite() || tick < 0.0f) return kBadValue;
        anchors_.swap(anchors);
        pieceShapes_.swap(shapes);
        pieceLights_.swap(lights);
        tickSeconds_ = tick;
        tickPending_ = true;
        ++messages_;
        return kOk;
    }

    int readHud(Reader& r) {
        if (!r.has(8)) return kTruncated;
        const uint32_t nPanels = r.u32(), nLabels = r.u32();
        if (nPanels > kMaxHudItems || nLabels > kMaxHudItems) return kTooLarge;
        if (!r.has(size_t(nPanels) * kPanelBytes)) return kTruncated;
        std::vector<Panel> panels(nPanels);
        for (Panel& p : panels) {
            p.x = r.f32();
            p.y = r.f32();
            p.w = r.f32();
            p.h = r.f32();
            r.floats(p.color, 4);
            if (!hudColorOk(p.color)) return kBadValue;
        }
        std::vector<Label> labels(nLabels);
        for (Label& l : labels) {
            if (!r.has(kLabelFixedBytes)) return kTruncated;
            l.x = r.f32();
            l.y = r.f32();
            l.size = r.f32();
            r.floats(l.color, 4);
            const uint16_t len = r.u16();
            r.u16();  // reserved
            const size_t padded = (size_t(len) + 3u) & ~size_t(3);
            if (!r.has(padded)) return kTruncated;
            l.text.assign(reinterpret_cast<const char*>(r.here()), len);
            r.skip(padded);
            if (l.text.find('\0') != std::string::npos || !hudColorOk(l.color)) return kBadValue;
        }
        if (!r.finite()) return kBadValue;
        panels_.swap(panels);
        labels_.swap(labels);
        ++messages_;
        return kOk;
    }

    int readView(Reader& r) {
        if (!r.has(kViewBytes)) return kTruncated;
        View v;
        r.floats(v.eye, 3);
        r.floats(v.target, 3);
        v.fov = r.f32();
        v.nearPlane = r.f32();
        v.farPlane = r.f32();
        r.floats(v.clear, 4);
        r.floats(v.sun, 3);
        if (!r.finite() || v.fov <= 0.0f || v.fov >= 180.0f || v.nearPlane <= 0.0f || v.farPlane <= v.nearPlane)
            return kBadValue;
        if (!within(v.eye, 3) || !within(v.target, 3)) return kBadValue;
        // an eye on its target looks nowhere (the orbit's distance would be 0)
        if (v.eye[0] == v.target[0] && v.eye[1] == v.target[1] && v.eye[2] == v.target[2]) return kBadValue;
        view_ = v;
        hasView_ = true;
        ++messages_;
        return kOk;
    }

    void anchorAt(uint16_t i, float& x, float& z) const {
        x = anchorsNow_[size_t(i) * 2];
        z = anchorsNow_[size_t(i) * 2 + 1];
    }

    void emitShape(const Shape& s, Frame& out) const {
        StrataRenderItem it{};
        it.kind = s.kind;
        std::copy(s.pos, s.pos + 3, it.position);
        std::copy(s.scale, s.scale + 3, it.scale);
        std::copy(s.color, s.color + 4, it.color);
        if (s.attach == kPiece) {
            float x, z;
            anchorAt(s.a, x, z);
            it.position[0] += x;
            it.position[2] += z;
        } else if (s.attach == kBridge) {
            float ax, az, bx, bz;
            anchorAt(s.a, ax, az);
            anchorAt(s.b, bx, bz);
            it.position[0] += (ax + bx) * 0.5f;
            it.position[2] += (az + bz) * 0.5f;
            it.scale[0] = std::max(it.scale[0], std::fabs(ax - bx));
            it.scale[2] = std::max(it.scale[2], std::fabs(az - bz));
        }
        out.items.push_back(it);
    }

    void emitLight(const Light& l, Frame& out, uint32_t& casters) const {
        StrataPointLight p{};
        std::copy(l.pos, l.pos + 3, p.position);
        std::copy(l.color, l.color + 3, p.color);
        p.range = l.radius;
        p.intensity = l.intensity;
        const bool shadows = (l.flags & 1u) && casters < kMaxShadowedLights;
        casters += shadows ? 1u : 0u;
        p.flags = shadows ? STRATA_POINT_LIGHT_CAST_SHADOWS : 0u;
        if (l.attach == kPiece) {
            float x, z;
            anchorAt(l.a, x, z);
            p.position[0] += x;
            p.position[2] += z;
        }
        out.lights.push_back(p);
    }

    std::vector<Shape> boardShapes_, pieceShapes_;
    std::vector<Light> boardLights_, pieceLights_;
    std::vector<Anchor> anchors_;
    std::vector<float> anchorsNow_;
    std::vector<Panel> panels_;
    std::vector<Label> labels_;
    View view_;
    bool hasView_ = false, hasBoard_ = false;
    float tickSeconds_ = 0.0f;
    bool tickPending_ = false;
    double tickStart_ = 0.0;
    uint32_t messages_ = 0;
};

}  // namespace grid_relay

#endif  // STRATA_GRID_RELAY_H
