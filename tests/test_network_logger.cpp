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
