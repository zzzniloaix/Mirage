#include "NetProbe.h"

#include "NetworkLogger.h"

#include <algorithm>
#include <cstdio>
#include <format>

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/dict.h>
}

namespace {

constexpr int kWorkers = 3;

struct Abort {
    const std::stop_token*            st;
    const std::atomic<std::uint64_t>* gen;
    std::uint64_t                     mine;
};

int abort_cb(void* opaque)
{
    const auto* a = static_cast<const Abort*>(opaque);
    return a->st->stop_requested() || a->gen->load() != a->mine ? 1 : 0;
}

bool is_http(const std::string& url)
{
    return url.starts_with("http://") || url.starts_with("https://");
}

} // namespace

NetProbe::NetProbe()
{
    for (int i = 0; i < kWorkers; ++i)
        workers_.emplace_back([this](std::stop_token st) { run(st); });
}

NetProbe::~NetProbe()
{
    for (auto& w : workers_) w.request_stop();
    cv_.notify_all();
    workers_.clear();   // joins
}

std::string NetProbe::key(const Job& j)
{
    return std::format("{}|{}|{}", j.url, j.offset, j.length);
}

void NetProbe::request(const std::vector<Job>& jobs)
{
    int added = 0;
    {
        std::lock_guard lk(mtx_);
        for (const auto& j : jobs) {
            if (j.url.empty() || !seen_.insert(key(j)).second) continue;
            queue_.push_back(j);
            ++added;
        }
        pending_ += added;
    }
    if (added) cv_.notify_all();
}

void NetProbe::cancel()
{
    std::lock_guard lk(mtx_);
    pending_ -= static_cast<int>(queue_.size());
    queue_.clear();
    seen_.clear();
    ++gen_;
}

bool NetProbe::fetched(const Job& j) const
{
    std::lock_guard lk(mtx_);
    return seen_.contains(key(j));
}

void NetProbe::run(std::stop_token st)
{
    while (!st.stop_requested()) {
        Job job;
        std::uint64_t gen;
        {
            std::unique_lock lk(mtx_);
            cv_.wait(lk, st, [&] { return !queue_.empty(); });
            if (st.stop_requested()) return;
            job = std::move(queue_.front());
            queue_.pop_front();
            gen = gen_.load();
        }
        fetch(job, gen, st);
        --pending_;
    }
}

void NetProbe::fetch(const Job& j, std::uint64_t gen, const std::stop_token& st)
{
    const Abort abort{ &st, &gen_, gen };
    const AVIOInterruptCB cb{ abort_cb, const_cast<Abort*>(&abort) };
    AVDictionary* opts = nullptr;
    const bool http = is_http(j.url);
    const std::int64_t first = std::max<std::int64_t>(j.offset, 0);
    if (http) {
        av_dict_set(&opts, "timeout",    "5000000", 0);
        av_dict_set(&opts, "rw_timeout", "8000000", 0);
        // http.c sends "Range: bytes=offset-(end_offset-1)".
        if (first > 0)    av_dict_set_int(&opts, "offset", first, 0);
        if (j.length > 0) av_dict_set_int(&opts, "end_offset", first + j.length, 0);
    }
    // A local file: seek to the range, as hls.c does; the logger keeps the
    // bytes of the range given here (the handle's buffer reads around it).
    const bool local_range = !http && (first > 0 || j.length > 0);
    NetworkLogger::track_open_begin(NetInitiator::Inspector, j.url.c_str(), local_range ? first : -1,
                                    local_range && j.length > 0 ? first + j.length - 1 : -1);
    AVIOContext* pb = nullptr;
    const int ret = avio_open2(&pb, j.url.c_str(), AVIO_FLAG_READ, &cb, &opts);
    av_dict_free(&opts);
    NetworkLogger::track_open_end(ret, pb);
    if (ret < 0) return;
    if (!http && first > 0 && avio_seek(pb, first, SEEK_SET) < 0) {
        NetworkLogger::track_close(pb);
        avio_closep(&pb);
        return;
    }

    // A server that ignores Range sends everything: stop at what was asked.
    std::int64_t want = j.length > 0 ? j.length : static_cast<std::int64_t>(NetworkLogger::kMaxTextBody);
    unsigned char buf[16384];
    while (want > 0) {
        const int n = avio_read(pb, buf, static_cast<int>(std::min<std::int64_t>(want, sizeof(buf))));
        if (n <= 0) break;
        want -= n;
    }
    NetworkLogger::track_close(pb);
    avio_closep(&pb);
}
