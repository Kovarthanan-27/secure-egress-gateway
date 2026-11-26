#include "proxy_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "net.hpp"
#include "util.hpp"

namespace {

using Clock = std::chrono::steady_clock;

// RAII: guarantees the socket is closed on every exit path (early returns!).
struct FdGuard {
    int fd;
    ~FdGuard() { if (fd >= 0) ::close(fd); }
};

// Read until the blank line that ends the HTTP header block. Bytes that arrive
// after it (the start of a request body) are returned in `extra`.
bool readHeaders(int fd, std::string& head, std::string& extra) {
    std::string buf;
    char tmp[4096];
    const size_t kMaxHeader = 16 * 1024;  // refuse oversized headers
    while (buf.size() < kMaxHeader) {
        ssize_t n = ::recv(fd, tmp, sizeof tmp, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;  // closed, error, or SO_RCVTIMEO fired (slow-loris defence)
        buf.append(tmp, static_cast<size_t>(n));
        size_t end = buf.find("\r\n\r\n");
        if (end != std::string::npos) {
            head = buf.substr(0, end + 4);
            extra = buf.substr(end + 4);
            return true;
        }
    }
    return false;
}

bool parseRequestLine(const std::string& head, std::string& method, std::string& target) {
    std::istringstream is(head.substr(0, head.find("\r\n")));
    std::string version;
    return static_cast<bool>(is >> method >> target >> version);
}

// "host", "host:port", "[v6]:port"  ->  host + port
bool parseAuthority(const std::string& authority, int defaultPort, std::string& host, int& port) {
    if (authority.empty()) return false;
    port = defaultPort;
    if (authority[0] == '[') {
        size_t close = authority.find(']');
        if (close == std::string::npos) return false;
        host = authority.substr(1, close - 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') return false;
            port = std::atoi(authority.c_str() + close + 2);
        }
    } else {
        size_t colon = authority.rfind(':');
        if (colon != std::string::npos) {
            host = authority.substr(0, colon);
            port = std::atoi(authority.c_str() + colon + 1);
        } else {
            host = authority;
        }
    }
    if (host.empty() || port <= 0 || port > 65535) return false;
    // Whitelist characters: blocks spaces, control chars and injection tricks.
    for (unsigned char c : host)
        if (!(std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == ':')) return false;
    return true;
}

// Rebuild the request for the origin server: origin-form request line, drop
// hop-by-hop headers (they only mean something between client and proxy), and
// force "Connection: close" so the response is delimited by connection close.
std::string buildForwardRequest(const std::string& head, const std::string& method, const std::string& path) {
    static const std::unordered_set<std::string> kHopByHop = {
        "proxy-connection", "connection", "proxy-authorization", "keep-alive", "upgrade"};
    std::string out = method + " " + path + " HTTP/1.1\r\n";
    size_t pos = head.find("\r\n") + 2;
    while (pos < head.size()) {
        size_t eol = head.find("\r\n", pos);
        if (eol == std::string::npos || eol == pos) break;
        std::string line = head.substr(pos, eol - pos);
        pos = eol + 2;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        if (kHopByHop.count(util::toLower(util::trim(line.substr(0, colon))))) continue;
        out += line + "\r\n";
    }
    out += "Connection: close\r\n\r\n";
    return out;
}

// Single-pass {{KEY}} substitution. Done in one pass so a substituted value
// can never be re-interpreted as another placeholder.
std::string renderTemplate(const std::string& tpl, const std::unordered_map<std::string, std::string>& vars) {
    std::string out;
    size_t i = 0;
    while (i < tpl.size()) {
        size_t open = tpl.find("{{", i);
        if (open == std::string::npos) { out.append(tpl, i, std::string::npos); break; }
        size_t close = tpl.find("}}", open + 2);
        if (close == std::string::npos) { out.append(tpl, i, std::string::npos); break; }
        out.append(tpl, i, open - i);
        auto it = vars.find(tpl.substr(open + 2, close - open - 2));
        if (it != vars.end()) out += it->second;
        i = close + 2;
    }
    return out;
}

// If we close a socket that still has unread data (e.g. a POST body), TCP sends
// RST and the client may never see our response. So: half-close, drain briefly.
void lingerClose(int fd) {
    ::shutdown(fd, SHUT_WR);
    net::setTimeouts(fd, 1);
    char buf[4096];
    size_t total = 0;
    while (total < 256 * 1024) {
        ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n <= 0) break;
        total += static_cast<size_t>(n);
    }
}

}  // namespace

ProxyServer::ProxyServer(const Config& cfg, PolicyStore& store, LogShipper& logs, std::string blockPageTemplate)
    : cfg_(cfg), store_(store), logs_(logs), blockPage_(std::move(blockPageTemplate)),
      pool_(static_cast<size_t>(cfg.workers), cfg.maxQueued) {}

std::string ProxyServer::nextRequestId() {
    char b[48];
    std::snprintf(b, sizeof b, "%llx-%llx", static_cast<unsigned long long>(util::nowMs() / 1000),
                  static_cast<unsigned long long>(++counter_));
    return b;
}

void ProxyServer::sendSimple(int fd, int status, const std::string& text) {
    const char* reason = status == 200 ? "OK" : status == 400 ? "Bad Request" : status == 502 ? "Bad Gateway"
                         : status == 503 ? "Service Unavailable" : "Error";
    std::string resp = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n" +
                       "Content-Type: text/plain; charset=utf-8\r\nContent-Length: " + std::to_string(text.size()) +
                       "\r\nConnection: close\r\n\r\n" + text;
    net::sendAll(fd, resp);
}

void ProxyServer::sendBlockPage(int fd, const std::string& host, const Decision& d, long policyVersion,
                                const std::string& requestId) {
    std::unordered_map<std::string, std::string> vars = {
        {"HOST", util::htmlEscape(host)},
        {"RULE", util::htmlEscape(d.pattern.empty() ? "(none)" : d.pattern)},
        {"CATEGORY", util::htmlEscape(d.category)},
        {"REASON", util::htmlEscape(d.reason)},
        {"REQUEST_ID", util::htmlEscape(requestId)},
        {"POLICY_VERSION", std::to_string(policyVersion)},
        {"TIME", util::isoTime(util::nowMs())},
    };
    std::string body = renderTemplate(blockPage_, vars);
    std::string resp = "HTTP/1.1 403 Forbidden\r\nContent-Type: text/html; charset=utf-8\r\n"
                       "Content-Length: " + std::to_string(body.size()) + "\r\n"
                       "Cache-Control: no-store\r\nX-Blocked-By: secure-egress-gateway\r\n"
                       "X-Request-Id: " + requestId + "\r\nConnection: close\r\n\r\n" + body;
    net::sendAll(fd, resp);
}

bool ProxyServer::run(std::atomic<bool>& stop) {
    stop_ = &stop;
    int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) { std::perror("socket"); return false; }
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(cfg_.port));
    if (::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 || ::listen(lfd, 512) < 0) {
        std::perror("bind/listen");
        ::close(lfd);
        return false;
    }
    util::info("proxy listening on port " + std::to_string(cfg_.port) + " with " +
               std::to_string(cfg_.workers) + " workers");

    while (!stop.load()) {
        pollfd p{lfd, POLLIN, 0};
        if (::poll(&p, 1, 500) <= 0) continue;  // wake twice a second to notice shutdown

        sockaddr_storage ss{};
        socklen_t sl = sizeof ss;
        int cfd = ::accept(lfd, reinterpret_cast<sockaddr*>(&ss), &sl);
        if (cfd < 0) continue;

        char ipbuf[INET6_ADDRSTRLEN] = "?";
        if (ss.ss_family == AF_INET)
            inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(&ss)->sin_addr, ipbuf, sizeof ipbuf);
        std::string ip = ipbuf;

        // Hand the connection to the pool; if it is saturated, shed load immediately.
        bool queued = pool_.submit([this, cfd, ip] { handleClient(cfd, ip); });
        if (!queued) {
            net::setTimeouts(cfd, 1);
            sendSimple(cfd, 503, "Gateway busy, try again shortly\n");
            ::close(cfd);
        }
    }
    ::close(lfd);
    return true;
}

void ProxyServer::handleClient(int clientFd, const std::string& clientIp) {
    FdGuard clientGuard{clientFd};
    net::setTimeouts(clientFd, 10);

    // ---- 1. read + parse the request ------------------------------------------------
    std::string head, extra;
    if (!readHeaders(clientFd, head, extra)) return;

    std::string method, target;
    if (!parseRequestLine(head, method, target)) {
        sendSimple(clientFd, 400, "Malformed request\n");
        lingerClose(clientFd);
        return;
    }

    Target t;
    std::string path;  // origin-form path for plain HTTP
    const bool isConnect = (method == "CONNECT");
    bool parsed = false;

    if (isConnect) {
        parsed = parseAuthority(target, 443, t.host, t.port);
    } else if (target.rfind("http://", 0) == 0) {
        std::string rest = target.substr(7);
        size_t slash = rest.find_first_of("/?#");
        std::string authority = (slash == std::string::npos) ? rest : rest.substr(0, slash);
        path = (slash == std::string::npos) ? "/" : rest.substr(slash);
        if (path[0] != '/') path = "/" + path;
        size_t at = authority.rfind('@');  // http://good.com@evil.com/ really goes to evil.com
        if (at != std::string::npos) authority = authority.substr(at + 1);
        parsed = parseAuthority(authority, 80, t.host, t.port);
    } else if (!target.empty() && target[0] == '/') {
        // Someone talked to the proxy directly (not as a proxy). Used for health checks.
        sendSimple(clientFd, target == "/healthz" ? 200 : 400,
                   target == "/healthz" ? "ok\n" : "This is a forward proxy. Use it as an HTTP proxy.\n");
        lingerClose(clientFd);
        return;
    }
    if (!parsed) {
        sendSimple(clientFd, 400, "Unsupported or malformed request target\n");
        lingerClose(clientFd);
        return;
    }

    // ---- 2. policy decision ---------------------------------------------------------
    const std::string requestId = nextRequestId();
    LogEntry entry;
    entry.tsMs = util::nowMs();
    entry.clientIp = clientIp;
    entry.host = t.host;
    entry.port = t.port;
    entry.method = method;

    auto snapshot = store_.current();  // shared_ptr copy: this request keeps a consistent policy
    Decision d;
    auto t0 = Clock::now();
    if (snapshot) {
        d = snapshot->evaluate(t.host);
    } else {  // never received a policy: behave according to FAIL_MODE
        d.allow = cfg_.failOpen;
        d.category = "Gateway policy";
        d.pattern = cfg_.failOpen ? "fail-open" : "fail-closed";
        d.reason = cfg_.failOpen ? "No policy loaded yet; gateway is configured to fail open"
                                 : "No policy loaded yet; gateway is configured to fail closed";
    }
    entry.decisionUs = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
    entry.policyVersion = snapshot ? snapshot->version() : -1;

    // Gateway-level guard: CONNECT is a raw TCP tunnel, so only allow well-known TLS ports.
    // Otherwise the proxy could be abused to tunnel to SSH, SMTP, databases, etc.
    if (d.allow && isConnect && !cfg_.connectPorts.count(t.port)) {
        d = Decision{false, 0, "port-restriction", "Gateway policy",
                     "CONNECT to port " + std::to_string(t.port) + " is not permitted"};
    }

    auto finishBlocked = [&](const Decision& why) {
        entry.allow = false;
        entry.ruleId = why.ruleId;
        entry.rulePattern = why.pattern;
        entry.category = why.category;
        entry.reason = why.reason;
        sendBlockPage(clientFd, t.host, why, entry.policyVersion, requestId);
        lingerClose(clientFd);
        logs_.push(entry);
    };

    if (!d.allow) {
        finishBlocked(d);
        return;
    }

    // ---- 3. connect upstream --------------------------------------------------------
    net::ConnectResult up = net::connectTo(t.host, t.port, 5000, cfg_.blockPrivate);
    if (up.fd < 0) {
        if (up.privateBlocked) {
            finishBlocked(Decision{false, 0, "ssrf-guard", "Gateway policy",
                                   "Destination resolves to a private/internal address (SSRF protection)"});
        } else {
            entry.allow = true;  // policy allowed it, the network did not
            entry.ruleId = d.ruleId;
            entry.rulePattern = d.pattern;
            entry.category = d.category;
            entry.reason = d.reason + " [upstream unreachable: " + up.error + "]";
            sendSimple(clientFd, 502, "Upstream unreachable: " + up.error + "\n");
            lingerClose(clientFd);
            logs_.push(entry);
        }
        return;
    }
    FdGuard upstreamGuard{up.fd};
    net::setTimeouts(up.fd, 30);

    // ---- 4. tunnel / forward --------------------------------------------------------
    bool ok;
    if (isConnect) {
        // For HTTPS we never see the plaintext: after this 200 the bytes are opaque TLS.
        ok = net::sendAll(clientFd, "HTTP/1.1 200 Connection Established\r\n\r\n");
        if (ok && !extra.empty()) ok = net::sendAll(up.fd, extra);
    } else {
        ok = net::sendAll(up.fd, buildForwardRequest(head, method, path) + extra);
    }

    net::RelayStats st;
    auto started = Clock::now();
    if (ok) st = net::relay(clientFd, up.fd, 60000, *stop_);

    // Allowed traffic is logged when the connection closes so byte counts are known.
    entry.allow = true;
    entry.ruleId = d.ruleId;
    entry.rulePattern = d.pattern;
    entry.category = d.category;
    entry.reason = d.reason;
    entry.bytesUp = st.up + extra.size();
    entry.bytesDown = st.down;
    entry.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    logs_.push(entry);
}
