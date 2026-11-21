#include "core/drive/adapters/unix_socket_link.hpp"

#include <cerrno>
#include <cstring>

#include "core/error.hpp"

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#define STRATA_DRIVE_POSIX 1
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace strata::drive {

namespace {

Received closed(std::string reason) {
    Received r;
    r.kind = Received::Kind::Closed;
    r.reason = std::move(reason);
    return r;
}

}  // namespace

UnixSocketLink::UnixSocketLink(int fd) {
#if defined(STRATA_DRIVE_POSIX)
    fd_ = fd;
#else
    (void)fd;
    fd_ = -1;
    fail(STRATA_ERROR_UNSUPPORTED, "the drive channel needs a POSIX platform");
#endif
}

UnixSocketLink::~UnixSocketLink() {
#if defined(STRATA_DRIVE_POSIX)
    if (fd_ >= 0) ::close(fd_);
#endif
}

std::unique_ptr<UnixSocketLink> UnixSocketLink::connect(const std::string& path, std::string& why) {
#if defined(STRATA_DRIVE_POSIX)
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof addr.sun_path) {
        why = "STRATA_DRIVE path is too long for a Unix socket (" + std::to_string(path.size()) +
              " bytes); running undriven";
        return nullptr;
    }
    std::strcpy(addr.sun_path, path.c_str());
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        why = std::string("STRATA_DRIVE: socket() failed (") + std::strerror(errno) + "); running undriven";
        return nullptr;
    }
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0) {
        why = "STRATA_DRIVE: could not connect to " + path + " (" + std::strerror(errno) + "); running undriven";
        ::close(fd);
        return nullptr;
    }
    return std::make_unique<UnixSocketLink>(fd);
#else
    (void)path;
    why = "STRATA_DRIVE is set, but the drive channel needs Linux; running undriven";
    return nullptr;
#endif
}

Received UnixSocketLink::receive(bool block) {
#if defined(STRATA_DRIVE_POSIX)
    for (;;) {
        pollfd p{};
        p.fd = fd_;
        p.events = POLLIN;
        const int ready = ::poll(&p, 1, block ? -1 : 0);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return closed(std::strerror(errno));
        }
        if (ready == 0) return {};
        char buffer[4096];
        const ssize_t n = ::recv(fd_, buffer, sizeof buffer, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return {};
            return closed(std::strerror(errno));
        }
        if (n == 0) return closed("the harness closed the connection");
        Received r;
        r.kind = Received::Kind::Bytes;
        r.bytes.assign(buffer, static_cast<size_t>(n));
        return r;
    }
#else
    (void)block;
    return {};
#endif
}

Sent UnixSocketLink::send(const std::string& data) {
#if defined(STRATA_DRIVE_POSIX)
    size_t sent = 0;
    while (sent < data.size()) {
#if defined(MSG_NOSIGNAL)
        const ssize_t n = ::send(fd_, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#else
        const ssize_t n = ::send(fd_, data.data() + sent, data.size() - sent, 0);
#endif
        if (n < 0) {
            if (errno == EINTR) continue;
            return {false, std::strerror(errno)};
        }
        sent += static_cast<size_t>(n);
    }
    return {};
#else
    (void)data;
    return {};
#endif
}

}  // namespace strata::drive
