// net.hpp - thin, well-commented wrappers over POSIX sockets.
#pragma once
#include <atomic>
#include <cstdint>
#include <string>

namespace net {

struct ConnectResult {
    int fd = -1;
    std::string error;
    bool privateBlocked = false;  // true if refused by the SSRF guard
};

// Resolve + connect with a timeout. If denyPrivate is set, we refuse to connect
// when the RESOLVED address is loopback/private/link-local (SSRF protection).
// We check the IP after DNS resolution on purpose: checking only the hostname
// would be bypassed by a public name that resolves to 10.x.x.x (DNS rebinding).
ConnectResult connectTo(const std::string& host, int port, int timeoutMs, bool denyPrivate);

// Send every byte (handles partial writes). MSG_NOSIGNAL avoids SIGPIPE.
bool sendAll(int fd, const char* data, size_t len);
inline bool sendAll(int fd, const std::string& s) { return sendAll(fd, s.data(), s.size()); }

struct RelayStats {
    uint64_t up = 0;    // bytes client -> upstream
    uint64_t down = 0;  // bytes upstream -> client
};

// Bidirectional byte pump between two sockets using poll(). Supports half-close
// (one side finishing sending while the other still replies). Ends when both
// directions are closed, nothing happens for idleMs, or `stop` is raised (graceful shutdown).
RelayStats relay(int clientFd, int upstreamFd, int idleMs, const std::atomic<bool>& stop);

void setTimeouts(int fd, int seconds);

}  // namespace net
