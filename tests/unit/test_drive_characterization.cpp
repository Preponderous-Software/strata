// Characterization of the drive channel: each scenario plays a harness over a
// socket pair (or through STRATA_DRIVE*) and pins the exact transcript, every
// reply line and every log line, byte for byte. They were written against the
// drive channel before it was split into ports and adapters, and pass on both.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/drive/adapters/steady_clock.hpp"
#include "core/drive/adapters/unix_socket_link.hpp"
#include "core/drive/drive.hpp"
#include "core/drive/service.hpp"
#include "core/error.hpp"
#include "strata/strata.h"
#include "test.hpp"

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using strata::drive::ClockMode;
using strata::drive::Drive;

namespace {

std::unique_ptr<strata::drive::MonotonicClock> steadyClock() { return std::make_unique<strata::drive::SteadyClock>(); }
std::unique_ptr<strata::drive::HarnessLink> socketLink(int fd) { return std::make_unique<strata::drive::UnixSocketLink>(fd); }

struct Transcript {
    std::string text;
    static void log(int32_t level, const char* message, void* user) {
        auto* t = static_cast<Transcript*>(user);
        t->text += "log " + std::to_string(level) + ": " + message + "\n";
    }
    void note(const std::string& line) { text += line + "\n"; }
};

std::string mask(std::string line, const std::string& key, const char* with) {
    const size_t at = line.find(key);
    if (at == std::string::npos) return line;
    const size_t end = line.find('"', at + key.size());
    return line.replace(at + key.size(), end - at - key.size(), with);
}

// Strips the library's version and ABI minor from a hello reply, so the pin
// survives a release: "strata":"0.10.0" becomes "strata":"V", "abi":"1.18"
// becomes "abi":"A".
std::string maskVersion(const std::string& line) {
    return mask(mask(line, "\"strata\":\"", "V"), "\"abi\":\"", "A");
}

struct Harness {
    int harness = -1;
    int engine = -1;
    Transcript* t;
    explicit Harness(Transcript& transcript) : t(&transcript) {
        int fds[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0) {
            harness = fds[0];
            engine = fds[1];
        }
    }
    ~Harness() { close(); }
    void close() {
        if (harness >= 0) ::close(harness);
        harness = -1;
    }
    void write(const std::string& bytes) const {
        size_t sent = 0;
        while (sent < bytes.size()) {
            const ssize_t n = ::send(harness, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) return;
            sent += static_cast<size_t>(n);
        }
    }
    // Appends every complete line the engine sent as "< line".
    void drain() const {
        std::string all;
        for (;;) {
            pollfd p{};
            p.fd = harness;
            p.events = POLLIN;
            if (::poll(&p, 1, 0) <= 0) break;
            char buffer[4096];
            const ssize_t n = ::recv(harness, buffer, sizeof buffer, 0);
            if (n <= 0) break;
            all.append(buffer, static_cast<size_t>(n));
        }
        size_t start = 0;
        for (size_t nl; (nl = all.find('\n', start)) != std::string::npos; start = nl + 1) {
            t->note("< " + maskVersion(all.substr(start, nl - start)));
        }
        if (start < all.size()) t->note("<partial " + all.substr(start));
    }
};

std::string state(const Drive& d) {
    char buffer[160];
    std::snprintf(buffer, sizeof buffer, "state connected=%d driven=%d clock=%s step=%.17g frames=%llu rel=%d",
                  d.connected() ? 1 : 0, d.wasDriven() ? 1 : 0,
                  d.clockMode() == ClockMode::Stepped ? "stepped" : "free", d.step(),
                  static_cast<unsigned long long>(d.framesPresented()), d.relativeMouse() ? 1 : 0);
    return buffer;
}

std::string event(const StrataEvent& e) {
    char buffer[200];
    std::snprintf(buffer, sizeof buffer,
                  "event type=%d scancode=%d mods=%d repeat=%d button=%d size=%dx%d x=%g y=%g dx=%g dy=%g text='%s'",
                  e.type, e.scancode, e.modifiers, e.repeat, e.button, e.width, e.height, e.x, e.y, e.dx, e.dy,
                  e.text);
    return buffer;
}

void popAll(Drive& d, Transcript& t) {
    StrataEvent e{};
    while (d.pop(e)) t.note(event(e));
}

bool matches(const char* name, const std::string& actual, const char* expected) {
    if (actual == expected) return true;
    std::fprintf(stderr, "---- %s: actual transcript ----\n%s---- end ----\n", name, actual.c_str());
    return false;
}

struct LogCapture {
    explicit LogCapture(Transcript& t) { strata_set_log_callback(&Transcript::log, &t); }
    ~LogCapture() { strata_set_log_callback(nullptr, nullptr); }
};

struct Env {
    std::vector<std::string> names;
    void set(const char* name, const char* value) {
        ::setenv(name, value, 1);
        names.emplace_back(name);
    }
    ~Env() {
        for (const std::string& n : names) ::unsetenv(n.c_str());
    }
};

}  // namespace

TEST(drive_char_free_clock_replies_and_refusals) {
    Transcript t;
    LogCapture logs(t);
    {
        Harness h(t);
        REQUIRE(h.engine >= 0);
        Drive d{steadyClock()};
        d.attach(socketLink(h.engine), ClockMode::Free, 1.0 / 60.0);
        t.note(state(d));
        h.write("not json\n"
                "\n"
                "   \r\n"
                "{}\n"
                "{\"op\":\"hello\",\"protocol\":2}\n"
                "{\"op\":\"hello\"}\n"
                "{\"op\":\"event\",\"type\":\"key_down\",\"scancode\":80,\"modifiers\":3,\"repeat\":1,\"button\":2,"
                "\"width\":5,\"height\":6,\"x\":1.5,\"y\":-2.25,\"dx\":0.5,\"dy\":3e9,\"text\":\"hi\"}\n"
                "{\"op\":\"event\",\"type\":9,\"scancode\":1e12,\"text\":\"0123456789012345678901234567890\"}\r\n"
                "{\"op\":\"event\",\"type\":\"text_input\",\"text\":\"01234567890123456789012345678901\"}\n"
                "{\"op\":\"event\",\"type\":\"warp\"}\n"
                "{\"op\":\"event\"}\n"
                "{\"op\":\"stats\"}\n"
                "{\"op\":\"shot\"}\n"
                "{\"op\":\"shot\",\"path\":\"/tmp/a.png\"}\n"
                "{\"op\":\"clock\",\"mode\":\"fast\"}\n"
                "{\"op\":\"clock\",\"mode\":\"free\",\"step\":1.5}\n"
                "{\"op\":\"clock\",\"mode\":\"free\",\"step\":0.125}\n"
                "{\"op\":\"step\",\"frames\":2}\n"
                "{\"op\":42}\n"
                "{\"op\":\"bye\"}\n"
                "{\"op\":\"stats\"}\n");
        d.beginPoll();
        h.drain();
        popAll(d, t);
        t.note(state(d));
        t.note("seconds " + std::to_string(d.frameSeconds()));
        d.presented();
        t.note(state(d));
    }
    CHECK(matches("free", t.text,
                  "state connected=1 driven=1 clock=free step=0.016666666666666666 frames=0 rel=0\n"
                  "log 1: drive channel closed (the harness said bye); running undriven\n"
                  "< {\"ok\":false,\"error\":\"bad JSON: expected an object at byte 0\"}\n"
                  "< {\"ok\":false,\"op\":\"\",\"error\":\"missing op\"}\n"
                  "< {\"ok\":false,\"op\":\"hello\",\"error\":\"this engine speaks drive protocol 1\"}\n"
                  "< {\"ok\":true,\"op\":\"hello\",\"protocol\":1,\"strata\":\"V\",\"abi\":\"A\",\"width\":0,"
                  "\"height\":0,\"surface\":\"\",\"clock\":\"free\",\"step\":0.016666666666666666,\"frame\":0}\n"
                  "< {\"ok\":true,\"op\":\"event\",\"queued\":1}\n"
                  "< {\"ok\":true,\"op\":\"event\",\"queued\":2}\n"
                  "< {\"ok\":false,\"op\":\"event\",\"error\":\"text is longer than 31 bytes; send it as several "
                  "text_input events\"}\n"
                  "< {\"ok\":false,\"op\":\"event\",\"error\":\"event type 0 is not a STRATA_EVENT_* value\"}\n"
                  "< {\"ok\":false,\"op\":\"event\",\"error\":\"event type 0 is not a STRATA_EVENT_* value\"}\n"
                  "< {\"ok\":true,\"op\":\"stats\",\"frame\":0,\"clock\":\"free\",\"step\":0.016666666666666666,"
                  "\"queued\":2,\"draw_calls\":0,\"instances\":0,\"cpu_ms_frame\":0}\n"
                  "< {\"ok\":false,\"op\":\"shot\",\"error\":\"shot needs a path\"}\n"
                  "< {\"ok\":false,\"op\":\"shot\",\"error\":\"this engine cannot take screenshots\"}\n"
                  "< {\"ok\":false,\"op\":\"clock\",\"error\":\"mode must be free or stepped\"}\n"
                  "< {\"ok\":false,\"op\":\"clock\",\"error\":\"step must be in (0, 1] seconds\"}\n"
                  "< {\"ok\":true,\"op\":\"clock\",\"clock\":\"free\",\"step\":0.125}\n"
                  "< {\"ok\":false,\"op\":\"step\",\"error\":\"step needs the stepped clock (send clock first)\"}\n"
                  "< {\"ok\":false,\"op\":\"\",\"error\":\"missing op\"}\n"
                  "< {\"ok\":true,\"op\":\"bye\"}\n"
                  "event type=2 scancode=80 mods=3 repeat=1 button=2 size=5x6 x=1.5 y=-2.25 dx=0.5 dy=3e+09 "
                  "text='hi'\n"
                  "event type=9 scancode=0 mods=0 repeat=0 button=0 size=0x0 x=0 y=0 dx=0 dy=0 "
                  "text='0123456789012345678901234567890'\n"
                  "state connected=0 driven=1 clock=free step=0.125 frames=0 rel=0\n"
                  "seconds 0.000000\n"
                  "state connected=0 driven=1 clock=free step=0.125 frames=1 rel=0\n"));
}

TEST(drive_char_stepped_clock_hooks_and_step_limits) {
    Transcript t;
    LogCapture logs(t);
    {
        Harness h(t);
        REQUIRE(h.engine >= 0);
        Drive d{steadyClock()};
        strata::drive::Hooks hooks;
        hooks.drawableSize = [](int32_t& w, int32_t& h2) { w = 640; h2 = 360; };
        hooks.screenshot = [&](const std::string& path) {
            t.note("hook shot " + path);
            if (path == "bad.png") strata::fail(STRATA_ERROR_IO, "cannot write %s", path.c_str());
            if (path == "worse.png") throw std::runtime_error("disk on fire");
        };
        hooks.frameStats = [] {
            StrataFrameStats s{};
            s.draw_calls = 7;
            s.instances = 99;
            s.cpu_ms_frame = 1.25f;
            return s;
        };
        hooks.surfaceKind = "window";
        d.setHooks(hooks);
        d.attach(socketLink(h.engine), ClockMode::Stepped, 2.0);  // out of range: falls back to 1/60
        d.setRelativeMouse(true);
        t.note(state(d));
        h.write("{\"op\":\"hello\",\"protocol\":1}\n"
                "{\"op\":\"shot\",\"path\":\"bad.png\"}\n"
                "{\"op\":\"shot\",\"path\":\"worse.png\"}\n"
                "{\"op\":\"shot\",\"path\":\"good.png\"}\n"
                "{\"op\":\"event\",\"type\":\"mouse_motion\",\"x\":10,\"y\":20,\"dx\":1,\"dy\":-1}\n"
                "{\"op\":\"step\",\"frames\":0}\n"
                "{\"op\":\"step\",\"frames\":1.5}\n"
                "{\"op\":\"step\",\"frames\":1000001}\n"
                "{\"op\":\"clock\",\"mode\":\"stepped\",\"step\":0.5}\n"
                "{\"op\":\"stats\"}\n"
                "{\"op\":\"step\"}\n"
                "{\"op\":\"stats\"}\n"
                "{\"op\":\"step\",\"frames\":2}\n");
        d.beginPoll();
        h.drain();
        popAll(d, t);
        t.note("seconds " + std::to_string(d.frameSeconds()));
        d.presented();
        h.drain();
        d.beginPoll();
        h.drain();
        t.note("seconds " + std::to_string(d.frameSeconds()));
        d.presented();
        t.note("seconds " + std::to_string(d.frameSeconds()));
        d.presented();
        h.drain();
        t.note(state(d));
    }
    CHECK(matches("stepped", t.text,
                  "state connected=1 driven=1 clock=stepped step=0.016666666666666666 frames=0 rel=1\n"
                  "hook shot bad.png\n"
                  "hook shot worse.png\n"
                  "hook shot good.png\n"
                  "< {\"ok\":true,\"op\":\"hello\",\"protocol\":1,\"strata\":\"V\",\"abi\":\"A\",\"width\":640,"
                  "\"height\":360,\"surface\":\"window\",\"clock\":\"stepped\",\"step\":0.016666666666666666,"
                  "\"frame\":0}\n"
                  "< {\"ok\":false,\"op\":\"shot\",\"error\":\"cannot write bad.png\"}\n"
                  "< {\"ok\":false,\"op\":\"shot\",\"error\":\"disk on fire\"}\n"
                  "< {\"ok\":true,\"op\":\"shot\",\"path\":\"good.png\",\"width\":640,\"height\":360}\n"
                  "< {\"ok\":true,\"op\":\"event\",\"queued\":1}\n"
                  "< {\"ok\":false,\"op\":\"step\",\"error\":\"frames must be a whole number from 1 to 1000000\"}\n"
                  "< {\"ok\":false,\"op\":\"step\",\"error\":\"frames must be a whole number from 1 to 1000000\"}\n"
                  "< {\"ok\":false,\"op\":\"step\",\"error\":\"frames must be a whole number from 1 to 1000000\"}\n"
                  "< {\"ok\":true,\"op\":\"clock\",\"clock\":\"stepped\",\"step\":0.5}\n"
                  "< {\"ok\":true,\"op\":\"stats\",\"frame\":0,\"clock\":\"stepped\",\"step\":0.5,\"queued\":1,"
                  "\"draw_calls\":7,\"instances\":99,\"cpu_ms_frame\":1.25}\n"
                  "event type=4 scancode=0 mods=0 repeat=0 button=0 size=0x0 x=0 y=0 dx=1 dy=-1 text=''\n"
                  "seconds 0.500000\n"
                  "< {\"ok\":true,\"op\":\"step\",\"frame\":1,\"frames_run\":1}\n"
                  "< {\"ok\":true,\"op\":\"stats\",\"frame\":1,\"clock\":\"stepped\",\"step\":0.5,\"queued\":0,"
                  "\"draw_calls\":7,\"instances\":99,\"cpu_ms_frame\":1.25}\n"
                  "seconds 0.500000\n"
                  "seconds 0.500000\n"
                  "< {\"ok\":true,\"op\":\"step\",\"frame\":3,\"frames_run\":2}\n"
                  "state connected=1 driven=1 clock=stepped step=0.5 frames=3 rel=1\n"));
}

TEST(drive_char_oversized_line_and_harness_close) {
    Transcript t;
    LogCapture logs(t);
    {
        Harness h(t);
        REQUIRE(h.engine >= 0);
        Drive d{steadyClock()};
        d.attach(socketLink(h.engine), ClockMode::Free, 0.1);
        h.write("{\"op\":\"stats\"}\n{\"op\":\"x\",\"pad\":\"");
        // More than the socket buffer holds: a thread writes while the engine reads.
        std::thread writer([&h] { h.write(std::string((1u << 20) + 16, 'a')); });
        for (int i = 0; i < 100000 && d.connected(); ++i) d.beginPoll(), d.presented();
        writer.join();
        h.drain();
        // How many frames ran before the cap tripped depends on the writer thread.
        t.note(std::string("connected=") + (d.connected() ? "1" : "0") +
               " clock=" + (d.clockMode() == ClockMode::Stepped ? "stepped" : "free"));
    }
    {
        Harness h(t);
        REQUIRE(h.engine >= 0);
        Drive d{steadyClock()};
        d.attach(socketLink(h.engine), ClockMode::Stepped, 0.1);
        h.write("{\"op\":\"step\",\"frames\":5}\n");
        d.beginPoll();
        d.presented();
        h.close();
        d.beginPoll();  // four granted frames remain: the close is not read yet
        t.note(state(d));
    }  // destruction answers the pending step into the closed socket
    CHECK(matches("oversized", t.text,
                  "log 1: drive channel closed (a line longer than 1 MiB); running undriven\n"
                  "< {\"ok\":true,\"op\":\"stats\",\"frame\":0,\"clock\":\"free\",\"step\":0.10000000000000001,"
                  "\"queued\":0,\"draw_calls\":0,\"instances\":0,\"cpu_ms_frame\":0}\n"
                  "connected=0 clock=free\n"
                  "state connected=1 driven=1 clock=stepped step=0.10000000000000001 frames=1 rel=0\n"
                  "log 1: drive channel closed (Broken pipe); running undriven\n"));
}

TEST(drive_char_environment_parsing_and_connect_failures) {
    Transcript t;
    LogCapture logs(t);
    {
        Drive d{steadyClock()};
        strata::drive::connectFromEnvironment(d);  // no STRATA_DRIVE: silent
        t.note(state(d));
    }
    {
        Env env;
        env.set("STRATA_DRIVE", "");
        env.set("STRATA_DRIVE_CLOCK", "bogus");
        Drive d{steadyClock()};
        strata::drive::connectFromEnvironment(d);  // empty path: silent, the clock is not even read
        t.note(state(d));
    }
    {
        Env env;
        env.set("STRATA_DRIVE", "/nonexistent/strata-drive.sock");
        env.set("STRATA_DRIVE_CLOCK", "bogus");
        env.set("STRATA_DRIVE_STEP", "5");
        Drive d{steadyClock()};
        strata::drive::connectFromEnvironment(d);
        t.note(state(d));
    }
    {
        Env env;
        env.set("STRATA_DRIVE", std::string(200, 'p').c_str());
        env.set("STRATA_DRIVE_CLOCK", "");
        env.set("STRATA_DRIVE_STEP", "");
        Drive d{steadyClock()};
        strata::drive::connectFromEnvironment(d);
        t.note(state(d));
    }
    {
        // A real listener: the engine connects with the clock and step from
        // the environment.
        char dir[] = "/tmp/strata-drive-char-XXXXXX";
        REQUIRE(::mkdtemp(dir) != nullptr);
        const std::string path = std::string(dir) + "/s";
        const int listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strcpy(addr.sun_path, path.c_str());
        REQUIRE(::bind(listener, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) == 0);
        REQUIRE(::listen(listener, 1) == 0);
        Env env;
        env.set("STRATA_DRIVE", path.c_str());
        env.set("STRATA_DRIVE_CLOCK", "stepped");
        env.set("STRATA_DRIVE_STEP", "0.25");
        {
            Drive d{steadyClock()};
            strata::drive::connectFromEnvironment(d);
            t.note(state(d));
        }
        ::close(listener);
        ::unlink(path.c_str());
        ::rmdir(dir);
    }
    std::string text = t.text;
    // The temporary directory's name varies.
    const size_t at = text.find("/tmp/strata-drive-char-");
    if (at != std::string::npos) text.replace(at, std::strlen("/tmp/strata-drive-char-XXXXXX"), "DIR");
    CHECK(matches("environment", text,
                  "state connected=0 driven=0 clock=free step=0.016666666666666666 frames=0 rel=0\n"
                  "state connected=0 driven=0 clock=free step=0.016666666666666666 frames=0 rel=0\n"
                  "log 2: STRATA_DRIVE_CLOCK must be free or stepped (got 'bogus'); using free\n"
                  "log 2: STRATA_DRIVE_STEP must be in (0, 1] seconds (got '5'); using 1/60\n"
                  "log 2: STRATA_DRIVE: could not connect to /nonexistent/strata-drive.sock (No such file or "
                  "directory); running undriven\n"
                  "state connected=0 driven=0 clock=free step=0.016666666666666666 frames=0 rel=0\n"
                  "log 2: STRATA_DRIVE path is too long for a Unix socket (200 bytes); running undriven\n"
                  "state connected=0 driven=0 clock=free step=0.016666666666666666 frames=0 rel=0\n"
                  "log 1: driven through DIR/s (stepped clock, step 0.25 s)\n"
                  "state connected=1 driven=1 clock=stepped step=0.25 frames=0 rel=0\n"));
}

#endif
