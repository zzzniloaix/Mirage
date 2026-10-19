#pragma once

// Requests the network inspector makes on its own, for the alignment check:
// a playlist the player has not fetched, an init segment, or the first bytes
// of a media segment (a byte range). They run through NetworkLogger with
// initiator Inspector, so they are listed like any other request and its
// body tap keeps what arrives — the inspector reads the results from those
// rows. Fetches run on a few worker threads, one request each at a time.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <set>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

class NetProbe {
public:
    struct Job {
        std::string  url;
        std::int64_t offset = -1;   // first byte; -1 = the start
        std::int64_t length = -1;   // bytes wanted; -1 = all of it (playlists)
    };

    NetProbe();
    ~NetProbe();
    NetProbe(const NetProbe&) = delete;
    NetProbe& operator=(const NetProbe&) = delete;

    // Queue the jobs not queued or fetched before (same URL and range).
    void request(const std::vector<Job>& jobs);
    // Drop the queue, abort fetches in flight, forget what was fetched (new media).
    void cancel();
    int  pending() const { return pending_.load(); }   // queued + in flight
    bool fetched(const Job& j) const;                  // queued or done since the last cancel()

    static std::string key(const Job& j);

private:
    void run(std::stop_token st);
    void fetch(const Job& j, std::uint64_t gen, const std::stop_token& st);

    mutable std::mutex           mtx_;
    std::condition_variable_any  cv_;
    std::deque<Job>              queue_;
    std::set<std::string>        seen_;
    std::atomic<int>             pending_{ 0 };
    std::atomic<std::uint64_t>   gen_{ 0 };
    std::vector<std::jthread>    workers_;
};
