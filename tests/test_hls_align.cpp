#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "HlsAlign.h"

#include <cstring>
#include <format>
#include <initializer_list>
#include <string>
#include <vector>

using Catch::Approx;
using namespace hls;

namespace {

// ── Playlist builders ────────────────────────────────────────────────────────

// A VOD media playlist of `n` segments of `dur` s, with optional
// discontinuities before the listed (0-based) segment indices.
std::string vod(int n, double dur, std::vector<int> disc_before = {}, const char* name = "seg",
                const std::string& extra_header = {})
{
    std::string s = std::format("#EXTM3U\n#EXT-X-VERSION:6\n#EXT-X-TARGETDURATION:{}\n#EXT-X-PLAYLIST-TYPE:VOD\n{}",
                                static_cast<int>(std::ceil(dur)), extra_header);
    for (int i = 0; i < n; ++i) {
        for (int d : disc_before) if (d == i) s += "#EXT-X-DISCONTINUITY\n";
        s += std::format("#EXTINF:{:.3f},\n{}{}.ts\n", dur, name, i);
    }
    return s + "#EXT-X-ENDLIST\n";
}

// ── Binary builders ──────────────────────────────────────────────────────────

using Bytes = std::vector<std::uint8_t>;

void put8(Bytes& b, std::uint32_t v) { b.push_back(static_cast<std::uint8_t>(v)); }
void put16(Bytes& b, std::uint32_t v) { put8(b, v >> 8); put8(b, v); }
void put24(Bytes& b, std::uint32_t v) { put8(b, v >> 16); put16(b, v); }
void put32(Bytes& b, std::uint32_t v) { put16(b, v >> 16); put16(b, v); }
void put64(Bytes& b, std::uint64_t v) { put32(b, static_cast<std::uint32_t>(v >> 32)); put32(b, static_cast<std::uint32_t>(v)); }
void append(Bytes& b, const Bytes& more) { b.insert(b.end(), more.begin(), more.end()); }
void append(Bytes& b, const char* s) { b.insert(b.end(), s, s + std::strlen(s)); }

Bytes cat(std::initializer_list<Bytes> parts)
{
    Bytes out;
    for (const auto& p : parts) append(out, p);
    return out;
}

Bytes box(const char* type, const Bytes& body)
{
    Bytes b;
    put32(b, static_cast<std::uint32_t>(8 + body.size()));
    append(b, type);
    append(b, body);
    return b;
}

Bytes full_box(const char* type, int version, std::uint32_t flags, const Bytes& body)
{
    Bytes h;
    put8(h, static_cast<std::uint32_t>(version));
    put24(h, flags);
    append(h, body);
    return box(type, h);
}

Bytes zeros(std::size_t n) { return Bytes(n, 0); }

// An init segment: track 1 video (90 kHz, edit list media_time 3000 = two
// 60 fps frames of B-frame delay), track 2 audio (48 kHz, 2112 samples of
// priming in the edit list).
Bytes make_init()
{
    auto trak = [](std::uint32_t id, std::uint32_t ts, const char* handler, const char* fmt, std::int32_t media_time) {
        Bytes tkhd_body;
        put32(tkhd_body, 0); put32(tkhd_body, 0); put32(tkhd_body, id);
        append(tkhd_body, zeros(72));
        Bytes elst_body;
        put32(elst_body, 1);                      // one edit
        put32(elst_body, 0);                      // segment duration (movie timescale)
        put32(elst_body, static_cast<std::uint32_t>(media_time));
        put32(elst_body, 0x00010000);
        Bytes mdhd_body;
        put32(mdhd_body, 0); put32(mdhd_body, 0); put32(mdhd_body, ts); put32(mdhd_body, 0);
        append(mdhd_body, zeros(4));
        Bytes hdlr_body;
        put32(hdlr_body, 0);
        append(hdlr_body, handler);
        append(hdlr_body, zeros(13));
        Bytes entry = box(fmt, zeros(16));
        Bytes stsd_body;
        put32(stsd_body, 1);
        append(stsd_body, entry);
        return box("trak", cat({
            full_box("tkhd", 0, 3, tkhd_body),
            box("edts", full_box("elst", 0, 0, elst_body)),
            box("mdia", cat({ full_box("mdhd", 0, 0, mdhd_body), full_box("hdlr", 0, 0, hdlr_body),
                              box("minf", box("stbl", full_box("stsd", 0, 0, stsd_body))) })),
        }));
    };
    Bytes mvhd_body;
    put32(mvhd_body, 0); put32(mvhd_body, 0); put32(mvhd_body, 1000); put32(mvhd_body, 0);
    append(mvhd_body, zeros(80));
    Bytes trex1, trex2;
    put32(trex1, 1); put32(trex1, 1); put32(trex1, 1500); put32(trex1, 0); put32(trex1, 0);
    put32(trex2, 2); put32(trex2, 1); put32(trex2, 1024); put32(trex2, 0); put32(trex2, 0);
    Bytes ftyp;
    append(ftyp, "iso6");
    put32(ftyp, 0);
    return cat({ box("ftyp", ftyp),
                 box("moov", cat({ full_box("mvhd", 0, 0, mvhd_body), trak(1, 90000, "vide", "avc1", 3000),
                                   trak(2, 48000, "soun", "mp4a", 2112),
                                   box("mvex", cat({ full_box("trex", 0, 0, trex1), full_box("trex", 0, 0, trex2) })) })) });
}

// A media segment whose video starts (presentation) at 10.0 s + video_off and
// audio at 10.0 s + audio_off, cut short after the moof the way a kept head is.
Bytes make_fmp4_segment(double video_off = 0.0, double audio_off = 0.0)
{
    // Video: decode order I P B B, 1500-tick frames; presentation I B B P.
    Bytes tfhd1; put32(tfhd1, 1);
    Bytes tfdt1; put64(tfdt1, static_cast<std::uint64_t>(900000 + video_off * 90000));
    Bytes trun1;
    put32(trun1, 4);                 // sample count
    put32(trun1, 0);                 // data offset
    const std::uint32_t cts[] = { 3000, 6000, 1500, 1500 };
    for (std::uint32_t c : cts) { put32(trun1, 1500); put32(trun1, 1000); put32(trun1, c); }
    // Audio: default duration 1024 from tfhd, sizes only.
    Bytes tfhd2; put32(tfhd2, 2); put32(tfhd2, 1024);
    Bytes tfdt2; put32(tfdt2, static_cast<std::uint32_t>(480000 + 2112 + audio_off * 48000));
    Bytes trun2;
    put32(trun2, 3);
    for (int i = 0; i < 3; ++i) put32(trun2, 200);
    Bytes mfhd; put32(mfhd, 7);
    Bytes styp;
    append(styp, "msdh");
    put32(styp, 0);
    Bytes seg = cat({
        box("styp", styp),
        box("moof", cat({ full_box("mfhd", 0, 0, mfhd),
                          box("traf", cat({ full_box("tfhd", 0, 0, tfhd1), full_box("tfdt", 1, 0, tfdt1),
                                            full_box("trun", 0, 0x001 | 0x100 | 0x200 | 0x800, trun1) })),
                          box("traf", cat({ full_box("tfhd", 0, 0x08, tfhd2), full_box("tfdt", 0, 0, tfdt2),
                                            full_box("trun", 0, 0x200, trun2) })) })),
    });
    Bytes mdat_head;
    put32(mdat_head, 500000);        // mdat far longer than what was kept
    append(mdat_head, "mdat");
    append(mdat_head, zeros(64));
    append(seg, mdat_head);
    return seg;
}

void put_pes_ts(Bytes& b, std::uint32_t prefix, std::int64_t v)
{
    put8(b, static_cast<std::uint32_t>((prefix << 4) | ((v >> 29) & 0x0E) | 1));
    put8(b, static_cast<std::uint32_t>((v >> 22) & 0xFF));
    put8(b, static_cast<std::uint32_t>(((v >> 14) & 0xFE) | 1));
    put8(b, static_cast<std::uint32_t>((v >> 7) & 0xFF));
    put8(b, static_cast<std::uint32_t>(((v << 1) & 0xFE) | 1));
}

Bytes ts_packet(int pid, bool pusi, const Bytes& payload)
{
    Bytes p;
    put8(p, 0x47);
    put8(p, static_cast<std::uint32_t>((pusi ? 0x40 : 0) | ((pid >> 8) & 0x1f)));
    put8(p, static_cast<std::uint32_t>(pid & 0xff));
    put8(p, 0x10);
    append(p, payload);
    p.resize(188, 0xFF);
    return p;
}

Bytes pes(int stream_id, std::int64_t pts, std::int64_t dts = -1)
{
    Bytes b;
    put8(b, 0); put8(b, 0); put8(b, 1); put8(b, static_cast<std::uint32_t>(stream_id));
    put16(b, 0);
    put8(b, 0x80);
    if (dts >= 0) {
        put8(b, 0xC0); put8(b, 10);
        put_pes_ts(b, 3, pts);
        put_pes_ts(b, 1, dts);
    } else {
        put8(b, 0x80); put8(b, 5);
        put_pes_ts(b, 2, pts);
    }
    append(b, zeros(20));
    return b;
}

// PAT → PMT (PID 0x1000): H.264 on 0x100, AAC on 0x101; then PES headers.
Bytes make_ts_segment(std::int64_t video_pts, std::int64_t audio_pts)
{
    Bytes pat = { 0x00, 0x00, 0xB0, 13, 0x00, 0x01, 0xC1, 0x00, 0x00, 0x00, 0x01, 0xF0, 0x00, 0, 0, 0, 0 };
    Bytes pmt = { 0x00, 0x02, 0xB0, 23, 0x00, 0x01, 0xC1, 0x00, 0x00, 0xE1, 0x00, 0xF0, 0x00,
                  0x1B, 0xE1, 0x00, 0xF0, 0x00, 0x0F, 0xE1, 0x01, 0xF0, 0x00, 0, 0, 0, 0 };
    return cat({ ts_packet(0, true, pat), ts_packet(0x1000, true, pmt),
                 ts_packet(0x100, true, pes(0xE0, video_pts + 3000, video_pts)),
                 ts_packet(0x101, true, pes(0xC0, audio_pts)),
                 ts_packet(0x100, true, pes(0xE0, video_pts + 1500, video_pts + 1500)) });
}

SegmentStart av_start(double video, double audio)
{
    SegmentStart s;
    s.format = SegmentStart::Format::Ts;
    if (!std::isnan(video)) { SegmentStart::T t; t.kind = Track::Video; t.start = video; s.tracks.push_back(t); }
    if (!std::isnan(audio)) { SegmentStart::T t; t.kind = Track::Audio; t.start = audio; s.tracks.push_back(t); }
    return s;
}

RenditionIn rendition(const char* label, Track kind, const std::string& text, bool in_use = true)
{
    RenditionIn r;
    r.label = label;
    r.kind = kind;
    r.in_use = in_use;
    r.tl = parse_media_timeline(text, "https://cdn.example.com/x/prog.m3u8");
    return r;
}

int count(const AlignReport& rep, Severity s)
{
    int n = 0;
    for (const auto& f : rep.findings) n += f.sev == s ? 1 : 0;
    return n;
}

bool has_title(const AlignReport& rep, Severity s, const std::string& needle)
{
    for (const auto& f : rep.findings)
        if (f.sev == s && f.title.find(needle) != std::string::npos) return true;
    return false;
}

} // namespace

// ── Playlist parsing ─────────────────────────────────────────────────────────

TEST_CASE("parse_media_timeline — fMP4 with EXT-X-MAP and byte ranges", "[hls_align][playlist]")
{
    const std::string text =
        "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-VERSION:7\n#EXT-X-MEDIA-SEQUENCE:1\n"
        "#EXT-X-PLAYLIST-TYPE:VOD\n#EXT-X-INDEPENDENT-SEGMENTS\n"
        "#EXT-X-MAP:URI=\"main.mp4\",BYTERANGE=\"720@0\"\n"
        "#EXTINF:6.00000,\n#EXT-X-BYTERANGE:5874288@721\nmain.mp4\n"
        "#EXTINF:6.00000,\n#EXT-X-BYTERANGE:5873264\nmain.mp4\n"
        "#EXTINF:5.50000,\n#EXT-X-BYTERANGE:4000@11748273\nmain.mp4\n"
        "#EXT-X-ENDLIST\n";
    const auto tl = parse_media_timeline(text, "https://cdn.example.com/bipbop/v9/prog_index.m3u8");
    REQUIRE(tl.ok);
    CHECK(tl.problems.empty());
    CHECK(tl.target == 6.0);
    CHECK(tl.version == 7);
    CHECK(tl.media_seq == 1);
    CHECK(tl.type == "VOD");
    CHECK(tl.endlist);
    CHECK_FALSE(tl.live());
    CHECK(tl.independent);
    REQUIRE(tl.segments.size() == 3);
    CHECK(tl.total == Approx(17.5));
    CHECK(tl.segments[0].seq == 1);
    CHECK(tl.segments[2].seq == 3);
    CHECK(tl.segments[1].start == Approx(6.0));
    CHECK(tl.segments[2].start == Approx(12.0));
    CHECK(tl.segments[0].uri == "https://cdn.example.com/bipbop/v9/main.mp4");
    CHECK(tl.segments[0].range.offset == 721);
    CHECK(tl.segments[0].range.length == 5874288);
    CHECK(tl.segments[1].range.offset == 721 + 5874288);   // carried on from the previous range
    CHECK(tl.segments[2].range.offset == 11748273);
    REQUIRE(tl.maps.size() == 1);
    CHECK(tl.maps[0].uri == "https://cdn.example.com/bipbop/v9/main.mp4");
    CHECK(tl.maps[0].range.offset == 0);
    CHECK(tl.maps[0].range.length == 720);
    CHECK(tl.segments[1].map == 0);
    CHECK(tl.segment_at(0.0) == 0);
    CHECK(tl.segment_at(6.0) == 1);
    CHECK(tl.segment_at(17.4) == 2);
    CHECK(tl.segment_at(17.6) == -1);
    CHECK(tl.index_of(2) == 1);
    CHECK(tl.index_of(9) == -1);
}

TEST_CASE("parse_media_timeline — discontinuities, sequence numbers and PROGRAM-DATE-TIME",
          "[hls_align][playlist]")
{
    const std::string text =
        "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-MEDIA-SEQUENCE:100\n#EXT-X-DISCONTINUITY-SEQUENCE:5\n"
        "#EXT-X-PROGRAM-DATE-TIME:2024-03-01T12:00:00.000Z\n"
        "#EXTINF:4.0,\na.ts\n#EXTINF:4.0,\nb.ts\n"
        "#EXT-X-DISCONTINUITY\n#EXT-X-KEY:METHOD=AES-128,URI=\"k.key\"\n#EXTINF:4.0,\nad1.ts\n"
        "#EXT-X-PROGRAM-DATE-TIME:2024-03-01T12:00:20.000+00:00\n#EXTINF:4.0,\nc.ts\n";
    const auto tl = parse_media_timeline(text, "https://h.example/live/v.m3u8");
    REQUIRE(tl.ok);
    CHECK(tl.live());
    CHECK(tl.discontinuities == 1);
    REQUIRE(tl.segments.size() == 4);
    CHECK(tl.segments[0].seq == 100);
    CHECK(tl.segments[0].disc_seq == 5);
    CHECK(tl.segments[1].disc_seq == 5);
    CHECK(tl.segments[2].discontinuity);
    CHECK(tl.segments[2].disc_seq == 6);
    CHECK(tl.segments[2].key == "AES-128");
    const double t0 = parse_iso8601("2024-03-01T12:00:00Z");
    CHECK(tl.segments[0].pdt == Approx(t0));
    CHECK(tl.segments[0].pdt_explicit);
    CHECK(tl.segments[1].pdt == Approx(t0 + 4.0));           // carried on
    CHECK_FALSE(tl.segments[1].pdt_explicit);
    CHECK(std::isnan(tl.segments[2].pdt));                   // not carried across a discontinuity
    CHECK(tl.segments[3].pdt == Approx(t0 + 20.0));
    CHECK(tl.has_pdt());
}

TEST_CASE("parse_media_timeline — spec problems, master playlists, junk", "[hls_align][playlist]")
{
    auto tl = parse_media_timeline("#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXTINF:7.2,\na.ts\n#EXTINF:6.4,\nb.ts\n", "");
    REQUIRE(tl.ok);
    REQUIRE(tl.problems.size() == 1);
    CHECK(tl.problems[0].find("1 segment is longer than EXT-X-TARGETDURATION 6") != std::string::npos);

    tl = parse_media_timeline("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nv.m3u8\n", "");
    CHECK_FALSE(tl.ok);
    CHECK(tl.problems[0].find("master") != std::string::npos);

    tl = parse_media_timeline("<html>not found</html>", "");
    CHECK_FALSE(tl.ok);

    tl = parse_media_timeline("#EXTM3U\n#EXTINF:5,\na.ts\n", "");
    REQUIRE(tl.ok);
    CHECK(tl.problems.back() == "no EXT-X-TARGETDURATION");
}

TEST_CASE("resolve_uri — RFC 3986 references", "[hls_align][uri]")
{
    const std::string base = "https://cdn.example.com/a/b/prog.m3u8?tok=1";
    CHECK(resolve_uri(base, "seg1.ts") == "https://cdn.example.com/a/b/seg1.ts");
    CHECK(resolve_uri(base, "../c/seg1.ts") == "https://cdn.example.com/a/c/seg1.ts");
    CHECK(resolve_uri(base, "./x/./y.ts") == "https://cdn.example.com/a/b/x/y.ts");
    CHECK(resolve_uri(base, "/root/s.ts?q=2") == "https://cdn.example.com/root/s.ts?q=2");
    CHECK(resolve_uri(base, "//other.example/s.ts") == "https://other.example/s.ts");
    CHECK(resolve_uri(base, "http://h2.example/s.ts") == "http://h2.example/s.ts");
    CHECK(resolve_uri(base, "?tok=2") == "https://cdn.example.com/a/b/prog.m3u8?tok=2");
    CHECK(resolve_uri("https://cdn.example.com", "s.ts") == "https://cdn.example.com/s.ts");
    CHECK(resolve_uri("/Users/me/hls/v/prog.m3u8", "../a/s.ts") == "/Users/me/hls/a/s.ts");
    CHECK(resolve_uri("/Users/me/hls/prog.m3u8", "/abs/s.ts") == "/abs/s.ts");
}

TEST_CASE("parse_iso8601", "[hls_align][uri]")
{
    CHECK(parse_iso8601("1970-01-01T00:00:00Z") == 0.0);
    CHECK(parse_iso8601("2024-03-01T12:00:00.250Z") == Approx(1709294400.25));
    CHECK(parse_iso8601("2024-03-01T20:00:00+08:00") == Approx(1709294400.0));
    CHECK(parse_iso8601("2024-03-01T07:00:00-0500") == Approx(1709294400.0));
    CHECK(std::isnan(parse_iso8601("yesterday")));
    CHECK(std::isnan(parse_iso8601("2024-03-01T12:00:00Zjunk")));
}

// ── Segment start timestamps ─────────────────────────────────────────────────

TEST_CASE("parse_init_segment / parse_segment_start — fMP4 tfdt, trun and edit lists", "[hls_align][media]")
{
    const Bytes init = make_init();
    const InitInfo info = parse_init_segment(init.data(), init.size());
    REQUIRE(info.ok);
    CHECK(info.movie_timescale == 1000);
    REQUIRE(info.tracks.size() == 2);
    CHECK(info.tracks[0].id == 1);
    CHECK(info.tracks[0].kind == Track::Video);
    CHECK(info.tracks[0].timescale == 90000);
    CHECK(info.tracks[0].codec == "avc1");
    CHECK(info.tracks[0].edit_shift == 3000);
    CHECK(info.tracks[0].default_duration == 1500);
    CHECK(info.tracks[1].kind == Track::Audio);
    CHECK(info.tracks[1].edit_shift == 2112);
    CHECK(info.tracks[1].default_duration == 1024);

    const Bytes seg = make_fmp4_segment();
    const SegmentStart s = parse_segment_start(seg.data(), seg.size(), &info);
    CHECK(s.format == SegmentStart::Format::Fmp4);
    CHECK(s.error.empty());
    CHECK(s.fragment_seq == 7);
    REQUIRE(s.tracks.size() == 2);
    // Video: tfdt 10.0 s − 2 frames of edit list + the earliest composition
    // (the I frame, 2 frames after its decode time) = 10.0 s.
    CHECK(s.start(Track::Video) == Approx(10.0));
    CHECK(s.tracks[0].decode_start == Approx(10.0 - 3000.0 / 90000.0));
    CHECK(s.tracks[0].note.find("edit list") != std::string::npos);
    CHECK(s.start(Track::Audio) == Approx(10.0));

    // Without the init segment there is no timescale: raw tfdt only.
    const SegmentStart bare = parse_segment_start(seg.data(), seg.size(), nullptr);
    CHECK_FALSE(bare.ok());
    CHECK(bare.error.find("timescale unknown") != std::string::npos);

    // A head that ends inside the moof still yields what was read.
    const SegmentStart cut = parse_segment_start(seg.data(), 140, &info);
    CHECK(cut.format == SegmentStart::Format::Fmp4);
}

TEST_CASE("parse_segment_start — MPEG-TS PAT/PMT and PES timestamps", "[hls_align][media]")
{
    const Bytes ts = make_ts_segment(900000, 899000);
    const SegmentStart s = parse_segment_start(ts.data(), ts.size(), nullptr);
    CHECK(s.format == SegmentStart::Format::Ts);
    REQUIRE(s.tracks.size() == 2);
    const auto* v = s.first(Track::Video);
    const auto* a = s.first(Track::Audio);
    REQUIRE(v);
    REQUIRE(a);
    CHECK(v->id == 0x100);
    CHECK(v->codec == "h264");
    CHECK(v->start == Approx(901500.0 / 90000.0));         // earliest PTS of the two PES
    CHECK(v->decode_start == Approx(10.0));
    CHECK(a->codec == "aac");
    CHECK(a->start == Approx(899000.0 / 90000.0));
    CHECK(v->note.empty());

    // PES before any PAT/PMT: kinds from the stream ids, and a note.
    const Bytes no_psi(ts.begin() + 2 * 188, ts.end());
    const SegmentStart s2 = parse_segment_start(no_psi.data(), no_psi.size(), nullptr);
    REQUIRE(s2.first(Track::Video));
    CHECK(s2.first(Track::Video)->note.find("PMT") != std::string::npos);
    CHECK(s2.start(Track::Audio) == Approx(899000.0 / 90000.0));
}

TEST_CASE("parse_segment_start — packed audio ID3 timestamp, WebVTT X-TIMESTAMP-MAP", "[hls_align][media]")
{
    Bytes id3;
    append(id3, "ID3");
    put8(id3, 4); put8(id3, 0); put8(id3, 0);
    const char owner[] = "com.apple.streaming.transportStreamTimestamp";
    const std::uint32_t frame = static_cast<std::uint32_t>(sizeof(owner) + 8);
    const std::uint32_t tag = 10 + frame;
    put8(id3, (tag >> 21) & 0x7f); put8(id3, (tag >> 14) & 0x7f); put8(id3, (tag >> 7) & 0x7f); put8(id3, tag & 0x7f);
    append(id3, "PRIV");
    put8(id3, 0); put8(id3, 0); put8(id3, (frame >> 7) & 0x7f); put8(id3, frame & 0x7f);
    put16(id3, 0);
    id3.insert(id3.end(), owner, owner + sizeof(owner));
    put64(id3, 900000);
    put8(id3, 0xFF); put8(id3, 0xF1); append(id3, zeros(10));
    const SegmentStart a = parse_segment_start(id3.data(), id3.size(), nullptr);
    CHECK(a.format == SegmentStart::Format::PackedAudio);
    CHECK(a.error.empty());
    CHECK(a.start(Track::Audio) == Approx(10.0));
    CHECK(a.tracks[0].codec == "aac");

    const std::string vtt = "WEBVTT\nX-TIMESTAMP-MAP=MPEGTS:900000,LOCAL:00:00:00.000\n\n"
                            "00:00:01.500 --> 00:00:03.000\nHello\n";
    const SegmentStart t = parse_segment_start(reinterpret_cast<const std::uint8_t*>(vtt.data()), vtt.size(), nullptr);
    CHECK(t.format == SegmentStart::Format::WebVtt);
    REQUIRE(t.first(Track::Text));
    CHECK(t.first(Track::Text)->local_zero == Approx(10.0));
    CHECK(t.start(Track::Text) == Approx(11.5));

    const std::string bare = "WEBVTT\n\n00:01.000 --> 00:02.000\nHi\n";
    const SegmentStart t2 = parse_segment_start(reinterpret_cast<const std::uint8_t*>(bare.data()), bare.size(), nullptr);
    REQUIRE(t2.first(Track::Text));
    CHECK(t2.first(Track::Text)->local_zero == 0.0);
    CHECK(t2.first(Track::Text)->note.find("X-TIMESTAMP-MAP") != std::string::npos);
}

// ── Alignment ────────────────────────────────────────────────────────────────

TEST_CASE("analyze_alignment — aligned audio and video", "[hls_align][analyze]")
{
    std::vector<RenditionIn> in = { rendition("v9", Track::Video, vod(10, 6.0, {}, "v")),
                                    rendition("a1", Track::Audio, vod(10, 6.0, {}, "a")) };
    in[0].starts[0] = av_start(10.0, kNaN);
    in[0].starts[5] = av_start(40.0, kNaN);
    in[1].starts[0] = av_start(kNaN, 10.02);
    const auto rep = analyze_alignment(in);
    CHECK(rep.reference == 0);
    CHECK(rep.errors == 0);
    CHECK(rep.warnings == 0);
    REQUIRE(!rep.findings.empty());
    CHECK(rep.findings.front().sev == Severity::Ok);
    CHECK(rep.findings.front().title == "2 renditions aligned");
    CHECK(rep.rows[1].offset_vs_ref == Approx(0.02));
    CHECK(rep.rows[0].probed == 2);
}

TEST_CASE("analyze_alignment — audio timestamps 2 s off the video's: blank start / lip sync", "[hls_align][analyze]")
{
    std::vector<RenditionIn> in = { rendition("v9", Track::Video, vod(10, 6.0, {}, "v")),
                                    rendition("a1", Track::Audio, vod(10, 6.0, {}, "a")) };
    in[0].starts[0] = av_start(10.0, kNaN);
    in[1].starts[0] = av_start(kNaN, 8.0);       // audio starts 2 s earlier: sound over a black screen
    const auto rep = analyze_alignment(in);
    CHECK(rep.errors == 1);
    CHECK(has_title(rep, Severity::Error, "a1: audio timestamps 2.000 s earlier than v9"));
    CHECK(rep.findings.front().detail.find("black") != std::string::npos);
    CHECK(rep.rows[1].offset_vs_ref == Approx(-2.0));
    CHECK(rep.rows[1].worst == Severity::Error);
}

TEST_CASE("analyze_alignment — discontinuities missing or misplaced in one rendition", "[hls_align][analyze]")
{
    // SSAI: the ad's discontinuities are in the video playlist only.
    std::vector<RenditionIn> in = { rendition("v9", Track::Video, vod(10, 6.0, { 3, 5 }, "v")),
                                    rendition("a1", Track::Audio, vod(10, 6.0, {}, "a")) };
    auto rep = analyze_alignment(in);
    CHECK(has_title(rep, Severity::Error, "a1 has 0 discontinuities, v9 has 2"));

    // Same count, one segment apart.
    in[1] = rendition("a1", Track::Audio, vod(10, 6.0, { 4, 5 }, "a"));
    rep = analyze_alignment(in);
    CHECK(has_title(rep, Severity::Error, "Discontinuity 1 is 6.000 s apart in a1 and v9"));
    CHECK(rep.errors == 1);
}

TEST_CASE("analyze_alignment — durations, timestamp jumps and variant boundaries", "[hls_align][analyze]")
{
    std::vector<RenditionIn> in = { rendition("v9", Track::Video, vod(10, 6.0, {}, "v")),
                                    rendition("a1", Track::Audio, vod(9, 6.0, {}, "a")),
                                    rendition("v5", Track::Video, vod(12, 5.0, {}, "w"), false) };
    in[0].starts[3] = av_start(28.0, kNaN);
    in[0].starts[4] = av_start(37.0, kNaN);      // should be 34.0: a 3 s jump between adjacent segments
    const auto rep = analyze_alignment(in);
    CHECK(has_title(rep, Severity::Error, "v9: timestamps jump 3.000 s at segment #4 without a discontinuity"));
    CHECK(has_title(rep, Severity::Warning, "a1 is 6.000 s shorter than v9"));
    CHECK(has_title(rep, Severity::Warning, "v5 segment boundaries differ from v9"));
    CHECK(has_title(rep, Severity::Info, "a1: media timestamps not read yet"));
    CHECK(rep.rows[2].max_boundary_delta > 0.9);
}

TEST_CASE("analyze_alignment — live windows placed by PROGRAM-DATE-TIME", "[hls_align][analyze]")
{
    auto live = [](int first_seq, const char* first_pdt, const char* name) {
        std::string s = std::format("#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-MEDIA-SEQUENCE:{}\n"
                                    "#EXT-X-PROGRAM-DATE-TIME:{}\n", first_seq, first_pdt);
        for (int i = 0; i < 5; ++i) s += std::format("#EXTINF:6.0,\n{}{}.ts\n", name, first_seq + i);
        return s;
    };
    // Audio's window starts one segment (6 s) later; its timestamps match.
    std::vector<RenditionIn> in = { rendition("v9", Track::Video, live(100, "2024-03-01T12:00:00Z", "v")),
                                    rendition("a1", Track::Audio, live(101, "2024-03-01T12:00:06Z", "a")) };
    in[0].starts[101] = av_start(1006.0, kNaN);  // playlist time 6 → media 1006
    in[1].starts[101] = av_start(kNaN, 1006.0);  // playlist time 0 → media 1006
    auto rep = analyze_alignment(in);
    CHECK(rep.rows[1].shift == Approx(6.0));
    CHECK(rep.errors == 0);
    CHECK(rep.warnings == 0);
    CHECK(rep.rows[1].offset_vs_ref == Approx(0.0));

    // The same windows with audio's media 1.5 s later.
    in[1].starts[101] = av_start(kNaN, 1007.5);
    rep = analyze_alignment(in);
    CHECK(has_title(rep, Severity::Error, "a1: audio timestamps 1.500 s later than v9"));
}

TEST_CASE("analyze_alignment — live windows without dates placed by media sequence number", "[hls_align][analyze]")
{
    auto live = [](int first_seq, const char* name) {
        std::string s = std::format("#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:{}\n", first_seq);
        for (int i = 0; i < 5; ++i) s += std::format("#EXTINF:1.92,\n{}{}.m4s\n", name, first_seq + i);
        return s;
    };
    // Fetched a reload apart: the audio window starts one segment later.
    std::vector<RenditionIn> in = { rendition("v", Track::Video, live(100, "v")),
                                    rendition("a", Track::Audio, live(101, "a")) };
    in[0].starts[102] = av_start(1003.84, kNaN);   // playlist time 3.84 → media 1003.84
    in[1].starts[102] = av_start(kNaN, 1003.84);   // playlist time 1.92 → the same media time
    auto rep = analyze_alignment(in);
    CHECK(rep.rows[1].shift == Approx(1.92));
    CHECK(rep.errors == 0);
    CHECK(rep.warnings == 0);
    CHECK(rep.rows[1].offset_vs_ref == Approx(0.0).margin(1e-9));
    CHECK(has_title(rep, Severity::Info, "placed by media sequence number"));

    // Windows that share no segment: compared from their first segments.
    in[1] = rendition("a", Track::Audio, live(200, "a"));
    rep = analyze_alignment(in);
    CHECK(rep.rows[1].shift == 0.0);
    CHECK(has_title(rep, Severity::Info, "compared from their first segments"));
}

TEST_CASE("format_clock — wall-clock media times as the UTC time of day", "[hls_align]")
{
    CHECK(format_clock(83.5) == "1:23.500");
    CHECK(format_clock(37052.876) == "10:17:32.876");             // MPEG-TS clock: not a date
    CHECK(format_clock(1792183923.84) == "20:52:03.840 UTC");      // seconds since 1970
}

TEST_CASE("analyze_alignment — subtitles and live/VOD mismatch", "[hls_align][analyze]")
{
    std::vector<RenditionIn> in = { rendition("v9", Track::Video, vod(10, 6.0, {}, "v")),
                                    rendition("s1", Track::Text, vod(10, 6.0, {}, "s")) };
    in[0].starts[0] = av_start(10.0, kNaN);
    SegmentStart vtt;
    vtt.format = SegmentStart::Format::WebVtt;
    SegmentStart::T t;
    t.kind = Track::Text;
    t.local_zero = 0.0;        // no X-TIMESTAMP-MAP: cues 10 s early
    vtt.tracks.push_back(t);
    in[1].starts[0] = vtt;
    auto rep = analyze_alignment(in);
    CHECK(has_title(rep, Severity::Error, "s1: subtitles 10.000 s earlier than v9"));

    std::string live = vod(10, 6.0, {}, "a");
    live.erase(live.find("#EXT-X-ENDLIST"));
    live.erase(live.find("#EXT-X-PLAYLIST-TYPE:VOD\n"), 25);
    in[1] = rendition("a1", Track::Audio, live);
    rep = analyze_alignment(in);
    CHECK(has_title(rep, Severity::Error, "a1 is live but v9 is VOD"));
}
