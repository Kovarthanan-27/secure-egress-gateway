// domain_trie.hpp - longest-suffix match on domain names.
//
// Rules are stored by REVERSED labels:   mail.google.com -> com / google / mail
// Looking up "a.b.mail.google.com" walks com -> google -> mail and remembers the
// deepest node that carries a rule. Deepest = most specific rule wins, which is
// how "block facebook.com but allow developers.facebook.com" works.
//
// Lookup cost is O(number of labels in the host) - independent of rule count.
#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "util.hpp"

class DomainTrie {
public:
    // pattern examples: "facebook.com", "*.facebook.com", ".facebook.com" (all equivalent)
    void insert(const std::string& pattern, int ruleIndex) {
        Node* n = &root_;
        auto labels = util::splitLabels(normalize(pattern));
        for (auto it = labels.rbegin(); it != labels.rend(); ++it) {
            auto& child = n->children[*it];
            if (!child) child = std::make_unique<Node>();
            n = child.get();
        }
        n->ruleIndex = ruleIndex;
    }

    // Returns the index of the most specific matching rule, or -1.
    int longestMatch(const std::string& host) const {
        const Node* n = &root_;
        int best = -1;
        auto labels = util::splitLabels(normalize(host));
        for (auto it = labels.rbegin(); it != labels.rend(); ++it) {
            auto found = n->children.find(*it);
            if (found == n->children.end()) break;
            n = found->second.get();
            if (n->ruleIndex >= 0) best = n->ruleIndex;
        }
        return best;
    }

    static std::string normalize(std::string s) {
        s = util::toLower(util::trim(s));
        if (s.rfind("*.", 0) == 0) s = s.substr(2);
        while (!s.empty() && s.front() == '.') s.erase(s.begin());
        while (!s.empty() && s.back() == '.') s.pop_back();
        return s;
    }

private:
    struct Node {
        std::unordered_map<std::string, std::unique_ptr<Node>> children;
        int ruleIndex = -1;
    };
    Node root_;
};
