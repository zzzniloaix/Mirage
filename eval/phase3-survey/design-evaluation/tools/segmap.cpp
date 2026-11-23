// segmap: a demux pass over one HLS media playlist, through FFmpeg's library
// API only (no decoding, no ffmpeg binary), as the Phase 3 design proposes for
// the timeline's frame -> segment map (design-evaluation.md). For the design's
// runs; not repository code.
//
//   segmap <playlist path or URL> <out.json>
//
// It opens the playlist as mirage_vmaf's analyzer opens an HLS input: with
// http_persistent 0 and http_multiple 0, so that the hls demuxer opens each
// file through the context's io_open, one at a time (src/VMAFSegments.h). Its
// own io_open / io_close2 callbacks record every file the demuxer opens, in
// order, with the bytes its AVIOContext read by the close. It then reads every
// packet with av_read_frame (avformat_find_stream_info first, as the analyzer
// does) and records the best video stream's packets: pts, dts, pos, size and
// the key and corrupt flags.
//
// Each packet is placed in a segment two ways:
//   by_pos   its byte position (AVPacket::pos) against the cumulative bytes of
//            the segment files in open order, the playlist itself left out;
//   (the JSON keeps pts, so a reader can check other rules).
// Writes <out.json>: the stream's time base, the files in open order with
// their bytes, and the packets; prints a per-segment summary on stdout.

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct File {
    std::string url;
    int64_t     bytes = -1;   // bytes_read at close; -1 = not closed
    int         open_result = 0;
};

struct Recorder {
    std::vector<File> files;
    std::vector<std::pair<const AVIOContext*, size_t>> open;   // context -> files index
    int (*io_open)(AVFormatContext*, AVIOContext**, const char*, int, AVDictionary**) = nullptr;
    int (*io_close2)(AVFormatContext*, AVIOContext*) = nullptr;
};

int on_open(AVFormatContext* s, AVIOContext** pb, const char* url, int flags, AVDictionary** options)
{
    auto* r = static_cast<Recorder*>(s->opaque);
    std::string name = url;
    if (const AVDictionaryEntry* off = options ? av_dict_get(*options, "offset", nullptr, 0) : nullptr)
        name += std::string(" (from byte ") + off->value + ")";
    const int ret = r->io_open(s, pb, url, flags, options);
    r->files.push_back({ name, -1, ret });
    if (ret >= 0 && *pb) r->open.push_back({ *pb, r->files.size() - 1 });
    return ret;
}

int on_close2(AVFormatContext* s, AVIOContext* pb)
{
    auto* r = static_cast<Recorder*>(s->opaque);
    for (auto it = r->open.begin(); it != r->open.end(); ++it) {
        if (it->first == pb) {
            r->files[it->second].bytes = pb->bytes_read;
            r->open.erase(it);
            break;
        }
    }
    return r->io_close2(s, pb);
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

struct Packet {
    int64_t pts, dts, pos;
    int     size;
    bool    key, corrupt;
};

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: segmap <playlist> <out.json>\n");
        return 2;
    }
    av_log_set_level(AV_LOG_WARNING);
    avformat_network_init();

    Recorder rec;
    AVFormatContext* fmt = avformat_alloc_context();
    rec.io_open   = fmt->io_open;
    rec.io_close2 = fmt->io_close2;
    fmt->opaque    = &rec;
    fmt->io_open   = on_open;
    fmt->io_close2 = on_close2;

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "http_persistent", "0", 0);
    av_dict_set(&opts, "http_multiple", "0", 0);
    if (int e = avformat_open_input(&fmt, argv[1], nullptr, &opts); e < 0) {
        char b[256];
        av_strerror(e, b, sizeof b);
        std::fprintf(stderr, "segmap: cannot open %s: %s\n", argv[1], b);
        av_dict_free(&opts);
        return 1;
    }
    av_dict_free(&opts);
    avformat_find_stream_info(fmt, nullptr);
    const int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vs < 0) {
        std::fprintf(stderr, "segmap: no video stream\n");
        return 1;
    }
    const AVRational tb = fmt->streams[vs]->time_base;

    std::vector<Packet> packets;
    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vs)
            packets.push_back({ pkt->pts, pkt->dts, pkt->pos, pkt->size,
                                (pkt->flags & AV_PKT_FLAG_KEY) != 0,
                                (pkt->flags & AV_PKT_FLAG_CORRUPT) != 0 });
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&fmt);

    // Segment files: every file but the first (the playlist), in open order.
    struct Seg { std::string url; int64_t begin, end; int packets = 0, keys = 0, corrupt = 0;
                 int64_t pts_min = INT64_MAX, pts_max = INT64_MIN; };
    std::vector<Seg> segs;
    int64_t at = 0;
    for (size_t i = 1; i < rec.files.size(); ++i) {
        const File& f = rec.files[i];
        if (f.open_result < 0) continue;
        segs.push_back({ f.url, at, at + (f.bytes < 0 ? 0 : f.bytes) });
        at += f.bytes < 0 ? 0 : f.bytes;
    }
    std::vector<int> seg_of(packets.size(), -1);
    for (size_t k = 0; k < packets.size(); ++k) {
        for (size_t s = 0; s < segs.size(); ++s)
            if (packets[k].pos >= segs[s].begin && packets[k].pos < segs[s].end) { seg_of[k] = int(s); break; }
        if (seg_of[k] >= 0) {
            Seg& s = segs[size_t(seg_of[k])];
            ++s.packets;
            s.keys    += packets[k].key ? 1 : 0;
            s.corrupt += packets[k].corrupt ? 1 : 0;
            if (packets[k].pts != AV_NOPTS_VALUE) {
                s.pts_min = std::min(s.pts_min, packets[k].pts);
                s.pts_max = std::max(s.pts_max, packets[k].pts);
            }
        }
    }

    FILE* out = std::fopen(argv[2], "w");
    if (!out) {
        std::fprintf(stderr, "segmap: cannot write %s\n", argv[2]);
        return 1;
    }
    std::fprintf(out, "{\"playlist\": %s, \"time_base\": [%d, %d],\n \"files\": [", json_string(argv[1]).c_str(),
                 tb.num, tb.den);
    for (size_t i = 0; i < rec.files.size(); ++i)
        std::fprintf(out, "%s\n  {\"url\": %s, \"open_result\": %d, \"bytes\": %" PRId64 "}", i ? "," : "",
                     json_string(rec.files[i].url).c_str(), rec.files[i].open_result, rec.files[i].bytes);
    std::fprintf(out, "],\n \"packets\": [");
    for (size_t k = 0; k < packets.size(); ++k) {
        const Packet& p = packets[k];
        std::fprintf(out, "%s\n  {\"pts\": %s, \"dts\": %s, \"pos\": %" PRId64 ", \"size\": %d, \"key\": %s, "
                     "\"corrupt\": %s, \"segment_by_pos\": %d}", k ? "," : "",
                     p.pts == AV_NOPTS_VALUE ? "null" : std::to_string(p.pts).c_str(),
                     p.dts == AV_NOPTS_VALUE ? "null" : std::to_string(p.dts).c_str(), p.pos, p.size,
                     p.key ? "true" : "false", p.corrupt ? "true" : "false", seg_of[k]);
    }
    std::fprintf(out, "]}\n");
    std::fclose(out);

    std::printf("playlist %s: video stream %d, time base %d/%d, %zu files opened (the first is the "
                "playlist), %zu video packets\n", argv[1], vs, tb.num, tb.den, rec.files.size(), packets.size());
    int unplaced = 0;
    for (int s : seg_of) unplaced += s < 0 ? 1 : 0;
    std::printf("packets placed in no segment by position: %d\n", unplaced);
    std::printf("segment\turl\tbytes\tpackets\tkey\tcorrupt\tpts_min_s\tpts_max_s\n");
    for (size_t s = 0; s < segs.size(); ++s)
        std::printf("%zu\t%s\t%" PRId64 "\t%d\t%d\t%d\t%.6f\t%.6f\n", s, segs[s].url.c_str(),
                    segs[s].end - segs[s].begin, segs[s].packets, segs[s].keys, segs[s].corrupt,
                    segs[s].pts_min * av_q2d(tb), segs[s].pts_max * av_q2d(tb));
    return 0;
}
