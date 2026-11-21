#pragma once

#include <deque>
#include <string>

#include "core/drive/ports/harness_link.hpp"

namespace strata::drive {

// HarnessLink for tests, on any platform. The test is the harness: it queues
// chunks with deliver(), ends the stream with hangUp(), and reads what the
// engine sent from `sent`. A blocking receive with nothing queued reports
// Closed ("the fake link would block") instead of hanging the test.
class FakeHarnessLink final : public HarnessLink {
public:
    std::deque<std::string> chunks;
    std::string sent;
    bool hungUp = false;
    bool sendFails = false;

    void deliver(const std::string& bytes) { chunks.push_back(bytes); }
    void hangUp() { hungUp = true; }

    Received receive(bool block) override {
        Received r;
        if (!chunks.empty()) {
            r.kind = Received::Kind::Bytes;
            r.bytes = chunks.front();
            chunks.pop_front();
        } else if (hungUp) {
            r.kind = Received::Kind::Closed;
            r.reason = "the harness closed the connection";
        } else if (block) {
            r.kind = Received::Kind::Closed;
            r.reason = "the fake link would block";
        }
        return r;
    }

    Sent send(const std::string& data) override {
        if (sendFails || hungUp) return {false, "Broken pipe"};
        sent += data;
        return {};
    }
};

}  // namespace strata::drive
