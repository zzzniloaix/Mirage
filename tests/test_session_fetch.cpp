// The session fetch (src/SessionFetch.h), over a local server
// (tests/HttpFileServer.h) that serves a ladder's files the test writes:
// each file once, in play order, one at a time and unpaced (D-104 (a));
// status 200 and the file's bytes; a segment answered late, measured at
// least its delay, under the analyzer's 5 s network limit (D-036); a 404 and
// a body cut short recorded as failures; a redirected file one request with
// its chain; a byte range; the rows tagged playback, each file's alone,
// NetworkLogger uninstalled afterwards (the design's P-N); the UTC anchor
// and the wall clock from the session's start (D-073); what a stop or
// another installed logger does; and a request another thread makes during
// the fetch kept out of the file's rows (design §15, R4). Nothing is
// downloaded. The events of these rows are request_events()'s, tested on
// rows built by hand in tests/test_session_plan.cpp and here on the rows the
// fetch returns.

#include <catch2/catch_test_macros.hpp>

#include "HttpFileServer.h"
#include "NetworkLogger.h"
#include "SessionFetch.h"
#include "SessionPlan.h"
#include "TestTemp.h"
#include "TimelineModel.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/dict.h>
}

namespace fs = std::filesystem;

using timeline::Event;
using timeline::FetchedFile;
using timeline::Request;
using timeline::RequestRow;
using timeline::Source;
using timeline::Value;

namespace {

bool contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

// `n` bytes of a pattern of `seed`'s.
std::string pattern(std::size_t n, int seed)
{
    std::string s(n, '\0');
    for (std::size_t i = 0; i < n; ++i) s[i] = static_cast<char>((i * 31 + static_cast<std::size_t>(seed) * 7) % 251);
    return s;
}

// A ladder's files, as a local server serves them: master.m3u8, and for each
// of the variants v0 (the reference), v1 and v2 its index.m3u8 and eight
// segments seg_000.ts to seg_007.ts, each of a length and a pattern of its
// own. Their contents don't matter to the fetch, which reads them whole.
struct Ladder {
    fs::path    dir;
    std::string master;                          // its path
    std::map<std::string, std::string> files;    // each file's bytes, by its path under dir
};

std::string segment_name(int v, std::int64_t k) { return std::format("v{}/seg_{:03}.ts", v, k); }

Ladder write_ladder(const std::string& name)
{
    Ladder l;
    l.dir = testtemp::path(name);
    fs::remove_all(l.dir);
    const auto put = [&l](const std::string& path, std::string bytes) {
        fs::create_directories((l.dir / path).parent_path());
        std::ofstream(l.dir / path, std::ios::binary) << bytes;
        l.files[path] = std::move(bytes);
    };
    put("master.m3u8", "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1000000\nv0/index.m3u8\n"
                       "#EXT-X-STREAM-INF:BANDWIDTH=600000\nv1/index.m3u8\n"
                       "#EXT-X-STREAM-INF:BANDWIDTH=350000\nv2/index.m3u8\n");
    for (int v = 0; v < 3; ++v) {
        std::string playlist = "#EXTM3U\n#EXT-X-TARGETDURATION:1\n#EXT-X-PLAYLIST-TYPE:VOD\n";
        for (std::int64_t k = 0; k < 8; ++k) {
            playlist += std::format("#EXTINF:1.000000,\nseg_{:03}.ts\n", k);
            put(segment_name(v, k), pattern(1000 + 37 * static_cast<std::size_t>(k) + 300 * static_cast<std::size_t>(v),
                                            v * 8 + static_cast<int>(k)));
        }
        put(std::format("v{}/index.m3u8", v), playlist + "#EXT-X-ENDLIST\n");
    }
    l.master = (l.dir / "master.m3u8").string();
    return l;
}

// A hand-built timeline of the ladder, as build() and make_session() would
// leave it for the fetch (src/SessionPlan.h): variant v at its playlist's
// path, its segment k over the reference frames [25k, 25k + 25) with its
// file's path as its URL, and a session that plays v1 for segments 0 to 2,
// v2 for 3 and 4 and v1 for 5 to 7 (design §9.3's plan).
timeline::Timeline timeline_of(const Ladder& l)
{
    timeline::Timeline t;
    for (int v = 0; v < 3; ++v) {
        timeline::Variant var;
        var.index = v;
        var.pair  = v - 1;
        var.url   = (l.dir / std::format("v{}", v) / "index.m3u8").string();
        t.variants.push_back(var);
        for (std::int64_t k = 0; k < 8; ++k) {
            Event e;
            e.source    = Source::Playlist;
            e.type      = "segment";
            e.variant   = v;
            e.segment   = k;
            e.ref_index = 25 * k;
            e.ref_end   = 25 * k + 25;
            e.payload   = { { "url", Value{ (l.dir / segment_name(v, k)).string() } }, { "byte_range", Value{} } };
            t.events.push_back(std::move(e));
        }
    }
    timeline::assign_ids(t.events);
    t.build_index();
    timeline::Session s;
    s.id    = "0";
    s.spans = { { .variant = 1, .entry = 1, .first_seq = 0, .last_seq = 2, .ref_index = 0, .ref_end = 75 },
                { .variant = 2, .entry = 2, .first_seq = 3, .last_seq = 4, .ref_index = 75, .ref_end = 125 },
                { .variant = 1, .entry = 1, .first_seq = 5, .last_seq = 7, .ref_index = 125, .ref_end = 200 } };
    t.sessions.push_back(s);
    return t;
}

// The files of the session's fetch in its order (D-104 (a)), by their paths
// under the ladder's directory: v1's playlist once, though two spans play it.
const std::vector<std::string> kOrder = {
    "master.m3u8",   "v1/index.m3u8", "v1/seg_000.ts", "v1/seg_001.ts", "v1/seg_002.ts", "v2/index.m3u8",
    "v2/seg_003.ts", "v2/seg_004.ts", "v1/seg_005.ts", "v1/seg_006.ts", "v1/seg_007.ts",
};

std::vector<Request> files_of(const timeline::Timeline& t, const Ladder& l, const std::string& base)
{
    std::string err;
    std::vector<Request> files = timeline::fetch_files(t, 0, l.master, base, err);
    INFO(err);
    REQUIRE(err.empty());
    REQUIRE(files.size() == kOrder.size());
    return files;
}

// The UTC time now, as fetch_session() writes its anchor.
std::string utc_now()
{
    return std::format("{:%FT%T}Z", std::chrono::floor<std::chrono::microseconds>(std::chrono::system_clock::now()));
}

const Value* member(const timeline::Payload& p, std::string_view name)
{
    for (const auto& [n, v] : p)
        if (n == name) return &v;
    return nullptr;
}

std::optional<double> double_member(const timeline::Payload& p, std::string_view name)
{
    const Value* v = member(p, name);
    if (!v || !std::holds_alternative<double>(v->v)) return std::nullopt;
    return std::get<double>(v->v);
}

std::optional<std::int64_t> int_member(const timeline::Payload& p, std::string_view name)
{
    const Value* v = member(p, name);
    if (!v || !std::holds_alternative<std::int64_t>(v->v)) return std::nullopt;
    return std::get<std::int64_t>(v->v);
}

std::optional<bool> bool_member(const timeline::Payload& p, std::string_view name)
{
    const Value* v = member(p, name);
    if (!v || !std::holds_alternative<bool>(v->v)) return std::nullopt;
    return std::get<bool>(v->v);
}

std::size_t chain_size(const timeline::Payload& p)
{
    const Value* v = member(p, "chain");
    return v && std::holds_alternative<Value::List>(v->v) ? std::get<Value::List>(v->v).size() : 0;
}

// What every row of the fetch holds, and its times in order.
void check_row(const RequestRow& row)
{
    CHECK(row.initiator == "playback");   // the design's P-N
    CHECK(row.t_start >= 0);
    CHECK(row.t_sent >= row.t_start);
    CHECK(row.t_first_byte >= row.t_sent);
    CHECK(row.t_end >= row.t_first_byte);
}

} // namespace

TEST_CASE("session fetch: each file once, in play order, one at a time and unpaced, status 200 and its "
          "bytes, each file's rows alone, tagged playback",
          "[timeline][session][session_fetch]")
{
    const Ladder l = write_ladder("mirage_test_session_fetch_order");
    HttpFileServer server(l.dir);
    REQUIRE(server.ok());
    const timeline::Timeline t = timeline_of(l);
    const std::vector<Request> files = files_of(t, l, server.base());

    const std::string            before = utc_now();
    const timeline::FetchResult  got    = timeline::fetch_session(files);
    const std::string            after  = utc_now();
    CHECK(got.error.empty());
    // Installed for the fetch alone (the design's P-N).
    CHECK(NetworkLogger::instance() == nullptr);

    // The server saw each file once, in the fetch's order (Q44 (f) and (g)).
    CHECK(server.paths() == kOrder);
    REQUIRE(got.files.size() == kOrder.size());
    std::int64_t previous_end = 0;
    for (std::size_t k = 0; k < kOrder.size(); ++k) {
        INFO("file " << k << ", " << kOrder[k]);
        const Request& r = got.files[k];
        CHECK(r.kind == files[k].kind);
        CHECK(r.variant == files[k].variant);
        CHECK(r.segment == files[k].segment);
        CHECK(r.url == server.url(kOrder[k]));
        CHECK(r.error.empty());
        // Its own row alone: the logger was cleared after the file before.
        REQUIRE(r.chain.size() == 1);
        const RequestRow& row = r.chain[0];
        check_row(row);
        CHECK(row.url == r.url);
        CHECK(row.status == 200);
        CHECK(row.status_text == "OK");
        CHECK(row.bytes == static_cast<std::int64_t>(l.files.at(kOrder[k]).size()));
        CHECK(row.content_length == static_cast<std::int64_t>(l.files.at(kOrder[k]).size()));
        CHECK_FALSE(row.failed);
        CHECK(row.error.empty());
        // One file at a time: each request starts once the one before it ended.
        CHECK(row.t_start >= previous_end);
        previous_end = row.t_end;
    }
    // Unpaced (Q44 (f) 1): the fetch takes far less than the 8 s of media
    // that the session plays, eight segments of 1 s.
    CHECK(previous_end < 8000000);

    // The anchor, UTC at the session's start (D-073), between the times
    // taken around the call; written as they are, so they compare as text.
    REQUIRE(got.anchor_utc.size() == before.size());
    CHECK(got.anchor_utc[4] == '-');
    CHECK(got.anchor_utc[10] == 'T');
    CHECK(got.anchor_utc[19] == '.');
    CHECK(got.anchor_utc.back() == 'Z');
    CHECK(before <= got.anchor_utc);
    CHECK(got.anchor_utc <= after);

    // Their events: one per file, each with its first row's start as its
    // wall clock, microseconds from the session's start (D-073).
    const timeline::RequestEvents net = timeline::request_events(t, 0, got.files);
    CHECK(net.warnings.empty());
    REQUIRE(net.events.size() == kOrder.size());
    for (std::size_t k = 0; k < kOrder.size(); ++k) {
        INFO("file " << k);
        const Event& e = net.events[k];
        CHECK(e.wallclock_us == got.files[k].chain[0].t_start);
        CHECK(e.type == (k == 0 || k == 1 || k == 5 ? "playlist_request" : "segment_request"));
        CHECK(int_member(e.payload, "status") == 200);
        CHECK(bool_member(e.payload, "failed") == false);
        CHECK(double_member(e.payload, "start") == static_cast<double>(got.files[k].chain[0].t_start) / 1e6);
    }
}

TEST_CASE("session fetch: a segment answered late measured at least its delay, a 404 and a body cut short "
          "recorded as failures, a redirected file one request with its chain",
          "[timeline][session][session_fetch]")
{
    Ladder l = write_ladder("mirage_test_session_fetch_rules");
    // v2's segment 4 is missing; v1's segment 2 has moved, and its old path
    // answers with a redirect; v1's segment 1 is cut short after 100 bytes;
    // v2's segment 3 is answered 400 ms late, under the analyzer's 5 s
    // limit (D-036; the design's P-S).
    fs::remove(l.dir / "v2/seg_004.ts");
    fs::create_directories(l.dir / "moved");
    fs::copy_file(l.dir / "v1/seg_002.ts", l.dir / "moved/seg_002.ts");
    constexpr auto kDelay = std::chrono::milliseconds(400);
    HttpFileServer server(l.dir, { { .path = "v2/seg_003.ts", .delay = kDelay },
                                   { .path = "v1/seg_001.ts", .cut_after = 100 },
                                   { .path = "v1/seg_002.ts", .redirect_to = "moved/seg_002.ts" } });
    REQUIRE(server.ok());
    const timeline::Timeline    t   = timeline_of(l);
    const timeline::FetchResult got = timeline::fetch_session(files_of(t, l, server.base()));
    CHECK(got.error.empty());
    CHECK(NetworkLogger::instance() == nullptr);
    REQUIRE(got.files.size() == kOrder.size());

    // One try per file (Q44 (c) 1): the server saw each once, the
    // redirect's target after its source, and the fetch went on after each
    // failure.
    std::vector<std::string> seen = kOrder;
    seen.insert(seen.begin() + 5, "moved/seg_002.ts");
    CHECK(server.paths() == seen);
    for (const Request& r : got.files)
        for (const RequestRow& row : r.chain) check_row(row);

    // The delayed segment: from its request to its status line, and to its
    // last byte, at least the delay; status 200 and its bytes.
    const Request& late = got.files[6];
    REQUIRE(late.chain.size() == 1);
    CHECK(late.error.empty());
    CHECK(late.chain[0].status == 200);
    CHECK(late.chain[0].bytes == static_cast<std::int64_t>(l.files.at("v2/seg_003.ts").size()));
    CHECK(late.chain[0].t_first_byte - late.chain[0].t_sent >= 400000);
    CHECK(late.chain[0].t_end - late.chain[0].t_start >= 400000);

    // The body cut short: status 200 and fewer bytes than its Content-Length,
    // which NetworkLogger doesn't mark failed; the fetch's read failed.
    const Request& cut = got.files[3];
    REQUIRE(cut.chain.size() == 1);
    CHECK(cut.chain[0].status == 200);
    CHECK(cut.chain[0].bytes == 100);
    CHECK(cut.chain[0].content_length == static_cast<std::int64_t>(l.files.at("v1/seg_001.ts").size()));
    CHECK_FALSE(cut.chain[0].failed);
    CHECK(contains(cut.error, "after 100 bytes"));

    // The redirect: one request, a row for each of its two requests
    // (src/NetworkLogger.cpp starts a row at each request line), the first
    // with its 302, the second its target's, from where the first ended.
    const Request& moved = got.files[4];
    CHECK(moved.url == server.url("v1/seg_002.ts"));
    CHECK(moved.error.empty());
    REQUIRE(moved.chain.size() == 2);
    CHECK(moved.chain[0].url == server.url("v1/seg_002.ts"));
    CHECK(moved.chain[0].status == 302);
    CHECK(moved.chain[0].status_text == "Found");
    CHECK_FALSE(moved.chain[0].failed);
    CHECK(moved.chain[1].url == server.url("moved/seg_002.ts"));
    CHECK(moved.chain[1].status == 200);
    CHECK(moved.chain[1].bytes == static_cast<std::int64_t>(l.files.at("v1/seg_002.ts").size()));
    CHECK(moved.chain[0].t_end <= moved.chain[1].t_start);

    // The missing segment: a failed row with its status, a failed open.
    const Request& missing = got.files[7];
    REQUIRE(missing.chain.size() == 1);
    CHECK(missing.chain[0].status == 404);
    CHECK(missing.chain[0].failed);
    CHECK_FALSE(missing.error.empty());
    for (std::size_t k = 8; k < kOrder.size(); ++k) {
        INFO("file " << k);
        CHECK(got.files[k].error.empty());
        CHECK(got.files[k].chain.at(0).status == 200);
    }

    // As events (D-104 (a)): the redirected file one event from its first
    // row's start to its last row's end, its status the last row's, the
    // chain in its payload; the failures marked.
    const timeline::RequestEvents net = timeline::request_events(t, 0, got.files);
    CHECK(net.warnings.empty());
    REQUIRE(net.events.size() == kOrder.size());
    const timeline::Payload& redirected = net.events[4].payload;
    CHECK(int_member(redirected, "status") == 200);
    CHECK(chain_size(redirected) == 2);
    CHECK(double_member(redirected, "start") == static_cast<double>(moved.chain[0].t_start) / 1e6);
    CHECK(double_member(redirected, "end") == static_cast<double>(moved.chain[1].t_end) / 1e6);
    CHECK(double_member(redirected, "duration") ==
          static_cast<double>(moved.chain[1].t_end - moved.chain[0].t_start) / 1e6);
    CHECK(bool_member(redirected, "failed") == false);
    CHECK(double_member(net.events[6].payload, "duration") >= 0.4);
    CHECK(int_member(net.events[6].payload, "status") == 200);
    CHECK(bool_member(net.events[3].payload, "failed") == true);
    CHECK(bool_member(net.events[7].payload, "failed") == true);
    CHECK(int_member(net.events[7].payload, "status") == 404);
}

TEST_CASE("session fetch: a segment's byte range, over HTTP and from a local file, and local files' rows",
          "[timeline][session][session_fetch]")
{
    const Ladder l = write_ladder("mirage_test_session_fetch_range");
    const std::string& bytes = l.files.at("v1/seg_000.ts");
    HttpFileServer server(l.dir, { .path = "v1/seg_000.ts", .ranges = true });
    REQUIRE(server.ok());

    // Over HTTP the range is the request's Range header, answered 206.
    Request http{ .kind = FetchedFile::Segment, .variant = 1, .segment = 0, .url = server.url("v1/seg_000.ts"),
                  .range_offset = 100, .range_length = 300 };
    // From a local file, a seek to its first byte; at most its length read.
    Request local = http;
    local.url = (l.dir / "v1/seg_000.ts").string();
    // A range past the file's end reads what there is, and says so.
    Request past = local;
    past.range_offset = 0;
    past.range_length = static_cast<std::int64_t>(bytes.size()) + 10;
    const timeline::FetchResult got = timeline::fetch_session({ http, local, past });
    CHECK(got.error.empty());
    REQUIRE(got.files.size() == 3);

    CHECK(server.ranges("v1/seg_000.ts") == std::vector<std::string>{ "bytes=100-399" });
    REQUIRE(got.files[0].chain.size() == 1);
    CHECK(got.files[0].error.empty());
    CHECK(got.files[0].chain[0].status == 206);
    CHECK(got.files[0].chain[0].bytes == 300);
    CHECK(got.files[0].chain[0].content_length == 300);

    // A local file's row has no HTTP status, and NetworkLogger keeps the
    // bytes of its range (src/NetworkLogger.h).
    REQUIRE(got.files[1].chain.size() == 1);
    CHECK(got.files[1].error.empty());
    CHECK(got.files[1].chain[0].status == 0);
    CHECK(got.files[1].chain[0].bytes == 300);
    CHECK(got.files[1].chain[0].initiator == "playback");
    CHECK(got.files[2].error == std::format("the file ended after {} of its byte range's {} bytes", bytes.size(),
                                            bytes.size() + 10));

    // A session of local files, with no fetch base: the report's URLs as
    // they are, each a row of its open (src/SessionFetch.h), its size the
    // file's.
    const timeline::Timeline    t    = timeline_of(l);
    const timeline::FetchResult disk = timeline::fetch_session(files_of(t, l, ""));
    CHECK(disk.error.empty());
    REQUIRE(disk.files.size() == kOrder.size());
    for (std::size_t k = 0; k < kOrder.size(); ++k) {
        INFO("file " << k);
        const Request& r = disk.files[k];
        CHECK(r.url == (l.dir / kOrder[k]).string());
        CHECK(r.error.empty());
        REQUIRE(r.chain.size() == 1);
        CHECK(r.chain[0].status == 0);
        CHECK(r.chain[0].bytes == static_cast<std::int64_t>(l.files.at(kOrder[k]).size()));
        CHECK(r.chain[0].content_length == static_cast<std::int64_t>(l.files.at(kOrder[k]).size()));
        check_row(r.chain[0]);
    }
    const timeline::RequestEvents net = timeline::request_events(t, 0, disk.files);
    REQUIRE(net.events.size() == kOrder.size());
    const Value* status = member(net.events[2].payload, "status");
    REQUIRE(status);
    CHECK(std::holds_alternative<std::monostate>(status->v));
}

TEST_CASE("session fetch: refused while another NetworkLogger is installed, and ended by a stop",
          "[timeline][session][session_fetch]")
{
    const Ladder l = write_ladder("mirage_test_session_fetch_stop");
    const timeline::Timeline t = timeline_of(l);

    // FFmpeg's log has one callback for the process: the fetch doesn't take
    // another logger's lines (src/SessionFetch.h).
    {
        NetworkLogger other;
        other.install();
        const timeline::FetchResult got = timeline::fetch_session(files_of(t, l, ""));
        CHECK(contains(got.error, "another NetworkLogger is installed"));
        CHECK(got.files.empty());
        CHECK(got.anchor_utc.empty());
        CHECK(NetworkLogger::instance() == &other);
        other.uninstall();
    }

    // A stop requested before the fetch: no file is tried.
    std::stop_source stopped;
    stopped.request_stop();
    const timeline::FetchResult none = timeline::fetch_session(files_of(t, l, ""), stopped.get_token());
    CHECK(none.error == "a stop was requested: the fetch ended before its file 1 of 11");
    CHECK(none.files.empty());
    CHECK(NetworkLogger::instance() == nullptr);

    // A stop requested once the server has the request for v1's playlist,
    // whose response head it sends 3 s late, interrupts the open while it
    // waits for the delayed response head: the file is recorded with the
    // open's error, and the fetch ends.
    HttpFileServer server(l.dir, { .path = "v1/index.m3u8", .delay = std::chrono::milliseconds(3000) });
    REQUIRE(server.ok());
    std::stop_source stop;
    std::jthread     stopper([&stop, &server] {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
        while (server.requests("v1/index.m3u8") == 0 && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        stop.request_stop();
    });
    const auto                  start = std::chrono::steady_clock::now();
    const timeline::FetchResult got   = timeline::fetch_session(files_of(t, l, server.base()), stop.get_token());
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(3000));
    CHECK(got.error == "a stop was requested: the fetch ended at its file 2 of 11");
    REQUIRE(got.files.size() == 2);
    CHECK(got.files[0].error.empty());
    CHECK(got.files[1].error == "Immediate exit requested");   // the open's, FFmpeg's AVERROR_EXIT
    CHECK(got.files[1].chain.size() == 1);
    CHECK(NetworkLogger::instance() == nullptr);

    // A stop while a body is read: the server sends v1's segment 0's head
    // and the first 100 bytes of its body, then nothing more, the
    // connection left open. A stop requested 500 ms after it has sent them,
    // the open having had the time to read the head, interrupts the body's
    // read in its loop: the file is recorded with its status, the bytes it
    // got and its read's error, and the fetch ends there.
    HttpFileServer stalling(l.dir, { .path = "v1/seg_000.ts", .stall_after = 100 });
    REQUIRE(stalling.ok());
    std::stop_source halt;
    std::jthread     halter([&halt, &stalling] {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
        while (stalling.stalled("v1/seg_000.ts") == 0 && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        halt.request_stop();
    });
    const timeline::FetchResult halted =
        timeline::fetch_session(files_of(t, l, stalling.base()), halt.get_token());
    CHECK(halted.error == "a stop was requested: the fetch ended at its file 3 of 11");
    REQUIRE(halted.files.size() == 3);
    CHECK(halted.files[1].error.empty());
    const Request& body = halted.files[2];
    CHECK(body.url == stalling.url("v1/seg_000.ts"));
    CHECK(body.error == "Immediate exit requested after 100 bytes");
    REQUIRE(body.chain.size() == 1);
    check_row(body.chain[0]);
    CHECK(body.chain[0].status == 200);
    CHECK(body.chain[0].bytes == 100);
    CHECK(body.chain[0].content_length == static_cast<std::int64_t>(l.files.at("v1/seg_000.ts").size()));
    CHECK(NetworkLogger::instance() == nullptr);
}

TEST_CASE("session fetch: a request another thread makes through FFmpeg during the fetch, on a context "
          "nothing tracks, is in no file's rows",
          "[timeline][session][session_fetch]")
{
    const Ladder       l          = write_ladder("mirage_test_session_fetch_other_thread");
    const std::string& master     = l.files.at("master.m3u8");
    const std::string& other_file = l.files.at("v2/seg_007.ts");
    // The master is answered 1 s late. Once the server has its request,
    // another thread reads v2's segment 7 from the same server through a
    // plain avio_open2(), which NetworkLogger, the fetch's being installed,
    // rows as NetInitiator::Other: FFmpeg's log has one callback for the
    // process (design §15, R4), and the fetch keeps its own rows alone
    // (src/SessionFetch.h).
    HttpFileServer server(l.dir, { .path = "master.m3u8", .delay = std::chrono::milliseconds(1000) });
    REQUIRE(server.ok());
    std::int64_t                          other_bytes = -1;
    std::chrono::steady_clock::time_point other_done;
    std::jthread                          reader([&server, &other_bytes, &other_done] {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
        while (server.requests("master.m3u8") == 0 && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "rw_timeout", "5000000", 0);
        AVIOContext* pb = nullptr;
        if (avio_open2(&pb, server.url("v2/seg_007.ts").c_str(), AVIO_FLAG_READ, nullptr, &opts) >= 0) {
            std::vector<unsigned char> buf(4096);
            other_bytes = 0;
            for (int n = 0; (n = avio_read(pb, buf.data(), static_cast<int>(buf.size()))) > 0;) other_bytes += n;
            avio_closep(&pb);
        }
        av_dict_free(&opts);
        other_done = std::chrono::steady_clock::now();
    });
    const timeline::FetchResult got =
        timeline::fetch_session({ Request{ .kind = FetchedFile::Master, .url = server.url("master.m3u8") } });
    const auto fetched = std::chrono::steady_clock::now();
    reader.join();

    // The other thread read its file whole while the master's response was
    // awaited.
    CHECK(other_bytes == static_cast<std::int64_t>(other_file.size()));
    CHECK(other_done < fetched);
    CHECK(server.paths() == std::vector<std::string>{ "master.m3u8", "v2/seg_007.ts" });

    // The master's rows are its own alone.
    CHECK(got.error.empty());
    REQUIRE(got.files.size() == 1);
    CHECK(got.files[0].error.empty());
    REQUIRE(got.files[0].chain.size() == 1);
    const RequestRow& row = got.files[0].chain[0];
    check_row(row);
    CHECK(row.url == server.url("master.m3u8"));
    CHECK(row.status == 200);
    CHECK(row.bytes == static_cast<std::int64_t>(master.size()));
    CHECK(row.t_first_byte - row.t_sent >= 1000000);
    CHECK(NetworkLogger::instance() == nullptr);
}
