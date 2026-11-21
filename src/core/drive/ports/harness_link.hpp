#pragma once

#include <string>

#include "core/drive/model.hpp"

namespace strata::drive {

// The byte stream to the test harness: the client end of its connection.
//
// Contract:
// - receive(false) never blocks. It returns Bytes (at least one byte), Nothing
//   when no byte is waiting, or Closed with a reason when the stream has ended
//   (the harness closed it, or an error). receive(true) waits until Bytes or
//   Closed. Interrupted system calls are retried, never reported.
// - send(data) writes all of `data` or returns !ok with a reason. It may
//   block until the harness reads. It never raises SIGPIPE.
// - After Closed or a failed send the link is dead; the caller drops it.
//   Destroying a link closes it.
// - Main thread only, like the engine. Neither method throws.
class HarnessLink {
public:
    virtual ~HarnessLink() = default;
    virtual Received receive(bool block) = 0;
    virtual Sent send(const std::string& data) = 0;
};

}  // namespace strata::drive
