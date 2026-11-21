#pragma once

#include <memory>
#include <string>

#include "core/drive/ports/harness_link.hpp"

namespace strata::drive {

// HarnessLink over a connected Unix stream socket. POSIX only: elsewhere
// adopting fails with STRATA_ERROR_UNSUPPORTED and connecting reports that
// the drive channel needs Linux.
class UnixSocketLink final : public HarnessLink {
public:
    // Takes ownership of an already connected stream socket (tests use a
    // socket pair).
    explicit UnixSocketLink(int fd);
    ~UnixSocketLink() override;
    UnixSocketLink(const UnixSocketLink&) = delete;
    UnixSocketLink& operator=(const UnixSocketLink&) = delete;

    // Connects to the socket at `path`. On failure returns null and sets
    // `why` to the whole log line explaining it.
    static std::unique_ptr<UnixSocketLink> connect(const std::string& path, std::string& why);

    Received receive(bool block) override;
    Sent send(const std::string& data) override;

private:
    int fd_ = -1;
};

}  // namespace strata::drive
