#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "NetworkLogger.h"

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
