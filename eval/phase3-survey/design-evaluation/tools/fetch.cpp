// fetch: a prototype of the session fetcher the Phase 3 design proposes for a
// scripted session's HTTP timing (design-evaluation.md, owner question on the
// timing's source): each URL of a plan fetched in order, one at a time, with
// FFmpeg's avio (library API only), and timed. For the design's runs; not
// repository code.
//
//   fetch <plan.tsv> <out.json>
//
// <plan.tsv>: one "<label>\t<url>" per line. For each URL: avio_open2 with
// the analyzer's network limits (timeout and rw_timeout 5 s, src/VMAFNet.h),
// then avio_read to the end. Recorded per URL, in microseconds of a steady
// clock from the fetcher's start: t_start (before avio_open2), t_open (after
// it returns, which over HTTP is after the response's head was read), t_end
// (the read's end); the system clock's UTC time at t_start; the bytes read;
// avio_open2's result; and the HTTP status line, from the TRACE line
// "header='HTTP/...'" that FFmpeg's http.c logs (libavformat/http.c of n8.1.1),
// which a custom av_log callback sees whatever the log level. The fetches are
// sequential on one thread, so each log line belongs to the fetch running.

extern "C" {
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/dict.h>
#include <libavutil/log.h>
}

#include <chrono>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string g_status_line;   // the last "HTTP/..." status line logged during the current fetch

void on_log(void* avcl, int level, const char* fmt, va_list vl)
{
    (void)avcl;
    (void)level;
    if (std::strncmp(fmt, "header='", 8) != 0) return;
    char line[1024];
    va_list copy;
    va_copy(copy, vl);
    std::vsnprintf(line, sizeof line, fmt, copy);
    va_end(copy);
    // "header='HTTP/1.1 200 OK'\n"
    if (std::strncmp(line, "header='HTTP/", 13) == 0) {
        std::string s(line + 8);
        const auto q = s.find('\'');
        g_status_line = q == std::string::npos ? s : s.substr(0, q);
    }
}

int64_t now_us(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0)
        .count();
}

std::string utc_now()
{
    const auto t  = std::chrono::system_clock::now();
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count();
    const std::time_t secs = static_cast<std::time_t>(us / 1000000);
    std::tm tm{};
    gmtime_r(&secs, &tm);
    char b[64];
    std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%S", &tm);
    char out[80];
    std::snprintf(out, sizeof out, "%s.%06lldZ", b, static_cast<long long>(us % 1000000));
    return out;
}

std::string json_string(const std::string& s)
{
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (static_cast<unsigned char>(c) < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
        else out += c;
    }
    return out + "\"";
}

struct Row {
    std::string label, url, wall_start, status_line;
    int64_t t_start = 0, t_open = 0, t_end = 0, bytes = 0;
    int open_result = 0, read_error = 0;
};

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: fetch <plan.tsv> <out.json>\n");
        return 2;
    }
    av_log_set_level(AV_LOG_WARNING);
    av_log_set_callback(on_log);
    avformat_network_init();

    std::ifstream plan(argv[1]);
    std::vector<std::pair<std::string, std::string>> items;
    for (std::string line; std::getline(plan, line);) {
        if (line.empty()) continue;
        const auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        items.push_back({ line.substr(0, tab), line.substr(tab + 1) });
    }

    const auto t0 = std::chrono::steady_clock::now();
    std::vector<Row> rows;
    std::vector<unsigned char> buf(64 * 1024);
    for (const auto& [label, url] : items) {
        Row r;
        r.label = label;
        r.url   = url;
        g_status_line.clear();
        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "timeout", "5000000", 0);
        av_dict_set(&opts, "rw_timeout", "5000000", 0);
        r.wall_start = utc_now();
        r.t_start    = now_us(t0);
        AVIOContext* io = nullptr;
        r.open_result = avio_open2(&io, url.c_str(), AVIO_FLAG_READ, nullptr, &opts);
        r.t_open = now_us(t0);
        av_dict_free(&opts);
        if (r.open_result >= 0) {
            for (;;) {
                const int n = avio_read(io, buf.data(), static_cast<int>(buf.size()));
                if (n > 0) { r.bytes += n; continue; }
                if (n != AVERROR_EOF) r.read_error = n;
                break;
            }
            avio_closep(&io);
        }
        r.t_end       = now_us(t0);
        r.status_line = g_status_line;
        rows.push_back(r);
    }

    FILE* out = std::fopen(argv[2], "w");
    if (!out) return 1;
    std::fprintf(out, "{\"clock\": \"steady microseconds from the fetcher's start\", \"fetches\": [");
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        std::fprintf(out, "%s\n  {\"label\": %s, \"url\": %s, \"wall_start\": %s, \"t_start\": %" PRId64
                     ", \"t_open\": %" PRId64 ", \"t_end\": %" PRId64 ", \"bytes\": %" PRId64
                     ", \"open_result\": %d, \"read_error\": %d, \"status_line\": %s}",
                     i ? "," : "", json_string(r.label).c_str(), json_string(r.url).c_str(),
                     json_string(r.wall_start).c_str(), r.t_start, r.t_open, r.t_end, r.bytes, r.open_result,
                     r.read_error, json_string(r.status_line).c_str());
    }
    std::fprintf(out, "]}\n");
    std::fclose(out);

    std::printf("label\tt_start_us\tt_open_us\tt_end_us\tduration_us\tbytes\topen_result\tstatus_line\n");
    for (const Row& r : rows)
        std::printf("%s\t%" PRId64 "\t%" PRId64 "\t%" PRId64 "\t%" PRId64 "\t%" PRId64 "\t%d\t%s\n",
                    r.label.c_str(), r.t_start, r.t_open, r.t_end, r.t_end - r.t_start, r.bytes,
                    r.open_result, r.status_line.c_str());
    return 0;
}
