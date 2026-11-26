// thread_pool.hpp - fixed-size worker pool with a BOUNDED queue.
//
// Why bounded? When traffic exceeds capacity an unbounded queue just grows
// until memory runs out and latency explodes. A bounded queue gives us
// back-pressure: submit() fails fast and the caller answers "503 busy".
#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class ThreadPool {
public:
    ThreadPool(size_t threads, size_t maxQueued) : maxQueued_(maxQueued) {
        for (size_t i = 0; i < threads; ++i) workers_.emplace_back([this] { workerLoop(); });
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> g(m_);
            stopping_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Returns false when the queue is full (caller must shed the load).
    bool submit(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> g(m_);
            if (stopping_ || queue_.size() >= maxQueued_) return false;
            queue_.push_back(std::move(job));
        }
        cv_.notify_one();
        return true;
    }

private:
    void workerLoop() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [this] { return stopping_ || !queue_.empty(); });
                if (queue_.empty()) return;  // stopping and nothing left to do
                job = std::move(queue_.front());
                queue_.pop_front();
            }
            job();  // run outside the lock
        }
    }

    size_t maxQueued_;
    bool stopping_ = false;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
};
