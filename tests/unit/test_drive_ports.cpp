// The drive channel's ports. Each contract below runs against the production
// adapter and the test fake, so a fake cannot drift from what it stands in
// for. The protocol tests at the end run on every platform: they use the fake
// link, where test_drive.cpp needs a POSIX socket pair.
#include <functional>
#include <memory>
#include <string>

#include "core/drive/adapters/steady_clock.hpp"
#include "core/drive/adapters/unix_socket_link.hpp"
#include "core/drive/drive.hpp"
#include "core/drive/settings.hpp"
#include "fakes/fake_harness_link.hpp"
#include "fakes/fake_monotonic_clock.hpp"
#include "test.hpp"

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <sys/socket.h>
#include <unistd.h>
#define STRATA_TEST_SOCKETS 1
#endif

using namespace strata::drive;

namespace {

// One side of the contract: a link, and the harness end's three moves.
struct LinkUnderTest {
    HarnessLink* link = nullptr;
    std::function<void(const std::string&)> harnessWrites;
    std::function<void()> harnessHangsUp;
    std::function<std::string()> harnessReads;
};

void checkLinkContract(const LinkUnderTest& t) {
    // Nothing waiting: a non-blocking receive says so.
    CHECK(t.link->receive(false).kind == Received::Kind::Nothing);
    // Bytes the harness wrote arrive, in order.
    t.harnessWrites("{\"op\":\"hello\"}\n");
    std::string got;
    for (int i = 0; i < 8; ++i) {
        Received r = t.link->receive(false);
        if (r.kind != Received::Kind::Bytes) break;
        got += r.bytes;
    }
    CHECK(got == "{\"op\":\"hello\"}\n");
    // A send arrives whole.
    const Sent sent = t.link->send("{\"ok\":true}\n");
    CHECK(sent.ok);
    CHECK(t.harnessReads() == "{\"ok\":true}\n");
    // A hang-up is Closed with a reason, blocking or not.
    t.harnessHangsUp();
    const Received closed = t.link->receive(true);
    CHECK(closed.kind == Received::Kind::Closed);
    CHECK(closed.reason == "the harness closed the connection");
}

void checkClockContract(MonotonicClock& clock, const std::function<void()>& letTimePass) {
    const double a = clock.now();
    letTimePass();
    const double b = clock.now();
    CHECK(b >= a);
}

}  // namespace

TEST(fake_harness_link_meets_the_link_contract) {
    FakeHarnessLink link;
    checkLinkContract({&link, [&](const std::string& s) { link.deliver(s); }, [&] { link.hangUp(); },
                       [&] {
                           std::string out;
                           out.swap(link.sent);
                           return out;
                       }});
}

#if defined(STRATA_TEST_SOCKETS)
TEST(unix_socket_link_meets_the_link_contract) {
    int fds[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    int harness = fds[0];
    UnixSocketLink link(fds[1]);
    checkLinkContract({&link,
                       [&](const std::string& s) { REQUIRE(::send(harness, s.data(), s.size(), 0) == (ssize_t)s.size()); },
                       [&] {
                           ::close(harness);
                           harness = -1;
                       },
                       [&] {
                           char buffer[256];
                           const ssize_t n = ::recv(harness, buffer, sizeof buffer, 0);
                           return n > 0 ? std::string(buffer, static_cast<size_t>(n)) : std::string();
                       }});
    if (harness >= 0) ::close(harness);
}

TEST(unix_socket_link_connect_explains_a_failure) {
    std::string why;
    CHECK(UnixSocketLink::connect("/nonexistent/strata-drive.sock", why) == nullptr);
    CHECK(why == "STRATA_DRIVE: could not connect to /nonexistent/strata-drive.sock (No such file or directory); "
                 "running undriven");
}
#endif

TEST(steady_and_fake_clocks_meet_the_clock_contract) {
    SteadyClock steady;
    checkClockContract(steady, [] {});
    double seconds = 3.0;
    FakeMonotonicClock fake(&seconds);
    checkClockContract(fake, [&] { seconds += 0.5; });
    CHECK(fake.now() == 3.5);
}

TEST(drive_settings_parse_without_the_environment) {
    CHECK(!parseDriveSettings(nullptr, "stepped", "0.5").enabled);
    CHECK(!parseDriveSettings("", "stepped", "0.5").enabled);
    const DriveSettings ok = parseDriveSettings("/tmp/s", "stepped", "0.5");
    CHECK(ok.enabled);
    CHECK(ok.path == "/tmp/s");
    CHECK(ok.mode == ClockMode::Stepped);
    CHECK(ok.step == 0.5);
    CHECK(ok.warnings.empty());
    const DriveSettings bad = parseDriveSettings("/tmp/s", "warp", "nan");
    CHECK(bad.mode == ClockMode::Free);
    CHECK(bad.step == 1.0 / 60.0);
    REQUIRE(bad.warnings.size() == 2);
    CHECK(bad.warnings[0] == "STRATA_DRIVE_CLOCK must be free or stepped (got 'warp'); using free");
    CHECK(bad.warnings[1] == "STRATA_DRIVE_STEP must be in (0, 1] seconds (got 'nan'); using 1/60");
}

TEST(drive_free_clock_measures_time_from_the_clock_port) {
    double seconds = 100.0;
    Drive d(std::make_unique<FakeMonotonicClock>(&seconds));
    CHECK(d.frameSeconds() == 0.0);  // the first frame has no previous one
    d.presented();
    seconds += 0.25;
    CHECK(d.frameSeconds() == 0.25);
    d.presented();
    CHECK(d.frameSeconds() == 0.0);  // no time passed
}

TEST(drive_stepped_protocol_runs_over_the_fake_link_on_any_platform) {
    double seconds = 0.0;
    Drive d(std::make_unique<FakeMonotonicClock>(&seconds));
    auto owned = std::make_unique<FakeHarnessLink>();
    FakeHarnessLink& link = *owned;
    link.deliver("{\"op\":\"event\",\"type\":\"key_down\",\"scancode\":80}\n{\"op\":\"step\",\"frames\":1}\n");
    d.attach(std::move(owned), ClockMode::Stepped, 0.05);
    d.beginPoll();
    StrataEvent e{};
    REQUIRE(d.pop(e));
    CHECK(e.scancode == 80);
    seconds += 7.0;                   // wall time is ignored on the stepped clock
    CHECK(d.frameSeconds() == 0.05);
    d.presented();
    CHECK(link.sent ==
          "{\"ok\":true,\"op\":\"event\",\"queued\":1}\n{\"ok\":true,\"op\":\"step\",\"frame\":1,\"frames_run\":1}\n");
    // The next frame waits for a grant; the fake reports that it would block,
    // which drops the link and frees the clock, so nothing hangs.
    d.beginPoll();
    CHECK(!d.connected());
    CHECK(d.clockMode() == ClockMode::Free);
}

TEST(drive_drops_a_link_whose_send_fails) {
    double seconds = 0.0;
    Drive d(std::make_unique<FakeMonotonicClock>(&seconds));
    auto owned = std::make_unique<FakeHarnessLink>();
    owned->sendFails = true;
    owned->deliver("{\"op\":\"stats\"}\n");
    d.attach(std::move(owned), ClockMode::Free, 0.1);
    d.beginPoll();
    CHECK(!d.connected());
    CHECK(d.wasDriven());
}
