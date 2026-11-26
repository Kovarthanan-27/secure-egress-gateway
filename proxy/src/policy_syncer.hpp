// policy_syncer.hpp - keeps the local policy in sync with the control plane.
//
// PULL model: every few seconds we ask "give me a policy newer than version N".
//   * 304 -> nothing to do (cheap).      * 200 -> compile new snapshot, swap it in.
// Why pull instead of push? The proxy can sit behind NAT/firewalls and needs no
// inbound port; it keeps working if the control plane is down; and it is trivially
// horizontally scalable (N proxies = N independent pollers).
//
// The last good policy is also written to disk, so a proxy that restarts while
// the control plane is unreachable still enforces policy ("fail static").
#pragma once
#include <atomic>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>

#include "config.hpp"
#include "http_client.hpp"
#include "policy.hpp"
#include "util.hpp"

class PolicySyncer {
public:
    PolicySyncer(const Config& cfg, PolicyStore& store) : cfg_(cfg), store_(store) {}

    ~PolicySyncer() { stop(); }

    void loadFromDisk() {
        std::ifstream in(cfg_.cacheFile);
        if (!in) return;
        std::stringstream ss;
        ss << in.rdbuf();
        try {
            store_.update(PolicySnapshot::fromJson(nlohmann::json::parse(ss.str())));
            util::info("loaded cached policy v" + std::to_string(store_.version()) + " from disk");
        } catch (const std::exception& e) {
            util::warn(std::string("ignoring unreadable policy cache: ") + e.what());
        }
    }

    // Returns true if the control plane answered (200 or 304).
    bool syncOnce() {
        HttpResponse r = httpRequest(cfg_.controlPlaneHost, cfg_.controlPlanePort, "GET",
                                     "/api/proxy/policy?version=" + std::to_string(store_.version()),
                                     {{"X-API-Key", cfg_.apiKey}}, "", 5);
        if (r.status == 304) return true;
        if (r.status != 200) {
            util::warn("policy sync failed (status " + std::to_string(r.status) +
                       "); keeping policy v" + std::to_string(store_.version()));
            return false;
        }
        try {
            auto snap = PolicySnapshot::fromJson(nlohmann::json::parse(r.body));
            store_.update(snap);  // atomic swap
            util::info("activated policy v" + std::to_string(snap->version()) + " (" +
                       std::to_string(snap->ruleCount()) + " rules)");
            persist(r.body);
            return true;
        } catch (const std::exception& e) {
            util::warn(std::string("rejected malformed policy: ") + e.what());
            return false;
        }
    }

    void start() {
        thread_ = std::thread([this] {
            while (!stop_.load()) {
                for (int i = 0; i < cfg_.pollSeconds * 10 && !stop_.load(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (!stop_.load()) syncOnce();
            }
        });
    }

    void stop() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
    }

private:
    void persist(const std::string& body) {
        // write-then-rename so a crash can never leave a half-written cache file
        std::string tmp = cfg_.cacheFile + ".tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            if (!out) return;  // cache dir not writable: not fatal
            out << body;
        }
        std::rename(tmp.c_str(), cfg_.cacheFile.c_str());
    }

    Config cfg_;
    PolicyStore& store_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};
