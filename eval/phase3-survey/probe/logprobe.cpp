// logprobe: what mirage_vmaf's analyzer's reads of one input show, through
// FFmpeg's library APIs only. The input is opened as DecodeCtx::open() opens it
// (src/VMAFAnalyzer.cpp): avformat_alloc_context(), an interrupt callback, the
// repository's SegmentWatch (src/VMAFSegments.h) on io_open / io_close2, the
// hls options of vmafsegments::set_hls_options() and the I/O limits of
// vmafnet::set_io_limits() (src/VMAFNet.h), avformat_find_stream_info(), the
// best video stream and its decoder with default options. Around SegmentWatch
// sits a recorder of every io_open and io_close2 the context gets; a
// process-global av_log callback records every log call whatever its level;
// every packet read and every frame decoded is recorded. Prints one line per
// event, in order, then counts.
//
//   logprobe file <playlist>                  a local path, as given
//   logprobe http <root> <path under root>    over HTTP from tests/HttpFileServer.h,
//                                             serving <root> on 127.0.0.1
//   logprobe concurrent <a> <b>               two inputs read at once on two threads;
//                                             thread 2 replaces the av_log callback
//                                             once it has opened its input
//
// Times are microseconds of std::chrono::steady_clock from the probe's start.

#include "VMAFNet.h"
#include "VMAFSegments.h"
#include "HttpFileServer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/log.h>
}

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace {

const auto g_t0 = std::chrono::steady_clock::now();
long long now_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - g_t0)
        .count();
}

std::mutex g_out;            // one printed line at a time
thread_local int t_thread = 1;   // the probe's own thread numbers: 1, 2

// Small ids for pointers, so that no address is printed.
std::mutex g_ids_mtx;
std::map<const void*, int> g_ids;
int id_of(const void* p)
{
    if (!p) return 0;
    std::lock_guard g(g_ids_mtx);
    auto [it, fresh] = g_ids.try_emplace(p, static_cast<int>(g_ids.size()) + 1);
    return it->second;
}

const char* level_name(int level)
{
    if (level <= AV_LOG_PANIC)   return "PANIC";
    if (level <= AV_LOG_FATAL)   return "FATAL";
    if (level <= AV_LOG_ERROR)   return "ERROR";
    if (level <= AV_LOG_WARNING) return "WARNING";
    if (level <= AV_LOG_INFO)    return "INFO";
    if (level <= AV_LOG_VERBOSE) return "VERBOSE";
    if (level <= AV_LOG_DEBUG)   return "DEBUG";
    return "TRACE";
}

std::string escaped(const char* s, std::size_t max = 240)
{
    std::string out;
    for (const char* p = s; *p && out.size() < max; ++p) {
        if (*p == '\n') out += "\\n";
        else if (*p == '\r') out += "\\r";
        else if (*p == '\t') out += "\\t";
        else out += *p;
    }
    return out;
}

// Counts of log calls by (callback, thread, level, first word of the format).
std::mutex g_count_mtx;
std::map<std::string, int> g_counts;

void record_log(const char* cb, void* avcl, int level, const char* fmt, va_list vl)
{
    char line[4096];
    va_list copy;
    va_copy(copy, vl);
    std::vsnprintf(line, sizeof line, fmt, copy);
    va_end(copy);
    const AVClass* cls = avcl ? *static_cast<AVClass**>(avcl) : nullptr;
    const char* cname = cls && cls->class_name ? cls->class_name : "-";
    std::string head(fmt, std::strcspn(fmt, " =:%'\n"));
    {
        std::lock_guard g(g_count_mtx);
        ++g_counts[std::string(cb) + " thread=" + std::to_string(t_thread) + " " + level_name(level) +
                   " " + cname + " '" + head + "'"];
    }
    std::lock_guard g(g_out);
    std::printf("%lld\tt%d\tlog\t%s\t%s\t%s#%d\t%s\n", now_us(), t_thread, cb, level_name(level), cname,
                id_of(avcl), escaped(line).c_str());
}

void log_cb_a(void* avcl, int level, const char* fmt, va_list vl) { record_log("cbA", avcl, level, fmt, vl); }
void log_cb_b(void* avcl, int level, const char* fmt, va_list vl) { record_log("cbB", avcl, level, fmt, vl); }

// The recorder around SegmentWatch: the context's io_open / io_close2 when it
// was installed, by context (SegmentWatch keeps AVFormatContext::opaque).
struct Saved {
    int (*open)(AVFormatContext*, AVIOContext**, const char*, int, AVDictionary**) = nullptr;
    int (*close2)(AVFormatContext*, AVIOContext*) = nullptr;
};
std::mutex g_saved_mtx;
std::map<const AVFormatContext*, Saved> g_saved;
thread_local int t_opens = 0;          // opens recorded on this thread
thread_local std::string t_last_open;  // the last one's URL

Saved saved_of(const AVFormatContext* s)
{
    std::lock_guard g(g_saved_mtx);
    return g_saved[s];
}

int rec_open(AVFormatContext* s, AVIOContext** pb, const char* url, int flags, AVDictionary** options)
{
    const AVDictionaryEntry* off = options ? av_dict_get(*options, "offset", nullptr, 0) : nullptr;
    const AVDictionaryEntry* end = options ? av_dict_get(*options, "end_offset", nullptr, 0) : nullptr;
    {
        std::lock_guard g(g_out);
        std::printf("%lld\tt%d\topen_begin\tctx#%d\turl=%s\toffset=%s\tend_offset=%s\n", now_us(), t_thread,
                    id_of(s), url, off ? off->value : "-", end ? end->value : "-");
    }
    const int r = saved_of(s).open(s, pb, url, flags, options);
    ++t_opens;
    t_last_open = url;
    std::lock_guard g(g_out);
    std::printf("%lld\tt%d\topen_end\tctx#%d\tret=%d\tpb#%d\n", now_us(), t_thread, id_of(s), r,
                r >= 0 && *pb ? id_of(*pb) : 0);
    return r;
}

int rec_close2(AVFormatContext* s, AVIOContext* pb)
{
    {
        std::lock_guard g(g_out);
        std::printf("%lld\tt%d\tclose\tctx#%d\tpb#%d\tbytes_read=%lld\terror=%d\n", now_us(), t_thread,
                    id_of(s), id_of(pb), pb ? static_cast<long long>(pb->bytes_read) : -1LL,
                    pb ? pb->error : 0);
    }
    return saved_of(s).close2(s, pb);
}

void wrap(AVFormatContext* ctx)
{
    std::lock_guard g(g_saved_mtx);
    g_saved[ctx] = { ctx->io_open, ctx->io_close2 };
    ctx->io_open   = rec_open;
    ctx->io_close2 = rec_close2;
}

double secs(int64_t ts, AVRational tb)
{
    return ts == AV_NOPTS_VALUE ? -1.0 : static_cast<double>(ts) * tb.num / tb.den;
}

// Opens `url` as DecodeCtx::open() does, reads every packet and decodes the
// video; `after_open` runs once the input is open. Returns 0 or 1.
int read_input(const std::string& url, const std::function<void()>& after_open = {})
{
    std::stop_source stop;
    const std::stop_token st = stop.get_token();
    vmafsegments::SegmentWatch segments;
    AVFormatContext* fmt = avformat_alloc_context();
    fmt->interrupt_callback.callback = vmafnet::stop_requested;
    fmt->interrupt_callback.opaque   = const_cast<std::stop_token*>(&st);
    segments.watch(fmt);   // as DecodeCtx::open()
    wrap(fmt);             // the recorder, outside SegmentWatch
    AVDictionary* opts = nullptr;
    vmafnet::set_io_limits(&opts, url);
    vmafsegments::set_hls_options(&opts);
    {
        std::lock_guard g(g_out);
        std::printf("%lld\tt%d\tavformat_open_input\t%s\n", now_us(), t_thread, url.c_str());
    }
    int r = avformat_open_input(&fmt, url.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    if (r < 0) {
        std::printf("avformat_open_input failed: %d\n", r);
        return 1;
    }
    {
        std::lock_guard g(g_out);
        std::printf("%lld\tt%d\tavformat_find_stream_info\n", now_us(), t_thread);
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) return 1;
    const AVCodec* codec = nullptr;
    const int vid = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (vid < 0 || !codec) return 1;
    AVStream* vs = fmt->streams[vid];
    AVCodecContext* dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, vs->codecpar);
    if (avcodec_open2(dec, codec, nullptr) < 0) return 1;
    {
        std::lock_guard g(g_out);
        std::printf("%lld\tt%d\topened\tformat=%s\tvideo_stream=%d\ttime_base=%d/%d\tstream_start=%.6f\t"
                    "container_start_us=%lld\tduration_us=%lld\tnb_programs=%u\topens_so_far=%d\n",
                    now_us(), t_thread, fmt->iformat->name, vid, vs->time_base.num, vs->time_base.den,
                    secs(vs->start_time, vs->time_base),
                    fmt->start_time == AV_NOPTS_VALUE ? -1LL : static_cast<long long>(fmt->start_time),
                    fmt->duration == AV_NOPTS_VALUE ? -1LL : static_cast<long long>(fmt->duration),
                    fmt->nb_programs, t_opens);
    }
    if (after_open) after_open();

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    int frames = 0;
    const auto receive = [&] {
        while (avcodec_receive_frame(dec, frame) == 0) {
            ++frames;
            std::lock_guard g(g_out);
            std::printf("%lld\tt%d\tframe\tpts=%.6f\tbest_effort=%.6f\tpkt_dts=%.6f\tduration=%lld\tkey=%d\t"
                        "corrupt=%d\tpict_type=%c\tdecode_error_flags=%d\n",
                        now_us(), t_thread, secs(frame->pts, vs->time_base),
                        secs(frame->best_effort_timestamp, vs->time_base), secs(frame->pkt_dts, vs->time_base),
                        static_cast<long long>(frame->duration), (frame->flags & AV_FRAME_FLAG_KEY) ? 1 : 0,
                        (frame->flags & AV_FRAME_FLAG_CORRUPT) ? 1 : 0, av_get_picture_type_char(frame->pict_type),
                        frame->decode_error_flags);
            av_frame_unref(frame);
        }
    };
    while ((r = av_read_frame(fmt, pkt)) >= 0) {
        if (pkt->stream_index == vid) {
            {
                std::lock_guard g(g_out);
                std::printf("%lld\tt%d\tpacket\tpts=%.6f\tdts=%.6f\tduration=%lld\tkey=%d\tcorrupt=%d\tsize=%d\t"
                            "pos=%lld\topens_so_far=%d\tlast_open=%s\n",
                            now_us(), t_thread, secs(pkt->pts, vs->time_base), secs(pkt->dts, vs->time_base),
                            static_cast<long long>(pkt->duration), (pkt->flags & AV_PKT_FLAG_KEY) ? 1 : 0,
                            (pkt->flags & AV_PKT_FLAG_CORRUPT) ? 1 : 0, pkt->size,
                            static_cast<long long>(pkt->pos), t_opens, t_last_open.c_str());
            }
            const int s = avcodec_send_packet(dec, pkt);
            if (s < 0) {
                std::lock_guard g(g_out);
                std::printf("%lld\tt%d\tsend_packet_error\t%d\n", now_us(), t_thread, s);
            }
            receive();
        }
        av_packet_unref(pkt);
    }
    if (r == AVERROR_EOF) segments.ended();
    avcodec_send_packet(dec, nullptr);
    receive();
    {
        std::lock_guard g(g_out);
        std::printf("%lld\tt%d\tend\tread=%d\tframes=%d\topens=%d\tsegment_watch_lost=%s\n", now_us(), t_thread, r,
                    frames, t_opens, segments.lost().empty() ? "(none)" : segments.lost().c_str());
    }
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return 0;
}

void print_counts()
{
    std::lock_guard g(g_count_mtx);
    std::printf("== av_log calls by callback, thread, level, AVClass and first word of the format ==\n");
    for (const auto& [k, n] : g_counts) std::printf("%6d  %s\n", n, k.c_str());
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: logprobe file <playlist> | http <root> <path> | concurrent <a> <b>\n");
        return 2;
    }
    const std::string mode = argv[1];
    // As tools/mirage_vmaf.cpp main(): warnings and errors only, for the
    // default callback. The callback installed here sees every call.
    av_log_set_level(AV_LOG_WARNING);
    av_log_set_callback(log_cb_a);
    avformat_network_init();
    std::printf("== events: t_us, thread, kind, fields ==\n");
    int rc = 0;
    if (mode == "file") {
        rc = read_input(argv[2]);
    } else if (mode == "http" && argc >= 4) {
        HttpFileServer server(argv[2]);
        if (!server.ok()) {
            std::printf("no server\n");
            return 1;
        }
        rc = read_input(server.url(argv[3]));
    } else if (mode == "concurrent" && argc >= 4) {
        // Thread 1 opens its input, then waits to read it until thread 2 has
        // opened its own and replaced the callback: what thread 1 logs while
        // reading shows which callback the process has then.
        std::atomic<bool> switched{ false };
        std::thread a([&] {
            t_thread = 1;
            read_input(argv[2], [&] {
                while (!switched.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            });
        });
        std::thread b([&] {
            t_thread = 2;
            read_input(argv[3], [&] {
                av_log_set_callback(log_cb_b);   // replaces it for the whole process
                {
                    std::lock_guard g(g_out);
                    std::printf("%lld\tt%d\tav_log_set_callback(cbB)\n", now_us(), t_thread);
                }
                switched.store(true);
            });
        });
        a.join();
        b.join();
    } else {
        std::fprintf(stderr, "bad arguments\n");
        return 2;
    }
    av_log_set_callback(av_log_default_callback);
    print_counts();
    return rc;
}
