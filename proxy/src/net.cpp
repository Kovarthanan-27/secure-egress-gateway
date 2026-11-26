#include "net.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace net {

static bool isPrivateAddress(const sockaddr* sa) {
    if (sa->sa_family == AF_INET) {
        uint32_t ip = ntohl(reinterpret_cast<const sockaddr_in*>(sa)->sin_addr.s_addr);
        uint8_t a = ip >> 24, b = (ip >> 16) & 0xff;
        return a == 0 || a == 10 || a == 127 ||              // this-net, 10/8, loopback
               (a == 172 && b >= 16 && b <= 31) ||           // 172.16/12
               (a == 192 && b == 168) ||                     // 192.168/16
               (a == 169 && b == 254) ||                     // link-local / cloud metadata
               (a == 100 && b >= 64 && b <= 127);            // carrier-grade NAT
    }
    if (sa->sa_family == AF_INET6) {
        const auto* in6 = reinterpret_cast<const sockaddr_in6*>(sa);
        const uint8_t* p = in6->sin6_addr.s6_addr;
        if (IN6_IS_ADDR_LOOPBACK(&in6->sin6_addr) || IN6_IS_ADDR_UNSPECIFIED(&in6->sin6_addr)) return true;
        if ((p[0] & 0xfe) == 0xfc) return true;                   // fc00::/7 unique local
        if (p[0] == 0xfe && (p[1] & 0xc0) == 0x80) return true;   // fe80::/10 link-local
        if (IN6_IS_ADDR_V4MAPPED(&in6->sin6_addr)) {              // ::ffff:a.b.c.d
            sockaddr_in v4{};
            v4.sin_family = AF_INET;
            std::memcpy(&v4.sin_addr, p + 12, 4);
            return isPrivateAddress(reinterpret_cast<sockaddr*>(&v4));
        }
    }
    return false;
}

ConnectResult connectTo(const std::string& host, int port, int timeoutMs, bool denyPrivate) {
    ConnectResult res;
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* list = nullptr;
    int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &list);
    if (rc != 0) {
        res.error = std::string("DNS lookup failed: ") + gai_strerror(rc);
        return res;
    }

    bool sawPublic = false;
    for (addrinfo* ai = list; ai != nullptr; ai = ai->ai_next) {
        if (denyPrivate && isPrivateAddress(ai->ai_addr)) continue;
        sawPublic = true;

        int fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;

        // Non-blocking connect so we can enforce our own timeout with poll().
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int c = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
        bool ok = (c == 0);
        if (c < 0 && errno == EINPROGRESS) {
            pollfd pfd{fd, POLLOUT, 0};
            if (poll(&pfd, 1, timeoutMs) == 1) {
                int err = 0;
                socklen_t len = sizeof err;
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                ok = (err == 0);
            }
        }
        if (!ok) {
            ::close(fd);
            continue;
        }
        fcntl(fd, F_SETFL, flags);  // back to blocking mode
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);  // proxies care about latency
        res.fd = fd;
        break;
    }
    freeaddrinfo(list);

    if (res.fd < 0) {
        if (denyPrivate && !sawPublic) {
            res.privateBlocked = true;
            res.error = "Destination resolves to a private/internal address";
        } else {
            res.error = "Could not connect to upstream";
        }
    }
    return res;
}

bool sendAll(int fd, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

RelayStats relay(int clientFd, int upstreamFd, int idleMs, const std::atomic<bool>& stop) {
    RelayStats st;
    int idleSoFar = 0;
    bool clientOpen = true, upstreamOpen = true;
    char buf[16384];

    while ((clientOpen || upstreamOpen) && !stop.load()) {
        pollfd fds[2];
        int which[2];
        int n = 0;
        if (clientOpen) { fds[n] = pollfd{clientFd, POLLIN, 0}; which[n++] = 0; }
        if (upstreamOpen) { fds[n] = pollfd{upstreamFd, POLLIN, 0}; which[n++] = 1; }

        // Poll in 1s slices so we notice shutdown quickly, and count idle time ourselves.
        int r = ::poll(fds, static_cast<nfds_t>(n), 1000);
        if (r == 0) {
            idleSoFar += 1000;
            if (idleSoFar >= idleMs) break;  // idle timeout: reclaim threads from dead tunnels
            continue;
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        idleSoFar = 0;

        for (int i = 0; i < n; ++i) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            bool fromClient = (which[i] == 0);
            int src = fromClient ? clientFd : upstreamFd;
            int dst = fromClient ? upstreamFd : clientFd;

            ssize_t got = ::recv(src, buf, sizeof buf, 0);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) {                 // EOF or error on this direction
                ::shutdown(dst, SHUT_WR);   // propagate the half-close
                (fromClient ? clientOpen : upstreamOpen) = false;
                continue;
            }
            if (!sendAll(dst, buf, static_cast<size_t>(got))) {
                clientOpen = upstreamOpen = false;
                break;
            }
            (fromClient ? st.up : st.down) += static_cast<uint64_t>(got);
        }
    }
    return st;
}

void setTimeouts(int fd, int seconds) {
    timeval tv{seconds, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

}  // namespace net
