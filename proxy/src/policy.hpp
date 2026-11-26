// policy.hpp - the policy engine (the heart of the data plane).
//
//  * PolicySnapshot : an IMMUTABLE compiled policy (trie + default action + its own cache).
//  * PolicyStore    : holds the "current" snapshot and swaps it atomically.
//
// Why immutable snapshots? Request threads never take a write lock while
// evaluating. When the control plane publishes a new version we build a brand
// new snapshot off to the side and swap one shared_ptr (RCU-style). In-flight
// requests keep using the old snapshot until they finish; it is freed
// automatically when the last reference disappears. Because each snapshot owns
// its decision cache, there is no cache-invalidation logic at all.
#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "domain_trie.hpp"
#include "lru_cache.hpp"
#include "util.hpp"

struct Rule {
    long id = 0;
    std::string pattern;
    bool allow = false;
    std::string category;
    std::string reason;
};

struct Decision {
    bool allow = true;
    long ruleId = 0;  // 0 => no rule matched, default action applied
    std::string pattern;
    std::string category;
    std::string reason;
};

class PolicySnapshot {
public:
    PolicySnapshot(long version, bool defaultAllow, std::vector<Rule> rules)
        : version_(version), defaultAllow_(defaultAllow), rules_(std::move(rules)), cache_(20000) {
        for (size_t i = 0; i < rules_.size(); ++i) trie_.insert(rules_[i].pattern, static_cast<int>(i));
    }

    // Expected JSON (produced by the Spring Boot control plane):
    // {"version":7,"defaultAction":"ALLOW","rules":[{"id":1,"pattern":"facebook.com",
    //   "action":"BLOCK","category":"Social Media","reason":"..."}]}
    static std::shared_ptr<const PolicySnapshot> fromJson(const nlohmann::json& j) {
        std::vector<Rule> rules;
        for (const auto& r : j.at("rules")) {
            Rule rule;
            rule.id = r.at("id").get<long>();
            rule.pattern = r.at("pattern").get<std::string>();
            rule.allow = r.at("action").get<std::string>() == "ALLOW";
            rule.category = r.value("category", std::string());
            rule.reason = r.value("reason", std::string());
            rules.push_back(std::move(rule));
        }
        bool defaultAllow = j.value("defaultAction", std::string("ALLOW")) == "ALLOW";
        return std::make_shared<PolicySnapshot>(j.at("version").get<long>(), defaultAllow, std::move(rules));
    }

    long version() const { return version_; }
    size_t ruleCount() const { return rules_.size(); }

    Decision evaluate(const std::string& rawHost) const {
        const std::string host = DomainTrie::normalize(rawHost);
        if (auto cached = cache_.get(host)) return *cached;  // hot path: O(1)

        Decision d;
        int idx = trie_.longestMatch(host);  // cold path: O(labels)
        if (idx >= 0) {
            const Rule& r = rules_[static_cast<size_t>(idx)];
            d.allow = r.allow;
            d.ruleId = r.id;
            d.pattern = r.pattern;
            d.category = r.category;
            d.reason = r.reason.empty() ? (r.allow ? "Allowed by policy rule" : "Blocked by policy rule") : r.reason;
        } else {
            d.allow = defaultAllow_;
            d.category = "Default policy";
            d.reason = std::string("No rule matched; default action is ") + (defaultAllow_ ? "ALLOW" : "BLOCK");
        }
        cache_.put(host, d);
        return d;
    }

private:
    long version_;
    bool defaultAllow_;
    std::vector<Rule> rules_;
    DomainTrie trie_;
    mutable LruCache<std::string, Decision> cache_;  // mutable: caching is not a logical mutation
};

class PolicyStore {
public:
    std::shared_ptr<const PolicySnapshot> current() const {
        std::lock_guard<std::mutex> g(m_);  // lock only guards the pointer copy (nanoseconds)
        return snap_;
    }
    void update(std::shared_ptr<const PolicySnapshot> s) {
        std::lock_guard<std::mutex> g(m_);
        snap_ = std::move(s);
    }
    long version() const {
        auto s = current();
        return s ? s->version() : -1;
    }

private:
    mutable std::mutex m_;
    std::shared_ptr<const PolicySnapshot> snap_;
};
