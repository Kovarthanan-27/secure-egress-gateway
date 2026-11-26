// proxy_server.hpp - the forward proxy: accept loop + per-connection handler.
#pragma once
#include <atomic>
#include <string>

#include "config.hpp"
#include "log_shipper.hpp"
#include "policy.hpp"
#include "thread_pool.hpp"

class ProxyServer {
public:
    ProxyServer(const Config& cfg, PolicyStore& store, LogShipper& logs, std::string blockPageTemplate);

    // Blocks until `stop` becomes true. Returns false if the listening socket could not be created.
    bool run(std::atomic<bool>& stop);

private:
    struct Target {
        std::string host;
        int port = 0;
    };

    void handleClient(int clientFd, const std::string& clientIp);
    void sendBlockPage(int fd, const std::string& host, const Decision& d, long policyVersion,
                       const std::string& requestId);
    void sendSimple(int fd, int status, const std::string& text);
    std::string nextRequestId();

    Config cfg_;
    PolicyStore& store_;
    LogShipper& logs_;
    std::string blockPage_;
    ThreadPool pool_;
    std::atomic<uint64_t> counter_{0};
    std::atomic<bool>* stop_ = nullptr;  // shutdown flag shared with the relay loops
};
