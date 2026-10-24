// Injected input, the frame clock and the optional drive channel (ABI 1.12).
//
// Every engine owns one Drive. Undriven it is an event queue
// (strata_push_event) and a monotonic frame clock (strata_frame_seconds).
// Driven, it is also the client end of a local socket named by STRATA_DRIVE:
// a test harness sends JSON lines that inject events, step the clock and ask
// for screenshots (docs/DRIVE.md). The engine only ever connects; it never
// listens. Everything runs on the engine's thread, inside pollEvent,
// frameSeconds and present, so a game's state is only touched where the game
// expects input anyway.
//
// No GL here: the engine passes in what it needs (drawable size, screenshots,
// frame stats) as hooks, so the protocol is unit-tested with a socket pair.
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>

#include "strata/strata.h"

namespace strata::drive {

constexpr size_t kMaxQueuedEvents = 4096;
constexpr int kProtocolVersion = 1;

enum class ClockMode { Free, Stepped };

struct Hooks {
    std::function<void(int32_t& width, int32_t& height)> drawableSize;
    // Writes the last presented frame to a PNG; throws strata::Error.
    std::function<void(const std::string& path)> screenshot;
    std::function<StrataFrameStats()> frameStats;
    std::string surfaceKind;
};

// Event type names on the wire, "key_down" for STRATA_EVENT_KEY_DOWN and so
// on. Returns STRATA_EVENT_NONE for an unknown name.
int32_t eventTypeFromName(const std::string& name);
const char* eventTypeName(int32_t type);

class Drive {
public:
    Drive();
    ~Drive();
    Drive(const Drive&) = delete;
    Drive& operator=(const Drive&) = delete;

    // Reads STRATA_DRIVE (a Unix socket path), STRATA_DRIVE_CLOCK
    // (free|stepped) and STRATA_DRIVE_STEP (seconds) and connects. Without
    // STRATA_DRIVE nothing happens; a failed connection logs a warning and
    // leaves the engine undriven. Never throws.
    void connectFromEnvironment();
    // Takes ownership of an already connected stream socket (tests use a
    // socket pair). POSIX only.
    void adopt(int fd, ClockMode mode, double step);

    void setHooks(Hooks hooks) { hooks_ = std::move(hooks); }

    // True from a successful connection until the harness says bye or the
    // connection drops.
    bool connected() const { return fd_ >= 0; }
    // True if this engine was ever driven. Stays true after a disconnect, so a
    // game that turned off its saves for a driven run keeps them off.
    bool wasDriven() const { return wasDriven_; }

    ClockMode clockMode() const { return mode_; }
    double step() const { return step_; }
    uint64_t framesPresented() const { return frame_; }

    // S1. Queues a copy of the event; throws INVALID_ARGUMENT for an unknown
    // type and INVALID_STATE when kMaxQueuedEvents are already waiting.
    void push(const StrataEvent& event);
    // Called by Engine::pollEvent before it asks the surface: at the first
    // call of a frame a stepped driven engine waits here for a grant. Then
    // reads whatever the harness has sent, without blocking.
    void beginPoll();
    // After the surface has no more events: pops the next injected one.
    bool pop(StrataEvent& out);

    // S2. Seconds to advance this frame: measured, or the granted step.
    double frameSeconds();

    // Called by Engine::present after the surface presented.
    void presented();

    // Set by Engine::setRelativeMouseMode; injected motion then carries dx/dy
    // only.
    void setRelativeMouse(bool enabled) { relativeMouse_ = enabled; }
    bool relativeMouse() const { return relativeMouse_; }

private:
    void startFrame();
    void awaitGrant();
    void pump(bool block);
    bool readSome(bool block);
    void handleLine(const std::string& line);
    void send(const std::string& line);
    void disconnect(const char* why);

    Hooks hooks_;
    std::deque<StrataEvent> queue_;
    int fd_ = -1;
    bool wasDriven_ = false;
    ClockMode mode_ = ClockMode::Free;
    double step_ = 1.0 / 60.0;
    std::string inbox_;

    bool frameStarted_ = false;
    uint64_t frame_ = 0;          // frames presented
    int64_t grantedFrames_ = 0;   // frames of the current step not yet started
    bool stepPending_ = false;    // a step's reply is owed
    int64_t stepFramesRun_ = 0;

    bool haveLast_ = false;
    std::chrono::steady_clock::time_point last_{};

    bool relativeMouse_ = false;
};

}  // namespace strata::drive
