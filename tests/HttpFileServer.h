#pragma once

// An HTTP/1.1 server on 127.0.0.1, on a port the system picks, that serves the
// files under a directory, one response per connection ("Connection: close"),
// and can misbehave for one path: hold its first requests without answering
// them, or cut its body short. A held connection stays open until the server
// is destroyed, so a client without a read limit would wait for good. For the
// tests of the analyzer's HLS segment reads (DECISIONS.md D-055).

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// How an HttpFileServer answers the requests for one path.
struct HttpFileRule {
    std::string path;                  // as requested, without the leading '/'
    int         hold_first = 0;        // the first this many requests get no answer
    std::size_t cut_after  = 0;        // > 0: send only this many bytes of the body, then close
};

class HttpFileServer {
public:
    using Rule = HttpFileRule;

    explicit HttpFileServer(std::filesystem::path root, Rule rule = {})
        : root_(std::move(root)), rule_(std::move(rule))
    {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port        = 0;
        socklen_t len = sizeof addr;
        ok_ = fd_ >= 0 &&
              ::bind(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) == 0 &&
              ::listen(fd_, 16) == 0 &&
              ::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0;
        port_ = ntohs(addr.sin_port);
        if (!ok_) return;
        thread_ = std::jthread([this](std::stop_token st) { accept_loop(st); });
    }

    ~HttpFileServer()
    {
        if (thread_.joinable()) {
            thread_.request_stop();
            thread_.join();
        }
        if (fd_ >= 0) ::close(fd_);
    }

    HttpFileServer(const HttpFileServer&)            = delete;
    HttpFileServer& operator=(const HttpFileServer&) = delete;

    bool ok() const { return ok_; }

    // An http URL of `path` on this server.
    std::string url(std::string_view path) const
    {
        return std::format("http://127.0.0.1:{}/{}", port_, path);
    }

    // The requests for `path` so far.
    int requests(const std::string& path) const
    {
        std::lock_guard g(mtx_);
        const auto it = requests_.find(path);
        return it == requests_.end() ? 0 : it->second;
    }

private:
    void accept_loop(std::stop_token st)
    {
        std::vector<std::jthread> connections;
        while (!st.stop_requested()) {
            pollfd p{ fd_, POLLIN, 0 };
            if (::poll(&p, 1, 20) <= 0) continue;
            const int c = ::accept(fd_, nullptr, nullptr);
            if (c < 0) continue;
            connections.emplace_back([this, c, st] { serve(c, st); });
        }
        for (std::jthread& t : connections) t.join();   // they end at the stop
    }

    // Reads the request's head; returns its path without the leading '/', or
    // "" when the connection ended first or the server stops.
    static std::string read_request(int c, const std::stop_token& st)
    {
        std::string head;
        char buf[1024];
        while (head.find("\r\n\r\n") == std::string::npos) {
            pollfd p{ c, POLLIN, 0 };
            if (st.stop_requested()) return {};
            if (::poll(&p, 1, 20) <= 0) continue;
            const ssize_t n = ::recv(c, buf, sizeof buf, 0);
            if (n <= 0) return {};
            head.append(buf, static_cast<std::size_t>(n));
        }
        // "GET /path HTTP/1.1"
        const std::size_t sp1 = head.find(' ');
        const std::size_t sp2 = sp1 == std::string::npos ? sp1 : head.find(' ', sp1 + 1);
        if (sp2 == std::string::npos || head[sp1 + 1] != '/') return {};
        return head.substr(sp1 + 2, sp2 - sp1 - 2);
    }

    static void send_all(int c, std::string_view data)
    {
        while (!data.empty()) {
            const ssize_t n = ::send(c, data.data(), data.size(), MSG_NOSIGNAL);
            if (n <= 0) return;
            data.remove_prefix(static_cast<std::size_t>(n));
        }
    }

    void serve(int c, std::stop_token st)
    {
        const std::string path = read_request(c, st);
        int nth = 0;
        if (!path.empty()) {
            std::lock_guard g(mtx_);
            nth = ++requests_[path];
        }
        if (path.empty()) {
            ::close(c);
            return;
        }
        if (path == rule_.path && nth <= rule_.hold_first) {
            while (!st.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            ::close(c);
            return;
        }
        std::ifstream f(root_ / path, std::ios::binary);
        if (!f) {
            send_all(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            ::close(c);
            return;
        }
        const std::string body{ std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
        send_all(c, std::format("HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                                body.size()));
        const bool cut = path == rule_.path && rule_.cut_after > 0 && rule_.cut_after < body.size();
        send_all(c, std::string_view(body).substr(0, cut ? rule_.cut_after : body.size()));
        ::close(c);
    }

    std::filesystem::path      root_;
    Rule                       rule_;
    int                        fd_   = -1;
    int                        port_ = 0;
    bool                       ok_   = false;
    mutable std::mutex         mtx_;
    std::map<std::string, int> requests_;
    std::jthread               thread_;   // last: started after, and stopped before, the rest
};
