// The synthetic ladders of §3.2's attribution tests (tests/SyntheticLadder.h;
// DECISIONS.md D-069, D-080; eval/phase3-survey/design.md §9.1, WP10).
// Without media: the layouts of D-080's four ladders, their playlists' text
// against what testgen's prototype wrote, BANDWIDTH from the segments' sizes
// and its rung order, layout.json, the media directory of P-U, and what
// write_ladder() refuses. With media: one ladder made twice in one process,
// byte-identical, and what its layout says of its segments, frames and tags,
// its BANDWIDTHs in rung order and the encoder it used, which the test's
// output records; then the other three ladders, and one made with the other
// encoder, each in a test case of its own, so that ctest runs them in
// parallel processes.
//
// A ctest run of this file makes six ladders, five in a build without
// libx264; ctest runs each test case in a process of its own (design §9.5).
// The six test cases without media make none: the last of them has
// write_ladder() refuse two directories before anything is encoded, and
// checks with made() that no ladder was made. The design's case makes two,
// the two that design §9.5 counts for this file among the seven per ctest
// run that P-Q and R9 count: the ad from the main origin through ladder(),
// and the same ladder once more beside it. Each of the four test cases after
// it makes one, four more than the design counts, but none that another
// case of this file makes, so one test case holding them all would make as
// many, one after another in one process; in a build without libx264 the
// last of them makes none, write_ladder() refusing libx264 at the first
// segment. Each of the four is here for what the design's case leaves
// unchecked:
// - the degraded ladder, acceptance test 1's (design §9.2): one
//   discontinuity sequence, so one muxer for a rung's eight segments and the
//   discontinuity indicator on no packet; no tags; the low quantizer on v270;
// - the ad from 1 s and the continuous ad, Q34 (d)'s two other arrangements,
//   which acceptance test 2 runs too: the PTS of an ad sequence that starts
//   at 1 s, the lowest origin the generator uses, since a sequence whose
//   first DTS would be negative is shifted off the layout's PTS (testgen.md
//   §3.6, item 3); and PTS that go on through both splices, which the
//   playlist and the packets mark all the same;
// - the other encoder, Q33 (c)'s fallback: MPEG-2 in a build with libx264,
//   whose ladders are otherwise all libx264's, so that the fallback's path
//   runs in its tests as well; in a build without libx264, the refusal.

#include <catch2/catch_test_macros.hpp>

#include "HlsAlign.h"
#include "SyntheticLadder.h"
#include "TestTemp.h"
#include "VMAFLadder.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

namespace fs = std::filesystem;
using synthladder::AdTimestamps;
using synthladder::Encoder;
using synthladder::Kind;
using synthladder::kRungs;
using synthladder::kTicksPerFrame;
using synthladder::Ladder;
using synthladder::Layout;
using synthladder::SegmentLayout;
using synthladder::Spec;
using synthladder::Tag;

namespace {

// The ad ladder's media playlist as testgen wrote it, the same in its three
// rungs (eval/phase3-survey/testgen.md §2.3, from
// eval/phase3-survey/testgen/media/ad-x264-a/v180/index.m3u8).
constexpr const char* kAdPlaylist =
    "#EXTM3U\n"
    "#EXT-X-VERSION:3\n"
    "#EXT-X-TARGETDURATION:1\n"
    "#EXT-X-MEDIA-SEQUENCE:0\n"
    "#EXT-X-DISCONTINUITY-SEQUENCE:0\n"
    "#EXT-X-PLAYLIST-TYPE:VOD\n"
    "#EXTINF:1.000000,\n"
    "main_000.ts\n"
    "#EXTINF:1.000000,\n"
    "main_001.ts\n"
    "#EXTINF:1.000000,\n"
    "main_002.ts\n"
    "#EXT-X-DISCONTINUITY\n"
    "#EXT-X-CUE-OUT:2.000\n"
    "#EXTINF:1.000000,\n"
    "ad_000.ts\n"
    "#EXT-X-CUE-OUT-CONT:1.000/2.000\n"
    "#EXTINF:1.000000,\n"
    "ad_001.ts\n"
    "#EXT-X-DISCONTINUITY\n"
    "#EXT-X-CUE-IN\n"
    "#EXTINF:1.000000,\n"
    "main_005.ts\n"
    "#EXTINF:1.000000,\n"
    "main_006.ts\n"
    "#EXTINF:1.000000,\n"
    "main_007.ts\n"
    "#EXT-X-ENDLIST\n";

// The degraded ladder's (eval/phase3-survey/testgen/media/degraded-x264-a/v270/index.m3u8).
constexpr const char* kDegradedPlaylist =
    "#EXTM3U\n"
    "#EXT-X-VERSION:3\n"
    "#EXT-X-TARGETDURATION:1\n"
    "#EXT-X-MEDIA-SEQUENCE:0\n"
    "#EXT-X-PLAYLIST-TYPE:VOD\n"
    "#EXTINF:1.000000,\n"
    "seg_000.ts\n"
    "#EXTINF:1.000000,\n"
    "seg_001.ts\n"
    "#EXTINF:1.000000,\n"
    "seg_002.ts\n"
    "#EXTINF:1.000000,\n"
    "seg_003.ts\n"
    "#EXTINF:1.000000,\n"
    "seg_004.ts\n"
    "#EXTINF:1.000000,\n"
    "seg_005.ts\n"
    "#EXTINF:1.000000,\n"
    "seg_006.ts\n"
    "#EXTINF:1.000000,\n"
    "seg_007.ts\n"
    "#EXT-X-ENDLIST\n";

// The ad ladder's masters as testgen wrote them, with libx264 and with
// MPEG-2 (eval/phase3-survey/testgen/media/ad-x264-a/master.m3u8 and
// ad-mpeg2-a/master.m3u8).
constexpr const char* kAdX264Master =
    "#EXTM3U\n"
    "#EXT-X-VERSION:3\n"
    "#EXT-X-INDEPENDENT-SEGMENTS\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=613632,AVERAGE-BANDWIDTH=580920,RESOLUTION=640x360,FRAME-RATE=25.000,"
    "CODECS=\"avc1.64001e\"\n"
    "v360/index.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=565504,AVERAGE-BANDWIDTH=525836,RESOLUTION=480x270,FRAME-RATE=25.000,"
    "CODECS=\"avc1.640015\"\n"
    "v270/index.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=345920,AVERAGE-BANDWIDTH=274668,RESOLUTION=320x180,FRAME-RATE=25.000,"
    "CODECS=\"avc1.64000c\"\n"
    "v180/index.m3u8\n";
constexpr const char* kAdMpeg2Master =
    "#EXTM3U\n"
    "#EXT-X-VERSION:3\n"
    "#EXT-X-INDEPENDENT-SEGMENTS\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=1069344,AVERAGE-BANDWIDTH=1028172,RESOLUTION=640x360,FRAME-RATE=25.000\n"
    "v360/index.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=971584,AVERAGE-BANDWIDTH=918380,RESOLUTION=480x270,FRAME-RATE=25.000\n"
    "v270/index.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=585056,AVERAGE-BANDWIDTH=463984,RESOLUTION=320x180,FRAME-RATE=25.000\n"
    "v180/index.m3u8\n";

Encoder other_encoder()
{
    return synthladder::default_encoder() == Encoder::X264 ? Encoder::Mpeg2 : Encoder::X264;
}

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

std::string read_file(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
}

std::vector<std::string> lines_of(const std::string& text)
{
    std::vector<std::string> out;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t nl = text.find('\n', at);
        out.push_back(text.substr(at, nl - at));
        at = nl == std::string::npos ? text.size() : nl + 1;
    }
    return out;
}

// Every regular file under `dir`, by its path relative to it, with its bytes.
std::map<std::string, std::string> tree(const fs::path& dir)
{
    std::map<std::string, std::string> out;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(dir))
        if (e.is_regular_file()) out[fs::relative(e.path(), dir).generic_string()] = read_file(e.path());
    return out;
}

// Each frame's PTS, in play order, as the layout gives them.
std::vector<std::int64_t> frame_pts(const Layout& l)
{
    std::vector<std::int64_t> out;
    for (const SegmentLayout& s : l.segments)
        for (int k = 0; k < s.frames; ++k) out.push_back(s.pts_first + std::int64_t{ k } * kTicksPerFrame);
    return out;
}

// Sets an environment variable, or with nullopt unsets it, while it lives.
class EnvVar {
public:
    EnvVar(const char* name, const std::optional<std::string>& value) : name_(name)
    {
        if (const char* old = std::getenv(name)) old_ = old;
        if (value) ::setenv(name, value->c_str(), 1);
        else ::unsetenv(name);
    }
    ~EnvVar()
    {
        if (old_) ::setenv(name_, old_->c_str(), 1);
        else ::unsetenv(name_);
    }
    EnvVar(const EnvVar&)            = delete;
    EnvVar& operator=(const EnvVar&) = delete;

private:
    const char*                name_;
    std::optional<std::string> old_;
};

// ── Media, read back ─────────────────────────────────────────────────────────

// An input's video packets, as FFmpeg's demuxers give them.
struct Demuxed {
    struct Packet {
        std::int64_t pts = 0, dts = 0;
        bool         key = false, corrupt = false;
    };
    std::string         error;
    AVCodecID           codec = AV_CODEC_ID_NONE;
    int                 width = 0, height = 0;
    int                 id = 0;   // the stream's id: in MPEG-TS, its PID
    std::vector<Packet> packets;
};

// Holds FFmpeg's log at AV_LOG_WARNING, or lower if it was, while it lives,
// as tests/test_vmaf_color.cpp's QuietLog holds it quiet: the hls demuxer
// logs each segment it opens at AV_LOG_INFO.
struct WarningLog {
    int level = av_log_get_level();
    WarningLog() { av_log_set_level(std::min(level, AV_LOG_WARNING)); }
    ~WarningLog() { av_log_set_level(level); }
    WarningLog(const WarningLog&)            = delete;
    WarningLog& operator=(const WarningLog&) = delete;
};

// `url` read to its end, packets only: an MPEG-TS segment through the mpegts
// demuxer, a playlist through the hls demuxer, as the analyzer reads it.
Demuxed demux(const std::string& url)
{
    const WarningLog quiet;
    Demuxed out;
    AVFormatContext* ic = nullptr;
    if (avformat_open_input(&ic, url.c_str(), nullptr, nullptr) < 0) {
        out.error = "cannot open " + url;
        return out;
    }
    const int vs = avformat_find_stream_info(ic, nullptr) < 0
                       ? -1
                       : av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vs < 0) {
        out.error = "no video stream in " + url;
        avformat_close_input(&ic);
        return out;
    }
    const AVStream* st = ic->streams[vs];
    out.codec  = st->codecpar->codec_id;
    out.width  = st->codecpar->width;
    out.height = st->codecpar->height;
    out.id     = st->id;
    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(ic, pkt) >= 0) {
        if (pkt->stream_index == vs)
            out.packets.push_back({ pkt->pts, pkt->dts, (pkt->flags & AV_PKT_FLAG_KEY) != 0,
                                    (pkt->flags & AV_PKT_FLAG_CORRUPT) != 0 });
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&ic);
    return out;
}

// One MPEG-TS packet's header (ISO/IEC 13818-1 §2.4.3.2 and §2.4.3.4).
struct TsPacket {
    int         pid = 0;
    int         cc = 0;                  // continuity_counter
    bool        unit_start = false;      // payload_unit_start_indicator
    bool        payload = false;         // adaptation_field_control: a payload follows
    bool        discontinuity = false;   // the adaptation field's discontinuity_indicator
    std::size_t payload_at = 0;          // where its payload starts in the file
};

// `bytes` as 188-byte packets, up to the first that doesn't start with the
// sync byte 0x47.
std::vector<TsPacket> ts_packets(const std::string& bytes)
{
    std::vector<TsPacket> out;
    for (std::size_t at = 0; at + 188 <= bytes.size(); at += 188) {
        const auto b = [&](std::size_t k) { return static_cast<unsigned char>(bytes[at + k]); };
        if (b(0) != 0x47) break;
        TsPacket p;
        p.unit_start = (b(1) & 0x40) != 0;
        p.pid        = ((b(1) & 0x1f) << 8) | b(2);
        p.cc         = b(3) & 0x0f;
        const int afc = (b(3) >> 4) & 3;
        p.payload    = (afc & 1) != 0;
        p.payload_at = at + 4;
        if (afc & 2) {
            if (b(4) > 0) p.discontinuity = (b(5) & 0x80) != 0;
            p.payload_at = at + 5 + b(4);
        }
        out.push_back(p);
    }
    return out;
}

// The PMT's PID from a PAT packet whose section starts in it: the first
// program's program_map_PID, bytes 10 and 11 of the section after the
// pointer field (ISO/IEC 13818-1 Table 2-30).
int pmt_pid(const std::string& bytes, const TsPacket& pat)
{
    const std::size_t s = pat.payload_at + 1 + static_cast<unsigned char>(bytes[pat.payload_at]);
    if (s + 12 > bytes.size()) return -1;
    const auto b = [&](std::size_t k) { return static_cast<unsigned char>(bytes[s + k]); };
    return ((b(10) & 0x1f) << 8) | b(11);
}

// What a made ladder's files say, checked against its layout: the files, the
// master as mirage_vmaf reads it, the media playlists as
// hls::parse_media_timeline reads them, each segment's frames and MPEG-TS
// packets, each rung read through the hls demuxer, and where the low
// quantizer landed.
void check_media(const Ladder& lad)
{
    const Layout& l = lad.layout;
    INFO(synthladder::name(lad.spec) << " in " << lad.dir.string());
    REQUIRE(lad.error.empty());
    CHECK(l.encoder == synthladder::encoder_of(lad.spec));
    const bool x264 = l.encoder == Encoder::X264;

    // 29 files: each rung's eight segments and media playlist, the master and
    // layout.json, which is the layout with what writing measured.
    const std::map<std::string, std::string> files = tree(lad.dir);
    CHECK(files.size() == kRungs.size() * (synthladder::kSegments + 1) + 2);
    REQUIRE(files.contains("layout.json"));
    CHECK(files.at("layout.json") == synthladder::layout_json(l));
    CHECK(contains(files.at("layout.json"),
                   std::format("\n  \"codec\": \"{}\",\n", synthladder::encoder_name(l.encoder))));

    // The master, as mirage_vmaf reads it: the three rungs from the top with
    // their measured BANDWIDTHs, each below the one before, so that top-rung
    // mode (D-002) scores v270 and v180 against v360 with no warning.
    REQUIRE(files.contains("master.m3u8"));
    CHECK(files.at("master.m3u8") == synthladder::master_playlist(l));
    const vmafladder::Master m = vmafladder::parse_master(files.at("master.m3u8"), lad.master);
    REQUIRE(m.error.empty());
    REQUIRE(m.entries.size() == kRungs.size());
    for (std::size_t r = 0; r < kRungs.size(); ++r) {
        INFO("rung " << kRungs[r].name);
        const vmafladder::Entry& e = m.entries[r];
        CHECK(e.uri == std::format("{}/index.m3u8", kRungs[r].name));
        CHECK(e.url == lad.playlist(static_cast<int>(r)));
        CHECK(e.video);
        CHECK(e.resolution == std::format("{}x{}", kRungs[r].width, kRungs[r].height));
        CHECK(e.frame_rate == "25.000");
        std::vector<std::uint64_t> sizes;
        for (const SegmentLayout& s : l.segments)
            sizes.push_back(files.at(std::format("{}/{}", kRungs[r].name, s.file)).size());
        const auto [bandwidth, average] = synthladder::bandwidths(sizes);
        CHECK(e.bandwidth == bandwidth);
        CHECK(e.bandwidth == l.rungs[r].bandwidth);
        CHECK(e.average_bandwidth == std::to_string(average));
        if (r > 0) CHECK(e.bandwidth < m.entries[r - 1].bandwidth);
        // CODECS from libx264's first SPS: High profile (0x64), as testgen's
        // were (testgen.md §2.3); MPEG-2 has none in HLS.
        if (x264) CHECK((e.codecs && e.codecs->starts_with("avc1.64") && e.codecs->size() == 11));
        else CHECK_FALSE(e.codecs);
    }
    const vmafladder::Plan p = vmafladder::plan(m, "");
    REQUIRE(p.error.empty());
    CHECK(p.reference_url == lad.playlist(0));
    REQUIRE(p.pairs.size() == 2);
    CHECK(p.pairs[0].url == lad.playlist(1));
    CHECK(p.pairs[1].url == lad.playlist(2));
    CHECK(p.warnings.empty());

    // The encoder's settings, which its SEI records for libx264.
    if (x264) {
        CHECK(l.x264_sei.starts_with("x264 - core "));
        for (const char* option : { " threads=1 ", " keyint=25 ", " scenecut=0 ", " rc=cqp ", " qp=22 " })
            CHECK(contains(l.x264_sei, option));
    } else {
        CHECK(l.x264_sei.empty());
    }

    const std::vector<std::int64_t> pts = frame_pts(l);
    for (std::size_t r = 0; r < kRungs.size(); ++r) {
        INFO("rung " << kRungs[r].name);
        const std::string name = kRungs[r].name;

        // Its media playlist: the layout's text, with each segment at the
        // layout's lines and sequence numbers.
        const std::string text = files.at(name + "/index.m3u8");
        CHECK(text == synthladder::media_playlist(l));
        const hls::MediaTimeline t = hls::parse_media_timeline(text, lad.playlist(static_cast<int>(r)));
        REQUIRE(t.ok);
        CHECK_FALSE(t.live());
        CHECK(t.version == 3);
        CHECK(t.target == 1.0);
        CHECK(t.problems.empty());
        REQUIRE(t.segments.size() == l.segments.size());
        const std::vector<std::string> lines = lines_of(text);
        for (const SegmentLayout& s : l.segments) {
            INFO("segment " << s.index);
            const hls::Segment& got = t.segments[static_cast<std::size_t>(s.index)];
            CHECK(got.seq == s.index);
            CHECK(got.uri == (lad.dir / name / s.file).string());
            CHECK(got.start == s.position_s());
            CHECK(got.duration == 1.0);
            CHECK(got.discontinuity == s.discontinuity);
            CHECK(got.disc_seq == s.sequence);
            CHECK(got.line == s.uri_line);
            const auto line = [&](int n) { return lines.at(static_cast<std::size_t>(n - 1)); };
            CHECK(line(s.extinf_line) == "#EXTINF:1.000000,");
            CHECK(line(s.uri_line) == s.file);
            if (s.discontinuity) CHECK(line(s.discontinuity_line) == "#EXT-X-DISCONTINUITY");
            for (const Tag& tag : s.tags) CHECK(line(tag.line) == tag.text);
        }

        // Each segment on its own: its 25 frames from its first PTS, the first
        // its only key frame; a PAT and its PMT, which each segment must hold
        // (RFC 8216 §3.2), before its first video packet; the discontinuity
        // indicator on the first packet of each PID where a playlist
        // discontinuity sequence after the first starts, the counters marked
        // at the splice (D-080), and on no other packet; and each PID's
        // continuity counter one more than at its previous packet with a
        // payload, from one segment to the next within a sequence
        // (RFC 8216 §3).
        std::map<int, int> cc;   // PID: the counter of its last packet with a payload
        for (const SegmentLayout& s : l.segments) {
            INFO("segment " << s.file);
            const std::string& bytes = files.at(name + "/" + s.file);
            CHECK(bytes.size() == s.bytes[r]);
            const Demuxed d = demux((lad.dir / name / s.file).string());
            REQUIRE(d.error.empty());
            CHECK(d.codec == (x264 ? AV_CODEC_ID_H264 : AV_CODEC_ID_MPEG2VIDEO));
            CHECK(d.width == kRungs[r].width);
            CHECK(d.height == kRungs[r].height);
            REQUIRE(d.packets.size() == static_cast<std::size_t>(s.frames));
            std::vector<std::int64_t> got;
            for (const Demuxed::Packet& k : d.packets) got.push_back(k.pts);
            std::sort(got.begin(), got.end());
            CHECK(got == std::vector<std::int64_t>(pts.begin() + s.first_frame,
                                                   pts.begin() + s.first_frame + s.frames));
            CHECK(d.packets.front().key);
            CHECK(std::ranges::count_if(d.packets, [](const auto& k) { return k.key; }) == 1);

            REQUIRE(bytes.size() % 188 == 0);
            const std::vector<TsPacket> ts = ts_packets(bytes);
            REQUIRE(ts.size() == bytes.size() / 188);
            const bool marked = s.discontinuity;   // the first segment of a sequence after the first
            if (marked) cc.clear();
            std::set<int> seen;
            std::optional<std::size_t> pat, pmt, video, wrong_indicator, wrong_cc;
            int pmt_at = -1;
            for (std::size_t i = 0; i < ts.size(); ++i) {
                const TsPacket& k = ts[i];
                const bool first = seen.insert(k.pid).second;
                if (k.discontinuity != (marked && first) && !wrong_indicator) wrong_indicator = i;
                if (k.payload) {
                    const auto it = cc.find(k.pid);
                    if (it != cc.end() && k.cc != (it->second + 1) % 16 && !wrong_cc) wrong_cc = i;
                    cc[k.pid] = k.cc;
                }
                if (k.pid == 0 && k.unit_start && !pat) {
                    pat    = i;
                    pmt_at = pmt_pid(bytes, k);
                }
                if (k.pid == pmt_at && !pmt) pmt = i;
                if (k.pid == d.id && !video) video = i;
            }
            CHECK_FALSE(wrong_indicator);
            CHECK_FALSE(wrong_cc);
            CHECK(seen.contains(d.id));
            REQUIRE(pat);
            REQUIRE(pmt);
            REQUIRE(video);
            CHECK(*pat < *pmt);
            CHECK(*pmt < *video);
        }

        // The rung through FFmpeg's hls demuxer, as mirage_vmaf reads it: its
        // 200 frames in play order, segment by segment, and no packet flagged
        // corrupt, which counters restarted unmarked at a splice give
        // (testgen.md §3.6, item 2).
        const Demuxed d = demux(lad.playlist(static_cast<int>(r)));
        REQUIRE(d.error.empty());
        REQUIRE(d.packets.size() == static_cast<std::size_t>(synthladder::kFrames));
        CHECK(std::none_of(d.packets.begin(), d.packets.end(), [](const auto& k) { return k.corrupt; }));
        for (const SegmentLayout& s : l.segments) {
            INFO("segment " << s.file << " through the hls demuxer");
            std::vector<std::int64_t> got;
            for (int k = s.first_frame; k < s.first_frame + s.frames; ++k)
                got.push_back(d.packets[static_cast<std::size_t>(k)].pts);
            std::sort(got.begin(), got.end());
            CHECK(got == std::vector<std::int64_t>(pts.begin() + s.first_frame,
                                                   pts.begin() + s.first_frame + s.frames));
            CHECK(d.packets[static_cast<std::size_t>(s.first_frame)].key);
        }

        // The low quantizer on the layout's segments of the low rung, and
        // nowhere else: those segments, and no other, are under half the size
        // of their rung's largest. In testgen's runs the low segments were 21%
        // to 33% of the smallest other segment of their rung
        // (eval/phase3-survey/testgen/logs/10, 20, 40 and 50).
        const std::uint64_t largest =
            std::ranges::max(l.segments, {}, [&](const SegmentLayout& s) { return s.bytes[r]; }).bytes[r];
        for (const SegmentLayout& s : l.segments) {
            const bool low = static_cast<int>(r) == l.low_rung &&
                             std::ranges::find(l.low_segments, s.index) != l.low_segments.end();
            INFO("segment " << s.file << ": " << s.bytes[r] << " bytes against " << largest);
            CHECK((2 * s.bytes[r] < largest) == low);
            CHECK(s.q[r] == (low ? l.q_low : l.q_normal));
        }
    }
}

// One made ladder checked, its encoder and time in the test's output.
void check_made(const Spec& spec)
{
    const Ladder& lad = synthladder::ladder(spec);
    check_media(lad);
    WARN(std::format("synthladder: {} made with {} in {:.3f} s ({:.3f} s of it the source's pictures)",
                     synthladder::name(spec), synthladder::encoder_name(lad.layout.encoder), lad.seconds,
                     lad.source_seconds));
}

} // namespace

// ── Without media ────────────────────────────────────────────────────────────

TEST_CASE("synthladder::plan: D-080's ladders, segment by segment", "[timeline][synthladder]")
{
    CHECK(synthladder::default_encoder() ==
          (avcodec_find_encoder_by_name("libx264") ? Encoder::X264 : Encoder::Mpeg2));
    CHECK(synthladder::plan({}).encoder == synthladder::default_encoder());
    CHECK(synthladder::encoder_name(Encoder::X264) == std::string("libx264"));
    CHECK(synthladder::encoder_name(Encoder::Mpeg2) == std::string("mpeg2video"));
    CHECK(synthladder::q_normal(Encoder::X264) == 22);
    CHECK(synthladder::q_low(Encoder::X264) == 46);
    CHECK(synthladder::q_normal(Encoder::Mpeg2) == 4);
    CHECK(synthladder::q_low(Encoder::Mpeg2) == 31);

    // The names of the ladders' directories; the encoder only when not the
    // default.
    CHECK(synthladder::name({}) == "degraded");
    CHECK(synthladder::name({ .kind = Kind::Ad }) == "ad-main-origin");
    CHECK(synthladder::name({ .kind = Kind::Ad, .ad = AdTimestamps::FromOneSecond }) == "ad-from-1s");
    CHECK(synthladder::name({ .kind = Kind::Ad, .ad = AdTimestamps::Continuous }) == "ad-continuous");
    CHECK(synthladder::name({ .encoder = synthladder::default_encoder() }) == "degraded");
    CHECK(synthladder::name({ .kind = Kind::Ad, .encoder = other_encoder() }) ==
          std::format("ad-main-origin-{}", synthladder::encoder_name(other_encoder())));

    // A layout without segments has no low frames and no sequence.
    CHECK(Layout{}.low_frames() == std::pair{ 0, 0 });
    CHECK(Layout{}.sequences() == 0);

    for (const Encoder enc : { Encoder::X264, Encoder::Mpeg2 }) {
        INFO(synthladder::encoder_name(enc));
        const int qn = synthladder::q_normal(enc), ql = synthladder::q_low(enc);

        // The degraded ladder: v270's segment 4, frames 100 to 124, at the low
        // quantizer (testgen.md §2.3), one discontinuity sequence of main
        // content from 10 s.
        const Layout d = synthladder::plan({ .kind = Kind::Degraded, .encoder = enc });
        CHECK(d.kind == Kind::Degraded);
        CHECK(d.encoder == enc);
        CHECK(d.q_normal == qn);
        CHECK(d.q_low == ql);
        CHECK(d.low_rung == 1);
        CHECK(d.control_rung == 2);
        CHECK(d.low_segments == std::vector<int>{ 4 });
        CHECK(d.low_frames() == std::pair{ 100, 125 });
        CHECK(d.cue_out_segment == -1);
        CHECK(d.cue_in_segment == -1);
        CHECK(d.sequences() == 1);
        CHECK(d.x264_sei.empty());
        REQUIRE(d.rungs.size() == 3);
        for (std::size_t r = 0; r < 3; ++r) {
            CHECK(d.rungs[r].name == kRungs[r].name);
            CHECK(d.rungs[r].width == kRungs[r].width);
            CHECK(d.rungs[r].height == kRungs[r].height);
            CHECK(d.rungs[r].uri == std::format("{}/index.m3u8", kRungs[r].name));
            CHECK(d.rungs[r].bandwidth == 0);
            CHECK(d.rungs[r].codecs.empty());
        }
        REQUIRE(d.segments.size() == 8);
        for (int s = 0; s < 8; ++s) {
            INFO("segment " << s);
            const SegmentLayout& seg = d.segments[static_cast<std::size_t>(s)];
            CHECK(seg.index == s);
            CHECK(seg.file == std::format("seg_{:03}.ts", s));
            CHECK(seg.period == "main");
            CHECK(seg.first_frame == 25 * s);
            CHECK(seg.frames == 25);
            CHECK(seg.position_s() == s);
            CHECK(seg.pts_first == 900000 + 90000 * s);
            CHECK(seg.source_first == 25 * s);
            CHECK(seg.sequence == 0);
            CHECK_FALSE(seg.discontinuity);
            CHECK(seg.discontinuity_line == 0);
            CHECK(seg.tags.empty());
            CHECK(seg.extinf_line == 6 + 2 * s);
            CHECK(seg.uri_line == 7 + 2 * s);
            CHECK(seg.q == (s == 4 ? std::array{ qn, ql, qn } : std::array{ qn, qn, qn }));
            CHECK(seg.bytes == std::array<std::uint64_t, 3>{});
        }

        // The ad ladder in Q34's three arrangements: the same segments, tags
        // and lines (testgen.md §2.3), v180's ad at the low quantizer, the
        // ad's PTS from 10 s, 1 s or 13 s.
        const std::vector<Tag> no_tags;
        for (const auto& [arrangement, origin] : { std::pair{ AdTimestamps::MainOrigin, 900000 },
                                                   std::pair{ AdTimestamps::FromOneSecond, 90000 },
                                                   std::pair{ AdTimestamps::Continuous, 1170000 } }) {
            INFO(synthladder::ad_timestamps_name(arrangement));
            const Layout a = synthladder::plan({ .kind = Kind::Ad, .ad = arrangement, .encoder = enc });
            CHECK(a.kind == Kind::Ad);
            CHECK(a.ad == arrangement);
            CHECK(a.low_rung == 2);
            CHECK(a.control_rung == 1);
            CHECK(a.low_segments == std::vector<int>{ 3, 4 });
            CHECK(a.low_frames() == std::pair{ 75, 125 });
            CHECK(a.cue_out_segment == 3);
            CHECK(a.cue_in_segment == 5);
            CHECK(a.break_s == 2);
            CHECK(a.ad_origin == origin);
            CHECK(a.sequences() == 3);
            REQUIRE(a.segments.size() == 8);
            const std::vector<std::string> files = { "main_000.ts", "main_001.ts", "main_002.ts",
                                                     "ad_000.ts",   "ad_001.ts",   "main_005.ts",
                                                     "main_006.ts", "main_007.ts" };
            const std::vector<int> source = { 0, 25, 50, 0, 25, 125, 150, 175 };
            const std::vector<int> sequence = { 0, 0, 0, 1, 1, 2, 2, 2 };
            const std::vector<std::pair<int, int>> extinf_uri = {
                { 7, 8 }, { 9, 10 }, { 11, 12 }, { 15, 16 }, { 18, 19 }, { 22, 23 }, { 24, 25 }, { 26, 27 },
            };
            const std::map<int, int> discontinuity_line = { { 3, 13 }, { 5, 20 } };
            const std::map<int, std::vector<Tag>> tags = {
                { 3, { { "#EXT-X-CUE-OUT:2.000", 14 } } },
                { 4, { { "#EXT-X-CUE-OUT-CONT:1.000/2.000", 17 } } },
                { 5, { { "#EXT-X-CUE-IN", 21 } } },
            };
            for (int s = 0; s < 8; ++s) {
                INFO("segment " << s);
                const auto k = static_cast<std::size_t>(s);
                const SegmentLayout& seg = a.segments[k];
                const bool ad = s == 3 || s == 4;
                CHECK(seg.index == s);
                CHECK(seg.file == files[k]);
                CHECK(seg.period == (ad ? "ad" : "main"));
                CHECK(seg.first_frame == 25 * s);
                CHECK(seg.source_first == source[k]);
                CHECK(seg.pts_first == (ad ? origin + 90000 * (s - 3) : 900000 + 90000 * s));
                CHECK(seg.sequence == sequence[k]);
                CHECK(seg.discontinuity == discontinuity_line.contains(s));
                CHECK(seg.discontinuity_line ==
                      (discontinuity_line.contains(s) ? discontinuity_line.at(s) : 0));
                CHECK(seg.tags == (tags.contains(s) ? tags.at(s) : no_tags));
                CHECK(std::pair{ seg.extinf_line, seg.uri_line } == extinf_uri[k]);
                CHECK(seg.q == (ad ? std::array{ qn, qn, ql } : std::array{ qn, qn, qn }));
            }

            // The PTS of the 200 frames: the main content's are the same in
            // the three arrangements; at the splices they jump back by 2.96 s
            // and on by 3.04 s with the ad from the main origin, so that 50
            // PTS name two frames each (design-evaluation run 09), back by
            // 11.96 s and on by 12.04 s with the ad from 1 s (testgen.md §3.5),
            // and not at all when continuous.
            const std::vector<std::int64_t> pts = frame_pts(a);
            REQUIRE(pts.size() == 200);
            for (int f = 0; f < 200; ++f)
                if (f < 75 || f >= 125) CHECK(pts[static_cast<std::size_t>(f)] == 900000 + 3600 * f);
            const std::int64_t at_out = pts[75] - pts[74], at_in = pts[125] - pts[124];
            const std::size_t distinct = std::set<std::int64_t>(pts.begin(), pts.end()).size();
            switch (arrangement) {
            case AdTimestamps::MainOrigin:
                CHECK(at_out == -266400);
                CHECK(at_in == 273600);
                CHECK(distinct == 150);
                break;
            case AdTimestamps::FromOneSecond:
                CHECK(at_out == -1076400);
                CHECK(at_in == 1083600);
                CHECK(distinct == 200);
                break;
            case AdTimestamps::Continuous:
                CHECK(at_out == 3600);
                CHECK(at_in == 3600);
                for (int f = 75; f < 125; ++f) CHECK(pts[static_cast<std::size_t>(f)] == 900000 + 3600 * f);
                break;
            }
        }
    }
}

TEST_CASE("synthladder::media_playlist and master_playlist: the text testgen wrote",
          "[timeline][synthladder]")
{
    for (const Encoder enc : { Encoder::X264, Encoder::Mpeg2 }) {
        CHECK(synthladder::media_playlist(synthladder::plan({ .kind = Kind::Degraded, .encoder = enc })) ==
              kDegradedPlaylist);
        for (const AdTimestamps a :
             { AdTimestamps::MainOrigin, AdTimestamps::FromOneSecond, AdTimestamps::Continuous })
            CHECK(synthladder::media_playlist(
                      synthladder::plan({ .kind = Kind::Ad, .ad = a, .encoder = enc })) == kAdPlaylist);
    }

    // With the BANDWIDTHs and CODECS testgen measured on its ad ladder.
    Layout x = synthladder::plan({ .kind = Kind::Ad, .encoder = Encoder::X264 });
    const std::array<std::int64_t, 3> x_peak = { 613632, 565504, 345920 };
    const std::array<std::int64_t, 3> x_average = { 580920, 525836, 274668 };
    const std::array<const char*, 3> x_codecs = { "avc1.64001e", "avc1.640015", "avc1.64000c" };
    for (std::size_t r = 0; r < 3; ++r) {
        x.rungs[r].bandwidth         = x_peak[r];
        x.rungs[r].average_bandwidth = x_average[r];
        x.rungs[r].codecs            = x_codecs[r];
    }
    CHECK(synthladder::master_playlist(x) == kAdX264Master);
    Layout m = synthladder::plan({ .kind = Kind::Ad, .encoder = Encoder::Mpeg2 });
    const std::array<std::int64_t, 3> m_peak = { 1069344, 971584, 585056 };
    const std::array<std::int64_t, 3> m_average = { 1028172, 918380, 463984 };
    for (std::size_t r = 0; r < 3; ++r) {
        m.rungs[r].bandwidth         = m_peak[r];
        m.rungs[r].average_bandwidth = m_average[r];
    }
    CHECK(synthladder::master_playlist(m) == kAdMpeg2Master);
}

TEST_CASE("synthladder::bandwidths and rung_order_error: BANDWIDTH from the segments' sizes, the top rung's "
          "the highest",
          "[timeline][synthladder]")
{
    using Rates = std::pair<std::int64_t, std::int64_t>;
    // The sizes testgen measured and the BANDWIDTH and AVERAGE-BANDWIDTH it
    // wrote for them (eval/phase3-survey/testgen/logs/: v360 and v270 in
    // 10_degraded_x264_gen_a.txt, v180 in 40_ad_x264_gen_a.txt): the peak
    // and the mean bit rate of 1 s segments.
    CHECK(synthladder::bandwidths({ 75388, 73320, 72568, 73696, 78020, 75764, 76704, 70876 }) ==
          Rates{ 624160, 596336 });
    CHECK(synthladder::bandwidths({ 66552, 66176, 63356, 63356, 13348, 66176, 70688, 63544 }) ==
          Rates{ 565504, 473196 });
    CHECK(synthladder::bandwidths({ 42864, 41172, 40796, 12032, 12408, 43240, 42864, 39292 }) ==
          Rates{ 345920, 274668 });
    // Rounded up to whole bits per second: 40 bits in 3 s.
    CHECK(synthladder::bandwidths({ 1, 2, 2 }) == Rates{ 16, 14 });
    CHECK(synthladder::bandwidths({}) == Rates{ 0, 0 });

    std::vector<synthladder::RungLayout> rungs = synthladder::plan({}).rungs;
    const auto with = [&](std::int64_t a, std::int64_t b, std::int64_t c) {
        rungs[0].bandwidth = a;
        rungs[1].bandwidth = b;
        rungs[2].bandwidth = c;
        return synthladder::rung_order_error(rungs);
    };
    CHECK(with(624160, 565504, 345920).empty());
    // The ramp source's ladder, which testgen refused (testgen.md §2.2).
    CHECK(with(169952, 234624, 166944) == "BANDWIDTH of v270 (234624) isn't below that of v360 (169952)");
    // Two rungs with one BANDWIDTH: neither is the top rung alone.
    CHECK(with(565504, 565504, 345920) == "BANDWIDTH of v270 (565504) isn't below that of v360 (565504)");
    CHECK(with(624160, 345920, 345920) == "BANDWIDTH of v180 (345920) isn't below that of v270 (345920)");
}

TEST_CASE("synthladder::layout_json: testgen's fields, the playlist lines, the encoder and FFmpeg's versions",
          "[timeline][synthladder]")
{
    const std::string a =
        synthladder::layout_json(synthladder::plan({ .kind = Kind::Ad, .ad = AdTimestamps::FromOneSecond,
                                                     .encoder = Encoder::X264 }));
    const auto version = [](unsigned v) {
        return std::format("{}.{}.{}", AV_VERSION_MAJOR(v), AV_VERSION_MINOR(v), AV_VERSION_MICRO(v));
    };
    const std::string libraries = std::format(
        "  \"ffmpeg_libraries\": {{\"libavutil\": \"{}\", \"libavcodec\": \"{}\", "
        "\"libavformat\": \"{}\", \"libswscale\": \"{}\"}},\n",
        version(avutil_version()), version(avcodec_version()), version(avformat_version()),
        version(swscale_version()));
    CHECK(a.starts_with("{\n"
                        "  \"kind\": \"ad\",\n"
                        "  \"codec\": \"libx264\",\n"
                        "  \"source\": \"noise\",\n"
                        "  \"fps\": 25,\n"
                        "  \"segment_frames\": 25,\n"
                        "  \"ts_discontinuity_indicator\": true,\n"
                        "  \"x264_sei\": \"\",\n" +
                        libraries +
                        "  \"ad\": {\"rung\": \"v180\", \"control\": \"v270\", \"segments\": [3, 4], "
                        "\"cue_out_segment\": 3, \"cue_in_segment\": 5, \"duration_s\": 2, "
                        "\"timestamps\": \"from-1s\", \"ad_origin_pts\": 90000, \"content\": \"calm\", "
                        "\"q_normal\": 22, \"q_low\": 46},\n"
                        "  \"rungs\": [\n"
                        "    {\"name\": \"v360\", \"width\": 640, \"height\": 360, \"uri\": \"v360/index.m3u8\", "
                        "\"bandwidth\": 0, \"average_bandwidth\": 0, \"codecs\": \"\", \"bytes\": 0},\n"));
    CHECK(contains(a, "\n    {\"index\": 3, \"file\": \"ad_000.ts\", \"period\": \"ad\", \"first_frame\": 75, "
                      "\"frames\": 25, \"position_s\": 3, \"pts_first\": 90000, \"pts_first_s\": 1, "
                      "\"source_first\": 0, \"discontinuity\": true, \"discontinuity_sequence\": 1, "
                      "\"discontinuity_line\": 13, \"tags\": [\"#EXT-X-CUE-OUT:2.000\"], \"tag_lines\": [14], "
                      "\"extinf_line\": 15, \"uri_line\": 16, \"q\": {\"v360\": 22, \"v270\": 22, \"v180\": 46}, "
                      "\"bytes\": {\"v360\": 0, \"v270\": 0, \"v180\": 0}},\n"));
    CHECK(a.ends_with("\n    {\"index\": 7, \"file\": \"main_007.ts\", \"period\": \"main\", \"first_frame\": 175, "
                      "\"frames\": 25, \"position_s\": 7, \"pts_first\": 1530000, \"pts_first_s\": 17, "
                      "\"source_first\": 175, \"discontinuity\": false, \"discontinuity_sequence\": 2, "
                      "\"discontinuity_line\": null, \"tags\": [], \"tag_lines\": [], \"extinf_line\": 26, "
                      "\"uri_line\": 27, \"q\": {\"v360\": 22, \"v270\": 22, \"v180\": 22}, "
                      "\"bytes\": {\"v360\": 0, \"v270\": 0, \"v180\": 0}}\n  ]\n}\n"));

    // The degraded ladder: its own object, and no TS indicator, which only a
    // splice has.
    const std::string d =
        synthladder::layout_json(synthladder::plan({ .kind = Kind::Degraded, .encoder = Encoder::Mpeg2 }));
    CHECK(d.starts_with("{\n  \"kind\": \"degraded\",\n  \"codec\": \"mpeg2video\",\n  \"source\": \"noise\",\n"
                        "  \"fps\": 25,\n  \"segment_frames\": 25,\n  \"x264_sei\": \"\",\n" +
                        libraries +
                        "  \"degraded\": {\"rung\": \"v270\", \"control\": \"v180\", \"segment\": 4, "
                        "\"q_normal\": 4, \"q_low\": 31},\n"));
    CHECK(contains(d, "\n    {\"index\": 4, \"file\": \"seg_004.ts\", \"period\": \"main\", \"first_frame\": 100, "
                      "\"frames\": 25, \"position_s\": 4, \"pts_first\": 1260000, \"pts_first_s\": 14, "
                      "\"source_first\": 100, \"discontinuity\": false, \"discontinuity_sequence\": 0, "
                      "\"discontinuity_line\": null, \"tags\": [], \"tag_lines\": [], \"extinf_line\": 14, "
                      "\"uri_line\": 15, \"q\": {\"v360\": 4, \"v270\": 31, \"v180\": 4}, "
                      "\"bytes\": {\"v360\": 0, \"v270\": 0, \"v180\": 0}},\n"));
}

TEST_CASE("synthladder::media_dir: the process's temporary directory, or one per test case under "
          "MIRAGE_KEEP_MEDIA",
          "[timeline][synthladder]")
{
    CHECK(synthladder::dir_name("a b:c/d,e's") == "a_b_c_d_e_s");
    CHECK(synthladder::dir_name("x.y-z_09AZ") == "x.y-z_09AZ");
    CHECK(synthladder::dir_name("\xc3\xa9t\xc3\xa9") == "__t__");   // UTF-8 bytes, one '_' each
    CHECK(synthladder::dir_name("") == "_");
    CHECK(synthladder::dir_name(".") == "_.");
    CHECK(synthladder::dir_name("..") == "_..");
    CHECK(synthladder::dir_name(".a") == ".a");

    {
        const EnvVar unset("MIRAGE_KEEP_MEDIA", std::nullopt);
        CHECK(synthladder::media_dir() == testtemp::dir());
    }
    {
        const EnvVar empty("MIRAGE_KEEP_MEDIA", "");
        CHECK(synthladder::media_dir() == testtemp::dir());
    }
    const fs::path keep = testtemp::path("mirage_test_keep_media");
    const EnvVar set("MIRAGE_KEEP_MEDIA", keep.string());
    const fs::path dir = synthladder::media_dir();
    CHECK(dir == keep / "synthladder__media_dir__the_process_s_temporary_directory__or_one_per_test_case_under_"
                        "MIRAGE_KEEP_MEDIA");
    CHECK(fs::is_directory(dir));
    fs::remove_all(keep);
}

TEST_CASE("synthladder::write_ladder: a directory that isn't empty, and one it can't make",
          "[timeline][synthladder]")
{
    const int before = synthladder::made();
    const fs::path full = testtemp::path("mirage_test_ladder_not_empty");
    fs::create_directories(full);
    std::ofstream(full / "kept.txt") << "kept\n";
    const Ladder a = synthladder::write_ladder(full, {});
    CHECK(a.error == full.string() + " is not empty: a ladder is written into a new or empty directory");
    CHECK(std::distance(fs::directory_iterator(full), fs::directory_iterator()) == 1);
    CHECK(read_file(full / "kept.txt") == "kept\n");

    const fs::path file = testtemp::path("mirage_test_ladder_file");
    std::ofstream(file) << "a file\n";
    const Ladder b = synthladder::write_ladder(file / "ladder", {});
    CHECK(b.error.starts_with("cannot create " + (file / "ladder" / "v360").string() + ": "));
    CHECK(synthladder::made() == before);
    fs::remove_all(full);
    fs::remove(file);
}

// ── With media ───────────────────────────────────────────────────────────────

TEST_CASE("synthladder: one ladder made twice in one process is byte-identical, and is what its layout says",
          "[timeline][synthladder]")
{
    // The ad ladder of acceptance test 2 proper (D-080), made once per
    // process (P-Q) and once more beside it.
    const Spec spec{ .kind = Kind::Ad, .ad = AdTimestamps::MainOrigin };
    const int before = synthladder::made();
    const Ladder& first = synthladder::ladder(spec);
    REQUIRE(first.error.empty());
    CHECK(first.dir == synthladder::media_dir() / "ad-main-origin");
    CHECK(first.master == (first.dir / "master.m3u8").string());
    CHECK(synthladder::made() - before <= 1);   // made now, or by a test case before this one
    const int made = synthladder::made();
    CHECK(&synthladder::ladder(spec) == &first);
    CHECK(synthladder::made() == made);

    const Ladder again = synthladder::write_ladder(synthladder::media_dir() / "ad-main-origin-again", spec);
    REQUIRE(again.error.empty());
    CHECK(synthladder::made() == made + 1);

    // The same 29 files with the same bytes, as testgen's two generations of
    // each ladder had (testgen.md §3.3).
    const std::map<std::string, std::string> a = tree(first.dir), b = tree(again.dir);
    CHECK(a.size() == 29);
    std::vector<std::string> names_a, names_b;
    for (const auto& [path, bytes] : a) names_a.push_back(path);
    for (const auto& [path, bytes] : b) names_b.push_back(path);
    CHECK(names_a == names_b);
    for (const auto& [path, bytes] : a) {
        INFO(path);
        REQUIRE(b.contains(path));
        CHECK(b.at(path).size() == bytes.size());
        CHECK((b.at(path) == bytes));
    }

    check_media(first);
    WARN(std::format("synthladder: {} made with {} in {:.3f} s ({:.3f} s of it the source's pictures), "
                     "and again in {:.3f} s ({:.3f} s)",
                     synthladder::name(spec), synthladder::encoder_name(first.layout.encoder), first.seconds,
                     first.source_seconds, again.seconds, again.source_seconds));
}

TEST_CASE("synthladder: the degraded ladder is what its layout says", "[timeline][synthladder]")
{
    check_made({ .kind = Kind::Degraded });
}

TEST_CASE("synthladder: the ad from 1 s is what its layout says", "[timeline][synthladder]")
{
    check_made({ .kind = Kind::Ad, .ad = AdTimestamps::FromOneSecond });
}

TEST_CASE("synthladder: the continuous ad is what its layout says", "[timeline][synthladder]")
{
    check_made({ .kind = Kind::Ad, .ad = AdTimestamps::Continuous });
}

TEST_CASE("synthladder: a ladder made with the other encoder is what its layout says",
          "[timeline][synthladder]")
{
    const Spec spec{ .kind = Kind::Degraded, .encoder = other_encoder() };
    const fs::path dir = synthladder::media_dir() / synthladder::name(spec);
    if (spec.encoder == Encoder::X264) {   // a build without libx264, whose ladders are MPEG-2's
        const Ladder l = synthladder::write_ladder(dir, spec);
        CHECK(l.error == "v360 seg_000.ts: this FFmpeg build has no encoder libx264");
        return;
    }
    fs::create_directories(dir);   // an empty directory is written into
    const Ladder l = synthladder::write_ladder(dir, spec);
    check_media(l);
    WARN(std::format("synthladder: {} made with {} in {:.3f} s ({:.3f} s of it the source's pictures)",
                     synthladder::name(spec), synthladder::encoder_name(l.layout.encoder), l.seconds,
                     l.source_seconds));
}
