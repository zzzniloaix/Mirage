// Ladder scoring (src/VMAFLadder.h, DECISIONS.md D-029): a master playlist's
// entries, the pairs of each reference mode, the scoring of a synthetic
// ladder written at run time, whose pairs must have the results of the same
// pairs scored one by one, its pool (D-043 to D-047), and mirage_vmaf
// --ladder run as a program. The ladders are local HLS playlists of MPEG-TS
// segments, coded by FFmpeg's own MPEG-2 encoder; the [vmafnet] tests add a
// rung on a server on 127.0.0.1 that sends nothing (tests/SilentServer.h).

#include <catch2/catch_test_macros.hpp>

#include "SilentServer.h"
#include "TestTemp.h"
#include "VMAFAnalyzer.h"
#include "VMAFLadder.h"
#include "VMAFProcess.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

extern char** environ;

namespace fs = std::filesystem;
using vmafladder::Entry;
using vmafladder::Master;
using vmafladder::Plan;

namespace {

// bipbop 16x9's master as Apple serves it, in shape: five video entries, an
// audio-only one, renditions and I-frame playlists.
constexpr const char* kBipbop =
    "#EXTM3U\n"
    "\n"
    "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"bipbop_audio\",LANGUAGE=\"eng\",NAME=\"BipBop Audio 1\","
    "AUTOSELECT=YES,DEFAULT=YES\n"
    "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",NAME=\"English\",DEFAULT=YES,AUTOSELECT=YES,"
    "FORCED=NO,LANGUAGE=\"en\",URI=\"subtitles/eng/prog_index.m3u8\"\n"
    "\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=263851,CODECS=\"mp4a.40.2, avc1.4d400d\",RESOLUTION=416x234,"
    "AUDIO=\"bipbop_audio\",SUBTITLES=\"subs\"\n"
    "gear1/prog_index.m3u8\n"
    "#EXT-X-I-FRAME-STREAM-INF:BANDWIDTH=28451,CODECS=\"avc1.4d400d\",URI=\"gear1/iframe_index.m3u8\"\n"
    "\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=577610,CODECS=\"mp4a.40.2, avc1.4d401e\",RESOLUTION=640x360,"
    "AUDIO=\"bipbop_audio\",SUBTITLES=\"subs\"\n"
    "gear2/prog_index.m3u8\n"
    "\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=915905,CODECS=\"mp4a.40.2, avc1.4d401f\",RESOLUTION=960x540,"
    "AUDIO=\"bipbop_audio\",SUBTITLES=\"subs\"\n"
    "gear3/prog_index.m3u8\n"
    "\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=1030138,CODECS=\"mp4a.40.2, avc1.4d401f\",RESOLUTION=1280x720,"
    "AUDIO=\"bipbop_audio\",SUBTITLES=\"subs\"\n"
    "gear4/prog_index.m3u8\n"
    "\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=1924009,CODECS=\"mp4a.40.2, avc1.4d401f\",RESOLUTION=1920x1080,"
    "AUDIO=\"bipbop_audio\",SUBTITLES=\"subs\"\n"
    "gear5/prog_index.m3u8\n"
    "\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=41457,CODECS=\"mp4a.40.2\",AUDIO=\"bipbop_audio\",SUBTITLES=\"subs\"\n"
    "gear0/prog_index.m3u8\n";

const std::string kBipbopUrl =
    "https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8";
const std::string kBipbopDir =
    "https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/";

std::string lookup(const std::vector<std::pair<std::string, std::string>>& attrs, const char* name)
{
    std::string out = "(absent)";
    for (const auto& [k, v] : attrs)
        if (k == name) out = v;
    return out;
}

void write_text(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}

constexpr int kFps = 25;

// One rung of a synthetic ladder: <dir>/index.m3u8, an HLS media playlist of
// one MPEG-TS segment, <dir>/seg0.ts, of `frames` frames of w x h at 25 fps:
// a gray diagonal ramp that moves every frame, MPEG-2 coded, every frame a
// key frame at a fixed quantizer, BT.709 tags. Returns the playlist's path.
std::string make_rung(const fs::path& dir, int w, int h, int frames = 12)
{
    fs::create_directories(dir);
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO);
    REQUIRE(codec);
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    enc->width          = w;
    enc->height         = h;
    enc->pix_fmt        = AV_PIX_FMT_YUV420P;
    enc->time_base      = { 1, kFps };
    enc->framerate      = { kFps, 1 };
    enc->gop_size       = 1;
    enc->max_b_frames   = 0;
    enc->flags         |= AV_CODEC_FLAG_QSCALE;
    enc->global_quality = FF_QP2LAMBDA * 2;
    enc->colorspace     = AVCOL_SPC_BT709;
    enc->color_range    = AVCOL_RANGE_MPEG;
    REQUIRE(avcodec_open2(enc, codec, nullptr) >= 0);

    const fs::path seg = dir / "seg0.ts";
    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "mpegts", seg.c_str()) >= 0);
    AVStream* st = avformat_new_stream(oc, nullptr);
    REQUIRE(avcodec_parameters_from_context(st->codecpar, enc) >= 0);
    st->time_base = { 1, 90000 };
    REQUIRE(avio_open(&oc->pb, seg.c_str(), AVIO_FLAG_WRITE) >= 0);
    REQUIRE(avformat_write_header(oc, nullptr) >= 0);

    const auto drain = [&] {
        AVPacket* pkt = av_packet_alloc();
        while (avcodec_receive_packet(enc, pkt) == 0) {
            pkt->stream_index = 0;
            av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
            REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
        }
        av_packet_free(&pkt);
    };
    AVFrame* f = av_frame_alloc();
    f->format = AV_PIX_FMT_YUV420P;
    f->width  = w;
    f->height = h;
    REQUIRE(av_frame_get_buffer(f, 0) >= 0);
    for (int k = 0; k < frames; ++k) {
        REQUIRE(av_frame_make_writable(f) >= 0);
        for (int row = 0; row < h; ++row)
            for (int col = 0; col < w; ++col)
                f->data[0][row * f->linesize[0] + col] =
                    static_cast<uint8_t>((col * 256 / w + row * 128 / h + 3 * k) & 0xff);
        for (int p = 1; p < 3; ++p)
            for (int row = 0; row < (h + 1) / 2; ++row)
                std::fill_n(f->data[p] + row * f->linesize[p], (w + 1) / 2, static_cast<uint8_t>(128));
        f->pts = k;
        REQUIRE(avcodec_send_frame(enc, f) >= 0);
        drain();
    }
    REQUIRE(avcodec_send_frame(enc, nullptr) >= 0);
    drain();
    av_frame_free(&f);
    REQUIRE(av_write_trailer(oc) >= 0);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    avcodec_free_context(&enc);

    write_text(dir / "index.m3u8",
               std::format("#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:{}\n"
                           "#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PLAYLIST-TYPE:VOD\n"
                           "#EXTINF:{:.6f},\nseg0.ts\n#EXT-X-ENDLIST\n",
                           (frames + kFps - 1) / kFps, static_cast<double>(frames) / kFps));
    return (dir / "index.m3u8").string();
}

// A synthetic ladder in testtemp::dir()/mirage_test_ladder, made once: rungs
// low (96x54), mid (128x72, listed twice, with two audio groups) and top
// (192x108), the top rung in the middle of the master, and an audio-only
// entry whose playlist doesn't exist, since it is never opened.
struct Ladder {
    fs::path    dir;
    std::string master, low, mid, top;
};

const Ladder& ladder()
{
    static const Ladder l = [] {
        Ladder x;
        x.dir    = testtemp::path("mirage_test_ladder");
        x.low    = make_rung(x.dir / "low", 96, 54);
        x.mid    = make_rung(x.dir / "mid", 128, 72);
        x.top    = make_rung(x.dir / "top", 192, 108);
        x.master = (x.dir / "master.m3u8").string();
        write_text(x.master,
                   "#EXTM3U\n#EXT-X-VERSION:3\n"
                   "#EXT-X-STREAM-INF:BANDWIDTH=300000,RESOLUTION=96x54,CODECS=\"mp2v,mp4a.40.2\"\n"
                   "low/index.m3u8\n"
                   "#EXT-X-STREAM-INF:BANDWIDTH=600000,RESOLUTION=128x72,AUDIO=\"a1\"\n"
                   "mid/index.m3u8\n"
                   "#EXT-X-STREAM-INF:BANDWIDTH=900000,RESOLUTION=192x108\n"
                   "top/index.m3u8\n"
                   "#EXT-X-STREAM-INF:BANDWIDTH=640000,RESOLUTION=128x72,AUDIO=\"a2\"\n"
                   "mid/index.m3u8\n"
                   "#EXT-X-STREAM-INF:BANDWIDTH=64000,CODECS=\"mp4a.40.2\"\n"
                   "audio/index.m3u8\n");
        return x;
    }();
    return l;
}

// A ladder whose pairs warm up (vmafladder::kWarmUp) long before they end,
// in testtemp::dir()/mirage_test_ladder_long, made once: rungs low (96x54)
// and mid (128x72), and the top rung (192x108), of 20 kWarmUp frames each.
struct LongLadder {
    fs::path    dir;
    std::string master, low, mid, top;
};

const LongLadder& long_ladder()
{
    static const LongLadder l = [] {
        constexpr int frames = 20 * static_cast<int>(vmafladder::kWarmUp);
        LongLadder x;
        x.dir    = testtemp::path("mirage_test_ladder_long");
        x.low    = make_rung(x.dir / "low", 96, 54, frames);
        x.mid    = make_rung(x.dir / "mid", 128, 72, frames);
        x.top    = make_rung(x.dir / "top", 192, 108, frames);
        x.master = (x.dir / "master.m3u8").string();
        write_text(x.master,
                   "#EXTM3U\n"
                   "#EXT-X-STREAM-INF:BANDWIDTH=300000,RESOLUTION=96x54\nlow/index.m3u8\n"
                   "#EXT-X-STREAM-INF:BANDWIDTH=600000,RESOLUTION=128x72\nmid/index.m3u8\n"
                   "#EXT-X-STREAM-INF:BANDWIDTH=900000,RESOLUTION=192x108\ntop/index.m3u8\n");
        return x;
    }();
    return l;
}

// A master playlist of `uris` in its order, each a video entry with the
// BANDWIDTH after it; the highest names the top rung.
std::string master_of(const std::vector<std::pair<std::string, int>>& uris)
{
    std::string text = "#EXTM3U\n";
    for (const auto& [uri, bandwidth] : uris)
        text += std::format("#EXT-X-STREAM-INF:BANDWIDTH={},RESOLUTION=96x54\n{}\n", bandwidth,
                            uri);
    return text;
}

std::string read_text(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
}

bool contains(const std::string& s, const std::string& part)
{
    return s.find(part) != std::string::npos;
}

// What a run of the built mirage_vmaf did.
struct ToolRun {
    int         status = -1;   // waitpid()'s
    std::string out, err;      // its stdout and stderr
};

// Runs the built mirage_vmaf with `args`, SIGINT and SIGTERM at their
// defaults, and reads its stdout as it comes: `on_line` gets each line and
// the process's id, and `on_end` is called once its stdout has ended, before
// the process is waited for.
ToolRun run_tool(std::vector<std::string> args,
                 const std::function<void(const std::string&, pid_t)>& on_line = {},
                 const std::function<void()>& on_end = {})
{
    static int runs = 0;
    const fs::path err_path = testtemp::path(std::format("mirage_vmaf_stderr_{}.txt", ++runs));
    int out[2];
    REQUIRE(::pipe(out) == 0);
    posix_spawn_file_actions_t files;
    posix_spawn_file_actions_init(&files);
    posix_spawn_file_actions_adddup2(&files, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&files, out[0]);
    posix_spawn_file_actions_addclose(&files, out[1]);
    posix_spawn_file_actions_addopen(&files, STDERR_FILENO, err_path.c_str(),
                                     O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t none, defaults;
    sigemptyset(&none);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGINT);
    sigaddset(&defaults, SIGTERM);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setsigdefault(&attr, &defaults);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    args.insert(args.begin(), MIRAGE_VMAF_PATH);
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, argv[0], &files, &attr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&files);
    posix_spawnattr_destroy(&attr);
    ::close(out[1]);
    if (spawned != 0) ::close(out[0]);
    REQUIRE(spawned == 0);

    ToolRun run;
    std::string pending;
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(out[0], buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        pending.append(buf, static_cast<std::size_t>(n));
        for (std::size_t nl; (nl = pending.find('\n')) != std::string::npos;) {
            const std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            run.out += line + '\n';
            if (on_line) on_line(line, pid);
        }
    }
    run.out += pending;
    ::close(out[0]);
    if (on_end) on_end();
    while (::waitpid(pid, &run.status, 0) < 0 && errno == EINTR) {}
    run.err = read_text(err_path);
    return run;
}

// Checks that `got`, a pair of a ladder run, has the result of `want`, the
// same two inputs scored on their own: every score, timestamp, position,
// pooled value and what the analysis recorded about the pair.
void check_same_pair(const VMAFResult& got, const VMAFResult& want)
{
    REQUIRE(want.error.empty());
    CHECK(got.ref_url == want.ref_url);
    CHECK(got.dis_url == want.dis_url);
    CHECK(got.per_frame == want.per_frame);   // ==: to the last bit
    CHECK(got.ref_pts == want.ref_pts);
    CHECK(got.dis_pts == want.dis_pts);
    CHECK(got.ref_index == want.ref_index);
    CHECK(got.dis_index == want.dis_index);
    CHECK(got.pooled.mean == want.pooled.mean);
    CHECK(got.pooled.harmonic_mean == want.pooled.harmonic_mean);
    CHECK(got.pooled.min == want.pooled.min);
    CHECK(got.pooled.p5 == want.pooled.p5);
    CHECK(got.pooled.p50 == want.pooled.p50);
    CHECK(got.settings == want.settings);
    CHECK(got.width == want.width);
    CHECK(got.height == want.height);
    CHECK(got.alignment.paired == want.alignment.paired);
    CHECK(got.alignment.failed == want.alignment.failed);
    CHECK(got.alignment.contexts == want.alignment.contexts);
    CHECK(got.warnings == want.warnings);
    CHECK(got.dis_conversions.size() == want.dis_conversions.size());
    CHECK(got.ref_info.width == want.ref_info.width);
    CHECK(got.dis_info.width == want.dis_info.width);
    CHECK(got.ref_info.ended == want.ref_info.ended);
    CHECK(got.dis_info.ended == want.dis_info.ended);
}

} // namespace

// ── The master playlist ──────────────────────────────────────────────────────

TEST_CASE("vmafladder::attributes: an attribute list read as fetch_streams.py reads it", "[ladder]")
{
    const auto a = vmafladder::attributes(
        "AVERAGE-BANDWIDTH=2168183,BANDWIDTH=2177116,CODECS=\"avc1.640020,mp4a.40.2\","
        "RESOLUTION=960x540,AUDIO=\"aud 1\"");
    REQUIRE(a.size() == 5);
    CHECK(lookup(a, "BANDWIDTH") == "2177116");   // not inside AVERAGE-BANDWIDTH
    CHECK(lookup(a, "AVERAGE-BANDWIDTH") == "2168183");
    CHECK(lookup(a, "CODECS") == "avc1.640020,mp4a.40.2");   // a quoted comma
    CHECK(lookup(a, "RESOLUTION") == "960x540");
    CHECK(lookup(a, "AUDIO") == "aud 1");

    // A name given twice: both listed, lookups take the last.
    CHECK(lookup(vmafladder::attributes("BANDWIDTH=1,BANDWIDTH=2"), "BANDWIDTH") == "2");
    // Lower-case names aren't names; an unquoted value ends at ',' or '"'.
    CHECK(vmafladder::attributes("bandwidth=5").empty());
    CHECK(lookup(vmafladder::attributes("X=a\"b\",Y=1"), "X") == "a");
    CHECK(lookup(vmafladder::attributes("X=a\"b\",Y=1"), "Y") == "1");
    // A quote that doesn't close gives an empty value, as the regular expression does.
    CHECK(lookup(vmafladder::attributes("CODECS=\"avc1,RESOLUTION=1x1"), "CODECS").empty());
    CHECK(lookup(vmafladder::attributes("CODECS=\"avc1,RESOLUTION=1x1"), "RESOLUTION") == "1x1");
    CHECK(lookup(vmafladder::attributes("A=,B=2"), "A").empty());
}

TEST_CASE("vmafladder::has_video: a RESOLUTION, or a video codec in CODECS", "[ladder]")
{
    using S = std::optional<std::string>;
    CHECK(vmafladder::has_video(S("416x234"), S("mp4a.40.2")));   // RESOLUTION decides
    CHECK(vmafladder::has_video(S(""), std::nullopt));            // given, even empty
    CHECK(vmafladder::has_video(std::nullopt, S("mp4a.40.2, avc1.4d400d")));
    CHECK(vmafladder::has_video(std::nullopt, S("hvc1.2.4.L123.B0")));
    CHECK(vmafladder::has_video(std::nullopt, S(" av01.0.08M.08 ")));
    CHECK(vmafladder::has_video(std::nullopt, S("vp09")));        // no '.'
    for (const char* name : { "avc1", "avc3", "hvc1", "hev1", "dvh1", "dvhe", "av01", "vp09",
                              "vp08", "mp4v" }) {
        INFO(name);
        CHECK(vmafladder::has_video(std::nullopt, S(std::format("mp4a.40.2,{}.1", name))));
    }
    CHECK_FALSE(vmafladder::has_video(std::nullopt, S("mp4a.40.2")));
    CHECK_FALSE(vmafladder::has_video(std::nullopt, S("ec-3,stpp.ttml.im1t")));
    CHECK_FALSE(vmafladder::has_video(std::nullopt, S("AVC1.4d401f")));   // case matters
    CHECK_FALSE(vmafladder::has_video(std::nullopt, S("")));
    CHECK_FALSE(vmafladder::has_video(std::nullopt, std::nullopt));
}

TEST_CASE("vmafladder::resolve: URIs against an http(s) location and a local one", "[ladder]")
{
    CHECK(vmafladder::resolve(kBipbopUrl, "gear1/prog_index.m3u8") ==
          kBipbopDir + "gear1/prog_index.m3u8");
    CHECK(vmafladder::resolve("https://h/a/m.m3u8?t=1", "/b/p.m3u8") == "https://h/b/p.m3u8");
    CHECK(vmafladder::resolve("https://h/a/m.m3u8", "https://o/p.m3u8") == "https://o/p.m3u8");
    // Forms FFmpeg would resolve another way, or not at all: refused.
    for (const char* uri : { "../p.m3u8", "./p.m3u8", "//o/p.m3u8", "?x=1", "skd://k", "a b.m3u8" }) {
        INFO(uri);
        CHECK(vmafladder::resolve("https://h/a/m.m3u8", uri).empty());
    }

    CHECK(vmafladder::resolve("/data/ladder/master.m3u8", "low/index.m3u8") ==
          "/data/ladder/low/index.m3u8");
    CHECK(vmafladder::resolve("ladder/master.m3u8", "../x/i.m3u8") == "ladder/../x/i.m3u8");
    CHECK(vmafladder::resolve("master.m3u8", "low/index.m3u8") == "low/index.m3u8");
    CHECK(vmafladder::resolve("/data/master.m3u8", "/abs/i.m3u8") == "/abs/i.m3u8");
    CHECK(vmafladder::resolve("/data/master.m3u8", "https://h/i.m3u8") == "https://h/i.m3u8");
    CHECK(vmafladder::resolve("/data/master.m3u8", "file:/x/i.m3u8") == "file:/x/i.m3u8");
    CHECK(vmafladder::resolve("file:/data/master.m3u8", "i.m3u8") == "file:/data/i.m3u8");
    CHECK(vmafladder::resolve("/data/master.m3u8", "").empty());
}

TEST_CASE("vmafladder::parse_master: bipbop 16x9's entries", "[ladder]")
{
    const Master m = vmafladder::parse_master(kBipbop, kBipbopUrl);
    REQUIRE(m.error.empty());
    REQUIRE(m.entries.size() == 6);
    const std::vector<int64_t> bandwidths = { 263851, 577610, 915905, 1030138, 1924009, 41457 };
    for (std::size_t i = 0; i < 6; ++i) {
        INFO(i);
        const Entry& e = m.entries[i];
        CHECK(e.index == i);
        CHECK(e.bandwidth == bandwidths[i]);
        const std::string gear = std::format("gear{}/prog_index.m3u8", i < 5 ? i + 1 : 0);
        CHECK(e.uri == gear);
        CHECK(e.url == kBipbopDir + gear);
        CHECK(e.video == (i < 5));
        CHECK_FALSE(e.average_bandwidth);
        CHECK_FALSE(e.frame_rate);
    }
    CHECK(m.entries[0].line == 6);
    CHECK(m.entries[0].codecs == "mp4a.40.2, avc1.4d400d");
    CHECK(m.entries[0].resolution == "416x234");
    CHECK(m.entries[4].resolution == "1920x1080");
    CHECK_FALSE(m.entries[5].resolution);
    CHECK(m.entries[5].codecs == "mp4a.40.2");

    // CRLF line ends, white space around lines, comments and tags between an
    // EXT-X-STREAM-INF and its URI.
    const Master crlf = vmafladder::parse_master(
        "#EXTM3U\r\n  #EXT-X-STREAM-INF:BANDWIDTH=5,FRAME-RATE=29.970,AVERAGE-BANDWIDTH=4\r\n"
        "# a comment\r\n#EXT-X-SOMETHING\r\n\r\n  v/p.m3u8  \r\n",
        "https://h/m.m3u8");
    REQUIRE(crlf.error.empty());
    REQUIRE(crlf.entries.size() == 1);
    CHECK(crlf.entries[0].uri == "v/p.m3u8");
    CHECK(crlf.entries[0].url == "https://h/v/p.m3u8");
    CHECK(crlf.entries[0].frame_rate == "29.970");
    CHECK(crlf.entries[0].average_bandwidth == "4");
    CHECK_FALSE(crlf.entries[0].video);   // neither RESOLUTION nor CODECS
}

TEST_CASE("vmafladder::parse_master: what isn't a master playlist it reads", "[ladder]")
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        { "", "it is empty, so it isn't an HLS playlist" },
        { "\xEF\xBB\xBF#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\na.m3u8\n",
          "it doesn't start with #EXTM3U, so it isn't an HLS playlist" },
        { "#EXT-X-STREAM-INF:BANDWIDTH=1\na.m3u8\n",
          "it doesn't start with #EXTM3U, so it isn't an HLS playlist" },
        { "#EXTM3U\n#EXT-X-VERSION:3\n",
          "it has no EXT-X-STREAM-INF entry, so it isn't a master playlist" },
        { "#EXTM3U\n#EXT-X-TARGETDURATION:10\n#EXTINF:9.97,\nseg0.ts\n",
          "line 4: a URI without an EXT-X-STREAM-INF before it: a media playlist's segment? "
          "--ladder takes a master playlist" },
        { "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\n#EXT-X-STREAM-INF:BANDWIDTH=2\nb.m3u8\n",
          "line 3: an EXT-X-STREAM-INF before the URI line of the one on line 2" },
        { "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\na.m3u8\n#EXT-X-STREAM-INF:BANDWIDTH=2\n\n",
          "the EXT-X-STREAM-INF on line 4 has no URI line after it" },
        { "#EXTM3U\n#EXT-X-STREAM-INF:RESOLUTION=1x1\na.m3u8\n",
          "line 2: the EXT-X-STREAM-INF has no BANDWIDTH" },
        { "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=../a.m3u8\n",   // value up to the end
          "line 2: BANDWIDTH '../a.m3u8' is not a decimal integer up to 9223372036854775807" },
        { "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=\"../\"\n../a.m3u8\n",
          "line 2: BANDWIDTH '../' is not a decimal integer up to 9223372036854775807" },
        { "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\n../a.m3u8\n",
          "line 3: the URI '../a.m3u8' is not one mirage_vmaf resolves against "
          "https://h/x/m.m3u8" },
    };
    for (const auto& [text, want] : cases) {
        INFO(text);
        const Master m = vmafladder::parse_master(text, "https://h/x/m.m3u8");
        CHECK(m.error == want);
        CHECK(m.entries.empty());
    }
    for (const char* bad : { "-5", "+5", "1e6", "5 ", "", "9223372036854775808", "0x10" }) {
        INFO(bad);
        const Master m = vmafladder::parse_master(
            std::format("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH={},RESOLUTION=1x1\na.m3u8\n", bad),
            "https://h/m.m3u8");
        CHECK(m.error.starts_with("line 2: BANDWIDTH '"));
    }
    const Master big = vmafladder::parse_master(
        "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=9223372036854775807\na.m3u8\n", "https://h/m.m3u8");
    REQUIRE(big.error.empty());
    CHECK(big.entries[0].bandwidth == 9223372036854775807);
}

// ── The plan of each reference mode ──────────────────────────────────────────

TEST_CASE("vmafladder::plan: top-rung mode on bipbop 16x9", "[ladder]")
{
    const Plan p = vmafladder::plan(vmafladder::parse_master(kBipbop, kBipbopUrl), "");
    REQUIRE(p.error.empty());
    CHECK(p.reference_mode == "top-rung");
    CHECK(p.reference_url == kBipbopDir + "gear5/prog_index.m3u8");
    CHECK(p.reference_entries == std::vector<std::size_t>{ 4 });
    REQUIRE(p.pairs.size() == 4);
    for (std::size_t k = 0; k < 4; ++k) {
        INFO(k);
        CHECK(p.pairs[k].url == kBipbopDir + std::format("gear{}/prog_index.m3u8", k + 1));
        CHECK(p.pairs[k].entries == std::vector<std::size_t>{ k });
    }
    REQUIRE(p.skipped.size() == 1);
    CHECK(p.skipped[0].entry == 5);   // gear0, audio only
    CHECK(p.skipped[0].reason == "no video");
    CHECK(p.warnings.empty());
}

TEST_CASE("vmafladder::plan: mezzanine mode scores the top rung too", "[ladder]")
{
    const Plan p = vmafladder::plan(vmafladder::parse_master(kBipbop, kBipbopUrl), "source.mov");
    REQUIRE(p.error.empty());
    CHECK(p.reference_mode == "mezzanine");
    CHECK(p.reference_url == "source.mov");
    CHECK(p.reference_entries.empty());
    REQUIRE(p.pairs.size() == 5);
    for (std::size_t k = 0; k < 5; ++k) {
        INFO(k);
        CHECK(p.pairs[k].entries == std::vector<std::size_t>{ k });
    }
    CHECK(p.pairs[4].url == kBipbopDir + "gear5/prog_index.m3u8");
    REQUIRE(p.skipped.size() == 1);
    CHECK(p.skipped[0].entry == 5);
}

TEST_CASE("vmafladder::plan: each video playlist once, the reference's other entries with it", "[ladder]")
{
    // Apple's advanced sample's shape: every video playlist once per audio
    // group, with BANDWIDTH counting the audio too.
    const Master m = vmafladder::parse_master(
        "#EXTM3U\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=2177116,RESOLUTION=960x540,AUDIO=\"a1\"\nv5/p.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=8001098,RESOLUTION=1920x1080,AUDIO=\"a1\"\nv9/p.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=541052,RESOLUTION=480x270,AUDIO=\"a1\"\nv2/p.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=2399619,RESOLUTION=960x540,AUDIO=\"a2\"\nv5/p.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=8223601,RESOLUTION=1920x1080,AUDIO=\"a2\"\nv9/p.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=763555,RESOLUTION=480x270,AUDIO=\"a2\"\nv2/p.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=50000,CODECS=\"mp4a.40.2\"\nv9/p.m3u8\n",
        "https://h/m.m3u8");
    REQUIRE(m.error.empty());
    const Plan p = vmafladder::plan(m, "");
    REQUIRE(p.error.empty());
    CHECK(p.reference_url == "https://h/v9/p.m3u8");
    CHECK(p.reference_entries == std::vector<std::size_t>{ 1, 4 });
    REQUIRE(p.pairs.size() == 2);
    CHECK(p.pairs[0].url == "https://h/v5/p.m3u8");
    CHECK(p.pairs[0].entries == std::vector<std::size_t>{ 0, 3 });
    CHECK(p.pairs[1].url == "https://h/v2/p.m3u8");
    CHECK(p.pairs[1].entries == std::vector<std::size_t>{ 2, 5 });
    // An entry without video is skipped, even where it names a video playlist.
    REQUIRE(p.skipped.size() == 1);
    CHECK(p.skipped[0].entry == 6);
    CHECK(p.warnings.empty());   // the second-highest BANDWIDTH names the same playlist

    const Plan mz = vmafladder::plan(m, "src.y4m");
    REQUIRE(mz.pairs.size() == 3);
    CHECK(mz.pairs[1].url == "https://h/v9/p.m3u8");
    CHECK(mz.pairs[1].entries == std::vector<std::size_t>{ 1, 4 });
}

TEST_CASE("vmafladder::plan: two playlists with the highest BANDWIDTH", "[ladder]")
{
    const Master m = vmafladder::parse_master(
        "#EXTM3U\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=100,RESOLUTION=1x1\nlow.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=900,CODECS=\"hvc1.1\"\nhevc.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=900,CODECS=\"avc1.64\"\navc.m3u8\n",
        "/d/m.m3u8");
    const Plan p = vmafladder::plan(m, "");
    REQUIRE(p.error.empty());
    CHECK(p.reference_url == "/d/hevc.m3u8");   // the first of them in the master
    CHECK(p.reference_entries == std::vector<std::size_t>{ 1 });
    REQUIRE(p.pairs.size() == 2);
    CHECK(p.pairs[1].url == "/d/avc.m3u8");
    REQUIRE(p.warnings.size() == 1);
    CHECK(p.warnings[0] == "entries 1 and 2 both have the highest BANDWIDTH, 900, and name "
                           "different playlists; the top rung is entry 1, the first of them in "
                           "the master");
    CHECK(vmafladder::plan(m, "src.mov").warnings.empty());   // no top rung in mezzanine mode
}

TEST_CASE("vmafladder::plan: nothing to score", "[ladder]")
{
    const Master audio = vmafladder::parse_master(
        "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=64000,CODECS=\"mp4a.40.2\"\na.m3u8\n", "/d/m.m3u8");
    CHECK(vmafladder::plan(audio, "").error == "no entry of the master playlist has video");
    CHECK(vmafladder::plan(audio, "src.mov").error == "no entry of the master playlist has video");

    const Master one = vmafladder::parse_master(
        "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=900,RESOLUTION=2x2\nv.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=950,RESOLUTION=2x2,AUDIO=\"b\"\nv.m3u8\n",
        "/d/m.m3u8");
    const Plan top = vmafladder::plan(one, "");
    CHECK(top.error == "the only video playlist of the master is the top rung (entries 0, 1): "
                       "there is nothing to score against it");
    const Plan mz = vmafladder::plan(one, "src.mov");
    REQUIRE(mz.error.empty());
    REQUIRE(mz.pairs.size() == 1);
    CHECK(mz.pairs[0].entries == std::vector<std::size_t>{ 0, 1 });

    Master broken;
    broken.error = "it is empty, so it isn't an HLS playlist";
    CHECK(vmafladder::plan(broken, "").error == broken.error);
    CHECK(vmafladder::plan(broken, "").reference_mode == "top-rung");
}

// ── Reading the master playlist ──────────────────────────────────────────────

TEST_CASE("vmafladder::read_master: a local file, and what isn't one to read", "[ladder]")
{
    const fs::path dir = testtemp::path("mirage_test_ladder_read");
    fs::create_directories(dir);
    const fs::path master = dir / "master.m3u8";
    write_text(master, kBipbop);
    const vmafladder::Fetched f = vmafladder::read_master(master.string());
    REQUIRE(f.error.empty());
    CHECK(f.text == kBipbop);
    CHECK(f.location == master.string());   // no redirect for a file
    CHECK(f.sha256.size() == 64);

    const vmafladder::Fetched missing = vmafladder::read_master((dir / "none.m3u8").string());
    CHECK(missing.error == "No such file or directory");
    CHECK(missing.text.empty());

    // A video file: refused at its first bytes, not read to its end.
    const fs::path video = dir / "video.ts";
    write_text(video, std::string(1 << 20, '\x47'));
    const vmafladder::Fetched not_m3u = vmafladder::read_master(video.string());
    CHECK(not_m3u.error == "it doesn't start with #EXTM3U, so it isn't an HLS playlist");
    CHECK(not_m3u.text.empty());
    CHECK(not_m3u.sha256.empty());

    write_text(dir / "empty.m3u8", "");
    const vmafladder::Fetched empty = vmafladder::read_master((dir / "empty.m3u8").string());
    CHECK(empty.error.empty());   // parse_master() says what is wrong with it
    CHECK(empty.text.empty());
    fs::remove_all(dir);
}

// ── Scoring a ladder ─────────────────────────────────────────────────────────

TEST_CASE("vmafladder::score: top-rung mode, each pair as it scores on its own", "[ladder][vmaf]")
{
    const Ladder& l = ladder();
    const VMAFOptions opts{ .record_pts = true };
    std::vector<std::pair<std::size_t, std::size_t>> calls;
    const vmafladder::Result r = vmafladder::score(
        l.master, "", opts, { .jobs = 1 }, {},
        [&](std::size_t k, std::size_t n, const VMAFResult&) { calls.emplace_back(k, n); });
    REQUIRE(r.error.empty());
    CHECK(r.status == "complete");
    CHECK(r.jobs == 1);
    CHECK(r.max_running == 1);
    CHECK(r.ladder == l.master);
    CHECK(r.fetched.location == l.master);
    CHECK(r.master.entries.size() == 5);
    CHECK(r.plan.reference_mode == "top-rung");
    CHECK(r.plan.reference_url == l.top);
    CHECK(r.plan.reference_entries == std::vector<std::size_t>{ 2 });
    REQUIRE(r.plan.pairs.size() == 2);
    CHECK(r.plan.pairs[0].entries == std::vector<std::size_t>{ 0 });
    CHECK(r.plan.pairs[1].entries == std::vector<std::size_t>{ 1, 3 });
    REQUIRE(r.plan.skipped.size() == 1);
    CHECK(r.plan.skipped[0].entry == 4);   // never opened: its playlist doesn't exist
    CHECK(calls == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 2 }, { 1, 2 } });

    REQUIRE(r.pairs.size() == 2);
    for (const vmafladder::PairRun& p : r.pairs) {
        CHECK(p.status == vmafladder::PairStatus::Complete);
        CHECK(p.started >= 0.0);
        CHECK(p.ended >= p.started);
    }
    CHECK(r.pairs[1].started >= r.pairs[0].ended);   // one after the other
    CHECK(r.pairs[0].result.per_frame.size() == 12);
    check_same_pair(r.pairs[0].result, VMAFAnalyzer::analyze(l.top, l.low, opts));
    check_same_pair(r.pairs[1].result, VMAFAnalyzer::analyze(l.top, l.mid, opts));
    CHECK(r.pairs[0].result.per_frame != r.pairs[1].result.per_frame);   // two rungs, two series
}

TEST_CASE("vmafladder::score: mezzanine mode, the frame limit of each pair", "[ladder][vmaf]")
{
    const Ladder& l = ladder();
    const std::string mezzanine = make_rung(l.dir / "mezzanine", 192, 108);
    const VMAFOptions opts{ .max_frames = 5, .record_pts = true, .scaler = VMAFScaler::Lanczos };
    const vmafladder::Result r = vmafladder::score(l.master, mezzanine, opts);
    REQUIRE(r.error.empty());
    CHECK(r.plan.reference_mode == "mezzanine");
    CHECK(r.plan.reference_url == mezzanine);
    CHECK(r.plan.reference_entries.empty());
    REQUIRE(r.plan.pairs.size() == 3);   // low, mid, top
    CHECK(r.plan.pairs[2].url == l.top);
    REQUIRE(r.pairs.size() == 3);
    const std::vector<std::string> rungs = { l.low, l.mid, l.top };
    for (std::size_t k = 0; k < 3; ++k) {
        INFO(k);
        CHECK(r.pairs[k].result.per_frame.size() == 5);
        CHECK(r.pairs[k].result.settings.scaler == "lanczos");
        check_same_pair(r.pairs[k].result, VMAFAnalyzer::analyze(mezzanine, rungs[k], opts));
    }
}

TEST_CASE("vmafladder::score: a pair that can't be scored ends the run", "[ladder][vmaf]")
{
    const Ladder& l = ladder();
    const fs::path dir = testtemp::path("mirage_test_ladder_broken");
    fs::create_directories(dir);
    const std::string master = (dir / "master.m3u8").string();
    // The second pair's playlist doesn't exist; the third would be scored.
    write_text(master, std::format("#EXTM3U\n"
                                   "#EXT-X-STREAM-INF:BANDWIDTH=900000,RESOLUTION=192x108\n{}\n"
                                   "#EXT-X-STREAM-INF:BANDWIDTH=300000,RESOLUTION=96x54\n{}\n"
                                   "#EXT-X-STREAM-INF:BANDWIDTH=200000,RESOLUTION=96x54\n"
                                   "missing/index.m3u8\n"
                                   "#EXT-X-STREAM-INF:BANDWIDTH=600000,RESOLUTION=128x72\n{}\n",
                                   l.top, l.low, l.mid));
    // One pair at a time: the third doesn't start once the second has failed.
    int calls = 0;
    const vmafladder::Result r = vmafladder::score(
        master, "", {}, { .jobs = 1 }, {},
        [&](std::size_t, std::size_t, const VMAFResult&) { ++calls; });
    const std::string missing = (dir / "missing/index.m3u8").string();
    CHECK(r.status == "failed");
    CHECK(r.error == std::format("pair 2 of 3, {} against {}: cannot open distorted: {}", missing,
                                 l.top, missing));
    REQUIRE(r.pairs.size() == 3);
    CHECK(r.pairs[0].status == vmafladder::PairStatus::Complete);
    CHECK(r.pairs[0].result.error.empty());
    CHECK(r.pairs[1].status == vmafladder::PairStatus::Failed);
    CHECK(r.pairs[1].why == "cannot open distorted: " + missing);
    CHECK(r.pairs[2].status == vmafladder::PairStatus::NotStarted);
    CHECK(r.pairs[2].started == -1.0);
    CHECK(calls == 1);

    const vmafladder::Result none = vmafladder::score((dir / "none.m3u8").string(), "", {});
    CHECK(none.error == std::format("cannot read the master playlist {}: No such file or directory",
                                    (dir / "none.m3u8").string()));
    CHECK(none.status.empty());   // nothing scored
    CHECK(none.pairs.empty());

    write_text(dir / "media.m3u8", "#EXTM3U\n#EXTINF:1,\nseg.ts\n");
    const vmafladder::Result media = vmafladder::score((dir / "media.m3u8").string(), "", {});
    CHECK(media.error == std::format("the master playlist {}: line 3: a URI without an "
                                     "EXT-X-STREAM-INF before it: a media playlist's segment? "
                                     "--ladder takes a master playlist",
                                     (dir / "media.m3u8").string()));
    CHECK(media.status.empty());
    fs::remove_all(dir);
}

TEST_CASE("vmafladder::score: a pair whose hls demuxer goes on without a segment ends the run",
          "[ladder][vmaf][segments]")
{
    const Ladder& l = ladder();
    const fs::path dir = testtemp::path("mirage_test_ladder_lost_segment");
    // The low rung's playlist lists a second segment, which doesn't exist:
    // the demuxer tries it 11 times and goes on without it (D-055).
    make_rung(dir / "low", 96, 54);
    write_text(dir / "low/index.m3u8",
               "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:1\n#EXT-X-MEDIA-SEQUENCE:0\n"
               "#EXT-X-PLAYLIST-TYPE:VOD\n#EXTINF:0.480000,\nseg0.ts\n#EXTINF:0.480000,\n"
               "gone.ts\n#EXT-X-ENDLIST\n");
    const std::string low    = (dir / "low/index.m3u8").string();
    const std::string master = (dir / "master.m3u8").string();
    write_text(master, master_of({ { l.top, 900000 }, { low, 300000 }, { l.mid, 600000 } }));
    const vmafladder::Result r = vmafladder::score(master, "", {}, { .jobs = 1 });
    const std::string why = std::format(
        "reading the distorted input stopped on an error: the hls demuxer went on without {}, "
        "which it couldn't open in 11 tries: No such file or directory",
        (dir / "low/gone.ts").string());
    CHECK(r.status == "failed");
    CHECK(r.error == std::format("pair 1 of 2, {} against {}: {}", low, l.top, why));
    REQUIRE(r.pairs.size() == 2);
    CHECK(r.pairs[0].status == vmafladder::PairStatus::Failed);
    CHECK(r.pairs[0].why == why);
    CHECK(r.pairs[1].status == vmafladder::PairStatus::NotStarted);
    fs::remove_all(dir);
}

TEST_CASE("vmafladder::score: a model file that changes during the run ends it", "[ladder][vmaf]")
{
    const fs::path file = fs::path(MIRAGE_LIBVMAF_MODEL_DIR) / "vmaf_v0.6.1.json";
    if (!fs::exists(file)) SKIP("no libvmaf model file at " << file.string());
    const Ladder& l = ladder();
    const fs::path copy = testtemp::path("mirage_test_ladder_model.json");
    fs::copy_file(file, copy, fs::copy_options::overwrite_existing);
    // After the first pair, the file gets one more newline: other bytes, the
    // same model, so the second pair's settings name another SHA-256.
    const vmafladder::Result r = vmafladder::score(
        l.master, "", { .model = { .path = copy.string() } }, { .jobs = 1 }, {},
        [&](std::size_t k, std::size_t, const VMAFResult&) {
            if (k == 0) std::ofstream(copy, std::ios::binary | std::ios::app) << '\n';
        });
    REQUIRE(r.pairs.size() == 2);
    CHECK(r.status == "failed");
    CHECK(r.pairs[0].status == vmafladder::PairStatus::Complete);
    CHECK(r.pairs[1].status == vmafladder::PairStatus::Failed);
    const std::string before = r.pairs[0].result.settings.model_sha256;
    const std::string after  = r.pairs[1].result.settings.model_sha256;
    CHECK(before != after);
    CHECK(r.error == std::format("pair 2 of 2 was scored with other settings than pair 1, model {} "
                                 "with the SHA-256 {} against {}: did the model file change "
                                 "during the run?",
                                 copy.string(), after, before));
    fs::remove(copy);
}

TEST_CASE("vmafladder::read_master: a server that sends nothing fails at the read limit",
          "[ladder][vmafnet]")
{
    // The server would hold the connection for 30 s; the read gives up after
    // 5 s, the analyzer's limit (DECISIONS.md D-036).
    SilentServer server;
    REQUIRE(server.ok());
    const auto t0 = std::chrono::steady_clock::now();
    const vmafladder::Fetched f = vmafladder::read_master(server.url("master.m3u8"));
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    INFO(f.error << ", after " << s << " s");
    CHECK_FALSE(f.error.empty());
    CHECK(f.text.empty());
    CHECK(server.accepted() >= 1);
    CHECK(s >= 4.0);
    CHECK(s < 15.0);
}

TEST_CASE("vmafladder::score: a cancel ends a read of the master playlist that waits",
          "[ladder][vmafnet]")
{
    // As above, and a cancel 200 ms after the server has taken the
    // connection: the callback on the run's stop token ends the read, and
    // the run, without a master to plan by, has nothing to report.
    SilentServer server;
    REQUIRE(server.ok());
    const std::string url = server.url("master.m3u8");
    std::stop_source cancel;
    std::jthread canceller([&] {
        server.wait_accepted(1, std::chrono::seconds(3));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        cancel.request_stop();
    });
    const auto t0 = std::chrono::steady_clock::now();
    const vmafladder::Result r = vmafladder::score(url, "", {}, {}, cancel.get_token());
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    INFO(r.error << ", after " << s << " s");
    CHECK(server.accepted() >= 1);
    CHECK(s < 2.0);   // well before the read limit
    CHECK(r.status.empty());
    CHECK(r.error ==
          std::format("cannot read the master playlist {}: Immediate exit requested", url));
    CHECK(r.fetched.text.empty());
    CHECK(r.pairs.empty());

    // Cancelled before it starts: the read that waits ends as soon as it has
    // gone on for 100 ms. A local master is read before a cancel can end it,
    // as in "a cancel keeps the complete pairs and starts no other".
    std::stop_source early;
    early.request_stop();
    const auto t1 = std::chrono::steady_clock::now();
    const vmafladder::Result e = vmafladder::score(url, "", {}, {}, early.get_token());
    const double s1 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
    INFO(e.error << ", after " << s1 << " s");
    CHECK(s1 < 2.0);
    CHECK(e.status.empty());
    CHECK(e.error == r.error);
}

// ── The pool (work order section 2; DECISIONS.md D-043 to D-047) ─────────────

TEST_CASE("vmafladder::score: every pool size gives every pair the same scores", "[ladder][vmaf]")
{
    const Ladder& l = ladder();
    const std::string mezzanine = make_rung(l.dir / "mezzanine", 192, 108);
    const VMAFOptions opts{ .record_pts = true };
    const vmafladder::Result one = vmafladder::score(l.master, mezzanine, opts, { .jobs = 1 });
    REQUIRE(one.status == "complete");
    REQUIRE(one.pairs.size() == 3);
    CHECK(one.max_running == 1);
    for (unsigned jobs : { 2u, 3u, 8u }) {
        INFO(jobs);
        const vmafladder::Result r = vmafladder::score(l.master, mezzanine, opts, { .jobs = jobs });
        REQUIRE(r.status == "complete");
        CHECK(r.jobs == jobs);
        CHECK(r.max_running >= 1);
        CHECK(r.max_running <= std::min(jobs, 3u));
        REQUIRE(r.pairs.size() == 3);
        for (std::size_t k = 0; k < 3; ++k) {
            INFO(k);
            CHECK(r.pairs[k].result.dis_url == one.pairs[k].result.dis_url);   // the plan's order
            CHECK(r.pairs[k].result.per_frame == one.pairs[k].result.per_frame);
            CHECK(r.pairs[k].result.ref_pts == one.pairs[k].result.ref_pts);
        }
    }
    // The default size: the machine's physical cores minus one, at least 1.
    const vmafladder::Result d = vmafladder::score(l.master, mezzanine, opts);
    CHECK(d.jobs == vmafproc::default_jobs());
}

TEST_CASE("vmafladder::score: a cancel keeps the complete pairs and starts no other", "[ladder][vmaf]")
{
    const Ladder& l = ladder();
    std::stop_source cancel;
    const vmafladder::Result r = vmafladder::score(
        l.master, "", {}, { .jobs = 1 }, cancel.get_token(),
        [&](std::size_t k, std::size_t, const VMAFResult&) {
            if (k == 0) cancel.request_stop();
        });
    CHECK(r.status == "cancelled");
    CHECK(r.error == "the run was cancelled: 1 of 2 pairs complete");
    REQUIRE(r.pairs.size() == 2);
    CHECK(r.pairs[0].status == vmafladder::PairStatus::Complete);
    CHECK(r.pairs[0].result.per_frame.size() == 12);
    CHECK(r.pairs[1].status == vmafladder::PairStatus::NotStarted);

    // Cancelled before it starts: the plan, and no pair.
    std::stop_source early;
    early.request_stop();
    const vmafladder::Result e = vmafladder::score(l.master, "", {}, {}, early.get_token());
    CHECK(e.status == "cancelled");
    CHECK(e.plan.pairs.size() == 2);
    REQUIRE(e.pairs.size() == 2);
    CHECK(e.pairs[0].status == vmafladder::PairStatus::NotStarted);
    CHECK(e.pairs[1].status == vmafladder::PairStatus::NotStarted);
    CHECK(e.settings == vmaf_settings({}));   // for the report, with no pair complete
}

TEST_CASE("vmafladder::score: the memory limit holds pairs back", "[ladder][vmaf]")
{
    const Ladder& l = ladder();
    const std::string mezzanine = make_rung(l.dir / "mezzanine", 192, 108);
    // A stand-in for the resident memory: 100 bytes before the run, 1000 once
    // it has started.
    std::atomic<int> reads{ 0 };
    const auto resident = [&] { return reads.fetch_add(1) == 0 ? std::uint64_t{ 100 } : 1000; };

    // The pairs, of 12 frame pairs each, end before they warm up (kWarmUp), so
    // what a pair uses is never measured: one pair at a time, though three may
    // run (vmafladder::admits is tested on its own below).
    static_assert(vmafladder::kWarmUp > 12);
    reads = 0;
    const vmafladder::Result held = vmafladder::score(
        l.master, mezzanine, {}, { .jobs = 3, .memory_limit = 1500, .resident = resident });
    CHECK(held.status == "complete");
    CHECK(held.memory_limit == 1500);
    CHECK(held.max_running == 1);
    CHECK(held.baseline == 100);
    CHECK(held.admissions.empty());   // each pair started with none running (D-058)
    CHECK(held.pool_warnings.empty());   // never over the limit on its own

    // Under 500 nothing fits: a pair alone still runs, with a warning.
    reads = 0;
    const vmafladder::Result over = vmafladder::score(
        l.master, mezzanine, {}, { .jobs = 3, .memory_limit = 500, .resident = resident });
    CHECK(over.status == "complete");
    CHECK(over.max_running == 1);
    CHECK(over.admissions.empty());
    REQUIRE(over.pool_warnings.size() == 1);
    CHECK(over.pool_warnings[0].find("over the memory limit on its own: 1000 bytes of resident "
                                     "memory, the limit 500") != std::string::npos);

    // Without a limit the stand-in isn't read.
    reads = 0;
    const vmafladder::Result unlimited = vmafladder::score(
        l.master, mezzanine, {}, { .jobs = 3, .resident = resident });
    CHECK(unlimited.status == "complete");
    CHECK(reads == 0);
}

TEST_CASE("vmafladder::score: a pair starts once the running one has warmed up", "[ladder][vmaf]")
{
    const LongLadder& l = long_ladder();
    // A stand-in for the resident memory: 100 bytes before the run, 1000 once
    // it has started.
    std::atomic<int> reads{ 0 };
    const auto resident = [&] { return reads.fetch_add(1) == 0 ? std::uint64_t{ 100 } : 1000; };

    // Without a limit the two pairs run at once (D-043), and nothing is measured.
    const vmafladder::Result free = vmafladder::score(l.master, "", {}, { .jobs = 2 });
    CHECK(free.status == "complete");
    CHECK(free.max_running == 2);
    CHECK(free.baseline == 0);
    CHECK(free.admissions.empty());

    // The first pair has warmed up once it has scored kWarmUp of its frame
    // pairs, and is measured then: 1000 - 100 = 900 bytes, counted at 1125 with
    // the margin (D-051, D-053). A limit of 100 + 2 * 1125 lets the second
    // pair start while the first runs (D-045).
    reads = 0;
    const vmafladder::Result two = vmafladder::score(
        l.master, "", {}, { .jobs = 2, .memory_limit = 2350, .resident = resident });
    CHECK(two.status == "complete");
    CHECK(two.baseline == 100);
    // The rule let the second pair start while the first ran, and the
    // report gives what it read and measured then (D-058).
    REQUIRE(two.admissions.size() == 1);
    CHECK(two.admissions[0].pair == 1);
    CHECK(two.admissions[0].running == 1);
    CHECK(two.admissions[0].resident == 1000);
    CHECK(two.admissions[0].measure == 900);
    CHECK(two.max_running == 2);
    REQUIRE(two.pairs.size() == 2);
    CHECK(two.pairs[1].started < two.pairs[0].ended);
    CHECK(two.pool_warnings.empty());

    // One byte less, and the second pair waits for the first to end.
    reads = 0;
    const vmafladder::Result one = vmafladder::score(
        l.master, "", {}, { .jobs = 2, .memory_limit = 2349, .resident = resident });
    CHECK(one.status == "complete");
    // The rule held the second pair back, and it started once none ran: no
    // admission of the rule's.
    CHECK(one.admissions.empty());
    CHECK(one.max_running == 1);
    REQUIRE(one.pairs.size() == 2);
    CHECK(one.pairs[1].started >= one.pairs[0].ended);
}

TEST_CASE("vmafladder::score: a memory limit whose memory can't be read", "[ladder][vmaf]")
{
    const LongLadder& l = long_ladder();
    constexpr std::uint64_t kLimit = std::uint64_t{ 1 } << 30;

    // Never read: the run fails before its first pair (D-059).
    std::atomic<int> reads{ 0 };
    const vmafladder::Result none = vmafladder::score(
        l.master, "", {},
        { .jobs = 2, .memory_limit = kLimit, .resident = [&] {
              ++reads;
              return std::uint64_t{ 0 };
          } });
    CHECK(none.status == "failed");
    CHECK(none.error == "the process's resident memory can't be read, so the memory limit of "
                        "1073741824 bytes can't be applied: no pair was started");
    CHECK(reads == 1);
    CHECK(none.max_running == 0);
    REQUIRE(none.pairs.size() == 2);
    for (const vmafladder::PairRun& p : none.pairs)
        CHECK(p.status == vmafladder::PairStatus::NotStarted);

    // Read before the run, never after: while a pair runs the next is held
    // back, so they run one at a time, though two may run and the limit
    // would let them.
    reads = 0;
    const vmafladder::Result later = vmafladder::score(
        l.master, "", {},
        { .jobs = 2, .memory_limit = kLimit, .resident = [&] {
              return reads.fetch_add(1) == 0 ? std::uint64_t{ 100 } : 0;
          } });
    CHECK(later.status == "complete");
    CHECK(later.baseline == 100);
    CHECK(later.max_running == 1);
    CHECK(later.admissions.empty());
    CHECK(later.pool_warnings.empty());
    REQUIRE(later.pairs.size() == 2);
    CHECK(later.pairs[1].started >= later.pairs[0].ended);
}

TEST_CASE("vmafladder::score: a cancel stops the pairs that are running", "[ladder][vmaf][vmafnet]")
{
    // The first pair's rung is on a server that sends nothing: the pair waits
    // in its first read, up to the read limit (D-036), while the second is
    // scored.
    const Ladder& l = ladder();
    SilentServer server;
    REQUIRE(server.ok());
    const fs::path dir = testtemp::path("mirage_test_ladder_stop");
    fs::create_directories(dir);
    const std::string master = (dir / "master.m3u8").string();
    const std::string held   = server.url("held/index.m3u8");
    write_text(master, master_of({ { held, 600000 }, { l.low, 300000 }, { l.top, 900000 } }));

    // Cancelled once the second pair is complete: the first is stopped, not
    // failed, and the run is cancelled (D-047).
    std::stop_source cancel;
    const auto t0 = std::chrono::steady_clock::now();
    const vmafladder::Result r = vmafladder::score(
        master, "", {}, { .jobs = 2 }, cancel.get_token(),
        [&](std::size_t, std::size_t, const VMAFResult&) { cancel.request_stop(); });
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    INFO(r.error << ", after " << s << " s");
    CHECK(s < 4.0);   // the cancel ended the read, well before the limit
    CHECK(r.status == "cancelled");
    CHECK(r.error == "the run was cancelled: 1 of 2 pairs complete");
    CHECK(r.max_running == 2);
    REQUIRE(r.pairs.size() == 2);
    CHECK(r.pairs[0].status == vmafladder::PairStatus::Stopped);
    CHECK(vmafladder::status_name(r.pairs[0].status) == "stopped");
    CHECK(r.pairs[0].why.empty());
    CHECK(r.pairs[0].started >= 0.0);
    CHECK(r.pairs[1].status == vmafladder::PairStatus::Complete);

    // A pair that can't be scored fails the run and stops the one that is
    // running, which isn't failed.
    const std::string missing = (dir / "missing/index.m3u8").string();
    write_text(master, master_of({ { held, 600000 }, { missing, 300000 }, { l.top, 900000 } }));
    const vmafladder::Result f = vmafladder::score(master, "", {}, { .jobs = 2 });
    CHECK(f.status == "failed");
    CHECK(f.error == std::format("pair 2 of 2, {} against {}: cannot open distorted: {}", missing,
                                 l.top, missing));
    REQUIRE(f.pairs.size() == 2);
    CHECK(f.pairs[0].status == vmafladder::PairStatus::Stopped);
    CHECK(f.pairs[0].why.empty());
    CHECK(f.pairs[1].status == vmafladder::PairStatus::Failed);
    CHECK(f.pairs[1].why == "cannot open distorted: " + missing);
    fs::remove_all(dir);
}

TEST_CASE("vmafladder::admits: the memory limit's rule", "[ladder]")
{
    using vmafladder::admits;
    std::uint64_t per_pair = 0;
    // No limit, or no pair running: a pair always starts.
    CHECK(admits(0, 5000, 100, 3, 3, per_pair));
    CHECK(admits(1500, 5000, 100, 0, 0, per_pair));
    CHECK(per_pair == 0);

    // While a pair is warming up no other starts, whatever the resident
    // memory says, and nothing is measured.
    CHECK_FALSE(admits(1500, 100, 100, 1, 1, per_pair));
    CHECK_FALSE(admits(std::uint64_t{ 1 } << 30, 120, 100, 1, 1, per_pair));
    CHECK_FALSE(admits(1500, 0, 100, 1, 1, per_pair));
    CHECK_FALSE(admits(std::uint64_t{ 1 } << 30, 1000, 100, 3, 1, per_pair));
    CHECK(per_pair == 0);

    // None warming up: a pair is measured at 1000 - 100 = 900, and counted at
    // 1125 with the margin (D-051, D-053: 25%): the memory before the run and
    // two pairs make 100 + 2 * 1125 = 2350.
    static_assert(vmafladder::kMarginPercent == 25);
    CHECK_FALSE(admits(2349, 1000, 100, 1, 0, per_pair));
    CHECK(per_pair == 900);
    CHECK(admits(2350, 1000, 100, 1, 0, per_pair));

    // The largest measurement stays: two pairs at 1500 use 700 each, and
    // three pairs count 100 + 3 * 1125 = 3475.
    CHECK(admits(3475, 1500, 100, 2, 0, per_pair));
    CHECK_FALSE(admits(3474, 1500, 100, 2, 0, per_pair));
    CHECK(per_pair == 900);
    // A larger one replaces it: two pairs at 2100 use 1000 each, counted at 1250.
    CHECK_FALSE(admits(3849, 2100, 100, 2, 0, per_pair));
    CHECK(per_pair == 1000);
    CHECK(admits(3850, 2100, 100, 2, 0, per_pair));
    // A pair warming up holds the next back, also once pairs have been measured.
    CHECK_FALSE(admits(std::uint64_t{ 1 } << 30, 2100, 100, 2, 1, per_pair));
    CHECK(per_pair == 1000);

    // Unknown memory, none warming up: held back until a reading succeeds (D-059).
    CHECK_FALSE(admits(std::uint64_t{ 1 } << 30, 0, 100, 2, 0, per_pair));
    CHECK(per_pair == 1000);
    // Memory that hasn't grown measures nothing, and no pair starts.
    std::uint64_t none = 0;
    CHECK_FALSE(admits(1500, 100, 100, 1, 0, none));
    CHECK(none == 0);
}

TEST_CASE("vmafladder::warm_up: kWarmUp frame pairs, or all of them under a lower frame limit",
          "[ladder]")
{
    using vmafladder::kWarmUp;
    using vmafladder::warm_up;
    constexpr unsigned warm = static_cast<unsigned>(kWarmUp);
    CHECK(warm_up(0) == kWarmUp);   // no frame limit
    CHECK(warm_up(5) == 5);
    CHECK(warm_up(warm - 1) == kWarmUp - 1);
    CHECK(warm_up(warm) == kWarmUp);
    CHECK(warm_up(warm + 1) == kWarmUp);
}

// ── mirage_vmaf --ladder (tools/mirage_vmaf.cpp) ─────────────────────────────

TEST_CASE("mirage_vmaf --ladder: exit 0 when complete, 1 with the report when a pair fails",
          "[ladder][vmaf]")
{
    const Ladder& l = ladder();
    const fs::path dir = testtemp::path("mirage_test_tool_ladder");
    fs::create_directories(dir);
    const std::string report = (dir / "report.json").string();
    const ToolRun ok = run_tool({ "--ladder", l.master, "--out", report });
    INFO(ok.out << ok.err);
    REQUIRE(WIFEXITED(ok.status));
    CHECK(WEXITSTATUS(ok.status) == 0);
    CHECK(read_text(report).starts_with("{\n  \"status\": \"complete\",\n  \"error\": null,\n"));
    CHECK(contains(ok.out, "; the run is complete; report written to " + report + "\n"));

    // The second pair's playlist doesn't exist: the report has the first
    // pair, and the second as failed (D-047).
    const std::string master  = (dir / "master.m3u8").string();
    const std::string missing = (dir / "missing/index.m3u8").string();
    write_text(master, master_of({ { l.low, 300000 }, { missing, 200000 }, { l.top, 900000 } }));
    const std::string failed = (dir / "failed.json").string();
    const ToolRun bad = run_tool({ "--ladder", master, "--jobs", "1", "--out", failed });
    INFO(bad.out << bad.err);
    REQUIRE(WIFEXITED(bad.status));
    CHECK(WEXITSTATUS(bad.status) == 1);
    const std::string error =
        std::format("pair 2 of 2, {} against {}: cannot open distorted: {}", missing, l.top,
                    missing);
    CHECK(contains(bad.err, "mirage_vmaf: " + error + "\n"));
    const std::string text = read_text(failed);
    CHECK(text.starts_with(
        std::format("{{\n  \"status\": \"failed\",\n  \"error\": \"{}\",\n", error)));
    CHECK(contains(text, "  \"pairs\": [\n    {\n      \"index\": 0,\n"));
    CHECK(contains(text, std::format("\"index\": 1, \"url\": \"{}\", \"entries\": [1], "
                                     "\"status\": \"failed\", "
                                     "\"why\": \"cannot open distorted: {}\"}}",
                                     missing, missing)));
    CHECK(contains(bad.out, "; the run is failed; report written to " + failed + "\n"));

    // Its only pair can't be scored: no pair is complete, and the report has
    // the settings asked for.
    write_text(master, master_of({ { missing, 200000 }, { l.top, 900000 } }));
    const std::string none = (dir / "none.json").string();
    const ToolRun empty = run_tool({ "--ladder", master, "--scaler", "lanczos", "--model",
                                     "vmaf_v0.6.1neg", "--out", none });
    INFO(empty.out << empty.err);
    REQUIRE(WIFEXITED(empty.status));
    CHECK(WEXITSTATUS(empty.status) == 1);
    const std::string settings = read_text(none);
    CHECK(contains(settings,
                   "\n  \"model\": \"vmaf_v0.6.1neg\",\n  \"model_source\": \"built-in\",\n"));
    CHECK(contains(settings,
                   "\n  \"scaler\": \"lanczos\",\n  \"n_threads\": 4,\n  \"n_subsample\": 1,\n"));
    CHECK(contains(settings, "\n  \"pairs\": [],\n"));
    fs::remove_all(dir);
}

TEST_CASE("mirage_vmaf --ladder: SIGINT cancels the run, which exits 1 with the report",
          "[ladder][vmaf][vmafnet]")
{
    // The first pair waits for a server that sends nothing, as in "a cancel
    // stops the pairs that are running"; SIGINT comes once the second is
    // complete.
    const Ladder& l = ladder();
    SilentServer server;
    REQUIRE(server.ok());
    const fs::path dir = testtemp::path("mirage_test_tool_sigint");
    fs::create_directories(dir);
    const std::string master = (dir / "master.m3u8").string();
    const std::string held   = server.url("held/index.m3u8");
    write_text(master, master_of({ { held, 600000 }, { l.low, 300000 }, { l.top, 900000 } }));
    const std::string report = (dir / "report.json").string();
    int signals = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const ToolRun run = run_tool({ "--ladder", master, "--jobs", "2", "--out", report },
                                 [&](const std::string& line, pid_t pid) {
                                     if (line.starts_with("mirage_vmaf: pair 2 of 2, ")) {
                                         ::kill(pid, SIGINT);
                                         ++signals;
                                     }
                                 });
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    INFO(run.out << run.err << "after " << s << " s");
    CHECK(signals == 1);
    CHECK(s < 4.0);   // well before the read limit
    REQUIRE(WIFEXITED(run.status));
    CHECK(WEXITSTATUS(run.status) == 1);
    CHECK(contains(run.err, "mirage_vmaf: the run was cancelled: 1 of 2 pairs complete\n"));
    const std::string text = read_text(report);
    CHECK(text.starts_with("{\n  \"status\": \"cancelled\",\n  \"error\": \"the run was cancelled: "
                           "1 of 2 pairs complete\",\n"));
    CHECK(contains(text, std::format("\"index\": 0, \"url\": \"{}\", \"entries\": [0], "
                                     "\"status\": \"stopped\", \"why\": null}}",
                                     held)));
    CHECK(contains(run.out, "; the run is cancelled; report written to " + report + "\n"));
    fs::remove_all(dir);
}

TEST_CASE("mirage_vmaf --ladder: SIGINTs that keep coming don't lose the report",
          "[ladder][vmaf][vmafnet]")
{
    // As above, with the long ladder's pairs, whose report takes longer to
    // write, and SIGINT every 200 us from the second pair's end until the
    // program has ended: the handlers stay until the report is written.
    const LongLadder& l = long_ladder();
    SilentServer server;
    REQUIRE(server.ok());
    const fs::path dir = testtemp::path("mirage_test_tool_sigints");
    fs::create_directories(dir);
    const std::string master = (dir / "master.m3u8").string();
    const std::string held   = server.url("held/index.m3u8");
    write_text(master, master_of({ { held, 600000 }, { l.low, 300000 }, { l.top, 900000 } }));
    const std::string report = (dir / "report.json").string();
    std::jthread signals;
    const ToolRun run = run_tool(
        { "--ladder", master, "--jobs", "2", "--out", report },
        [&](const std::string& line, pid_t pid) {
            if (!line.starts_with("mirage_vmaf: pair 2 of 2, ")) return;
            signals = std::jthread([pid](std::stop_token st) {
                while (!st.stop_requested()) {
                    ::kill(pid, SIGINT);
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                }
            });
        },
        [&] {
            REQUIRE(signals.joinable());
            signals.request_stop();
            signals.join();
        });
    INFO(run.out << run.err);
    // Exited 1, or ended by a SIGINT once the report was written.
    if (WIFEXITED(run.status))
        CHECK(WEXITSTATUS(run.status) == 1);
    else
        CHECK((WIFSIGNALED(run.status) && WTERMSIG(run.status) == SIGINT));
    const std::string text = read_text(report);
    CHECK(text.starts_with("{\n  \"status\": \"cancelled\",\n  \"error\": \"the run was cancelled: "
                           "1 of 2 pairs complete\",\n"));
    CHECK(text.ends_with("]\n}\n"));
    CHECK_FALSE(fs::exists(report + ".partial"));
    fs::remove_all(dir);
}

TEST_CASE("vmafproc: the machine's cores and the process's memory", "[ladder]")
{
    const unsigned cores = vmafproc::physical_cores();
    CHECK(cores >= 1);
    CHECK(vmafproc::default_jobs() == std::max(1u, cores - 1));
    const std::uint64_t now  = vmafproc::resident_bytes();
    const std::uint64_t peak = vmafproc::peak_resident_bytes();
    CHECK(now > 0);
    CHECK(peak >= now);
}

