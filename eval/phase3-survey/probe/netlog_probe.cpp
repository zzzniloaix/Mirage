// netlog_probe: what the player's NetworkLogger (src/NetworkLogger.cpp,
// compiled from the repository) records of the reads of mirage_vmaf's
// analyzer, and what it records with its own hook instead.
//
//   netlog_probe <root> <path under root>
//
// The NetworkLogger is installed (the process-global av_log callback). The
// HLS stream at <root>/<path> is then read three times, each as the analyzer
// reads an input (avformat_open_input with vmafsegments::set_hls_options and
// vmafnet::set_io_limits, avformat_find_stream_info, every packet), and the
// rows each read added are printed:
//   1. over HTTP (tests/HttpFileServer.h on 127.0.0.1), the context watched
//      by SegmentWatch, as DecodeCtx::open() watches it: the logger sees it
//      through av_log alone;
//   2. the same, from the local path;
//   3. over HTTP, the context hooked with NetworkLogger::hook_format_context()
//      instead of SegmentWatch, as the player's Demuxer hooks its own.
// Times are microseconds of NetworkLogger::now_us(), from the read's start.

#include "HttpFileServer.h"
#include "NetworkLogger.h"
#include "VMAFNet.h"
#include "VMAFSegments.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <cstdio>
#include <stop_token>
#include <string>
#include <vector>

namespace {

void read_input(const std::string& url, bool segment_watch)
{
    std::stop_source stop;
    const std::stop_token st = stop.get_token();
    vmafsegments::SegmentWatch segments;
    AVFormatContext* fmt = avformat_alloc_context();
    fmt->interrupt_callback.callback = vmafnet::stop_requested;
    fmt->interrupt_callback.opaque   = const_cast<std::stop_token*>(&st);
    if (segment_watch) segments.watch(fmt);
    else NetworkLogger::hook_format_context(fmt, NetInitiator::Playback);
    AVDictionary* opts = nullptr;
    vmafnet::set_io_limits(&opts, url);
    vmafsegments::set_hls_options(&opts);
    if (avformat_open_input(&fmt, url.c_str(), nullptr, &opts) < 0) {
        std::printf("cannot open %s\n", url.c_str());
        av_dict_free(&opts);
        return;
    }
    av_dict_free(&opts);
    avformat_find_stream_info(fmt, nullptr);
    AVPacket* pkt = av_packet_alloc();
    int packets = 0;
    while (av_read_frame(fmt, pkt) >= 0) {
        ++packets;
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&fmt);
    std::printf("read %d packets; SegmentWatch %s\n", packets,
                segment_watch ? (segments.lost().empty() ? "lost nothing" : segments.lost().c_str())
                              : "not used");
}

void print_rows(NetworkLogger& log, std::uint64_t& seen_id, std::int64_t t0)
{
    log.poll();
    std::vector<NetRequest> rows;
    std::uint64_t oldest = 0;
    log.copy_changed_since(0, rows, oldest);
    std::printf("id\turl\tprotocol\tinitiator\tt_start\tt_sent\tt_first_byte\tt_end\tt_last_byte\tbytes\t"
                "content_length\tstatus\tfailed\tpartial\tconn_id\treused_connection\trange\n");
    const auto rel = [&](std::int64_t t) { return t < 0 ? -1 : t - t0; };
    int n = 0;
    for (const NetRequest& r : rows) {
        if (r.id <= seen_id) continue;
        ++n;
        std::printf("%llu\t%s\t%s\t%s\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%d\t%d\t%d\t%u\t%d\t%lld-%lld\n",
                    static_cast<unsigned long long>(r.id), r.url.c_str(), r.protocol.c_str(),
                    net_initiator_name(r.initiator), static_cast<long long>(rel(r.t_start)),
                    static_cast<long long>(rel(r.t_sent)), static_cast<long long>(rel(r.t_first_byte)),
                    static_cast<long long>(rel(r.t_end)), static_cast<long long>(rel(r.t_last_byte)),
                    static_cast<long long>(r.bytes), static_cast<long long>(r.content_length), r.status,
                    r.failed ? 1 : 0, r.partial ? 1 : 0, r.conn_id, r.reused_connection ? 1 : 0,
                    static_cast<long long>(r.range_start), static_cast<long long>(r.range_end));
    }
    for (const NetRequest& r : rows) seen_id = std::max(seen_id, r.id);
    std::printf("%d rows\n", n);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: netlog_probe <root> <path under root>\n");
        return 2;
    }
    av_log_set_level(AV_LOG_WARNING);   // as tools/mirage_vmaf.cpp main()
    NetworkLogger log;
    log.install();
    avformat_network_init();
    HttpFileServer server(argv[1]);
    if (!server.ok()) return 1;
    std::uint64_t seen = 0;

    std::printf("== 1. HTTP, SegmentWatch on the context (the analyzer's open) ==\n");
    std::int64_t t0 = NetworkLogger::now_us();
    read_input(server.url(argv[2]), true);
    print_rows(log, seen, t0);

    std::printf("== 2. local path, SegmentWatch on the context ==\n");
    t0 = NetworkLogger::now_us();
    read_input(std::string(argv[1]) + "/" + argv[2], true);
    print_rows(log, seen, t0);

    std::printf("== 3. HTTP, NetworkLogger::hook_format_context instead of SegmentWatch ==\n");
    t0 = NetworkLogger::now_us();
    read_input(server.url(argv[2]), false);
    print_rows(log, seen, t0);

    log.uninstall();
    return 0;
}
