#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "NetworkLogger.h"
#include "TestTemp.h"

using namespace Catch::Matchers;

// ── classify() ────────────────────────────────────────────────────────────────

TEST_CASE("NetworkLogger::classify — DASH manifest (.mpd)", "[network_logger][classify]")
{
    float r, g, b;
    auto type = NetworkLogger::classify("https://cdn.example.com/video.mpd", r, g, b);
    CHECK(type == "manifest");
    CHECK(g > 0.5f);  // greenish
}

TEST_CASE("NetworkLogger::classify — HLS master playlist", "[network_logger][classify]")
{
    float r, g, b;
    auto type = NetworkLogger::classify("https://cdn.example.com/master.m3u8", r, g, b);
    CHECK(type == "manifest");
}

TEST_CASE("NetworkLogger::classify — HLS variant playlist (no master keyword)", "[network_logger][classify]")
{
    float r, g, b;
    auto type = NetworkLogger::classify("https://cdn.example.com/720p/stream.m3u8", r, g, b);
    CHECK(type == "variant");
}

TEST_CASE("NetworkLogger::classify — init segment (.mp4 with init in name)", "[network_logger][classify]")
{
    float r, g, b;
    auto type = NetworkLogger::classify("https://cdn.example.com/v/init-0.mp4", r, g, b);
    CHECK(type == "init");
    CHECK(r > g);  // orangeish (r > g)
}

TEST_CASE("NetworkLogger::classify — media segment (.m4s)", "[network_logger][classify]")
{
    float r, g, b;
    auto type = NetworkLogger::classify("https://cdn.example.com/v/seg_001.m4s", r, g, b);
    CHECK(type == "segment");
}

TEST_CASE("NetworkLogger::classify — media segment (.ts)", "[network_logger][classify]")
{
    float r, g, b;
    auto type = NetworkLogger::classify("https://cdn.example.com/live/0001.ts", r, g, b);
    CHECK(type == "segment");
}

TEST_CASE("NetworkLogger::classify — plain MP4 file", "[network_logger][classify]")
{
    float r, g, b;
    auto type = NetworkLogger::classify("https://cdn.example.com/movie.mp4", r, g, b);
    // mp4 without seg/frag/init keyword → "media" or "other" (not "segment" or "init")
    CHECK((type == "media" || type == "other"));
}

TEST_CASE("NetworkLogger::classify — unknown extension", "[network_logger][classify]")
{
    float r, g, b;
    auto type = NetworkLogger::classify("https://cdn.example.com/data.bin", r, g, b);
    CHECK(type == "other");
}

// ── ingest_line() + get_recent() ─────────────────────────────────────────────

TEST_CASE("NetworkLogger::ingest_line — valid HTTP open line adds entry", "[network_logger][ingest]")
{
    NetworkLogger logger;
    logger.ingest_line("Opening 'https://cdn.example.com/video.mpd' for reading\n");

    auto entries = logger.get_recent(10);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].type == "manifest");
    CHECK_THAT(entries[0].url, ContainsSubstring("video.mpd"));
}

TEST_CASE("NetworkLogger::ingest_line — non-HTTP URL is ignored", "[network_logger][ingest]")
{
    NetworkLogger logger;
    logger.ingest_line("Opening '/local/path/video.mp4' for reading\n");
    CHECK(logger.total_count() == 0);
}

TEST_CASE("NetworkLogger::ingest_line — line without opening pattern is ignored", "[network_logger][ingest]")
{
    NetworkLogger logger;
    logger.ingest_line("Some random FFmpeg log message\n");
    CHECK(logger.total_count() == 0);
}

TEST_CASE("NetworkLogger::ingest_line — multiple entries accumulate", "[network_logger][ingest]")
{
    NetworkLogger logger;
    logger.ingest_line("Opening 'https://cdn.example.com/master.m3u8' for reading\n");
    logger.ingest_line("Opening 'https://cdn.example.com/720p.m3u8' for reading\n");
    logger.ingest_line("Opening 'https://cdn.example.com/init.mp4' for reading\n");
    logger.ingest_line("Opening 'https://cdn.example.com/seg001.m4s' for reading\n");

    CHECK(logger.total_count() == 4);

    auto entries = logger.get_recent(10);
    REQUIRE(entries.size() == 4);
    CHECK(entries[0].type == "manifest");
    CHECK(entries[3].type == "segment");
}

TEST_CASE("NetworkLogger::get_recent — limits results to n", "[network_logger][get_recent]")
{
    NetworkLogger logger;
    for (int i = 0; i < 10; ++i) {
        logger.ingest_line("Opening 'https://cdn.example.com/seg.m4s' for reading\n");
    }
    CHECK(logger.total_count() == 10);

    auto three = logger.get_recent(3);
    CHECK(three.size() == 3);
}

TEST_CASE("NetworkLogger::get_recent — returns oldest-first subset", "[network_logger][get_recent]")
{
    NetworkLogger logger;
    logger.ingest_line("Opening 'https://cdn.example.com/master.m3u8' for reading\n");  // #0
    logger.ingest_line("Opening 'https://cdn.example.com/seg.m4s' for reading\n");      // #1

    // get_recent(1) should return only the most recent entry
    auto one = logger.get_recent(1);
    REQUIRE(one.size() == 1);
    CHECK(one[0].type == "segment");
}

TEST_CASE("NetworkLogger — URL truncated to 60 chars for display", "[network_logger][truncate]")
{
    NetworkLogger logger;
    // Build a URL longer than 60 chars
    std::string long_url = "https://cdn.example.com/";
    long_url += std::string(100, 'a');
    long_url += ".m4s";

    std::string line = "Opening '" + long_url + "' for reading\n";
    logger.ingest_line(line.c_str());

    auto entries = logger.get_recent(1);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].url.size() <= 60);
}

// ── Request model (event API, no FFmpeg I/O) ─────────────────────────────────

#include "NetDescribe.h"
#include "ManifestParser.h"

namespace {
std::vector<NetRequest> all_requests(const NetworkLogger& log)
{
    std::vector<NetRequest> out;
    std::uint64_t oldest = 0;
    log.copy_changed_since(0, out, oldest);
    return out;
}
const char* kReq =
    "request: GET /hls/v9/main.mp4 HTTP/1.1\r\nUser-Agent: Lavf\r\nAccept: */*\r\n"
    "Range: bytes=721-5875008\r\nHost: cdn.example.com\r\n\r\n\n";
} // namespace

TEST_CASE("NetworkLogger events — HTTP open: status, headers, bytes, initiator", "[network_logger][events]")
{
    NetworkLogger log;
    int ctx = 0, pb = 0;                 // stand-ins for URLContext* / AVIOContext*
    std::int64_t counter = 0;

    log.ev_open_begin(NetInitiator::Playback, "https://cdn.example.com/hls/v9/main.mp4");
    log.ev_log(&ctx, kReq);
    log.ev_log(&ctx, "header='HTTP/1.1 206 Partial Content'\n");
    log.ev_log(&ctx, "header='Content-Type: video/mp4'\n");
    log.ev_log(&ctx, "header='Content-Length: 5874288'\n");
    log.ev_log(&ctx, "header=''\n");
    log.ev_open_end(0, &pb, &ctx, &counter, 5874288, "video/mp4", "");
    counter = 5874288;
    log.ev_close(&pb);

    auto reqs = all_requests(log);
    REQUIRE(reqs.size() == 1);
    const auto& r = reqs[0];
    CHECK(r.url == "https://cdn.example.com/hls/v9/main.mp4");
    CHECK(r.protocol == "https");
    CHECK(r.initiator == NetInitiator::Playback);
    CHECK(r.status == 206);
    CHECK(r.status_text == "Partial Content");
    CHECK(r.http_version == "1.1");
    CHECK(r.mime == "video/mp4");
    CHECK(r.content_length == 5874288);
    CHECK(r.range_start == 721);
    CHECK(r.range_end == 5875008);
    CHECK(r.bytes == 5874288);
    CHECK(r.done());
    CHECK(r.t_sent >= r.t_start);
    CHECK(r.t_first_byte >= r.t_sent);
    CHECK(r.t_end >= r.t_first_byte);
    CHECK(r.response_headers.size() == 2);
    CHECK_FALSE(r.failed);
}

TEST_CASE("NetworkLogger events — keep-alive requests on one handle are separate rows", "[network_logger][events]")
{
    NetworkLogger log;
    int ctx = 0, pb = 0;
    std::int64_t counter = 0;

    log.ev_open_begin(NetInitiator::Playback, "https://cdn.example.com/hls/v9/seg1.m4s");
    log.ev_log(&ctx, "request: GET /hls/v9/seg1.m4s HTTP/1.1\r\nHost: cdn.example.com\r\n\r\n");
    log.ev_log(&ctx, "header='HTTP/1.1 200 OK'");
    log.ev_open_end(0, &pb, &ctx, &counter, 1000, "", "");
    counter = 1000;                                   // segment 1 fully read

    // HLS http_persistent: next segment on the same AVIOContext, no io_open.
    log.ev_log(nullptr, "Opening 'https://cdn.example.com/hls/v9/seg2.m4s' for reading\n");
    log.ev_log(&ctx, "request: GET /hls/v9/seg2.m4s HTTP/1.1\r\nHost: cdn.example.com\r\n\r\n");
    log.ev_log(&ctx, "header='HTTP/1.1 200 OK'");
    counter = 1000 + 2500;
    log.ev_close(&pb);

    auto reqs = all_requests(log);
    REQUIRE(reqs.size() == 2);
    CHECK(reqs[0].url == "https://cdn.example.com/hls/v9/seg1.m4s");
    CHECK(reqs[0].bytes == 1000);
    CHECK(reqs[1].url == "https://cdn.example.com/hls/v9/seg2.m4s");
    CHECK(reqs[1].bytes == 2500);
    CHECK(reqs[1].reused_connection);
    CHECK(reqs[1].initiator == NetInitiator::Playback);
    CHECK(reqs[0].conn_id == reqs[1].conn_id);
    CHECK(reqs[1].done());
}

TEST_CASE("NetworkLogger events — HTTP 404 and network failure are failed rows; retry flagged",
          "[network_logger][events]")
{
    NetworkLogger log;
    int ctx = 0;

    log.ev_open_begin(NetInitiator::Playback, "https://cdn.example.com/missing.m3u8");
    log.ev_log(&ctx, "request: GET /missing.m3u8 HTTP/1.1\r\nHost: cdn.example.com\r\n\r\n");
    log.ev_log(&ctx, "header='HTTP/1.1 404 Not Found'");
    log.ev_log(&ctx, "HTTP error 404 Not Found\n");
    log.ev_log(&ctx, "Statistics: 1 connection, 0 requests, 0 retries, 0 reconnections, 0 redirects\n");
    log.ev_open_end(-1, nullptr, nullptr, nullptr, -1, "", "Server returned 404 Not Found");

    // Connection-level failure: no http.c request line at all.
    log.ev_open_begin(NetInitiator::Thumbnails, "https://unreachable.example/x.m3u8");
    log.ev_open_end(-60, nullptr, nullptr, nullptr, -1, "", "Operation timed out");

    // Same URL again → marked as a retry.
    int ctx2 = 0;
    log.ev_open_begin(NetInitiator::Playback, "https://cdn.example.com/missing.m3u8");
    log.ev_log(&ctx2, "request: GET /missing.m3u8 HTTP/1.1\r\nHost: cdn.example.com\r\n\r\n");

    auto reqs = all_requests(log);
    REQUIRE(reqs.size() == 3);
    CHECK(reqs[0].failed);
    CHECK(reqs[0].status == 404);
    CHECK(reqs[0].error == "HTTP error 404 Not Found");
    CHECK(reqs[0].done());
    CHECK(reqs[1].failed);
    CHECK(reqs[1].error == "Operation timed out");
    CHECK(reqs[1].initiator == NetInitiator::Thumbnails);
    CHECK(reqs[2].retry_of_failed);
    CHECK_FALSE(reqs[2].done());
}

TEST_CASE("NetworkLogger events — local file open is one row with its size", "[network_logger][events]")
{
    NetworkLogger log;
    int pb = 0;
    std::int64_t counter = 0;
    log.ev_open_begin(NetInitiator::Playback, "/Users/me/movie.mkv");
    log.ev_open_end(0, &pb, nullptr, &counter, 123456, "", "");
    counter = 4096;
    log.ev_close(&pb);
    auto reqs = all_requests(log);
    REQUIRE(reqs.size() == 1);
    CHECK(reqs[0].protocol == "file");
    CHECK(reqs[0].status == 0);
    CHECK(reqs[0].content_length == 123456);
    CHECK(reqs[0].bytes == 4096);
    CHECK(reqs[0].done());                  // done at open, not "pending" for the whole playback
    CHECK(net_request_name(reqs[0].url) == "movie.mkv");
}

TEST_CASE("NetworkLogger poll — an open local file shows what was read, not its size", "[network_logger][events]")
{
    // The sidebar's Size column and its "read from disk" total: a file the
    // player has read 5 s of is not "16.7 MB".
    NetworkLogger log;
    int pb = 0, pb2 = 0;
    std::int64_t counter = 0, counter2 = 0;
    log.ev_open_begin(NetInitiator::Playback, "/Users/me/movie.mkv");
    log.ev_open_end(0, &pb, nullptr, &counter, 16'700'000, "", "");
    auto reqs = all_requests(log);
    REQUIRE(reqs.size() == 1);
    CHECK(reqs[0].effective_bytes() == 0);   // nothing read yet
    counter = 300'000;
    log.poll();
    counter = 900'000;
    log.poll();
    reqs = all_requests(log);
    CHECK(reqs[0].bytes == 900'000);
    CHECK(reqs[0].done());
    CHECK(log.progress().bytes == 900'000);
    // A ranged read (an HLS byte-range segment in a local file) counts its range only.
    log.ev_open_begin(NetInitiator::Playback, "/Users/me/media.mp4", 1000, 1999);
    log.ev_open_end(0, &pb2, nullptr, &counter2, 50'000, "", "");
    counter2 = 32'768;   // the buffer reads past the range
    log.poll();
    log.ev_close(&pb);
    log.ev_close(&pb2);
    reqs = all_requests(log);
    REQUIRE(reqs.size() == 2);
    CHECK(reqs[0].bytes == 900'000);
    CHECK(reqs[1].bytes == 1000);
}

TEST_CASE("NetworkLogger poll — body completion, partial (idle) rows and resume", "[network_logger][events]")
{
    NetworkLogger log;
    int ctx = 0, pb = 0, ctx2 = 0, pb2 = 0;
    std::int64_t counter = 0, counter2 = 0;

    // A: Content-Length 1000, fully read → done at the last byte, no next request needed.
    log.ev_open_begin(NetInitiator::Playback, "https://cdn.example.com/hls/v9/seg1.m4s");
    log.ev_log(&ctx, "request: GET /hls/v9/seg1.m4s HTTP/1.1\r\nHost: cdn.example.com\r\n\r\n");
    log.ev_log(&ctx, "header='HTTP/1.1 200 OK'");
    log.ev_log(&ctx, "header='Content-Length: 1000'");
    log.ev_open_end(0, &pb, &ctx, &counter, 1000, "", "");
    // B: a probe segment the player stops reading (variant discarded).
    log.ev_open_begin(NetInitiator::Playback, "https://cdn.example.com/hls/v8/seg1.m4s");
    log.ev_log(&ctx2, "request: GET /hls/v8/seg1.m4s HTTP/1.1\r\nHost: cdn.example.com\r\n\r\n");
    log.ev_log(&ctx2, "header='HTTP/1.1 200 OK'");
    log.ev_log(&ctx2, "header='Content-Length: 5000'");
    log.ev_open_end(0, &pb2, &ctx2, &counter2, 5000, "", "");

    const std::int64_t t0 = NetworkLogger::now_us();
    counter = 400;  counter2 = 300;
    log.poll(t0 + 10'000);
    auto reqs = all_requests(log);
    REQUIRE(reqs.size() == 2);
    CHECK(reqs[0].bytes == 400);            // live progress
    CHECK_FALSE(reqs[0].done());
    counter = 1000;
    log.poll(t0 + 50'000);
    reqs = all_requests(log);
    CHECK(reqs[0].done());
    CHECK(reqs[0].t_end == t0 + 50'000);
    CHECK(reqs[0].bytes == 1000);
    CHECK_FALSE(reqs[0].partial);
    CHECK_FALSE(reqs[1].done());

    // No bytes for kIdleUs → partial, ended at the last byte seen.
    log.poll(t0 + 10'000 + NetworkLogger::kIdleUs);
    reqs = all_requests(log);
    CHECK(reqs[1].done());
    CHECK(reqs[1].partial);
    CHECK(reqs[1].t_end == t0 + 10'000);
    CHECK(reqs[1].bytes == 300);

    // Reading resumes → in flight again; then the handle closes.
    counter2 = 900;
    log.poll(t0 + 3'000'000);
    reqs = all_requests(log);
    CHECK_FALSE(reqs[1].done());
    CHECK_FALSE(reqs[1].partial);
    CHECK(reqs[1].bytes == 900);

    // avio_close() logs "Statistics:" with the AVIOContext itself: the handle
    // is forgotten (poll must not touch it afterwards) and the row finished.
    counter2 = 950;
    log.ev_log(&pb2, "Statistics: 950 bytes read, 0 seeks\n");
    reqs = all_requests(log);
    CHECK(reqs[1].done());
    CHECK(reqs[1].bytes == 950);
    counter2 = 12345;
    log.poll(t0 + 9'000'000);
    CHECK(all_requests(log)[1].bytes == 950);
}

TEST_CASE("NetworkLogger — bounded, change tracking, clear and pause", "[network_logger][events]")
{
    NetworkLogger log;
    int ctx = 0;
    for (std::size_t i = 0; i < NetworkLogger::kMaxRequests + 50; ++i)
        log.ev_log(&ctx, "request: GET /a.ts HTTP/1.1\r\nHost: h\r\n\r\n");
    CHECK(log.size() == NetworkLogger::kMaxRequests);

    const auto seq = log.change_seq();
    std::vector<NetRequest> changed;
    std::uint64_t oldest = 0;
    log.copy_changed_since(seq, changed, oldest);
    CHECK(changed.empty());
    CHECK(oldest == 51);      // ids start at 1; the first 50 were evicted

    log.ev_log(&ctx, "header='HTTP/1.1 200 OK'");
    log.copy_changed_since(seq, changed, oldest);
    REQUIRE(changed.size() == 1);
    CHECK(changed[0].status == 200);

    const auto gen = log.generation();
    log.clear();
    CHECK(log.size() == 0);
    CHECK(log.generation() != gen);

    log.set_recording(false);
    log.ev_log(&ctx, "request: GET /b.ts HTTP/1.1\r\nHost: h\r\n\r\n");
    CHECK(log.size() == 0);
}

// ── NetDescribe ──────────────────────────────────────────────────────────────

TEST_CASE("net_describe — HLS kinds and renditions from the master playlist", "[network_logger][describe]")
{
    const std::string base = "https://cdn.example.com/bipbop/master.m3u8";
    ManifestParser mp;
    REQUIRE(mp.parse_hls_text(
        "#EXTM3U\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud1\",LANGUAGE=\"en\",NAME=\"English\",DEFAULT=YES,CHANNELS=\"2\",URI=\"a1/prog_index.m3u8\"\n"
        "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"sub1\",LANGUAGE=\"en\",NAME=\"English\",URI=\"s1/en/prog_index.m3u8\"\n"
        "#EXT-X-STREAM-INF:AVERAGE-BANDWIDTH=7968416,BANDWIDTH=8001098,CODECS=\"avc1.64002a,mp4a.40.2\",RESOLUTION=1920x1080,FRAME-RATE=60.000,AUDIO=\"aud1\",SUBTITLES=\"sub1\"\n"
        "v9/prog_index.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=541052,RESOLUTION=480x270,FRAME-RATE=30.000,AUDIO=\"aud1\"\n"
        "v2/prog_index.m3u8\n", base));

    REQUIRE(mp.variants().size() == 2);
    CHECK(mp.variants()[0].bandwidth == 8001098);          // not AVERAGE-BANDWIDTH
    CHECK(mp.variants()[0].avg_bandwidth == 7968416);
    CHECK(mp.variants()[0].frame_rate == 60.0);
    CHECK(mp.variants()[0].audio_group == "aud1");
    REQUIRE(mp.renditions().size() == 2);
    CHECK(mp.renditions()[0].type == "AUDIO");
    CHECK(mp.renditions()[0].url == "https://cdn.example.com/bipbop/a1/prog_index.m3u8");
    CHECK(mp.renditions()[0].is_default);
    CHECK(mp.tags().empty());                               // EXT-X-MEDIA is not a timeline tag

    const NetCatalog cat = NetCatalog::build(base, mp.variants(), mp.renditions());
    auto desc = [&](const std::string& url, std::int64_t rs = -1, std::int64_t re = -1) {
        NetRequest r;
        r.url = url;
        r.protocol = "https";
        r.range_start = rs;
        r.range_end = re;
        return net_describe(r, cat);
    };

    auto d = desc(base);
    CHECK(d.kind == NetKind::Manifest);

    d = desc("https://cdn.example.com/bipbop/v9/prog_index.m3u8");
    CHECK(d.kind == NetKind::Playlist);
    CHECK(d.media == NetMedia::Video);
    CHECK(d.rendition == "v9 1080p60");

    d = desc("https://cdn.example.com/bipbop/v9/main.mp4", 0, 720);       // EXT-X-MAP byterange
    CHECK(d.kind == NetKind::Init);
    CHECK(d.rendition == "v9 1080p60");

    d = desc("https://cdn.example.com/bipbop/v9/main.mp4", 721, 5875008);
    CHECK(d.kind == NetKind::Segment);
    CHECK(d.media == NetMedia::Video);

    d = desc("https://cdn.example.com/bipbop/v2/main.mp4", 5000, 9000);
    CHECK(d.rendition == "v2 270p");

    d = desc("https://cdn.example.com/bipbop/a1/main.mp4", 616, 121705);
    CHECK(d.kind == NetKind::Segment);
    CHECK(d.media == NetMedia::Audio);
    CHECK(d.rendition == "a1 English 2ch");

    d = desc("https://cdn.example.com/bipbop/s1/en/fileSequence3.webvtt");
    CHECK(d.kind == NetKind::Subtitle);
    CHECK(d.media == NetMedia::Subs);

    d = desc("https://cdn.example.com/keys/k1.key");
    CHECK(d.kind == NetKind::Key);
}

TEST_CASE("NetCatalog::learn — segments of every rendition in one directory find their rendition",
          "[network_logger][describe]")
{
    // Unified Streaming's live layout: every playlist next to the master,
    // the segments of all renditions in "hls/", init segments not named so.
    const std::string base = "https://demo.example.com/live.isml/.m3u8?hls_fmp4";
    ManifestParser mp;
    REQUIRE(mp.parse_hls_text(
        "#EXTM3U\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud\",LANGUAGE=\"en\",NAME=\"English\",DEFAULT=YES,CHANNELS=\"1\","
        "URI=\"live-audio_eng=64000.m3u8?hls_fmp4\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=658000,CODECS=\"mp4a.40.2,avc1.42C01F\",RESOLUTION=1280x720,AUDIO=\"aud\"\n"
        "live-video=500000.m3u8?hls_fmp4\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=75000,CODECS=\"mp4a.40.2\",AUDIO=\"aud\"\n"
        "live-audio_eng=64000-only.m3u8\n", base));
    NetCatalog cat = NetCatalog::build(base, mp.variants(), mp.renditions());
    auto desc = [&](const std::string& url, std::int64_t rs = -1) {
        NetRequest r;
        r.url = url;
        r.protocol = "https";
        r.range_start = rs;
        return net_describe(r, cat);
    };
    const std::string dir = "https://demo.example.com/live.isml/";

    // An audio-only EXT-X-STREAM-INF is audio.
    CHECK(desc(dir + "live-audio_eng=64000-only.m3u8").media == NetMedia::Audio);
    // Before any playlist came in: the shared directory says nothing.
    CHECK(desc(dir + "hls/live-video=500000-933428799.m4s").rendition.empty());

    const std::string video =
        "#EXTM3U\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:933428799\n#EXT-X-TARGETDURATION:2\n"
        "#EXT-X-MAP:URI=\"hls/live-video=500000.m4s\"\n"
        "#EXT-X-PROGRAM-DATE-TIME:2026-10-16T20:41:32.160000Z\n"
        "#EXTINF:1.92, no desc\nhls/live-video=500000-933428799.m4s\n"
        "#EXTINF:1.92, no desc\nhls/live-video=500000-933428800.m4s\n";
    const std::string audio =
        "#EXTM3U\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:933428799\n#EXT-X-TARGETDURATION:2\n"
        "#EXT-X-MAP:URI=\"hls/live-audio_eng=64000.m4s\"\n"
        "#EXTINF:1.92, no desc\nhls/live-audio_eng=64000-933428799.m4s\n";
    CHECK_FALSE(cat.learn(dir + "elsewhere.m3u8", video));     // not one of the master's playlists
    const std::uint64_t before = cat.learned;
    CHECK(cat.learn(dir + "live-video=500000.m3u8?hls_fmp4", video));
    CHECK(cat.learn(dir + "live-audio_eng=64000.m3u8?hls_fmp4", audio));
    CHECK(cat.learned > before);
    CHECK_FALSE(cat.learn(dir + "live-video=500000.m3u8?hls_fmp4", video));   // nothing new

    NetDesc d = desc(dir + "hls/live-video=500000-933428800.m4s?token=1");
    CHECK(d.kind == NetKind::Segment);
    CHECK(d.media == NetMedia::Video);
    CHECK(d.rendition == "live-video=500000 720p");
    d = desc(dir + "hls/live-video=500000.m4s");
    CHECK(d.kind == NetKind::Init);
    CHECK(d.rendition == "live-video=500000 720p");
    d = desc(dir + "hls/live-audio_eng=64000-933428799.m4s");
    CHECK(d.kind == NetKind::Segment);
    CHECK(d.media == NetMedia::Audio);
    CHECK(desc(dir + "hls/live-audio_eng=64000.m4s").kind == NetKind::Init);
}

TEST_CASE("NetCatalog::learn — an init segment that is a byte range of the media file", "[network_logger][describe]")
{
    const std::string base = "https://cdn.example.com/bipbop/master.m3u8";
    ManifestParser mp;
    REQUIRE(mp.parse_hls_text("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=8001098,RESOLUTION=1920x1080\n"
                              "v9/prog_index.m3u8\n", base));
    NetCatalog cat = NetCatalog::build(base, mp.variants(), mp.renditions());
    REQUIRE(cat.learn("https://cdn.example.com/bipbop/v9/prog_index.m3u8",
                      "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-PLAYLIST-TYPE:VOD\n"
                      "#EXT-X-MAP:URI=\"main.mp4\",BYTERANGE=\"720@0\"\n"
                      "#EXTINF:6.0,\n#EXT-X-BYTERANGE:5874288@720\nmain.mp4\n"
                      "#EXTINF:6.0,\n#EXT-X-BYTERANGE:5871752\nmain.mp4\n#EXT-X-ENDLIST\n"));
    auto kind = [&](std::int64_t rs) {
        NetRequest r;
        r.url = "https://cdn.example.com/bipbop/v9/main.mp4";
        r.protocol = "https";
        r.range_start = rs;
        return net_describe(r, cat).kind;
    };
    CHECK(kind(0) == NetKind::Init);
    CHECK(kind(720) == NetKind::Segment);
    CHECK(kind(5875008) == NetKind::Segment);
}

TEST_CASE("NetCatalog::learn — local HLS: the files a playlist lists are its segments", "[network_logger][describe]")
{
    const std::string base = "/Users/me/pkg/master.m3u8";
    ManifestParser mp;
    REQUIRE(mp.parse_hls_text("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=300000,RESOLUTION=320x240\nstream_0.m3u8\n", base));
    NetCatalog cat = NetCatalog::build(base, mp.variants(), mp.renditions());
    auto desc = [&](const std::string& url, std::int64_t rs) {
        NetRequest r;
        r.url = url;
        r.protocol = "file";
        r.range_start = rs;
        return net_describe(r, cat);
    };
    CHECK(desc("/Users/me/movie.mkv", -1).kind == NetKind::File);   // a local file being played
    REQUIRE(cat.learn("/Users/me/pkg/stream_0.m3u8",
                      "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MAP:URI=\"stream_0.m4s\",BYTERANGE=\"827@0\"\n"
                      "#EXTINF:2.0,\n#EXT-X-BYTERANGE:68489@827\nstream_0.m4s\n#EXT-X-ENDLIST\n"));
    CHECK(desc("/Users/me/pkg/stream_0.m4s", 0).kind == NetKind::Init);
    const NetDesc d = desc("/Users/me/pkg/stream_0.m4s", 827);
    CHECK(d.kind == NetKind::Segment);
    CHECK(d.rendition == "pkg 240p");          // the only playlist: named by its directory
    CHECK(desc("/Users/me/movie.mkv", -1).kind == NetKind::File);
}

TEST_CASE("net_request_name / formatting helpers", "[network_logger][describe]")
{
    CHECK(net_request_name("https://h.example/a/b/seg-1.m4s?tok=1") == "seg-1.m4s");
    CHECK(net_request_name("https://h.example/") == "h.example");
    CHECK(net_request_name("/Users/x/movie.mkv") == "movie.mkv");
    CHECK(net_format_bytes(721) == "721 B");
    CHECK(net_format_bytes(12345) == "12.3 kB");
    CHECK(net_format_bytes(5874288) == "5.87 MB");
    CHECK(net_format_duration_us(84'000) == "84 ms");
    CHECK(net_format_duration_us(6'020'000) == "6.02 s");
}

// ── Response bodies (AVIOContext tap) ────────────────────────────────────────

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

#include <cstring>
#include <functional>

namespace {

// A keep-alive HTTP handle stand-in: an AVIOContext serving one response
// after another from memory, in uneven chunks, the way http.c's handle
// serves the requests HLS issues on it. `on_read` runs inside a read (from
// inside FFmpeg's buffer refill) — where http.c logs a reconnect.
struct FakeHandle {
    std::vector<std::string> responses;
    std::size_t cur = 0, pos = 0;
    int chunk = 7000;
    std::function<void(std::size_t pos)> on_read;
    AVIOContext* pb = nullptr;

    explicit FakeHandle(std::vector<std::string> r) : responses(std::move(r))
    {
        auto* buf = static_cast<unsigned char*>(av_malloc(32768));
        pb = avio_alloc_context(buf, 32768, 0, this, &FakeHandle::read, nullptr, nullptr);
    }
    ~FakeHandle()
    {
        if (pb) av_freep(&pb->buffer);
        avio_context_free(&pb);
    }
    static int read(void* opaque, std::uint8_t* buf, int size)
    {
        auto* f = static_cast<FakeHandle*>(opaque);
        if (f->cur >= f->responses.size()) return AVERROR_EOF;
        const std::string& r = f->responses[f->cur];
        if (f->on_read) f->on_read(f->pos);
        if (f->pos >= r.size()) return AVERROR_EOF;
        const int n = std::min({ size, f->chunk, static_cast<int>(r.size() - f->pos) });
        std::memcpy(buf, r.data() + f->pos, static_cast<std::size_t>(n));
        f->pos += static_cast<std::size_t>(n);
        return n;
    }
    // Read the current response to its end, the way the hls demuxer does.
    std::string drain()
    {
        std::string got;
        std::uint8_t tmp[3000];
        int n;
        while ((n = avio_read(pb, tmp, sizeof(tmp))) > 0) got.append(reinterpret_cast<char*>(tmp), static_cast<std::size_t>(n));
        return got;
    }
    // hls.c open_url_keepalive(): the next request on the same handle.
    void next() { ++cur; pos = 0; pb->eof_reached = 0; }
};

std::string pattern(std::size_t n, unsigned seed)
{
    std::string s(n, '\0');
    for (std::size_t i = 0; i < n; ++i) s[i] = static_cast<char>((i * 131u + seed * 7u + (i >> 9)) & 0xff);
    return s;
}

std::string playlist_text(int segments)
{
    std::string s = "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:6\n#EXT-X-MAP:URI=\"main.mp4\",BYTERANGE=\"720@0\"\n";
    for (int i = 0; i < segments; ++i)
        s += std::format("#EXTINF:6.00000,\n#EXT-X-BYTERANGE:{}@{}\nmain.mp4\n", 50000 + i, 721 + i * 50000);
    return s + "#EXT-X-ENDLIST\n";
}

std::string request_line(const char* path, const char* extra = "")
{
    return std::format("request: GET {} HTTP/1.1\r\nHost: cdn.example.com\r\n{}\r\n", path, extra);
}

} // namespace

TEST_CASE("NetworkLogger bodies — keep-alive requests on one tapped handle each keep their own body",
          "[network_logger][body]")
{
    NetworkLogger log;
    log.install();
    const std::string pl  = playlist_text(900);           // ~50 kB: several refills
    const std::string seg = pattern(100000, 1);           // a media segment
    const std::string pl2 = playlist_text(3);
    FakeHandle h({ pl, seg, pl2 });
    int ctx = 0;

    log.ev_open_begin(NetInitiator::Playback, "https://cdn.example.com/v9/prog_index.m3u8");
    log.ev_log(&ctx, request_line("/v9/prog_index.m3u8").c_str());
    log.ev_log(&ctx, "header='HTTP/1.1 200 OK'");
    log.ev_open_end(0, h.pb, &ctx, &h.pb->bytes_read, -1, "", "");
    log.tap_body(h.pb);
    CHECK(h.drain() == pl);

    h.next();
    log.ev_log(&ctx, request_line("/v9/main.mp4", "Range: bytes=721-100720\r\n").c_str());
    log.ev_log(&ctx, "header='HTTP/1.1 206 Partial Content'");
    log.ev_log(&ctx, "header='Content-Length: 100000'");
    CHECK(h.drain() == seg);

    h.next();
    log.ev_log(&ctx, request_line("/a1/prog_index.m3u8").c_str());
    log.ev_log(&ctx, "header='HTTP/1.1 200 OK'");
    log.ev_log(&ctx, "header='Content-Type: application/vnd.apple.mpegurl'");
    CHECK(h.drain() == pl2);
    log.ev_close(h.pb);

    auto reqs = all_requests(log);
    REQUIRE(reqs.size() == 3);
    std::string body;
    REQUIRE(log.copy_body(reqs[0].id, body));
    CHECK(body == pl);                                     // a playlist: whole
    CHECK(reqs[0].body_bytes == static_cast<std::int64_t>(pl.size()));
    CHECK_FALSE(reqs[0].body_truncated);
    REQUIRE(log.copy_body(reqs[1].id, body));
    CHECK(body == seg.substr(0, NetworkLogger::kHeadBytes));   // a segment: its head
    CHECK(reqs[1].body_truncated);
    REQUIRE(log.copy_body(reqs[2].id, body));
    CHECK(body == pl2);
    CHECK(reqs[2].url == "https://cdn.example.com/a1/prog_index.m3u8");
}

TEST_CASE("NetworkLogger bodies — a request switch logged inside a refill credits nothing twice",
          "[network_logger][body]")
{
    // http.c reconnecting mid-body logs a new "request:" from inside
    // FFmpeg's buffer refill: the data before it belongs to the first row,
    // the rest to the continuation, no byte lost or repeated.
    NetworkLogger log;
    log.install();
    const std::string pl = playlist_text(1500);            // ~85 kB
    FakeHandle h({ pl });
    int ctx = 0;
    bool reconnected = false;
    h.on_read = [&](std::size_t pos) {
        if (!reconnected && pos >= 40000) {
            reconnected = true;
            log.ev_log(&ctx, "Will reconnect at 40000 in 0 second(s), error=Connection reset by peer.\n");
            log.ev_log(&ctx, request_line("/v9/prog_index.m3u8", "Range: bytes=40000-\r\n").c_str());
            log.ev_log(&ctx, "header='HTTP/1.1 206 Partial Content'");
        }
    };

    log.ev_open_begin(NetInitiator::Playback, "https://cdn.example.com/v9/prog_index.m3u8");
    log.ev_log(&ctx, request_line("/v9/prog_index.m3u8").c_str());
    log.ev_log(&ctx, "header='HTTP/1.1 200 OK'");
    log.ev_open_end(0, h.pb, &ctx, &h.pb->bytes_read, -1, "", "");
    log.tap_body(h.pb);
    CHECK(h.drain() == pl);
    log.ev_close(h.pb);
    REQUIRE(reconnected);

    auto reqs = all_requests(log);
    REQUIRE(reqs.size() == 2);
    std::string a, b;
    REQUIRE(log.copy_body(reqs[0].id, a));
    REQUIRE(log.copy_body(reqs[1].id, b));
    CHECK(a.size() >= 40000);
    CHECK(a + b == pl);
}

TEST_CASE("NetworkLogger bodies — keys are never kept; untapped handles keep nothing", "[network_logger][body]")
{
    NetworkLogger log;
    log.install();
    const std::string key(16, '\x5a');
    FakeHandle h({ key, key });
    int ctx = 0;

    log.ev_open_begin(NetInitiator::Playback, "https://cdn.example.com/keys/k1.key");
    log.ev_log(&ctx, request_line("/keys/k1.key").c_str());
    log.ev_log(&ctx, "header='HTTP/1.1 200 OK'");
    log.ev_open_end(0, h.pb, &ctx, &h.pb->bytes_read, -1, "", "");
    log.tap_body(h.pb);
    CHECK(h.drain() == key);

    // A key from a license path without Content-Length or extension: dropped
    // once its size is known (16 bytes).
    h.next();
    log.ev_log(&ctx, request_line("/drm/fetch?id=3").c_str());
    log.ev_log(&ctx, "header='HTTP/1.1 200 OK'");
    CHECK(h.drain() == key);
    log.ev_close(h.pb);

    auto reqs = all_requests(log);
    REQUIRE(reqs.size() == 2);
    std::string body;
    CHECK_FALSE(log.copy_body(reqs[0].id, body));
    CHECK_FALSE(log.copy_body(reqs[1].id, body));
    CHECK(reqs[1].body_bytes == 0);

    // Not tapped (e.g. avformat_open_input's own probed input): nothing kept.
    FakeHandle h2({ playlist_text(2) });
    int ctx2 = 0;
    log.ev_open_begin(NetInitiator::Playback, "https://cdn.example.com/master.m3u8");
    log.ev_log(&ctx2, request_line("/master.m3u8").c_str());
    log.ev_open_end(0, h2.pb, &ctx2, &h2.pb->bytes_read, -1, "", "");
    h2.drain();
    log.ev_close(h2.pb);
    reqs = all_requests(log);
    REQUIRE(reqs.size() == 3);
    CHECK_FALSE(log.copy_body(reqs[2].id, body));
}

// ── NetProbe ─────────────────────────────────────────────────────────────────

#include "NetProbe.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {

bool wait_idle(const NetProbe& p, double secs = 10.0)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(secs);
    while (p.pending() > 0 && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return p.pending() == 0;
}

std::vector<NetRequest> inspector_requests(const NetworkLogger& log)
{
    std::vector<NetRequest> out;
    for (auto& r : all_requests(log))
        if (r.initiator == NetInitiator::Inspector) out.push_back(std::move(r));
    return out;
}

} // namespace

TEST_CASE("NetProbe — fetches as inspector requests whose bodies the logger keeps", "[network_logger][probe]")
{
    namespace fs = std::filesystem;
    const fs::path dir = testtemp::path("mirage_test_net_probe");
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string seg = pattern(200000, 3);
    const std::string pl  = playlist_text(3);
    std::ofstream(dir / "seg.m4s", std::ios::binary) << seg;
    std::ofstream(dir / "index.m3u8", std::ios::binary) << pl;
    const std::string seg_path = (dir / "seg.m4s").string();

    NetworkLogger log;
    log.install();
    {
        NetProbe probe;
        const NetProbe::Job head{ seg_path, 0, 5000 };
        const NetProbe::Job list{ (dir / "index.m3u8").string(), -1, -1 };
        probe.request({ head, head, list });            // the same job twice is fetched once
        REQUIRE(wait_idle(probe));
        auto reqs = inspector_requests(log);
        REQUIRE(reqs.size() == 2);
        std::string body;
        for (const auto& r : reqs) {
            REQUIRE(log.copy_body(r.id, body));
            if (r.url == seg_path) {
                CHECK(body == seg.substr(0, 5000));   // what was asked for, no more
                CHECK(r.range_start == 0);
                CHECK(r.range_end == 4999);
            }
            else                   CHECK(body == pl);                    // a playlist: whole
        }
        CHECK(probe.fetched(head));

        probe.request({ head });                         // fetched already: nothing new
        REQUIRE(wait_idle(probe));
        CHECK(inspector_requests(log).size() == 2);

        // A byte range inside the file (an EXT-X-BYTERANGE segment): those bytes, that range.
        probe.request({ { seg_path, 70000, 5000 } });
        REQUIRE(wait_idle(probe));
        reqs = inspector_requests(log);
        REQUIRE(reqs.size() == 3);
        REQUIRE(log.copy_body(reqs[2].id, body));
        CHECK(body == seg.substr(70000, 5000));
        CHECK(reqs[2].range_start == 70000);
        CHECK(reqs[2].range_end == 74999);

        probe.cancel();                                  // new media: forgotten
        CHECK_FALSE(probe.fetched(head));
        probe.request({ head, { (dir / "missing.m4s").string(), 0, 100 } });
        REQUIRE(wait_idle(probe));
        reqs = inspector_requests(log);
        REQUIRE(reqs.size() == 5);
        CHECK(std::count_if(reqs.begin(), reqs.end(), [](const NetRequest& r) { return r.failed; }) == 1);
    }
    log.uninstall();
    fs::remove_all(dir);
}

TEST_CASE("NetworkLogger bodies — a local file read from a seek keeps its offset as the range",
          "[network_logger][body]")
{
    // hls.c opens a local EXT-X-BYTERANGE segment and seeks to its offset
    // (no Range header to log): the row's range is where the body begins.
    namespace fs = std::filesystem;
    const fs::path dir = testtemp::path("mirage_test_local_range");
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string data = pattern(300000, 5);
    const std::string path = (dir / "main.mp4").string();
    std::ofstream(path, std::ios::binary) << data;

    NetworkLogger log;
    log.install();
    auto read_from = [&](std::int64_t offset, int bytes, std::int64_t range_end = -1) {
        NetworkLogger::track_open_begin(NetInitiator::Playback, path.c_str(), range_end >= 0 ? offset : -1, range_end);
        AVIOContext* pb = nullptr;
        const int ret = avio_open2(&pb, path.c_str(), AVIO_FLAG_READ, nullptr, nullptr);
        NetworkLogger::track_open_end(ret, pb);
        REQUIRE(ret >= 0);
        if (offset > 0) REQUIRE(avio_seek(pb, offset, SEEK_SET) == offset);
        std::string got(static_cast<std::size_t>(bytes), '\0');
        REQUIRE(avio_read(pb, reinterpret_cast<unsigned char*>(got.data()), bytes) == bytes);
        NetworkLogger::track_close(pb);
        avio_closep(&pb);
    };
    read_from(0, 720);           // the init segment at the start
    read_from(150000, 40000);    // a media segment further in
    // With the range hls.c asks for: a short seek reads forward from the
    // start within one buffer, and the buffer runs past the range's end.
    read_from(827, 5000, 827 + 5000 - 1);
    log.uninstall();

    const auto reqs = all_requests(log);
    REQUIRE(reqs.size() == 3);
    {
        std::string b;
        REQUIRE(log.copy_body(reqs[2].id, b));
        CHECK(b == data.substr(827, 5000));
        CHECK(reqs[2].range_start == 827);
        CHECK(reqs[2].range_end == 5826);
        CHECK(reqs[2].bytes == 5000);
    }
    std::string body;
    CHECK(reqs[0].range_start == -1);                 // from the start: no range
    REQUIRE(log.copy_body(reqs[0].id, body));
    CHECK(body == data.substr(0, body.size()));
    CHECK(reqs[1].range_start == 150000);
    REQUIRE(log.copy_body(reqs[1].id, body));
    CHECK(body.size() == NetworkLogger::kHeadBytes);
    CHECK(body == data.substr(150000, body.size()));
    fs::remove_all(dir);
}
