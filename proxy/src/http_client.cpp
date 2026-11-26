#include "http_client.hpp"

#include <unistd.h>
#include <sys/socket.h>

#include <cstdio>
#include <cstdlib>

#include "net.hpp"
#include "util.hpp"

// Decode "Transfer-Encoding: chunked" (Tomcat uses it for larger JSON bodies).
// Format:  <hex-size>\r\n<data>\r\n ... 0\r\n\r\n
static std::string decodeChunked(const std::string& raw) {
    std::string out;
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t eol = raw.find("\r\n", pos);
        if (eol == std::string::npos) break;
        size_t size = std::strtoul(raw.substr(pos, eol - pos).c_str(), nullptr, 16);
        pos = eol + 2;
        if (size == 0) break;
        if (pos + size > raw.size()) break;  // truncated body
        out.append(raw, pos, size);
        pos += size + 2;  // skip data + trailing CRLF
    }
    return out;
}

HttpResponse httpRequest(const std::string& host, int port, const std::string& method,
                         const std::string& path, const Headers& headers,
                         const std::string& body, int timeoutSeconds) {
    HttpResponse resp;
    // Control-plane traffic is internal, so the SSRF guard is off for this call.
    net::ConnectResult c = net::connectTo(host, port, timeoutSeconds * 1000, /*denyPrivate=*/false);
    if (c.fd < 0) return resp;
    net::setTimeouts(c.fd, timeoutSeconds);

    std::string req = method + " " + path + " HTTP/1.1\r\n";
    req += "Host: " + host + ":" + std::to_string(port) + "\r\n";
    req += "Connection: close\r\n";  // server closes when done, so we simply read to EOF
    for (const auto& h : headers) req += h.first + ": " + h.second + "\r\n";
    if (!body.empty() || method == "POST" || method == "PUT")
        req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    req += "\r\n" + body;

    if (!net::sendAll(c.fd, req)) {
        ::close(c.fd);
        return resp;
    }

    std::string raw;
    char buf[8192];
    const size_t kMaxResponse = 16u * 1024 * 1024;
    while (raw.size() < kMaxResponse) {
        ssize_t n = ::recv(c.fd, buf, sizeof buf, 0);
        if (n <= 0) break;
        raw.append(buf, static_cast<size_t>(n));
    }
    ::close(c.fd);

    size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return resp;
    std::string head = raw.substr(0, headerEnd);
    std::string payload = raw.substr(headerEnd + 4);

    int status = 0;
    if (std::sscanf(head.c_str(), "HTTP/%*d.%*d %d", &status) != 1) return resp;
    resp.status = status;

    if (util::toLower(head).find("transfer-encoding: chunked") != std::string::npos)
        resp.body = decodeChunked(payload);
    else
        resp.body = std::move(payload);
    return resp;
}
