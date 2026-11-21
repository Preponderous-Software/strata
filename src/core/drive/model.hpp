// The drive channel's plain values: its limits, its clock modes, the settings
// read at start-up and what a harness link reports. No I/O here.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace strata::drive {

constexpr size_t kMaxQueuedEvents = 4096;
constexpr int kProtocolVersion = 1;
// A harness line longer than this, with no newline yet, drops the connection.
constexpr size_t kMaxLineBytes = 1u << 20;
constexpr long long kMaxStepFrames = 1000000;

enum class ClockMode { Free, Stepped };

// STRATA_DRIVE, STRATA_DRIVE_CLOCK and STRATA_DRIVE_STEP after parsing.
// `warnings` are what the parse found wrong, in order, to be logged.
struct DriveSettings {
    bool enabled = false;  // STRATA_DRIVE named a path
    std::string path;
    ClockMode mode = ClockMode::Free;
    double step = 1.0 / 60.0;
    std::vector<std::string> warnings;
};

// What one HarnessLink::receive call found.
struct Received {
    enum class Kind { Nothing, Bytes, Closed };
    Kind kind = Kind::Nothing;
    std::string bytes;   // Kind::Bytes
    std::string reason;  // Kind::Closed: why, for the log
};

// What one HarnessLink::send call did.
struct Sent {
    bool ok = true;
    std::string reason;  // !ok: why the link is gone, for the log
};

}  // namespace strata::drive
