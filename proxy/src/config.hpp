// config.hpp - all runtime configuration comes from environment variables
// (12-factor style), so the same image runs unchanged in dev and prod.
#pragma once
#include <cstdlib>
#include <set>
#include <sstream>
#include <string>

struct Config {
    int port = 8080;
    std::string controlPlaneHost = "control-plane";
    int controlPlanePort = 8081;
    std::string apiKey = "dev-proxy-key";
    int workers = 256;                // thread-per-connection: size of the worker pool
    size_t maxQueued = 256;           // accepted-but-not-yet-served connections
    int pollSeconds = 5;              // how often we ask for a new policy
    bool failOpen = false;            // behaviour when NO policy was ever loaded
    bool blockPrivate = true;         // SSRF protection (see net.cpp)
    std::set<int> connectPorts = {443, 8443};  // CONNECT is only allowed to these
    std::string blockPagePath = "/app/static/blockpage.html";
    std::string cacheFile = "/data/policy-cache.json";

    static std::string env(const char* name, const std::string& def) {
        const char* v = std::getenv(name);
        return (v && *v) ? std::string(v) : def;
    }
    static int envInt(const char* name, int def) {
        const char* v = std::getenv(name);
        return (v && *v) ? std::atoi(v) : def;
    }
    static bool envBool(const char* name, bool def) {
        const char* v = std::getenv(name);
        if (!v || !*v) return def;
        std::string s(v);
        return s == "1" || s == "true" || s == "TRUE" || s == "yes";
    }

    static Config fromEnv() {
        Config c;
        c.port = envInt("PROXY_PORT", c.port);
        c.controlPlaneHost = env("CONTROL_PLANE_HOST", c.controlPlaneHost);
        c.controlPlanePort = envInt("CONTROL_PLANE_PORT", c.controlPlanePort);
        c.apiKey = env("PROXY_API_KEY", c.apiKey);
        c.workers = envInt("WORKERS", c.workers);
        c.maxQueued = static_cast<size_t>(envInt("MAX_QUEUED", static_cast<int>(c.maxQueued)));
        c.pollSeconds = envInt("POLL_SECONDS", c.pollSeconds);
        c.failOpen = env("FAIL_MODE", "closed") == "open";
        c.blockPrivate = envBool("BLOCK_PRIVATE_RANGES", c.blockPrivate);
        c.blockPagePath = env("BLOCKPAGE_PATH", c.blockPagePath);
        c.cacheFile = env("POLICY_CACHE_FILE", c.cacheFile);

        std::string ports = env("CONNECT_PORTS", "");
        if (!ports.empty()) {
            c.connectPorts.clear();
            std::stringstream ss(ports);
            std::string item;
            while (std::getline(ss, item, ',')) {
                int p = std::atoi(item.c_str());
                if (p > 0 && p < 65536) c.connectPorts.insert(p);
            }
        }
        return c;
    }
};
