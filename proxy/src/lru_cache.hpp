// lru_cache.hpp - thread-safe fixed-size LRU cache.
//
// Classic design: a doubly linked list keeps entries in recency order
// (front = most recently used) and a hash map gives O(1) access to list nodes.
// get/put are both O(1). One mutex guards both structures.
#pragma once
#include <list>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

template <typename K, typename V>
class LruCache {
public:
    explicit LruCache(size_t capacity) : capacity_(capacity) {}

    std::optional<V> get(const K& key) {
        std::lock_guard<std::mutex> g(m_);
        auto it = map_.find(key);
        if (it == map_.end()) return std::nullopt;
        order_.splice(order_.begin(), order_, it->second);  // move to front, O(1)
        return it->second->second;
    }

    void put(const K& key, V value) {
        std::lock_guard<std::mutex> g(m_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second->second = std::move(value);
            order_.splice(order_.begin(), order_, it->second);
            return;
        }
        order_.emplace_front(key, std::move(value));
        map_[key] = order_.begin();
        if (map_.size() > capacity_) {  // evict least recently used (back of list)
            map_.erase(order_.back().first);
            order_.pop_back();
        }
    }

    size_t size() const {
        std::lock_guard<std::mutex> g(m_);
        return map_.size();
    }

private:
    using Entry = std::pair<K, V>;
    size_t capacity_;
    mutable std::mutex m_;
    std::list<Entry> order_;
    std::unordered_map<K, typename std::list<Entry>::iterator> map_;
};
