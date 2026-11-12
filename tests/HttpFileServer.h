#pragma once

// An HTTP/1.1 server on 127.0.0.1, on a port the system picks, that serves the
// files under a directory, one response per connection ("Connection: close"),
// and can misbehave for a path, each by a rule of its own: hold its first
// requests without answering them, cut its body short, stop sending its body
// part way with the connection left open, send its response's head only
// after a set time, or answer it with a redirect to another path; and it can
// answer a path's Range header. A held or stalled connection stays open until
// the server is destroyed, so a client without a read limit would wait for
// good. For the tests of the analyzer's HLS segment reads (DECISIONS.md
// D-055) and of the timeline's session fetch (D-104; the design's P-O, in
// eval/phase3-survey/design.md §13).

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

// How an HttpFileServer answers the requests for one path.
struct HttpFileRule {
    std::string path;                  // as requested, without the leading '/'
    int         hold_first = 0;        // the first this many requests get no answer
    std::size_t cut_after  = 0;        // > 0: send only this many bytes of the body, then close
    // > 0: each response's head is sent this long after its request was
    // read, as a slow server's first byte (the session fetch's delayed
    // segment, design §9.3).
    std::chrono::milliseconds delay{ 0 };
    // Not empty: each request is answered "302 Found" with a Location of
    // this path on the same server, and no body.
    std::string redirect_to;
    // A Range header, "bytes=<first>-<last>" or "bytes=<first>-", is
    // answered "206 Partial Content" with those bytes and their
    // Content-Range, and one whose first byte is past the file's end "416
    // Range Not Satisfiable". Without the rule every request gets the whole
    // file with "200 OK", a Range header or not.
    bool        ranges = false;
    // > 0: send only this many bytes of the body, then nothing more, the
    // connection held open until the server is destroyed, as a server that
    // stops sending in the middle of a body (stalled() counts them); a
    // client's read of the rest then waits for its read limit or its
    // interrupt. Given with cut_after, it holds instead.
    std::size_t stall_after = 0;
};

class HttpFileServer {
public:
    using Rule = HttpFileRule;

    explicit HttpFileServer(std::filesystem::path root, Rule rule = {})
        : HttpFileServer(std::move(root), std::vector<Rule>{ std::move(rule) })
    {
    }

    // A rule for each of several paths; a path's first rule is the one that
    // holds for it, and one with an empty path holds for none.
    HttpFileServer(std::filesystem::path root, std::vector<Rule> rules)
        : root_(std::move(root)), rules_(std::move(rules))
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

    // The base of this server's URLs, "http://127.0.0.1:<port>".
    std::string base() const { return std::format("http://127.0.0.1:{}", port_); }

    // An http URL of `path` on this server.
    std::string url(std::string_view path) const { return std::format("{}/{}", base(), path); }

    // The requests for `path` so far.
    int requests(const std::string& path) const
    {
        std::lock_guard g(mtx_);
        const auto it = requests_.find(path);
        return it == requests_.end() ? 0 : it->second;
    }

    // The path of each request so far, in the order the server read them.
    std::vector<std::string> paths() const
    {
        std::lock_guard g(mtx_);
        return order_;
    }

    // The Range header of each request for `path` so far, in order, ""
    // for one without.
    std::vector<std::string> ranges(const std::string& path) const
    {
        std::lock_guard g(mtx_);
        const auto it = ranges_.find(path);
        return it == ranges_.end() ? std::vector<std::string>{} : it->second;
    }

    // The responses for `path` so far that have sent a stall_after rule's
    // bytes and send nothing more.
    int stalled(const std::string& path) const
    {
        std::lock_guard g(mtx_);
        const auto it = stalled_.find(path);
        return it == stalled_.end() ? 0 : it->second;
    }

private:
    struct Head {
        std::string path;                      // without the leading '/'; "" when none was read
        std::string range;                     // the Range header's value, or ""
    };

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

    // Reads the request's head: its path without the leading '/', "" when
    // the connection ended first or the server stops, and its Range header.
    static Head read_request(int c, const std::stop_token& st)
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
        Head out{ .path = head.substr(sp1 + 2, sp2 - sp1 - 2) };
        // "Range: bytes=0-" among the header lines, its name in any case.
        constexpr std::string_view kName = "range:";
        for (std::size_t at = head.find("\r\n"); at != std::string::npos && at + 2 < head.size();) {
            const std::size_t      end = head.find("\r\n", at + 2);
            const std::string_view line(head.data() + at + 2,
                                        (end == std::string::npos ? head.size() : end) - at - 2);
            if (line.size() > kName.size() &&
                std::equal(kName.begin(), kName.end(), line.begin(), [](char a, char b) {
                    return a == std::tolower(static_cast<unsigned char>(b));
                })) {
                std::string_view v = line.substr(kName.size());
                while (!v.empty() && v.front() == ' ') v.remove_prefix(1);
                out.range = std::string(v);
            }
            at = end;
        }
        return out;
    }

    static void send_all(int c, std::string_view data)
    {
        while (!data.empty()) {
            const ssize_t n = ::send(c, data.data(), data.size(), MSG_NOSIGNAL);
            if (n <= 0) return;
            data.remove_prefix(static_cast<std::size_t>(n));
        }
    }

    // The first and the last byte "bytes=<first>-[<last>]" asks for of a
    // body of `size` bytes, the last no further than its end; nullopt for
    // any other form.
    static std::optional<std::pair<std::size_t, std::size_t>> byte_range(std::string_view v, std::size_t size)
    {
        constexpr std::string_view kUnit = "bytes=";
        if (!v.starts_with(kUnit)) return std::nullopt;
        v.remove_prefix(kUnit.size());
        const std::size_t dash = v.find('-');
        if (dash == std::string_view::npos || dash == 0) return std::nullopt;
        std::size_t first = 0, last = size == 0 ? 0 : size - 1;
        if (std::from_chars(v.data(), v.data() + dash, first).ptr != v.data() + dash) return std::nullopt;
        if (dash + 1 < v.size()) {
            const char* end = v.data() + v.size();
            if (std::from_chars(v.data() + dash + 1, end, last).ptr != end) return std::nullopt;
            if (size > 0) last = std::min(last, size - 1);
        }
        if (last < first && first < size) return std::nullopt;
        return std::pair{ first, last };
    }

    const Rule* rule_for(const std::string& path) const
    {
        for (const Rule& r : rules_)
            if (!r.path.empty() && r.path == path) return &r;
        return nullptr;
    }

    void serve(int c, std::stop_token st)
    {
        const Head         req  = read_request(c, st);
        const std::string& path = req.path;
        if (path.empty()) {
            ::close(c);
            return;
        }
        int nth = 0;
        {
            std::lock_guard g(mtx_);
            nth = ++requests_[path];
            order_.push_back(path);
            ranges_[path].push_back(req.range);
        }
        const Rule* rule = rule_for(path);
        if (rule && nth <= rule->hold_first) {
            while (!st.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            ::close(c);
            return;
        }
        if (rule && rule->delay.count() > 0) {
            const auto until = std::chrono::steady_clock::now() + rule->delay;
            while (!st.stop_requested() && std::chrono::steady_clock::now() < until)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (rule && !rule->redirect_to.empty()) {
            send_all(c, std::format("HTTP/1.1 302 Found\r\nLocation: /{}\r\nContent-Length: 0\r\n"
                                    "Connection: close\r\n\r\n",
                                    rule->redirect_to));
            ::close(c);
            return;
        }
        std::ifstream f(root_ / path, std::ios::binary);
        if (!f) {
            send_all(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            ::close(c);
            return;
        }
        std::string body{ std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
        std::string status = "200 OK", range;
        if (rule && rule->ranges && !req.range.empty()) {
            if (const auto r = byte_range(req.range, body.size())) {
                if (r->first >= body.size()) {
                    send_all(c, std::format("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */{}\r\n"
                                            "Content-Length: 0\r\nConnection: close\r\n\r\n",
                                            body.size()));
                    ::close(c);
                    return;
                }
                range  = std::format("Content-Range: bytes {}-{}/{}\r\n", r->first, r->second, body.size());
                status = "206 Partial Content";
                body   = body.substr(r->first, r->second - r->first + 1);
            }
        }
        send_all(c, std::format("HTTP/1.1 {}\r\n{}Content-Length: {}\r\nConnection: close\r\n\r\n", status, range,
                                body.size()));
        const bool cut   = rule && rule->cut_after > 0 && rule->cut_after < body.size();
        const bool stall = rule && rule->stall_after > 0 && rule->stall_after < body.size();
        send_all(c, std::string_view(body).substr(0, stall ? rule->stall_after
                                                     : cut ? rule->cut_after
                                                           : body.size()));
        if (stall) {
            {
                std::lock_guard g(mtx_);
                ++stalled_[path];
            }
            while (!st.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        ::close(c);
    }

    std::filesystem::path      root_;
    std::vector<Rule>          rules_;
    int                        fd_   = -1;
    int                        port_ = 0;
    bool                       ok_   = false;
    mutable std::mutex         mtx_;
    std::map<std::string, int> requests_;
    std::vector<std::string>   order_;
    std::map<std::string, std::vector<std::string>> ranges_;
    std::map<std::string, int> stalled_;
    std::jthread               thread_;   // last: started after, and stopped before, the rest
};
