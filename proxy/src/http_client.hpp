// http_client.hpp - a deliberately tiny HTTP/1.1 client.
// The proxy only needs two calls to the control plane (GET policy, POST logs),
// so a few dozen lines of sockets beat pulling in libcurl.
#pragma once
#include <string>
#include <utility>
#include <vector>

struct HttpResponse {
    int status = 0;  // 0 => transport error (connect/timeout/parse)
    std::string body;
};

using Headers = std::vector<std::pair<std::string, std::string>>;

HttpResponse httpRequest(const std::string& host, int port, const std::string& method,
                         const std::string& path, const Headers& headers,
                         const std::string& body, int timeoutSeconds);
