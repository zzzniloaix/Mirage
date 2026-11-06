// The timeline build's demux pass (src/TimelineMedia.h; DECISIONS.md D-074,
// D-081 (a), (c), (h)) on HLS streams the test writes with FFmpeg's
// libraries, read from a local directory and from a local HTTP server
// (tests/HttpFileServer.h): every frame placed in its segment by byte
// position and checked against what the writer wrote, with B-frames,
// timestamps that restart at a discontinuity, byte ranges, a missing frame,
// an unmarked splice's corrupt packets, FFmpeg's hls muxer's short last
// EXTINF and an input that isn't a playlist; and the placement rules alone
// (map_frames()) on packets the test makes up.

#include <catch2/catch_test_macros.hpp>

#include "FileHash.h"
#include "HlsAlign.h"
#include "HttpFileServer.h"
#include "TestTemp.h"
#include "TimelineMedia.h"
#include "VMAFSegments.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/opt.h>
}

namespace fs = std::filesystem;
using timeline::Demuxed;
using timeline::MediaFile;
using timeline::MediaPacket;
using timeline::ScoredFrames;
using timeline::VariantMedia;

namespace {

constexpr int          kFps   = 25;
constexpr AVRational   kTs    = { 1, 90000 };   // MPEG-TS's time base
constexpr std::int64_t kFrame = 90000 / kFps;   // one frame, in ticks of kTs

// ── Streams the test writes ──────────────────────────────────────────────────

// One segment of a stream.
struct Seg {
    int          frames = 10;
    std::int64_t first  = 250;            // its first frame's PTS, in frames of 1/25 s: 10 s
    bool         discontinuity = false;   // an EXT-X-DISCONTINUITY before it, and a new muxer
    int          skip = -1;               // a frame of it left out, by its place in it; -1: none
};

struct Options {
    int  width = 96, height = 54;
    int  gop = 0;                         // frames per GOP; 0: the segment's
    int  b_frames = 0;
    bool mark_splices = true;             // the discontinuity indicator on a new muxer's packets
    bool byte_ranges = false;             // the segments as EXT-X-BYTERANGEs of one file
    std::int64_t media_sequence = 0;
};

// What the writer wrote: the ground truth.
struct Stream {
    fs::path dir, playlist;
    std::vector<std::string>  uris;           // per segment, as the playlist names it
    std::vector<std::int64_t> offset, bytes;  // per segment: its first byte in its file, its size
    std::vector<std::int64_t> first_index, frames;   // per segment
    std::vector<std::int64_t> pts;            // per frame, in presentation order: ticks of kTs
    std::vector<std::int64_t> keyframes;      // the frames of key packets
};

// Frame `k` of the test's picture: a gray diagonal ramp that moves every
// frame, as tests/test_vmaf_segments.cpp draws it.
void draw(AVFrame* f, int k)
{
    REQUIRE(av_frame_make_writable(f) >= 0);
    for (int row = 0; row < f->height; ++row)
        for (int col = 0; col < f->width; ++col)
            f->data[0][row * f->linesize[0] + col] =
                static_cast<uint8_t>((col * 256 / f->width + row * 128 / f->height + 3 * k) & 0xff);
    for (int p = 1; p < 3; ++p)
        for (int row = 0; row < (f->height + 1) / 2; ++row)
            std::fill_n(f->data[p] + row * f->linesize[p], (f->width + 1) / 2, uint8_t{ 128 });
}

// An MPEG-2 encoder for one segment: its own, so that the segment starts on
// a key frame, with a closed GOP, which FFmpeg's MPEG-2 encoder takes only
// with scene change detection off (eval/phase3-survey/testgen.md §2.4).
AVCodecContext* open_encoder(const Options& o, int gop)
{
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO);
    REQUIRE(codec);
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    REQUIRE(enc);
    enc->width          = o.width;
    enc->height         = o.height;
    enc->pix_fmt        = AV_PIX_FMT_YUV420P;
    enc->time_base      = { 1, kFps };
    enc->framerate      = { kFps, 1 };
    enc->gop_size       = gop;
    enc->max_b_frames   = o.b_frames;
    enc->flags         |= AV_CODEC_FLAG_QSCALE | AV_CODEC_FLAG_CLOSED_GOP | AV_CODEC_FLAG_BITEXACT;
    enc->global_quality = FF_QP2LAMBDA * 2;
    REQUIRE(av_opt_set_int(enc, "sc_threshold", 1000000000, AV_OPT_SEARCH_CHILDREN) >= 0);
    REQUIRE(avcodec_open2(enc, codec, nullptr) >= 0);
    return enc;
}

// The MPEG-TS muxer of one discontinuity sequence, as FFmpeg's hls muxer
// writes one: an output whose file changes at each segment, PAT and PMT sent
// again at the start of each, so that continuity counters and timestamps go
// on from one segment to the next; a new sequence gets a new muxer, whose
// counters start again, with the discontinuity indicator on its first
// packets unless the test leaves it out (eval/phase3-survey/testgen.md §2.4).
class SequenceMuxer {
public:
    SequenceMuxer(const AVCodecContext* enc, bool mark) : mark_(mark)
    {
        REQUIRE(avformat_alloc_output_context2(&oc_, nullptr, "mpegts", nullptr) >= 0);
        oc_->flags |= AVFMT_FLAG_BITEXACT;
        st_ = avformat_new_stream(oc_, nullptr);
        REQUIRE(st_);
        REQUIRE(avcodec_parameters_from_context(st_->codecpar, enc) >= 0);
        st_->time_base = kTs;
    }
    ~SequenceMuxer()
    {
        if (oc_->pb) avio_closep(&oc_->pb);
        avformat_free_context(oc_);
    }
    SequenceMuxer(const SequenceMuxer&)            = delete;
    SequenceMuxer& operator=(const SequenceMuxer&) = delete;

    // Starts the next segment, in `file`, or, with `same_file`, after the
    // last one in the file open. Returns the segment's first byte in its file.
    std::int64_t begin(const fs::path& file, bool same_file)
    {
        if (!started_) {
            REQUIRE(avio_open(&oc_->pb, file.c_str(), AVIO_FLAG_WRITE) >= 0);
            AVDictionary* opts = nullptr;
            if (mark_) av_dict_set(&opts, "mpegts_flags", "initial_discontinuity", 0);
            const int r = avformat_write_header(oc_, &opts);
            av_dict_free(&opts);
            REQUIRE(r >= 0);
            started_ = true;
            return 0;
        }
        REQUIRE(av_write_frame(oc_, nullptr) >= 0);   // what the muxer holds, into the file
        avio_flush(oc_->pb);
        std::int64_t at = 0;
        if (same_file) {
            at = avio_tell(oc_->pb);
        } else {
            avio_closep(&oc_->pb);
            REQUIRE(avio_open(&oc_->pb, file.c_str(), AVIO_FLAG_WRITE) >= 0);
        }
        REQUIRE(av_opt_set(oc_->priv_data, "mpegts_flags", "resend_headers", 0) >= 0);
        return at;
    }

    void write(const AVPacket* in)
    {
        AVPacket* pkt = av_packet_clone(in);
        REQUIRE(pkt);
        pkt->stream_index = 0;
        av_packet_rescale_ts(pkt, { 1, kFps }, st_->time_base);
        const int r = av_write_frame(oc_, pkt);
        av_packet_free(&pkt);
        REQUIRE(r >= 0);
    }

    void end()
    {
        REQUIRE(av_write_trailer(oc_) >= 0);
        avio_closep(&oc_->pb);
    }

private:
    AVFormatContext* oc_ = nullptr;
    AVStream*        st_ = nullptr;
    bool             mark_ = true;
    bool             started_ = false;
};

// Writes the stream of `plan` into testtemp::dir()/`name`: its MPEG-TS
// segments (seg<k>.ts, or one file all.ts with byte ranges) and its VOD
// playlist index.m3u8, which the test writes itself, EXTINF from each
// segment's frames. Returns what it wrote.
Stream write_stream(const std::string& name, const std::vector<Seg>& plan, const Options& o = {})
{
    Stream out;
    out.dir      = testtemp::path(name);
    out.playlist = out.dir / "index.m3u8";
    fs::create_directories(out.dir);

    std::optional<SequenceMuxer> mux;
    int picture = 0;   // the source's frame counter, which moves the ramp
    for (std::size_t k = 0; k < plan.size(); ++k) {
        const Seg& s = plan[k];
        REQUIRE((!o.byte_ranges || !s.discontinuity));   // one muxer writes one file
        AVCodecContext* enc = open_encoder(o, o.gop > 0 ? o.gop : s.frames);
        std::vector<AVPacket*> packets;
        const auto drain = [&] {
            for (;;) {
                AVPacket* pkt = av_packet_alloc();
                REQUIRE(pkt);
                if (avcodec_receive_packet(enc, pkt) < 0) {
                    av_packet_free(&pkt);
                    return;
                }
                packets.push_back(pkt);
            }
        };
        AVFrame* f = av_frame_alloc();
        REQUIRE(f);
        f->format = AV_PIX_FMT_YUV420P;
        f->width  = o.width;
        f->height = o.height;
        REQUIRE(av_frame_get_buffer(f, 0) >= 0);
        for (int i = 0; i < s.frames; ++i, ++picture) {
            if (i == s.skip) continue;
            draw(f, picture);
            f->pts = s.first + i;
            REQUIRE(avcodec_send_frame(enc, f) >= 0);
            drain();
        }
        REQUIRE(avcodec_send_frame(enc, nullptr) >= 0);
        drain();
        av_frame_free(&f);

        // The segment's frames in presentation order, and its key packets.
        std::vector<std::int64_t> pts, keys;
        for (const AVPacket* p : packets) {
            pts.push_back(p->pts * kFrame);
            if (p->flags & AV_PKT_FLAG_KEY) keys.push_back(p->pts * kFrame);
        }
        std::ranges::sort(pts);
        out.first_index.push_back(static_cast<std::int64_t>(out.pts.size()));
        out.frames.push_back(static_cast<std::int64_t>(pts.size()));
        for (const std::int64_t key : keys)
            out.keyframes.push_back(out.first_index.back() +
                                    (std::ranges::find(pts, key) - pts.begin()));
        out.pts.insert(out.pts.end(), pts.begin(), pts.end());

        if (k == 0 || s.discontinuity) {
            if (mux) mux->end();
            mux.emplace(enc, o.mark_splices && k > 0);
        }
        const std::string file = o.byte_ranges ? "all.ts" : std::format("seg{}.ts", k);
        out.uris.push_back(file);
        out.offset.push_back(mux->begin(out.dir / file, o.byte_ranges && k > 0));
        for (const AVPacket* p : packets) mux->write(p);
        for (AVPacket*& p : packets) av_packet_free(&p);
        avcodec_free_context(&enc);
    }
    mux->end();
    for (std::size_t k = 0; k < plan.size(); ++k) {
        const std::int64_t size = static_cast<std::int64_t>(fs::file_size(out.dir / out.uris[k]));
        out.bytes.push_back(o.byte_ranges && k + 1 < plan.size() ? out.offset[k + 1] - out.offset[k]
                                                                 : size - out.offset[k]);
    }
    std::ranges::sort(out.keyframes);

    std::string text = std::format("#EXTM3U\n#EXT-X-VERSION:{}\n#EXT-X-TARGETDURATION:1\n"
                                   "#EXT-X-MEDIA-SEQUENCE:{}\n#EXT-X-PLAYLIST-TYPE:VOD\n",
                                   o.byte_ranges ? 4 : 3, o.media_sequence);
    for (std::size_t k = 0; k < plan.size(); ++k) {
        if (plan[k].discontinuity) text += "#EXT-X-DISCONTINUITY\n";
        text += std::format("#EXTINF:{:.6f},\n", static_cast<double>(plan[k].frames) / kFps);
        if (o.byte_ranges) text += std::format("#EXT-X-BYTERANGE:{}@{}\n", out.bytes[k], out.offset[k]);
        text += out.uris[k] + "\n";
    }
    text += "#EXT-X-ENDLIST\n";
    std::ofstream(out.playlist, std::ios::binary) << text;
    return out;
}

std::string read_file(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

// Writes `frames` frames of the test's picture, coded by `enc`, which it
// frees, with FFmpeg's hls muxer and its `options` into the playlist
// `dir`/index.m3u8, as tests/test_vmaf_segments.cpp:46-117 makes its
// streams. Returns the playlist's path.
fs::path write_with_hls_muxer(const fs::path& dir, AVCodecContext* enc, int frames,
                              const std::vector<std::pair<const char*, std::string>>& options)
{
    fs::create_directories(dir);
    const fs::path playlist = dir / "index.m3u8";
    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "hls", playlist.c_str()) >= 0);
    AVStream* st = avformat_new_stream(oc, nullptr);
    REQUIRE(st);
    REQUIRE(avcodec_parameters_from_context(st->codecpar, enc) >= 0);
    st->time_base = enc->time_base;
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "hls_time", "0.4", 0);
    av_dict_set(&opts, "hls_list_size", "0", 0);
    av_dict_set(&opts, "hls_playlist_type", "vod", 0);
    for (const auto& [key, value] : options) av_dict_set(&opts, key, value.c_str(), 0);
    const int header = avformat_write_header(oc, &opts);
    av_dict_free(&opts);
    REQUIRE(header >= 0);
    const auto drain = [&] {
        AVPacket* pkt = av_packet_alloc();
        REQUIRE(pkt);
        while (avcodec_receive_packet(enc, pkt) == 0) {
            pkt->stream_index = 0;
            av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
            REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
        }
        av_packet_free(&pkt);
    };
    AVFrame* f = av_frame_alloc();
    REQUIRE(f);
    f->format = AV_PIX_FMT_YUV420P;
    f->width  = enc->width;
    f->height = enc->height;
    REQUIRE(av_frame_get_buffer(f, 0) >= 0);
    for (int k = 0; k < frames; ++k) {
        draw(f, k);
        f->pts = k;
        REQUIRE(avcodec_send_frame(enc, f) >= 0);
        drain();
    }
    REQUIRE(avcodec_send_frame(enc, nullptr) >= 0);
    drain();
    av_frame_free(&f);
    REQUIRE(av_write_trailer(oc) >= 0);
    avformat_free_context(oc);
    avcodec_free_context(&enc);
    return playlist;
}

// The playlist of `s`, parsed as the timeline build parses one: against
// where it was read from.
hls::MediaTimeline parsed(const Stream& s)
{
    hls::MediaTimeline tl = hls::parse_media_timeline(read_file(s.playlist), s.playlist.string());
    REQUIRE(tl.ok);
    return tl;
}

// A pair's scored frames of an input, as a ladder report gives them.
struct Report {
    std::vector<std::int64_t> index, pts;
};

// Every frame of `s`, as a pair that scored each would report it.
Report every_frame(const Stream& s)
{
    Report r;
    for (std::size_t i = 0; i < s.pts.size(); ++i) {
        r.index.push_back(static_cast<std::int64_t>(i));
        r.pts.push_back(s.pts[i]);
    }
    return r;
}

ScoredFrames scored(const Report& r, std::size_t pair = 0, AVRational time_base = kTs)
{
    return { .pair = pair, .time_base = time_base, .index = r.index, .pts = r.pts };
}

// The demux pass of `s`, with `checks`; nothing refused and no error.
VariantMedia pass(const Stream& s, const std::vector<ScoredFrames>& checks)
{
    const hls::MediaTimeline tl = parsed(s);
    std::string err;
    VariantMedia m = timeline::read_media(s.playlist.string(), &tl, checks, {}, err);
    INFO(err);
    REQUIRE(err.empty());
    INFO(m.refused);
    REQUIRE(m.refused.empty());
    return m;
}

// `m`'s segments as the writer laid them out.
void check_layout(const VariantMedia& m, const Stream& s, std::int64_t media_sequence = 0)
{
    REQUIRE(m.segments.size() == s.uris.size());
    for (std::size_t k = 0; k < m.segments.size(); ++k) {
        INFO("segment " << k);
        CHECK(m.segments[k].seq == media_sequence + static_cast<std::int64_t>(k));
        CHECK(m.segments[k].first_index == s.first_index[k]);
        CHECK(m.segments[k].frames == s.frames[k]);
        CHECK(m.segments[k].bytes == s.bytes[k]);
    }
    CHECK(m.pts == s.pts);
    CHECK(m.keyframes == s.keyframes);
}

// `n` segments of 10 frames, the main content's timestamps from 10 s.
std::vector<Seg> plain(int n)
{
    std::vector<Seg> plan;
    for (int k = 0; k < n; ++k) plan.push_back({ .frames = 10, .first = 250 + 10 * k });
    return plan;
}

// The ad shape of D-069's second ladder in small: the main content in
// segments 0 and 1, an ad in 2 and 3 whose timestamps restart at the main
// content's origin, so that each of its PTS is also one of segments 0 and
// 1's, and the main content again in 4 and 5, its timestamps going on as if
// the ad had replaced content of the same length
// (eval/phase3-survey/testgen.md §2.3), each period after an
// EXT-X-DISCONTINUITY.
std::vector<Seg> ad_shape()
{
    return {
        { .frames = 10, .first = 250 }, { .frames = 10, .first = 260 },
        { .frames = 10, .first = 250, .discontinuity = true }, { .frames = 10, .first = 260 },
        { .frames = 10, .first = 290, .discontinuity = true }, { .frames = 10, .first = 300 },
    };
}

} // namespace

// ── Streams the test writes ──────────────────────────────────────────────────

TEST_CASE("TimelineMedia: the demux pass places frames by byte position, as the writer laid them out",
          "[timeline][timeline_media]")
{
    const Stream s = write_stream("mirage_test_media_plain", plain(3),
                                  { .gop = 5, .media_sequence = 7 });
    const Report r = every_frame(s);

    SECTION("what the demuxer read: each segment file once, in order, and every packet's position") {
        std::string err;
        const Demuxed d = timeline::demux(s.playlist.string(), {}, err);
        REQUIRE(err.empty());
        CHECK(d.hls);
        CHECK(av_cmp_q(d.time_base, kTs) == 0);
        REQUIRE(d.files.size() == 3);
        std::int64_t at = 0;
        for (std::size_t k = 0; k < 3; ++k) {
            INFO("segment " << k);
            CHECK(d.files[k].url == (s.dir / s.uris[k]).string());
            CHECK_FALSE(d.files[k].offset);
            CHECK(d.files[k].bytes == s.bytes[k]);
            CHECK(d.files[k].bytes_read == s.bytes[k]);   // P-F's count agrees on a whole file
            // Its 10 packets lie in its bytes of the stream the demuxer reads.
            const auto in_it = std::ranges::count_if(d.packets, [&](const MediaPacket& p) {
                return p.pos >= at && p.pos < at + s.bytes[k];
            });
            CHECK(in_it == 10);
            at += s.bytes[k];
        }
        CHECK(d.packets.size() == 30);
    }

    SECTION("segments, PTS and keyframes; the media sequence numbers from the playlist") {
        const VariantMedia m = pass(s, { scored(r) });
        check_layout(m, s, 7);
        CHECK(m.keyframes == std::vector<std::int64_t>{ 0, 5, 10, 15, 20, 25 });
        CHECK(m.corrupt.empty());
    }

    SECTION("without a check, and with a pair that scored every other frame") {
        CHECK(pass(s, {}).pts == s.pts);
        Report half;
        for (std::size_t i = 0; i < s.pts.size(); i += 2) {
            half.index.push_back(static_cast<std::int64_t>(i));
            half.pts.push_back(s.pts[i]);
        }
        // The frames no pair scored keep their PTS from the pass.
        const VariantMedia m = pass(s, { scored(half, 1), scored(r, 2) });
        CHECK(m.pts == s.pts);
    }
}

TEST_CASE("TimelineMedia: B-frames, read in decode order, are each segment's packets in PTS order",
          "[timeline][timeline_media]")
{
    const Stream s = write_stream("mirage_test_media_bframes", plain(3), { .b_frames = 2 });
    std::string err;
    const Demuxed d = timeline::demux(s.playlist.string(), {}, err);
    REQUIRE(err.empty());
    REQUIRE(d.packets.size() == 30);
    // The demuxer gives them in decode order, which isn't their PTS order.
    CHECK_FALSE(std::ranges::is_sorted(d.packets, {}, [](const MediaPacket& p) { return *p.pts; }));

    const VariantMedia m = pass(s, { scored(every_frame(s)) });
    check_layout(m, s);
    CHECK(std::ranges::is_sorted(m.pts));
    CHECK(m.keyframes == std::vector<std::int64_t>{ 0, 10, 20 });
}

TEST_CASE("TimelineMedia: timestamps that restart at a discontinuity, a PTS two segments share",
          "[timeline][timeline_media]")
{
    const Stream s = write_stream("mirage_test_media_restart", ad_shape(), { .b_frames = 2 });
    // The ad's 20 frames repeat the PTS of the first 20: a PTS alone names
    // two frames.
    CHECK(std::set<std::int64_t>(s.pts.begin(), s.pts.end()).size() == 40);
    REQUIRE(std::ranges::equal(s.pts.begin(), s.pts.begin() + 20, s.pts.begin() + 20,
                               s.pts.begin() + 40));

    const VariantMedia m = pass(s, { scored(every_frame(s)) });
    check_layout(m, s);
    CHECK(m.keyframes == std::vector<std::int64_t>{ 0, 10, 20, 30, 40, 50 });
    // The splices are marked: no packet is corrupt.
    CHECK(m.corrupt.empty());
}

TEST_CASE("TimelineMedia: EXT-X-BYTERANGE segments of one file", "[timeline][timeline_media]")
{
    // Every frame a key frame at 192x108, so that the second range starts
    // near enough to the file's first byte for the file's context to read up
    // to it instead of seeking, and the third far enough on for it to seek.
    // AVIOContext::bytes_read, P-F's count, then isn't a range's size: the
    // context reads ahead past a range's end, and counts the bytes before a
    // range it reads up to. The pass counts what the demuxer took of each
    // range, after a read or a seek.
    const Stream s = write_stream("mirage_test_media_ranges", plain(4),
                                  { .width = 192, .height = 108, .gop = 1, .byte_ranges = true });
    INFO("ranges from byte " << s.offset[1] << ", " << s.offset[2] << ", " << s.offset[3]);
    std::string err;
    const Demuxed d = timeline::demux(s.playlist.string(), {}, err);
    REQUIRE(err.empty());
    REQUIRE(d.files.size() == 4);
    for (std::size_t k = 0; k < 4; ++k) {
        INFO("segment " << k);
        CHECK(d.files[k].url == (s.dir / "all.ts").string());
        CHECK(d.files[k].offset == s.offset[k]);
        // The bytes the demuxer took of the range, not what the file's
        // context read past its end.
        CHECK(d.files[k].bytes == s.bytes[k]);
    }
    // What bytes_read holds instead: past the first range's end; for the
    // second, every byte from the file's first to the range's last, read
    // instead of seeking; for the third, none before its first byte, since
    // the context seeked there, and again more than the range.
    INFO("bytes_read " << d.files[0].bytes_read << ", " << d.files[1].bytes_read << ", "
                       << d.files[2].bytes_read << ", " << d.files[3].bytes_read);
    CHECK(d.files[0].bytes_read > s.bytes[0]);
    CHECK(d.files[1].bytes_read >= s.offset[1] + s.bytes[1]);
    CHECK(d.files[2].bytes_read < s.offset[2]);
    CHECK(d.files[2].bytes_read > s.bytes[2]);

    const VariantMedia m = pass(s, { scored(every_frame(s)) });
    check_layout(m, s);
    CHECK(m.keyframes.size() == 40);
}

TEST_CASE("TimelineMedia: a frame missing on either side refuses the variant's segment map (D-081 (c))",
          "[timeline][timeline_media]")
{
    const Stream s = write_stream("mirage_test_media_missing_ref", plain(3));
    const auto refused = [&](const Stream& read, const std::vector<ScoredFrames>& checks) {
        const hls::MediaTimeline tl = parsed(read);
        std::string err;
        const VariantMedia m = timeline::read_media(read.playlist.string(), &tl, checks, {}, err);
        INFO(err);
        REQUIRE(err.empty());
        // Nothing of the variant is placed: no segment, keyframe, corrupt
        // packet or PTS (the owner's answer to Q45 (D-086)).
        CHECK(m.segments.empty());
        CHECK(m.keyframes.empty());
        CHECK(m.corrupt.empty());
        CHECK(m.pts.empty());
        return m.refused;
    };

    SECTION("the report lacks a frame the pass has, as when a decoder never output one") {
        Report r = every_frame(s);
        r.pts.erase(r.pts.begin() + 14);
        r.index.pop_back();
        CHECK(refused(s, { scored(r) }) ==
              std::format("frame 14 of the input has PTS {} in pair 0's report and {} in the demux "
                          "pass, in ticks of 1/90000",
                          s.pts[15], s.pts[14]));
    }

    SECTION("the media lacks a frame the report has") {
        std::vector<Seg> plan = plain(3);
        plan[1].skip = 4;   // frame 14
        const Stream short_of_one = write_stream("mirage_test_media_missing_media", plan);
        REQUIRE(short_of_one.pts.size() == 29);
        CHECK(refused(short_of_one, { scored(every_frame(s)) }) ==
              std::format("frame 14 of the input has PTS {} in pair 0's report and {} in the demux "
                          "pass, in ticks of 1/90000",
                          s.pts[14], s.pts[15]));
    }

    SECTION("the report scores a frame past the last the pass read") {
        std::vector<Seg> plan = plain(3);
        plan[2].skip = 9;   // the last frame
        const Stream short_of_one = write_stream("mirage_test_media_missing_last", plan);
        CHECK(refused(short_of_one, { scored(every_frame(s)) }) ==
              "pair 0's report scores frame 29 of the input, which the demux pass doesn't have: it "
              "read 29 frames");
    }

    SECTION("one pair of the reference's disagrees, another agrees") {
        const Report good = every_frame(s);
        Report off = good;
        off.pts[3] += 1;
        CHECK(refused(s, { scored(good, 0), scored(off, 1) }) ==
              std::format("frame 3 of the input has PTS {} in pair 1's report and {} in the demux "
                          "pass, in ticks of 1/90000",
                          s.pts[3] + 1, s.pts[3]));
    }

    SECTION("the report's time base isn't the stream's") {
        CHECK(refused(s, { scored(every_frame(s), 0, { 1, 1000 }) }) ==
              "pair 0's report gives the input's time base as 1/1000, the demux pass read it in "
              "1/90000");
    }
}

TEST_CASE("TimelineMedia: the same over HTTP, from the playlist read with read_playlist()",
          "[timeline][timeline_media]")
{
    const Stream s = write_stream("mirage_test_media_http", ad_shape(), { .b_frames = 2 });
    const VariantMedia local = pass(s, { scored(every_frame(s)) });

    HttpFileServer server(s.dir);
    REQUIRE(server.ok());
    const std::string url = server.url("index.m3u8");
    const timeline::PlaylistText p = timeline::read_playlist(url);
    REQUIRE(p.error.empty());
    CHECK(p.url == url);
    CHECK(p.location == url);
    CHECK(p.text == read_file(s.playlist));
    CHECK(p.sha256 == filehash::sha256_hex(read_file(s.playlist)));

    const hls::MediaTimeline tl = hls::parse_media_timeline(p.text, p.location);
    REQUIRE(tl.ok);
    std::string err;
    const Demuxed d = timeline::demux(url, {}, err);
    REQUIRE(err.empty());
    REQUIRE(d.files.size() == s.uris.size());
    for (std::size_t k = 0; k < s.uris.size(); ++k) {
        INFO("segment " << k);
        CHECK(d.files[k].url == server.url(s.uris[k]));
        CHECK(d.files[k].bytes == s.bytes[k]);
        CHECK(d.files[k].bytes_read == s.bytes[k]);
        CHECK(server.requests(s.uris[k]) == 1);
    }
    const Report r = every_frame(s);
    const VariantMedia m = timeline::map_frames(d, &tl, { scored(r) });
    REQUIRE(m.refused.empty());
    check_layout(m, s);
    CHECK(m.keyframes == local.keyframes);
    CHECK(m.corrupt.empty());
}

TEST_CASE("TimelineMedia: a segment whose first request gets no answer is tried again, and read once",
          "[timeline][timeline_media]")
{
    // The first request for seg1.ts gets no answer: it fails at the 5 s read
    // limit (D-036), and the demuxer's next try is answered, as for the
    // analyzer in tests/test_vmaf_segments.cpp. The open that failed isn't a
    // file of the stream.
    const Stream s = write_stream("mirage_test_media_retry", plain(3));
    HttpFileServer server(s.dir, { .path = "seg1.ts", .hold_first = 1 });
    REQUIRE(server.ok());
    const std::string url = server.url("index.m3u8");
    std::string err;
    const Demuxed d = timeline::demux(url, {}, err);
    REQUIRE(err.empty());
    CHECK(server.requests("seg1.ts") == 2);
    REQUIRE(d.files.size() == 3);
    CHECK(d.files[1].url == server.url("seg1.ts"));
    CHECK(d.files[1].bytes == s.bytes[1]);
    const hls::MediaTimeline tl = hls::parse_media_timeline(read_file(s.playlist), url);
    const VariantMedia m = timeline::map_frames(d, &tl, { scored(every_frame(s)) });
    REQUIRE(m.refused.empty());
    check_layout(m, s);
}

TEST_CASE("TimelineMedia: an unmarked splice's corrupt packets, each with the boundary after it (D-081 (h))",
          "[timeline][timeline_media]")
{
    // The ad shape with continuity counters that restart at each splice
    // without the discontinuity indicator: FFmpeg's MPEG-TS demuxer flags
    // packets near each splice corrupt (eval/phase3-survey/testgen.md §3.6,
    // item 2). In this test's run with FFmpeg 8.1.1 the flags fell on the
    // next to last packet read before each splice, the frames at PTS 10.68 s
    // in segments 1 and 3: frames 17 and 37, three frames before the
    // boundaries at 20 and 40, where the events stay (D-081 (h)).
    const Stream s = write_stream("mirage_test_media_unmarked", ad_shape(),
                                  { .b_frames = 2, .mark_splices = false });
    const VariantMedia m = pass(s, { scored(every_frame(s)) });
    check_layout(m, s);
    const hls::MediaTimeline tl = parsed(s);
    INFO("corrupt frames: " << m.corrupt.size());
    REQUIRE_FALSE(m.corrupt.empty());
    // Each sits at its own frame, in the segment before a splice, and names
    // the splice's segment as the boundary after it.
    std::set<std::int64_t> boundaries;
    for (const std::int64_t f : m.corrupt) {
        INFO("frame " << f);
        const auto b = timeline::next_boundary(m, tl, f);
        REQUIRE(b);
        CHECK(b->discontinuity);
        CHECK((b->seq == 2 || b->seq == 4));
        CHECK(b->first_index == s.first_index[static_cast<std::size_t>(b->seq)]);
        CHECK(f >= b->first_index - 10);
        CHECK(f < b->first_index);
        boundaries.insert(b->seq);
    }
    CHECK(boundaries == std::set<std::int64_t>{ 2, 4 });
    CHECK(m.corrupt == std::vector<std::int64_t>{ 17, 37 });
}

TEST_CASE("TimelineMedia: FFmpeg's hls muxer's last EXTINF, shorter than its frames, maps exactly",
          "[timeline][timeline_media]")
{
    // As tests/test_vmaf_segments.cpp makes its streams: 30 frames at 25 fps,
    // every frame a key frame, hls_time 0.4.
    const fs::path dir = testtemp::path("mirage_test_media_hls_muxer");
    const fs::path playlist = write_with_hls_muxer(
        dir, open_encoder({ .gop = 1 }, 1), 30,
        { { "hls_segment_filename", (dir / "seg%d.ts").string() } });

    const hls::MediaTimeline tl = hls::parse_media_timeline(read_file(playlist), playlist.string());
    REQUIRE(tl.segments.size() == 3);
    // The muxer's last EXTINF is 0.36 s for its 10 frames, 0.4 s at 25 fps
    // (eval/phase3-survey/sources.md §2).
    INFO("EXTINF " << tl.segments[0].duration << ", " << tl.segments[1].duration << ", "
                   << tl.segments[2].duration);
    CHECK(tl.segments[0].duration == 0.4);
    CHECK(tl.segments[1].duration == 0.4);
    CHECK(tl.segments[2].duration == 0.36);

    std::string err;
    const VariantMedia m = timeline::read_media(playlist.string(), &tl, {}, {}, err);
    REQUIRE(err.empty());
    REQUIRE(m.refused.empty());
    REQUIRE(m.segments.size() == 3);
    for (std::size_t k = 0; k < 3; ++k) {
        INFO("segment " << k);
        CHECK(m.segments[k].first_index == 10 * static_cast<std::int64_t>(k));
        CHECK(m.segments[k].frames == 10);
        CHECK(m.segments[k].bytes ==
              static_cast<std::int64_t>(fs::file_size(dir / std::format("seg{}.ts", k))));
    }
    REQUIRE(m.pts.size() == 30);
    for (std::size_t i = 1; i < 30; ++i) CHECK(m.pts[i] - m.pts[i - 1] == kFrame);
    CHECK(m.keyframes.size() == 30);
}

TEST_CASE("TimelineMedia: an input that isn't a playlist is one run of frames in PTS order",
          "[timeline][timeline_media]")
{
    // A mezzanine-like file: one MPEG-TS file with B-frames, read as a file.
    const Stream s = write_stream("mirage_test_media_file", { { .frames = 30, .first = 250 } },
                                  { .gop = 10, .b_frames = 2 });
    const std::string file = (s.dir / "seg0.ts").string();
    const Report r = every_frame(s);
    std::string err;
    const VariantMedia m = timeline::read_media(file, nullptr, { scored(r) }, {}, err);
    REQUIRE(err.empty());
    REQUIRE(m.refused.empty());
    CHECK(m.segments.empty());
    CHECK(m.pts == s.pts);
    CHECK(m.keyframes == std::vector<std::int64_t>{ 0, 10, 20 });

    SECTION("an HLS input needs its playlist, and a playlist an HLS input") {
        const VariantMedia hls_without = timeline::read_media(s.playlist.string(), nullptr, {}, {}, err);
        REQUIRE(err.empty());
        CHECK(hls_without.refused ==
              "it was read as an HLS playlist, and no playlist was given for its segments");
        const hls::MediaTimeline tl = parsed(s);
        const VariantMedia file_with = timeline::read_media(file, &tl, {}, {}, err);
        REQUIRE(err.empty());
        CHECK(file_with.refused == "it wasn't read by FFmpeg's hls demuxer");
    }
}

TEST_CASE("TimelineMedia: an fMP4 playlist's initialization section isn't a segment",
          "[timeline][timeline_media]")
{
    // FFmpeg's hls muxer, fMP4 segments: the demuxer opens the EXT-X-MAP's
    // initialization section before the first segment, a file the byte
    // map isn't measured on (design §5.2), so the variant gets no map.
    const fs::path dir = testtemp::path("mirage_test_media_fmp4");
    const fs::path playlist = write_with_hls_muxer(
        dir, open_encoder({}, 10), 20,
        { { "hls_segment_type", "fmp4" }, { "hls_fmp4_init_filename", "init.mp4" },
          { "hls_segment_filename", (dir / "seg%d.m4s").string() } });

    const hls::MediaTimeline tl = hls::parse_media_timeline(read_file(playlist), playlist.string());
    REQUIRE(tl.ok);
    REQUIRE(tl.maps.size() == 1);
    std::string err;
    const VariantMedia m = timeline::read_media(playlist.string(), &tl, {}, {}, err);
    INFO(err);
    REQUIRE(err.empty());
    CHECK(m.refused == std::format("the 1st file FFmpeg's hls demuxer read after the playlist, "
                                   "{}, isn't its segment 0, {}",
                                   (dir / "init.mp4").string(), (dir / "seg0.m4s").string()));
    CHECK(m.segments.empty());
}

TEST_CASE("TimelineMedia: inputs the pass can't read", "[timeline][timeline_media]")
{
    const Stream s = write_stream("mirage_test_media_errors", plain(3));
    const hls::MediaTimeline tl = parsed(s);
    std::string err;

    SECTION("an input that isn't there") {
        const VariantMedia m =
            timeline::read_media((s.dir / "none.m3u8").string(), nullptr, {}, {}, err);
        CHECK(err.starts_with("can't open "));
        CHECK(m.refused.empty());
        CHECK(m.pts.empty());
    }

    SECTION("a segment the demuxer goes on without, tried as the analyzer tries it (D-055)") {
        const fs::path dir = testtemp::path("mirage_test_media_errors_gone");
        fs::create_directories(dir);
        for (const fs::directory_entry& e : fs::directory_iterator(s.dir))
            if (e.path().filename() != "seg1.ts") fs::copy_file(e.path(), dir / e.path().filename());
        const fs::path playlist = dir / "index.m3u8";
        const hls::MediaTimeline gone = hls::parse_media_timeline(read_file(playlist),
                                                                  playlist.string());
        timeline::read_media(playlist.string(), &gone, {}, {}, err);
        CHECK(err == std::format("the hls demuxer went on without {}, which it couldn't open in 11 "
                                 "tries: {}",
                                 (dir / "seg1.ts").string(),
                                 vmafsegments::error_text(AVERROR(ENOENT))));
    }

    SECTION("a segment whose body is cut short over HTTP") {
        HttpFileServer server(s.dir, { .path = "seg1.ts",
                                       .cut_after = static_cast<std::size_t>(s.bytes[1] / 2) });
        REQUIRE(server.ok());
        const std::string url = server.url("index.m3u8");
        const hls::MediaTimeline over = hls::parse_media_timeline(read_file(s.playlist), url);
        timeline::read_media(url, &over, {}, {}, err);
        CHECK(err.starts_with(std::format("the hls demuxer went on without the rest of {} after a "
                                          "read error: ",
                                          server.url("seg1.ts"))));
    }

    SECTION("a stop requested before the pass") {
        std::stop_source stop;
        stop.request_stop();
        timeline::read_media(s.playlist.string(), &tl, {}, stop.get_token(), err);
        CHECK(err == "cancelled");
    }

    SECTION("a live playlist (D-081 (a))") {
        std::string text = read_file(s.playlist);
        constexpr std::string_view kVod = "#EXT-X-PLAYLIST-TYPE:VOD\n";
        text.erase(text.find(kVod), kVod.size());
        text.erase(text.find("#EXT-X-ENDLIST\n"));
        const hls::MediaTimeline live = hls::parse_media_timeline(text, s.playlist.string());
        REQUIRE(live.live());
        timeline::read_media(s.playlist.string(), &live, {}, {}, err);
        CHECK(err == std::format("{} is a live playlist, without EXT-X-ENDLIST or "
                                 "EXT-X-PLAYLIST-TYPE:VOD, which the timeline doesn't read "
                                 "(D-081 (a))",
                                 s.playlist.string()));
    }
}

TEST_CASE("TimelineMedia: read_playlist() reads any #EXTM3U text and hashes it (P-D)",
          "[timeline][timeline_media]")
{
    const Stream s = write_stream("mirage_test_media_playlist", plain(1));
    const timeline::PlaylistText p = timeline::read_playlist(s.playlist.string());
    REQUIRE(p.error.empty());
    CHECK(p.url == s.playlist.string());
    CHECK(p.location == s.playlist.string());
    CHECK(p.text == read_file(s.playlist));
    CHECK(p.sha256 == filehash::sha256_hex(p.text));

    const timeline::PlaylistText none = timeline::read_playlist((s.dir / "none.m3u8").string());
    CHECK_FALSE(none.error.empty());
    CHECK(none.text.empty());
    const timeline::PlaylistText segment = timeline::read_playlist((s.dir / "seg0.ts").string());
    CHECK(segment.error == "it doesn't start with #EXTM3U, so it isn't an HLS playlist");
}

// ── The rules alone, on packets the test makes up ────────────────────────────

namespace {

// A playlist of `n` 10-frame segments a.ts, b.ts, ... from media sequence
// 3, an EXT-X-DISCONTINUITY before the third.
hls::MediaTimeline made_up_playlist(int n, bool ranges = false)
{
    std::string text = "#EXTM3U\n#EXT-X-VERSION:4\n#EXT-X-TARGETDURATION:1\n"
                       "#EXT-X-MEDIA-SEQUENCE:3\n#EXT-X-PLAYLIST-TYPE:VOD\n";
    for (int k = 0; k < n; ++k) {
        if (k == 2) text += "#EXT-X-DISCONTINUITY\n";
        text += "#EXTINF:0.400000,\n";
        if (ranges) text += std::format("#EXT-X-BYTERANGE:1000@{}\nall.ts\n", 1000 * k);
        else text += std::format("{}.ts\n", static_cast<char>('a' + k));
    }
    text += "#EXT-X-ENDLIST\n";
    hls::MediaTimeline tl = hls::parse_media_timeline(text, "/m/index.m3u8");
    REQUIRE(tl.ok);
    return tl;
}

// What a demuxer would have read of made_up_playlist(n): 1000 bytes per
// segment, 10 packets each, in decode order with one B-frame pattern.
Demuxed made_up_demuxed(int n, bool ranges = false)
{
    // Decode order I0 P2 B1 P4 B3 ... within a segment.
    static constexpr int kOrder[10] = { 0, 2, 1, 4, 3, 6, 5, 8, 7, 9 };
    Demuxed d;
    d.hls       = true;
    d.time_base = kTs;
    for (int k = 0; k < n; ++k) {
        d.files.push_back({
            .url    = ranges ? "/m/all.ts" : std::format("/m/{}.ts", static_cast<char>('a' + k)),
            .offset = ranges ? std::optional<std::int64_t>(1000 * k) : std::nullopt,
            .bytes  = 1000,
        });
        for (int i = 0; i < 10; ++i)
            d.packets.push_back({ .pts = (250 + 10 * k + kOrder[i]) * kFrame,
                                  .pos = 1000 * k + 90 * i, .key = i == 0, .corrupt = false });
    }
    return d;
}

} // namespace

TEST_CASE("TimelineMedia: map_frames() places made-up packets and refuses what it can't place",
          "[timeline][timeline_media]")
{
    const hls::MediaTimeline tl = made_up_playlist(4);
    Demuxed d = made_up_demuxed(4);

    SECTION("the map") {
        d.packets[12].corrupt = true;   // segment 1's third packet: frame 11
        const VariantMedia m = timeline::map_frames(d, &tl, {});
        REQUIRE(m.refused.empty());
        REQUIRE(m.segments.size() == 4);
        for (std::size_t k = 0; k < 4; ++k) {
            CHECK(m.segments[k].seq == 3 + static_cast<std::int64_t>(k));
            CHECK(m.segments[k].first_index == 10 * static_cast<std::int64_t>(k));
            CHECK(m.segments[k].frames == 10);
            CHECK(m.segments[k].bytes == 1000);
        }
        for (std::size_t i = 0; i < 40; ++i)
            CHECK(m.pts[i] == (250 + static_cast<std::int64_t>(i)) * kFrame);
        CHECK(m.keyframes == std::vector<std::int64_t>{ 0, 10, 20, 30 });
        CHECK(m.corrupt == std::vector<std::int64_t>{ 11 });
        const auto b = timeline::next_boundary(m, tl, 11);
        REQUIRE(b);
        CHECK(b->seq == 5);
        CHECK(b->first_index == 20);
        CHECK(b->discontinuity);
        const auto c = timeline::next_boundary(m, tl, 21);
        REQUIRE(c);
        CHECK(c->seq == 6);
        CHECK_FALSE(c->discontinuity);
        CHECK_FALSE(timeline::next_boundary(m, tl, 39));   // the last segment
        CHECK_FALSE(timeline::next_boundary(m, tl, 40));   // past the last frame
        CHECK_FALSE(timeline::next_boundary(VariantMedia{}, tl, 0));
    }

    SECTION("a segment of no bytes holds no frame") {
        d.files[1].bytes = 0;
        for (MediaPacket& p : d.packets)
            if (p.pos >= 2000) p.pos -= 1000;
        d.packets.erase(d.packets.begin() + 10, d.packets.begin() + 20);
        const VariantMedia m = timeline::map_frames(d, &tl, {});
        REQUIRE(m.refused.empty());
        CHECK(m.segments[1].frames == 0);
        CHECK(m.segments[1].first_index == 10);
        CHECK(m.segments[2].first_index == 10);
        CHECK(m.pts.size() == 30);
    }

    SECTION("byte ranges") {
        const hls::MediaTimeline ranges = made_up_playlist(4, true);
        Demuxed r = made_up_demuxed(4, true);
        REQUIRE(timeline::map_frames(r, &ranges, {}).refused.empty());
        r.files[2].offset = 2500;
        CHECK(timeline::map_frames(r, &ranges, {}).refused ==
              "FFmpeg's hls demuxer read segment 5 from byte 2500, where its EXT-X-BYTERANGE starts "
              "at byte 2000");
        r.files[2].offset = 2000;
        r.files[2].bytes  = 999;
        CHECK(timeline::map_frames(r, &ranges, {}).refused ==
              "FFmpeg's hls demuxer took 999 bytes of segment 5, whose EXT-X-BYTERANGE has 1000");
        // A whole segment read from an offset.
        d.files[0].offset = 0;
        CHECK(timeline::map_frames(d, &tl, {}).refused ==
              "FFmpeg's hls demuxer read segment 3, which has no EXT-X-BYTERANGE, from byte 0");
    }

    SECTION("files that aren't the playlist's segments") {
        Demuxed fewer = d;
        fewer.files.pop_back();
        CHECK(timeline::map_frames(fewer, &tl, {}).refused ==
              "FFmpeg's hls demuxer read 3 files after the playlist, for its 4 segments");
        // An initialization section first, as an EXT-X-MAP's.
        Demuxed init = d;
        init.files.insert(init.files.begin(), { .url = "/m/init.mp4", .bytes = 700 });
        init.files.pop_back();
        CHECK(timeline::map_frames(init, &tl, {}).refused ==
              "the 1st file FFmpeg's hls demuxer read after the playlist, /m/init.mp4, isn't its "
              "segment 3, /m/a.ts");
    }

    SECTION("packets it can't place or order") {
        Demuxed no_pts = d;
        no_pts.packets[5].pts.reset();
        CHECK(timeline::map_frames(no_pts, &tl, {}).refused ==
              "the 6th video packet read has no PTS, so its frame can't be ordered");
        Demuxed no_pos = d;
        no_pos.packets[21].pos = -1;
        CHECK(timeline::map_frames(no_pos, &tl, {}).refused ==
              "the 22nd video packet read has no byte position");
        Demuxed past = d;
        past.packets[33].pos = 4000;
        CHECK(timeline::map_frames(past, &tl, {}).refused ==
              "the 34th video packet read, at byte 4000, lies past the segments, which end at byte "
              "4000");
    }

    SECTION("an input read as HLS with no playlist, a playlist read otherwise") {
        CHECK(timeline::map_frames(d, nullptr, {}).refused ==
              "it was read as an HLS playlist, and no playlist was given for its segments");
        Demuxed file = d;
        file.hls = false;
        CHECK(timeline::map_frames(file, &tl, {}).refused == "it wasn't read by FFmpeg's hls demuxer");
        // Without a playlist, one run in PTS order across the input.
        file.files.clear();
        const VariantMedia m = timeline::map_frames(file, nullptr, {});
        REQUIRE(m.refused.empty());
        CHECK(m.segments.empty());
        CHECK(std::ranges::is_sorted(m.pts));
        CHECK(m.keyframes == std::vector<std::int64_t>{ 0, 10, 20, 30 });
    }

    SECTION("a report whose positions and PTS don't pair up") {
        const Report three_for_two{ .index = { 0, 1, 2 }, .pts = { 250 * kFrame, 251 * kFrame } };
        CHECK(timeline::map_frames(d, &tl, { scored(three_for_two, 4) }).refused ==
              "pair 4's report gives 3 positions for 2 PTS");
        const Report before{ .index = { -1 }, .pts = { 0 } };
        CHECK(timeline::map_frames(d, &tl, { scored(before, 4) }).refused ==
              "pair 4's report scores frame -1 of the input, which the demux pass doesn't have: it "
              "read 40 frames");
        // A pair that scored nothing has nothing to check, whatever its time base.
        CHECK(timeline::map_frames(d, &tl, { scored(Report{}, 4, { 0, 1 }) }).refused.empty());
    }
}

TEST_CASE("TimelineMedia: the scored frames of a ladder report's pairs", "[timeline][timeline_media]")
{
    timeline::LadderScores scores;
    timeline::PairScores a, b;
    a.index         = 0;
    a.ref_time_base = kTs;
    a.dis_time_base = { 1, 1000 };
    a.ref_index     = { 0, 1 };
    a.ref_pts       = { 900000, 903600 };
    a.dis_index     = { 0, 1 };
    a.dis_pts       = { 10000, 10040 };
    b               = a;
    b.index         = 1;
    b.ref_index     = { 0, 2 };
    b.ref_pts       = { 900000, 907200 };
    scores.pairs    = { a, b };

    // The reference's frames in every pair, views of the pairs' vectors.
    const std::vector<ScoredFrames> ref = timeline::reference_frames(scores);
    REQUIRE(ref.size() == 2);
    for (std::size_t k = 0; k < 2; ++k) {
        CHECK(ref[k].pair == k);
        CHECK(av_cmp_q(ref[k].time_base, kTs) == 0);
        CHECK(ref[k].index.data() == scores.pairs[k].ref_index.data());
        CHECK(ref[k].index.size() == 2);
        CHECK(ref[k].pts.data() == scores.pairs[k].ref_pts.data());
        CHECK(ref[k].pts.size() == 2);
    }
    const ScoredFrames dis = timeline::distorted_frames(scores.pairs[0]);
    CHECK(dis.pair == 0);
    CHECK(av_cmp_q(dis.time_base, AVRational{ 1, 1000 }) == 0);
    CHECK(dis.index.data() == scores.pairs[0].dis_index.data());
    CHECK(dis.pts.data() == scores.pairs[0].dis_pts.data());
    CHECK(timeline::reference_frames({}).empty());
}

