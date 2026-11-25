// The web kit's camera basis and camera-facing planes (web/kit/strata_kit.h,
// kit 1.4), checked against the matrices strata_camera_look_at makes for the
// same orbit camera: right must land to the right of the screen and up above
// its middle, from any yaw, so glyphs drawn on a Billboard never read mirrored.
#include "strata_kit.h"
#include "test.hpp"

namespace {

struct Ndc {
    float x, y;
};

// The orbit camera exactly as sk_frame builds it, and a world point through it.
struct View {
    float vp[16];

    View(float yaw, float pitch, float dist, const float target[3]) {
        float eye[3];
        strata_kit::orbitEye(yaw, pitch, dist, target, eye);
        const float up[3] = {0.0f, 1.0f, 0.0f};
        StrataCamera cam{};
        REQUIRE(strata_camera_look_at(eye, target, up, 50.0f, 16.0f / 9.0f, 0.3f, 400.0f, &cam) == STRATA_OK);
        strata_kit::mul4(cam.projection, cam.view, vp);
    }

    Ndc project(const float p[3]) const {
        const float x = vp[0] * p[0] + vp[4] * p[1] + vp[8] * p[2] + vp[12];
        const float y = vp[1] * p[0] + vp[5] * p[1] + vp[9] * p[2] + vp[13];
        const float w = vp[3] * p[0] + vp[7] * p[1] + vp[11] * p[2] + vp[15];
        return {x / w, y / w};
    }
};

float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

}  // namespace

TEST(kit_camera_basis_is_orthonormal) {
    for (float yaw : {0.0f, 0.8f, 2.5f, -1.9f}) {
        for (float pitch : {0.12f, 0.6f, 1.45f}) {
            float r[3], u[3], f[3];
            strata_kit::cameraBasis(yaw, pitch, r, u, f);
            CHECK_NEAR(dot(r, r), 1.0, 1e-5);
            CHECK_NEAR(dot(u, u), 1.0, 1e-5);
            CHECK_NEAR(dot(f, f), 1.0, 1e-5);
            CHECK_NEAR(dot(r, u), 0.0, 1e-5);
            CHECK_NEAR(dot(r, f), 0.0, 1e-5);
            CHECK_NEAR(dot(u, f), 0.0, 1e-5);
            CHECK(r[1] == 0.0f);  // right stays level
            CHECK(u[1] > 0.0f);
        }
    }
}

TEST(kit_camera_basis_matches_the_rendered_view) {
    const float target[3] = {2.0f, 1.0f, -3.0f};
    for (float yaw : {0.0f, 0.8f, 2.5f, -1.9f, 4.0f}) {
        for (float pitch : {0.12f, 0.6f, 1.2f}) {
            const View view(yaw, pitch, 30.0f, target);
            float r[3], u[3], f[3];
            strata_kit::cameraBasis(yaw, pitch, r, u, f);
            const Ndc c = view.project(target);
            CHECK_NEAR(c.x, 0.0, 1e-4);  // the target is the middle of the screen
            CHECK_NEAR(c.y, 0.0, 1e-4);
            const float pr[3] = {target[0] + r[0], target[1] + r[1], target[2] + r[2]};
            const float pu[3] = {target[0] + u[0], target[1] + u[1], target[2] + u[2]};
            const float pf[3] = {target[0] + 5 * f[0], target[1] + 5 * f[1], target[2] + 5 * f[2]};
            const Ndc a = view.project(pr), b = view.project(pu), d = view.project(pf);
            CHECK(a.x > 0.01f);              // right is to the right ...
            CHECK_NEAR(a.y, 0.0, 1e-4);      // ... and level
            CHECK(b.y > 0.01f);              // up is up ...
            CHECK_NEAR(b.x, 0.0, 1e-4);      // ... and straight
            CHECK_NEAR(d.x, 0.0, 1e-4);      // forward stays in the middle
            CHECK_NEAR(d.y, 0.0, 1e-4);
        }
    }
}

TEST(kit_default_camera_puts_plus_x_on_the_right) {
    // At the kit's default yaw and pitch, +x runs right and +z left: why
    // glyphs laid out along +z (or along the vector strata-kit.js 1.3 called
    // screen-right) came out mirrored.
    const float origin[3] = {0.0f, 0.0f, 0.0f}, px[3] = {1.0f, 0.0f, 0.0f}, pz[3] = {0.0f, 0.0f, 1.0f};
    const View view(0.8f, 0.6f, 30.0f, origin);
    CHECK(view.project(px).x > 0.0f);
    CHECK(view.project(pz).x < 0.0f);
}

TEST(kit_billboard_reads_left_to_right_from_any_yaw) {
    strata_kit::Host h;
    const float target[3] = {0.0f, 0.0f, 0.0f};
    for (float yaw : {0.0f, 0.8f, 2.5f, -1.9f}) {
        h.yaw = yaw;
        h.pitch = 0.9f;
        strata_kit::cameraBasis(h.yaw, h.pitch, h.camRight, h.camUp, h.camForward);
        const View view(h.yaw, h.pitch, 30.0f, target);
        for (bool upright : {true, false}) {
            const strata_kit::Billboard b = h.billboard(1.0f, 0.5f, -1.0f, 0.5f, upright);
            float o[3], u1[3], v1[3];
            b.at(0, 0, o);
            b.at(1, 0, u1);
            b.at(0, 1, v1);
            CHECK_NEAR(o[0], 1.0, 1e-6);
            CHECK_NEAR(o[1], 0.5, 1e-6);
            CHECK_NEAR(o[2], -1.0, 1e-6);
            const Ndc no = view.project(o), nu = view.project(u1), nv = view.project(v1);
            CHECK(nu.x > no.x);  // u runs right
            CHECK(nv.y > no.y);  // v runs up
            if (upright) {
                CHECK_NEAR(v1[0], o[0], 1e-6);  // an upright plane stands on world +y
                CHECK_NEAR(v1[1] - o[1], 0.5, 1e-6);
            }
        }
    }
    h.lines.clear();
    h.line(h.billboard(0, 0, 0, 2.0f), -1, 0, 1, 0, {1, 1, 1});
    REQUIRE(h.lines.size() == 2);
    CHECK_NEAR(h.lines[1].position[0] - h.lines[0].position[0], 4.0 * h.camRight[0], 1e-5);
}
