#pragma once

// A TCP server on 127.0.0.1, on a port the system picks, that accepts
// connections and never sends anything: a client's read on one waits. Each
// connection stays open until the server is destroyed, or `hold` has passed,
// whichever comes first, so a client without a read limit is released then
// instead of waiting for good. For the tests of the analyzer's network read
// limits (DECISIONS.md D-036, D-056).

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <format>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

class SilentServer {
public:
    explicit SilentServer(std::chrono::seconds hold = std::chrono::seconds(30))
    {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port        = 0;
        socklen_t len = sizeof addr;
        ok_ = fd_ >= 0 &&
              ::bind(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) == 0 &&
              ::listen(fd_, 8) == 0 &&
              ::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0;
        port_ = ntohs(addr.sin_port);
        if (!ok_) return;
        thread_ = std::jthread([this, hold](std::stop_token st) {
            const auto end = std::chrono::steady_clock::now() + hold;
            std::vector<int> clients;
            while (!st.stop_requested() && std::chrono::steady_clock::now() < end) {
                pollfd p{ fd_, POLLIN, 0 };
                if (::poll(&p, 1, 20) <= 0) continue;
                if (const int c = ::accept(fd_, nullptr, nullptr); c >= 0) {
                    clients.push_back(c);
                    accepted_.fetch_add(1);
                }
            }
            for (const int c : clients) ::close(c);
        });
    }

    ~SilentServer()
    {
        if (thread_.joinable()) {
            thread_.request_stop();
            thread_.join();
        }
        if (fd_ >= 0) ::close(fd_);
    }

    SilentServer(const SilentServer&)            = delete;
    SilentServer& operator=(const SilentServer&) = delete;

    bool ok() const { return ok_; }
    int  port() const { return port_; }
    int  accepted() const { return accepted_.load(); }

    // An http URL of `path` on this server.
    std::string url(std::string_view path) const
    {
        return std::format("http://127.0.0.1:{}/{}", port_, path);
    }

    // Waits until `n` connections have been accepted, at most `limit`.
    bool wait_accepted(int n, std::chrono::milliseconds limit) const
    {
        const auto end = std::chrono::steady_clock::now() + limit;
        while (accepted() < n) {
            if (std::chrono::steady_clock::now() >= end) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return true;
    }

private:
    int              fd_   = -1;
    int              port_ = 0;
    bool             ok_   = false;
    std::atomic<int> accepted_{ 0 };
    std::jthread     thread_;   // last: started after, and stopped before, the rest
};
