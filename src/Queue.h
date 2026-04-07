#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <concepts>

// Constraint: T must be a pointer type (AVPacket*, AVFrame*, etc.)
// so callers are responsible for memory management of each item.
template<typename T>
concept AvPointer = std::is_pointer_v<T>;

// Thread-safe bounded queue.
// push() blocks when full; pop() blocks when empty.
// Call shutdown() to unblock all waiters (threads can then exit cleanly).
// Call flush() to drain without shutting down (e.g. after a seek).
//
// Note: capacity limiting here plays the same role as std::counting_semaphore —
// it prevents unbounded memory growth when the consumer is slower than the producer.
template<AvPointer T>
class Queue {
public:
    explicit Queue(size_t capacity = 64) : capacity_(capacity) {}

    // Push item. Blocks if at capacity. Returns false if shut down.
    bool push(T item) {
        std::unique_lock lock(mu_);
        not_full_.wait(lock, [&] { return q_.size() < capacity_ || shutdown_; });
        if (shutdown_) return false;
        q_.push(item);
        not_empty_.notify_one();
        return true;
    }

    // Blocking pop. Returns false if shut down and queue is empty.
    bool pop(T& out) {
        std::unique_lock lock(mu_);
        not_empty_.wait(lock, [&] { return !q_.empty() || shutdown_; });
        if (q_.empty()) return false;
        out = q_.front();
        q_.pop();
        not_full_.notify_one();
        return true;
    }

    // Non-blocking pop. Returns false immediately if empty.
    bool try_pop(T& out) {
        std::lock_guard lock(mu_);
        if (q_.empty()) return false;
        out = q_.front();
        q_.pop();
        not_full_.notify_one();
        return true;
    }

    // Wake all blocked push/pop calls so threads can exit.
    void shutdown() {
        shutdown_ = true;
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    // Drain the queue without shutting it down (for seek).
    // Caller is responsible for freeing any items that were in flight.
    void flush() {
        std::lock_guard lock(mu_);
        while (!q_.empty()) q_.pop();
        not_full_.notify_all();
    }

    int  size()        const { std::lock_guard l(mu_); return static_cast<int>(q_.size()); }
    bool is_shutdown() const { return shutdown_; }

private:
    std::queue<T>           q_;
    mutable std::mutex      mu_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::atomic<bool>       shutdown_{ false };
    const size_t            capacity_;
};
