// ABI 1.12: injected events, the frame clock and the drive channel. The drive
// protocol is exercised through a socket pair with no GL: the test plays the
// harness, writes its lines first, then runs the engine's side of a frame.
#include <cstring>
#include <string>
#include <vector>

#include "core/drive.hpp"
#include "core/error.hpp"
#include "core/json_line.hpp"
#include "strata/strata.h"
#include "test.hpp"

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#define STRATA_TEST_SOCKETS 1
#endif

using strata::drive::ClockMode;
using strata::drive::Drive;

namespace {

StrataEvent key(int32_t type, int32_t scancode) {
    StrataEvent e{};
    e.type = type;
    e.scancode = scancode;
    return e;
}

#if defined(STRATA_TEST_SOCKETS)
struct Pair {
    int harness = -1;
    int engine = -1;
    Pair() {
        int fds[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0) {
            harness = fds[0];
            engine = fds[1];
        }
    }
    ~Pair() {
        if (harness >= 0) ::close(harness);
    }
    void write(const std::string& lines) const {
        size_t sent = 0;
        while (sent < lines.size()) {
            const ssize_t n = ::send(harness, lines.data() + sent, lines.size() - sent, 0);
            if (n <= 0) return;
            sent += static_cast<size_t>(n);
        }
    }
    // Every complete line the engine has sent so far.
    std::vector<std::string> replies() const {
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
        std::vector<std::string> lines;
        size_t start = 0;
        for (size_t nl; (nl = all.find('\n', start)) != std::string::npos; start = nl + 1) {
            lines.push_back(all.substr(start, nl - start));
        }
        return lines;
    }
};

bool has(const std::string& line, const char* fragment) { return line.find(fragment) != std::string::npos; }
#endif

}  // namespace

TEST(json_line_parses_a_flat_object) {
    strata::json::Object o;
    std::string error;
    // Raw strings stay out of the CHECK macros (MSVC mis-stringifies them), and
    // the source stays ASCII (the JSON escape u00e9 is an e-acute).
    const std::string line =
        "{\"op\":\"event\",\"type\":\"key_down\",\"scancode\":80,\"x\":-1.5e1,\"ok\":true,\"n\":null,"
        "\"t\":\"a\\\"\\u00e9\\n\"}";
    REQUIRE(strata::json::parseObject(line, o, &error));
    CHECK(o["op"].text == "event");
    CHECK(o["scancode"].number == 80.0);
    CHECK(o["x"].number == -15.0);
    CHECK(o["ok"].boolean);
    CHECK(o["n"].kind == strata::json::Value::Kind::Null);
    CHECK(o["t"].text == "a\"\xc3\xa9\n");
    CHECK(strata::json::parseObject("{}", o, &error));
    CHECK(o.empty());
}

TEST(json_line_rejects_what_the_protocol_does_not_use) {
    strata::json::Object o;
    std::string error;
    const std::string nested = R"({"a":{"b":1}})", array = R"({"a":[1]})", trailing = R"({"a":1} x)",
                      unterminated = R"({"a":"unterminated)";
    CHECK(!strata::json::parseObject(nested, o, &error));
    CHECK(!error.empty());
    CHECK(!strata::json::parseObject(array, o, &error));
    CHECK(!strata::json::parseObject(trailing, o, &error));
    CHECK(!strata::json::parseObject(unterminated, o, &error));
    CHECK(!strata::json::parseObject("[]", o, &error));
    CHECK(!strata::json::parseObject("", o, &error));
}

TEST(json_line_writer_escapes_and_orders_fields) {
    const std::string line = strata::json::Writer()
                                 .field("ok", true)
                                 .field("op", "shot")
                                 .field("path", std::string("a\"b\\c\n"))
                                 .field("w", 640)
                                 .field("step", 0.5)
                                 .str();
    const std::string expected = R"({"ok":true,"op":"shot","path":"a\"b\\c\n","w":640,"step":0.5})";
    CHECK(line == expected);
    strata::json::Object back;
    REQUIRE(strata::json::parseObject(line, back, nullptr));
    CHECK(back["path"].text == "a\"b\\c\n");
}

TEST(event_type_names_round_trip) {
    for (int32_t t = STRATA_EVENT_QUIT; t <= STRATA_EVENT_TEXT_INPUT; ++t) {
        CHECK(strata::drive::eventTypeFromName(strata::drive::eventTypeName(t)) == t);
    }
    CHECK(strata::drive::eventTypeFromName("teleport") == STRATA_EVENT_NONE);
}

TEST(pushed_events_come_back_first_in_first_out) {
    Drive d;
    d.push(key(STRATA_EVENT_KEY_DOWN, 4));
    d.push(key(STRATA_EVENT_KEY_UP, 4));
    StrataEvent e{};
    d.beginPoll();
    REQUIRE(d.pop(e));
    CHECK(e.type == STRATA_EVENT_KEY_DOWN);
    REQUIRE(d.pop(e));
    CHECK(e.type == STRATA_EVENT_KEY_UP);
    CHECK(!d.pop(e));
}

TEST(push_rejects_unknown_types_and_a_full_queue) {
    Drive d;
    bool threw = false;
    try {
        d.push(key(42, 0));
    } catch (const strata::Error& err) {
        threw = err.status() == STRATA_ERROR_INVALID_ARGUMENT;
    }
    CHECK(threw);
    for (size_t i = 0; i < strata::drive::kMaxQueuedEvents; ++i) d.push(key(STRATA_EVENT_KEY_DOWN, 4));
    threw = false;
    try {
        d.push(key(STRATA_EVENT_KEY_DOWN, 4));
    } catch (const strata::Error& err) {
        threw = err.status() == STRATA_ERROR_INVALID_STATE;
    }
    CHECK(threw);
}

TEST(relative_mouse_mode_zeroes_injected_positions) {
    Drive d;
    d.setRelativeMouse(true);
    StrataEvent m{};
    m.type = STRATA_EVENT_MOUSE_MOTION;
    m.x = 100;
    m.y = 50;
    m.dx = 7;
    m.dy = -3;
    d.push(m);
    StrataEvent e{};
    REQUIRE(d.pop(e));
    CHECK(e.x == 0.0f);
    CHECK(e.y == 0.0f);
    CHECK(e.dx == 7.0f);
    CHECK(e.dy == -3.0f);
}

TEST(undriven_frame_seconds_starts_at_zero_and_never_goes_back) {
    Drive d;
    CHECK(!d.connected());
    CHECK(!d.wasDriven());
    CHECK(d.frameSeconds() == 0.0);
    d.presented();
    CHECK(d.frameSeconds() >= 0.0);
}

TEST(abi_1_12_calls_reject_bad_arguments_without_a_context) {
    StrataEvent e = key(STRATA_EVENT_KEY_DOWN, 4);
    double seconds = -1.0;
    StrataEngine* engine = reinterpret_cast<StrataEngine*>(0x1);  // never dereferenced: pointers are checked first
    CHECK(strata_push_event(nullptr, &e) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_push_event(engine, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_frame_seconds(nullptr, &seconds) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_frame_seconds(engine, nullptr) == STRATA_ERROR_INVALID_ARGUMENT);
    CHECK(strata_is_driven(nullptr) == 0);
}

#if defined(STRATA_TEST_SOCKETS)

TEST(drive_stepped_clock_delivers_events_on_the_first_granted_frame) {
    Pair pair;
    REQUIRE(pair.engine >= 0);
    Drive d;
    strata::drive::Hooks hooks;
    hooks.drawableSize = [](int32_t& w, int32_t& h) { w = 320; h = 200; };
    hooks.surfaceKind = "headless";
    d.setHooks(hooks);
    d.adopt(pair.engine, ClockMode::Stepped, 0.05);
    CHECK(d.connected());
    CHECK(d.wasDriven());

    pair.write("{\"op\":\"hello\",\"protocol\":1}\n"
               "{\"op\":\"event\",\"type\":\"key_down\",\"scancode\":80}\n"
               "{\"op\":\"step\",\"frames\":2}\n");
    StrataEvent e{};
    d.beginPoll();  // takes the grant
    REQUIRE(d.pop(e));
    CHECK(e.type == STRATA_EVENT_KEY_DOWN);
    CHECK(e.scancode == 80);
    CHECK(!d.pop(e));
    CHECK(d.frameSeconds() == 0.05);
    d.presented();
    std::vector<std::string> lines = pair.replies();
    REQUIRE(lines.size() == 2);  // hello and event; the step answers when it is done
    CHECK(has(lines[0], "\"op\":\"hello\""));
    CHECK(has(lines[0], "\"width\":320"));
    CHECK(has(lines[0], "\"surface\":\"headless\""));
    CHECK(has(lines[0], "\"clock\":\"stepped\""));
    CHECK(has(lines[1], "\"op\":\"event\""));

    CHECK(d.frameSeconds() == 0.05);  // the second granted frame, without a poll first
    d.presented();
    lines = pair.replies();
    REQUIRE(lines.size() == 1);
    CHECK(has(lines[0], "\"op\":\"step\""));
    CHECK(has(lines[0], "\"frame\":2"));
    CHECK(has(lines[0], "\"frames_run\":2"));
    CHECK(d.framesPresented() == 2);
}

TEST(drive_holds_pipelined_lines_for_the_next_step) {
    Pair pair;
    REQUIRE(pair.engine >= 0);
    Drive d;
    d.adopt(pair.engine, ClockMode::Stepped, 1.0 / 60.0);
    pair.write("{\"op\":\"step\",\"frames\":1}\n"
               "{\"op\":\"event\",\"type\":\"key_down\",\"scancode\":4}\n"
               "{\"op\":\"step\",\"frames\":1}\n");
    StrataEvent e{};
    d.beginPoll();
    CHECK(!d.pop(e));  // the event was sent after the first step
    d.presented();
    d.beginPoll();
    REQUIRE(d.pop(e));
    CHECK(e.scancode == 4);
    d.presented();
    const std::vector<std::string> lines = pair.replies();
    REQUIRE(lines.size() == 3);
    CHECK(has(lines[0], "\"op\":\"step\""));
    CHECK(has(lines[1], "\"op\":\"event\""));
    CHECK(has(lines[2], "\"frame\":2"));
}

TEST(drive_refuses_bad_lines_without_disconnecting) {
    Pair pair;
    REQUIRE(pair.engine >= 0);
    Drive d;
    d.adopt(pair.engine, ClockMode::Free, 1.0 / 60.0);
    pair.write("not json\n"
               "{\"op\":\"teleport\"}\n"
               "{\"op\":\"step\",\"frames\":3}\n"
               "{\"op\":\"event\",\"type\":\"warp\"}\n"
               "{\"op\":\"clock\",\"mode\":\"stepped\",\"step\":0}\n"
               "{\"op\":\"stats\"}\n");
    d.beginPoll();
    CHECK(d.connected());
    const std::vector<std::string> lines = pair.replies();
    REQUIRE(lines.size() == 6);
    CHECK(has(lines[0], "\"ok\":false"));
    CHECK(has(lines[1], "unknown op"));
    CHECK(has(lines[2], "stepped clock"));
    CHECK(has(lines[3], "\"ok\":false"));
    CHECK(has(lines[4], "\"ok\":false"));
    CHECK(has(lines[5], "\"ok\":true"));
    CHECK(has(lines[5], "\"clock\":\"free\""));
}

TEST(drive_switches_clock_and_takes_screenshots_through_hooks) {
    Pair pair;
    REQUIRE(pair.engine >= 0);
    Drive d;
    std::string shotPath;
    strata::drive::Hooks hooks;
    hooks.screenshot = [&](const std::string& path) { shotPath = path; };
    d.setHooks(hooks);
    d.adopt(pair.engine, ClockMode::Free, 1.0 / 60.0);
    pair.write("{\"op\":\"clock\",\"mode\":\"stepped\",\"step\":0.25}\n"
               "{\"op\":\"shot\",\"path\":\"/tmp/x.png\"}\n"
               "{\"op\":\"step\",\"frames\":1}\n");
    d.beginPoll();     // free: reads the clock line, then stops for the stepped ones
    d.presented();
    CHECK(d.clockMode() == ClockMode::Stepped);
    CHECK(d.frameSeconds() == 0.25);  // waits for and takes the grant
    CHECK(shotPath == "/tmp/x.png");
    d.presented();
    const std::vector<std::string> lines = pair.replies();
    REQUIRE(lines.size() == 3);
    CHECK(has(lines[0], "\"step\":0.25"));
    CHECK(has(lines[1], "\"path\":\"/tmp/x.png\""));
    CHECK(has(lines[2], "\"frames_run\":1"));
}

TEST(drive_falls_back_to_a_free_clock_when_the_harness_goes) {
    Drive d;
    {
        Pair pair;
        REQUIRE(pair.engine >= 0);
        d.adopt(pair.engine, ClockMode::Stepped, 0.1);
    }  // the harness end closes
    d.beginPoll();  // would wait for ever if the drop were not noticed
    CHECK(!d.connected());
    CHECK(d.wasDriven());
    CHECK(d.clockMode() == ClockMode::Free);
    d.presented();
    CHECK(d.frameSeconds() >= 0.0);
}

TEST(drive_bye_disconnects_cleanly) {
    Pair pair;
    REQUIRE(pair.engine >= 0);
    Drive d;
    d.adopt(pair.engine, ClockMode::Stepped, 0.1);
    pair.write("{\"op\":\"bye\"}\n");
    d.beginPoll();
    CHECK(!d.connected());
    const std::vector<std::string> lines = pair.replies();
    REQUIRE(lines.size() == 1);
    CHECK(has(lines[0], "\"op\":\"bye\""));
}

#endif
