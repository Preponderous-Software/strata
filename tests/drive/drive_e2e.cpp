// End-to-end check of the drive channel (ABI 1.12) on a real headless engine.
//
// The test is the harness and the game at once, on one thread: it listens on
// a Unix socket, points STRATA_DRIVE at it, creates an engine (which connects),
// writes its commands, and then runs a tiny game loop: a cube that moves while
// the right arrow is held. A stepped run is played twice and the two final
// frames must be identical, byte for byte.
//
// Exit codes: 0 pass, 1 fail, 77 skipped (no EGL / GPU on this machine).
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "strata/strata.hpp"

namespace {

int g_failures = 0;

#define EXPECT(cond)                                                       \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr, "%s:%d: EXPECT(%s)\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

constexpr int32_t kRight = 79;  // USB HID usage id of the right arrow

struct Listener {
    int fd = -1;
    std::string path;
    explicit Listener(const std::string& p) : path(p) {
        ::unlink(path.c_str());
        fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path.c_str());
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(fd, 1) != 0) {
            std::perror("bind/listen");
            std::exit(1);
        }
    }
    ~Listener() {
        if (fd >= 0) ::close(fd);
        ::unlink(path.c_str());
    }
};

void writeAll(int fd, const std::string& text) {
    size_t sent = 0;
    while (sent < text.size()) {
        const ssize_t n = ::send(fd, text.data() + sent, text.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) return;
        sent += static_cast<size_t>(n);
    }
}

std::string readLine(int fd) {
    std::string line;
    char c;
    while (::recv(fd, &c, 1, 0) == 1) {
        if (c == '\n') return line;
        line += c;
    }
    return line;
}

// One frame of the tiny game. Returns the seconds the frame advanced.
double frame(strata::Engine& engine, bool& held, float& x) {
    StrataEvent e{};
    while (engine.pollEvent(e)) {
        if (e.type == STRATA_EVENT_KEY_DOWN && e.scancode == kRight) held = true;
        if (e.type == STRATA_EVENT_KEY_UP && e.scancode == kRight) held = false;
    }
    const double dt = engine.frameSeconds();
    if (held) x += static_cast<float>(dt * 2.0);  // two units a second
    int32_t w = 0, h = 0;
    engine.drawableSize(w, h);
    const float eye[3] = {0, 2, 6}, target[3] = {0, 0, 0}, up[3] = {0, 1, 0};
    engine.setCamera(strata::cameraLookAt(eye, target, up, 50.0f, float(w) / float(h), 0.1f, 50.0f));
    engine.setLighting(strata::lightingDefaults(STRATA_LIGHTING_LEGACY));
    StrataFrameDesc fd{};
    fd.clear_flags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH;
    fd.clear_color[0] = 0.1f;
    fd.clear_color[1] = 0.12f;
    fd.clear_color[2] = 0.15f;
    fd.clear_color[3] = 1.0f;
    engine.beginFrame(fd);
    StrataRenderItem cube{};
    cube.kind = STRATA_PRIMITIVE_CUBE;
    cube.position[0] = x - 2.0f;
    cube.scale[0] = cube.scale[1] = cube.scale[2] = 1.0f;
    cube.color[0] = 0.9f;
    cube.color[1] = 0.5f;
    cube.color[2] = 0.2f;
    cube.color[3] = 1.0f;
    engine.submitPrimitives(&cube, 1);
    engine.endFrame();
    engine.present();
    return dt;
}

// A stepped run: hold right for 30 frames of 1/30 s, release, 10 more frames.
// Returns the final frame's pixels.
std::vector<uint8_t> steppedRun(const std::string& dir, int run, bool& skipped) {
    Listener listener(dir + "/drive-" + std::to_string(run) + ".sock");
    ::setenv("STRATA_DRIVE", listener.path.c_str(), 1);
    ::setenv("STRATA_DRIVE_CLOCK", "stepped", 1);
    ::setenv("STRATA_DRIVE_STEP", "0.0333333333333", 1);
    ::setenv("STRATA_DRIVE_SURFACE", "headless", 1);

    strata::EngineDesc desc;
    desc.surface = STRATA_SURFACE_WINDOW;  // S5 turns it into a headless surface
    desc.width = 160;
    desc.height = 120;
    desc.flags = STRATA_FLAG_VSYNC;
    std::unique_ptr<strata::Engine> engine;
    try {
        engine = strata::Engine::create(desc);
    } catch (const strata::Error& e) {
        std::fprintf(stderr, "skipped: no headless GL here (%s)\n", e.what());
        skipped = true;
        return {};
    }
    const int harness = ::accept(listener.fd, nullptr, nullptr);
    EXPECT(harness >= 0);
    EXPECT(engine->driven());
    engine->setRelativeMouseMode(true);  // S3: OK when driven, even headless
    engine->setRelativeMouseMode(false);

    writeAll(harness,
             "{\"op\":\"hello\",\"protocol\":1}\n"
             "{\"op\":\"event\",\"type\":\"key_down\",\"scancode\":79}\n"
             "{\"op\":\"step\",\"frames\":30}\n");
    bool held = false;
    float x = 0.0f;
    double total = 0.0;
    for (int i = 0; i < 30; ++i) total += frame(*engine, held, x);
    const std::string hello = readLine(harness);
    EXPECT(hello.find("\"surface\":\"headless\"") != std::string::npos);
    const std::string abi =
        "\"abi\":\"" + std::to_string(STRATA_ABI_VERSION) + "." + std::to_string(STRATA_ABI_MINOR) + "\"";
    EXPECT(hello.find(abi) != std::string::npos);
    EXPECT(hello.find("\"clock\":\"stepped\"") != std::string::npos);
    EXPECT(readLine(harness).find("\"op\":\"event\"") != std::string::npos);
    EXPECT(readLine(harness).find("\"frames_run\":30") != std::string::npos);
    EXPECT(std::fabs(total - 1.0) < 1e-9);
    EXPECT(std::fabs(x - 2.0f) < 1e-4f);

    const std::string shot = dir + "/drive-" + std::to_string(run) + ".png";
    writeAll(harness,
             "{\"op\":\"event\",\"type\":\"key_up\",\"scancode\":79}\n"
             "{\"op\":\"step\",\"frames\":10}\n");
    for (int i = 0; i < 10; ++i) frame(*engine, held, x);
    EXPECT(readLine(harness).find("\"op\":\"event\"") != std::string::npos);
    EXPECT(readLine(harness).find("\"frame\":40") != std::string::npos);
    EXPECT(std::fabs(x - 2.0f) < 1e-4f);  // released: no further movement

    writeAll(harness, "{\"op\":\"shot\",\"path\":\"" + shot + "\"}\n{\"op\":\"bye\"}\n");
    StrataEvent e{};
    while (engine->pollEvent(e)) {
    }  // the frame boundary handles shot and bye, then the clock runs free
    EXPECT(readLine(harness).find("\"width\":160") != std::string::npos);
    EXPECT(readLine(harness).find("\"op\":\"bye\"") != std::string::npos);
    EXPECT(::access(shot.c_str(), R_OK) == 0);
    EXPECT(engine->driven());  // sticky after bye

    std::vector<uint8_t> pixels;
    int32_t w = 0, h = 0;
    engine->readPixels(pixels, w, h);
    ::close(harness);
    return pixels;
}

}  // namespace

int main() {
    // Unix socket paths are short (108 bytes), so the sockets live in a fresh
    // directory under /tmp rather than the build tree.
    char tmpl[] = "/tmp/strata-drive-XXXXXX";
    if (!::mkdtemp(tmpl)) {
        std::perror("mkdtemp");
        return 1;
    }
    const std::string dir = tmpl;
    bool skipped = false;
    const std::vector<uint8_t> first = steppedRun(dir, 1, skipped);
    if (skipped) return 77;
    const std::vector<uint8_t> second = steppedRun(dir, 2, skipped);
    EXPECT(!first.empty());
    EXPECT(first == second);

    // Undriven: no STRATA_DRIVE, nothing changes, and push_event still works.
    ::unsetenv("STRATA_DRIVE");
    strata::EngineDesc desc;
    desc.surface = STRATA_SURFACE_HEADLESS;
    desc.width = 64;
    desc.height = 48;
    auto engine = strata::Engine::create(desc);
    EXPECT(!engine->driven());
    StrataEvent k{};
    k.type = STRATA_EVENT_KEY_DOWN;
    k.scancode = kRight;
    engine->pushEvent(k);
    bool held = false;
    float x = 0.0f;
    frame(*engine, held, x);
    EXPECT(held);
    bool unsupported = false;
    try {
        engine->setRelativeMouseMode(true);
    } catch (const strata::Error& e) {
        unsupported = e.status() == STRATA_ERROR_UNSUPPORTED;
    }
    EXPECT(unsupported);  // undriven headless keeps its ABI 1.8 behaviour

    for (int run = 1; run <= 2; ++run) ::unlink((dir + "/drive-" + std::to_string(run) + ".png").c_str());
    ::rmdir(dir.c_str());
    if (g_failures == 0) std::puts("drive end-to-end: ok (two stepped runs, identical frames)");
    return g_failures == 0 ? 0 : 1;
}
