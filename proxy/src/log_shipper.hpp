// log_shipper.hpp - asynchronous, batched log delivery to the control plane.
//
// Request threads only push into an in-memory queue (microseconds) and never
// wait on the network or the database. One background thread drains the queue
// every ~2s (or sooner when a full batch is ready) and POSTs a JSON array.
//   * Control plane down?  The batch is put back and retried (at-least-once).
//   * Queue full?          The OLDEST entries are dropped and counted, so a
//                          long outage can never exhaust the proxy's memory.
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "config.hpp"
#include "http_client.hpp"
#include "util.hpp"

struct LogEntry {
    int64_t tsMs = 0;
    std::string clientIp;
    std::string host;
    int port = 0;
    std::string method;
    bool allow = true;
    long ruleId = 0;
    std::string rulePattern, category, reason;
    long policyVersion = -1;
    long decisionUs = 0;
    uint64_t bytesUp = 0, bytesDown = 0;
    int64_t durationMs = 0;

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["ts"] = tsMs;
        j["clientIp"] = clientIp;
        j["host"] = host;
        j["port"] = port;
        j["method"] = method;
        j["decision"] = allow ? "ALLOW" : "BLOCK";
        j["ruleId"] = ruleId > 0 ? nlohmann::json(ruleId) : nlohmann::json(nullptr);
        j["rulePattern"] = rulePattern;
        j["category"] = category;
        j["reason"] = reason;
        j["policyVersion"] = policyVersion;
        j["decisionUs"] = decisionUs;
        j["bytesUp"] = bytesUp;
        j["bytesDown"] = bytesDown;
        j["durationMs"] = durationMs;
        return j;
    }
};

class LogShipper {
public:
    explicit LogShipper(const Config& cfg) : cfg_(cfg), thread_([this] { loop(); }) {}

    ~LogShipper() {
        {
            std::lock_guard<std::mutex> g(m_);
            stopping_ = true;
        }
        cv_.notify_all();
        thread_.join();
    }

    void push(LogEntry e) {
        {
            std::lock_guard<std::mutex> g(m_);
            queue_.push_back(std::move(e));
            if (queue_.size() > kCapacity) {
                queue_.pop_front();
                ++dropped_;
            }
        }
        if (queue_.size() >= kBatch) cv_.notify_one();  // racy read is fine: only a hint
    }

    uint64_t dropped() const { return dropped_.load(); }

private:
    static constexpr size_t kCapacity = 20000;
    static constexpr size_t kBatch = 200;

    void loop() {
        for (;;) {
            std::deque<LogEntry> batch;
            bool stopNow = false;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait_for(lk, std::chrono::seconds(2),
                             [this] { return stopping_ || queue_.size() >= kBatch; });
                stopNow = stopping_;
                size_t take = std::min(queue_.size(), static_cast<size_t>(500));
                for (size_t i = 0; i < take; ++i) {
                    batch.push_back(std::move(queue_.front()));
                    queue_.pop_front();
                }
            }
            if (!batch.empty() && !send(batch)) requeue(batch);
            if (stopNow) return;
        }
    }

    bool send(const std::deque<LogEntry>& batch) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : batch) arr.push_back(e.toJson());
        HttpResponse r = httpRequest(cfg_.controlPlaneHost, cfg_.controlPlanePort, "POST",
                                     "/api/proxy/logs",
                                     {{"X-API-Key", cfg_.apiKey}, {"Content-Type", "application/json"}},
                                     arr.dump(), 5);
        if (r.status >= 200 && r.status < 300) return true;
        util::warn("log shipping failed (status " + std::to_string(r.status) + "), will retry");
        return false;
    }

    void requeue(std::deque<LogEntry>& batch) {
        std::lock_guard<std::mutex> g(m_);
        for (auto it = batch.rbegin(); it != batch.rend(); ++it) queue_.push_front(std::move(*it));
        while (queue_.size() > kCapacity) {
            queue_.pop_front();
            ++dropped_;
        }
    }

    Config cfg_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<LogEntry> queue_;
    bool stopping_ = false;
    std::atomic<uint64_t> dropped_{0};
    std::thread thread_;  // declared last: starts after all other members are initialised
};
