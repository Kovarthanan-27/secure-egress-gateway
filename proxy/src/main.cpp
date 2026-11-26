// Secure Egress Gateway - data plane (forward proxy).
//
// Startup order matters:
//   1. read config           2. load cached policy from disk (if any)
//   3. try to fetch the latest policy   4. start background threads
//   5. serve traffic until SIGINT/SIGTERM, then shut down cleanly.
#include <atomic>
#include <csignal>
#include <fstream>
#include <sstream>

#include "config.hpp"
#include "log_shipper.hpp"
#include "policy.hpp"
#include "policy_syncer.hpp"
#include "proxy_server.hpp"
#include "util.hpp"

static std::atomic<bool> g_stop{false};
static void onSignal(int) { g_stop = true; }  // only an atomic store: async-signal-safe

static std::string readFile(const std::string& path, const std::string& fallback) {
    std::ifstream in(path);
    if (!in) {
        util::warn("block page template not found at " + path + "; using built-in fallback");
        return fallback;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main() {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::signal(SIGPIPE, SIG_IGN);  // writing to a closed socket must not kill the process

    Config cfg = Config::fromEnv();
    util::info("control plane: " + cfg.controlPlaneHost + ":" + std::to_string(cfg.controlPlanePort) +
               ", fail mode: " + (cfg.failOpen ? "open" : "closed") +
               ", SSRF guard: " + (cfg.blockPrivate ? "on" : "off"));

    PolicyStore store;
    PolicySyncer syncer(cfg, store);
    syncer.loadFromDisk();

    // Try a few times at startup so a proxy that boots slightly before the control plane still works.
    for (int attempt = 1; attempt <= 10 && !g_stop.load(); ++attempt) {
        if (syncer.syncOnce()) break;
        util::warn("control plane not reachable yet (attempt " + std::to_string(attempt) + "/10)");
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    if (store.version() < 0)
        util::warn("starting WITHOUT a policy; requests will " + std::string(cfg.failOpen ? "be allowed" : "be blocked") +
                   " until the control plane becomes reachable");

    const std::string fallbackPage = "<h1>Access blocked</h1><p>{{HOST}}: {{REASON}}</p>";
    LogShipper logs(cfg);
    syncer.start();

    int exitCode = 0;
    {
        ProxyServer server(cfg, store, logs, readFile(cfg.blockPagePath, fallbackPage));
        if (!server.run(g_stop)) exitCode = 1;
        util::info("shutting down: waiting for active connections to finish...");
    }  // ProxyServer destructor joins the worker threads
    syncer.stop();
    util::info("bye (log entries dropped under back-pressure: " + std::to_string(logs.dropped()) + ")");
    return exitCode;
}
