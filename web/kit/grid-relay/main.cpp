// The grid relay: a web-kit scene that draws a grid scene described by the
// page (WIRE.md). Scripts in the page cannot submit primitives themselves, so
// a game running elsewhere in the page (Python under pygbag, for one) hands
// this scene versioned binary messages: the board once, pieces once per game
// tick, the HUD when it changes, the view when the camera or window does. The
// relay slides pieces between ticks (not under reduced motion) and draws
// what the desktop draws for the same scene: same camera, sun, shadows, flat
// clear colour, screen panels and text. It knows nothing about any game.
//
// relay.h holds the format and the state; this file hands them to the kit.
#define STRATA_KIT_MAIN
#include "strata_kit.h"

#include "relay.h"

namespace {

grid_relay::Relay relay;
grid_relay::Frame frameOut;
float uiScale = 1.0f;
int lastStatus = 0;
float orbit[6] = {};

// The desktop view's lighting: four 2048 cascades, shadows to 40 units, no
// fog. Lower tiers give up shadows first, as every kit scene does.
void applyView(strata_kit::Host& h) {
    const grid_relay::View& v = relay.view();
    h.opts.fovDegrees = v.fov;
    h.opts.nearPlane = v.nearPlane;
    h.opts.farPlane = v.farPlane;
    h.clearFlags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH;
    std::copy(v.clear, v.clear + 4, h.clearColor);
    std::copy(v.sun, v.sun + 3, h.toSun);
    if (h.engine) strata_set_sun_direction(h.engine, h.toSun);
    const grid_relay::Orbit o = grid_relay::orbitOf(v);
    orbit[0] = o.yaw;
    orbit[1] = o.pitch;
    orbit[2] = o.dist;
    std::copy(o.target, o.target + 3, orbit + 3);
}

}  // namespace

void strata_kit_configure(strata_kit::Options& o) {
    o.name = "grid-relay";
    // Desktop Strata draws without multisampling (its SDL window and headless
    // surfaces both ask for none), and the relay is held to the desktop's
    // golden images, so the browser's MSAA is turned off too.
    o.antialias = false;
    o.fog = false;
    o.shadowDistanceMin = 40.0f;
    o.shadowDistancePerDist = 0.0f;
    o.farPlane = 200.0f;
    o.fovDegrees = 42.0f;
    o.tiers[0] = {0, 1024, 4, false};
    o.tiers[1] = {1, 1024, 8, false};
    o.tiers[2] = {4, 2048, STRATA_MAX_POINT_LIGHTS, false};
}

void strata_kit_engine_ready(strata_kit::Host& h) { applyView(h); }

void strata_kit_build(strata_kit::Host& h, double t) {
    if (!relay.hasBoard()) return;
    relay.build(t, h.reducedMotion, uiScale, frameOut);
    for (const StrataRenderItem& it : frameOut.items) {
        h.add(it.kind, it.position[0], it.position[1], it.position[2], it.scale[0], it.scale[1], it.scale[2],
              {it.color[0], it.color[1], it.color[2]}, it.color[3]);
    }
    for (const StrataPointLight& l : frameOut.lights) h.light(l, 0);
    for (const StrataRect& r : frameOut.rects)
        h.rect(r.x, r.y, r.width, r.height, {r.color[0], r.color[1], r.color[2]}, r.color[3]);
    for (const StrataText& x : frameOut.texts)
        h.text(x.x, x.y, x.utf8, x.pixel_size, {x.color[0], x.color[1], x.color[2]}, x.color[3], x.flags);
}

// One wire message (WIRE.md) from the module heap. Returns 0 or a negative
// grid_relay::Status; the page reports errors.
STRATA_KIT_EXPORT int gr_submit(const uint8_t* data, int size) {
    if (size < 0) return grid_relay::kTruncated;
    lastStatus = relay.submit(data, static_cast<size_t>(size));
    if (lastStatus == grid_relay::kOk && data[4] == grid_relay::kView) applyView(strata_kit::host());
    return lastStatus;
}

// The format version this build reads: major * 256 + minor.
STRATA_KIT_EXPORT int gr_wire_version() { return grid_relay::kMajor * 256 + grid_relay::kMinor; }

// CSS pixels to drawing-buffer pixels for the HUD (the page's device pixel ratio).
STRATA_KIT_EXPORT void gr_set_ui_scale(float scale) { uiScale = scale > 0.0f ? scale : 1.0f; }

// The view's camera as the kit's orbit: yaw, pitch, dist, target x, y, z.
STRATA_KIT_EXPORT const float* gr_orbit() { return orbit; }

// Messages taken, and whether a board has arrived (for tools and tests).
STRATA_KIT_EXPORT int gr_messages() { return static_cast<int>(relay.messages()); }
STRATA_KIT_EXPORT int gr_has_board() { return relay.hasBoard() ? 1 : 0; }

#ifndef __EMSCRIPTEN__
// Natively the same scene renders stills: the kit's sk_init_headless and
// sk_save_png, with the camera the view asked for.
STRATA_KIT_EXPORT int gr_frame_headless(double t) {
    sk_set_camera(orbit[0], orbit[1], orbit[2], orbit[3], orbit[4], orbit[5]);
    return sk_frame(t);
}
#endif
