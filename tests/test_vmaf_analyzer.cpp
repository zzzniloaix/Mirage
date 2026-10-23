// VMAFAnalyzer on generated local clips: Matroska files with uncompressed
// yuv420p video (no encoder needed) whose color tags survive the container.
// Checks what the analyzer records about a run (frame limit, paired PTS,
// input parameters, how the inputs ended), not the scores themselves.

#include <catch2/catch_test_macros.hpp>

#include "ManifestParser.h"
#include "VMAFAnalyzer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
}

namespace fs = std::filesystem;

namespace {

constexpr int kFps = 25;   // Matroska stores milliseconds: frame k is at k * 40 ms

// A clip of `frames` frames of w x h yuv420p: a diagonal ramp that moves
// every frame, so libvmaf's motion feature has something to measure. With
// `uneven` the ramp moves 4 and 1 steps in turn instead of 3 every frame.
std::string make_clip(const char* name, int w, int h, int frames, AVColorSpace space,
                      bool uneven = false)
{
    const fs::path path = fs::temp_directory_path() / name;
    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "matroska", path.c_str()) >= 0);
    AVStream* st = avformat_new_stream(oc, nullptr);
    st->codecpar->codec_type  = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id    = AV_CODEC_ID_RAWVIDEO;
    st->codecpar->codec_tag   = avcodec_pix_fmt_to_codec_tag(AV_PIX_FMT_YUV420P);   // "I420"
    st->codecpar->format      = AV_PIX_FMT_YUV420P;
    st->codecpar->width       = w;
    st->codecpar->height      = h;
    st->codecpar->color_space = space;
    st->codecpar->color_range = AVCOL_RANGE_MPEG;
    st->time_base             = { 1, kFps };
    st->avg_frame_rate        = { kFps, 1 };
    REQUIRE(avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0);
    REQUIRE(avformat_write_header(oc, nullptr) >= 0);

    const int size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, w, h, 1);
    for (int k = 0; k < frames; ++k) {
        const int shift = uneven ? (k / 2) * 5 + (k % 2) * 4 : 3 * k;
        AVPacket* pkt = av_packet_alloc();
        REQUIRE(av_new_packet(pkt, size) >= 0);
        uint8_t* y = pkt->data;
        for (int row = 0; row < h; ++row)
            for (int col = 0; col < w; ++col)
                y[row * w + col] = static_cast<uint8_t>((col * 256 / w + row * 128 / h + shift) & 0xff);
        std::fill(pkt->data + w * h, pkt->data + size, static_cast<uint8_t>(128));
        pkt->stream_index = 0;
        pkt->pts = pkt->dts = k;
        pkt->duration = 1;
        pkt->flags |= AV_PKT_FLAG_KEY;
        av_packet_rescale_ts(pkt, { 1, kFps }, st->time_base);
        REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
        av_packet_free(&pkt);
    }
    REQUIRE(av_write_trailer(oc) >= 0);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    return path.string();
}

const std::string& ref_clip()
{
    static const std::string p = make_clip("mirage_test_vmaf_ref.mkv", 192, 108, 12, AVCOL_SPC_BT709);
    return p;
}
const std::string& dis_clip()
{
    static const std::string p = make_clip("mirage_test_vmaf_dis.mkv", 96, 54, 12, AVCOL_SPC_SMPTE170M);
    return p;
}
const std::string& short_clip()
{
    static const std::string p = make_clip("mirage_test_vmaf_short.mkv", 96, 54, 8, AVCOL_SPC_SMPTE170M);
    return p;
}
const std::string& uneven_ref_clip()
{
    static const std::string p =
        make_clip("mirage_test_vmaf_uneven_ref.mkv", 192, 108, 8, AVCOL_SPC_BT709, true);
    return p;
}
const std::string& uneven_dis_clip()
{
    static const std::string p =
        make_clip("mirage_test_vmaf_uneven_dis.mkv", 96, 54, 8, AVCOL_SPC_BT709, true);
    return p;
}

double ms(int k) { return static_cast<double>(k * 1000 / kFps) / 1000.0; }

// Waits for a background analysis; false if it is still running after a minute.
bool wait_done(const VMAFAnalyzer& a)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (a.running() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return !a.running();
}

} // namespace

TEST_CASE("VMAFAnalyzer::analyze: frame limit, paired PTS, settings and inputs", "[vmaf]")
{
    const VMAFResult r = VMAFAnalyzer::analyze(ref_clip(), dis_clip(),
                                               { .max_frames = 5, .record_pts = true });
    REQUIRE(r.error.empty());
    CHECK(r.done);
    CHECK(r.score_error.empty());
    REQUIRE(r.per_frame.size() == 5);
    REQUIRE(r.ref_pts.size() == 5);
    REQUIRE(r.dis_pts.size() == 5);
    for (int k = 0; k < 5; ++k) {
        INFO(k);
        CHECK(r.ref_pts[k] == ms(k));
        CHECK(r.dis_pts[k] == ms(k));
        CHECK(std::isfinite(r.per_frame[k]));
    }

    CHECK(r.settings.model == "vmaf_v0.6.1");
    CHECK(r.settings.scaler == "bilinear");
    CHECK(r.settings.pix_fmt == "yuv420p");
    CHECK(r.settings.bit_depth == 8);
    CHECK(r.settings.n_threads == 4);
    CHECK(r.settings.n_subsample == 1);
    CHECK(r.width == 192);
    CHECK(r.height == 108);

    CHECK(r.ref_info.width == 192);
    CHECK(r.ref_info.height == 108);
    CHECK(r.ref_info.pix_fmt == "yuv420p");
    CHECK(r.ref_info.color_space == "bt709");
    CHECK(r.ref_info.color_range == "tv");
    CHECK(r.ref_info.time_base_num == 1);
    CHECK(r.ref_info.time_base_den == 1000);
    CHECK(r.dis_info.width == 96);
    CHECK(r.dis_info.height == 54);
    CHECK(r.dis_info.color_space == "smpte170m");

    // The limit stopped the run: neither input was read to its end.
    CHECK_FALSE(r.ref_info.ended);
    CHECK_FALSE(r.dis_info.ended);
    CHECK(r.ref_info.end_error.empty());
    CHECK(r.dis_info.end_error.empty());
}

TEST_CASE("VMAFAnalyzer::analyze: without a limit both inputs are read to the end", "[vmaf]")
{
    const VMAFResult full = VMAFAnalyzer::analyze(ref_clip(), dis_clip(), { .record_pts = true });
    REQUIRE(full.error.empty());
    CHECK(full.score_error.empty());
    REQUIRE(full.per_frame.size() == 12);
    REQUIRE(full.ref_pts.size() == 12);
    CHECK(full.ref_pts.back() == ms(11));
    CHECK(full.ref_info.ended);
    CHECK(full.dis_info.ended);
    CHECK(full.ref_info.end_error.empty());

    const VMAFResult part = VMAFAnalyzer::analyze(ref_clip(), dis_clip(), { .max_frames = 5 });
    REQUIRE(part.per_frame.size() == 5);
    for (int k = 0; k < 4; ++k) {
        INFO(k);
        CHECK(part.per_frame[k] == full.per_frame[k]);
    }
    CHECK(part.ref_pts.empty());   // record_pts off
}

TEST_CASE("VMAFAnalyzer::analyze: the last pair of a limited run has no successor", "[vmaf]")
{
    // libvmaf's motion2 feature at frame i is min(motion i, motion i+1), so
    // the last scored frame can score differently from the same frame in a
    // longer run. The reference here moves 4, 1, 4, 1, ... steps: frame 3's
    // own motion is large and frame 4's is small.
    const VMAFResult full = VMAFAnalyzer::analyze(uneven_ref_clip(), uneven_dis_clip());
    const VMAFResult part = VMAFAnalyzer::analyze(uneven_ref_clip(), uneven_dis_clip(),
                                                  { .max_frames = 4 });
    REQUIRE(full.per_frame.size() == 8);
    REQUIRE(part.per_frame.size() == 4);
    for (int k = 0; k < 3; ++k) {
        INFO(k);
        CHECK(part.per_frame[k] == full.per_frame[k]);
    }
    CHECK(part.per_frame[3] != full.per_frame[3]);
}

TEST_CASE("VMAFAnalyzer::analyze: the shorter input ends the pairing", "[vmaf]")
{
    const VMAFResult r = VMAFAnalyzer::analyze(ref_clip(), short_clip(), { .record_pts = true });
    REQUIRE(r.error.empty());
    CHECK(r.per_frame.size() == 8);
    CHECK(r.dis_pts.size() == 8);
    CHECK(r.dis_info.ended);
    CHECK_FALSE(r.ref_info.ended);
}

TEST_CASE("VMAFAnalyzer::analyze: an input that cannot be opened is an error", "[vmaf]")
{
    const std::string missing = (fs::temp_directory_path() / "mirage_test_vmaf_missing.mkv").string();
    fs::remove(missing);
    const VMAFResult r = VMAFAnalyzer::analyze(missing, dis_clip());
    CHECK(r.done);
    CHECK(r.error == "cannot open reference: " + missing);
    CHECK(r.per_frame.empty());

    const VMAFResult d = VMAFAnalyzer::analyze(ref_clip(), missing);
    CHECK(d.error == "cannot open distorted: " + missing);
}

TEST_CASE("VMAFAnalyzer::start: scores like analyze() and records no PTS", "[vmaf]")
{
    VMAFAnalyzer a;
    a.start(ref_clip(), dis_clip());
    REQUIRE(wait_done(a));

    const auto results = a.results();
    REQUIRE(results.size() == 1);
    const VMAFResult& r = results[0];
    REQUIRE(r.error.empty());
    CHECK(r.label == "Analysis");
    CHECK(r.ref_pts.empty());
    CHECK(r.dis_pts.empty());
    CHECK(r.per_frame == VMAFAnalyzer::analyze(ref_clip(), dis_clip()).per_frame);
}

TEST_CASE("VMAFAnalyzer::start_manifest: top-rung pairs and write_json's format", "[vmaf]")
{
    const std::vector<VariantStream> variants = {
        { .url = dis_clip(), .bandwidth = 500000, .width = 96, .height = 54 },
        { .url = ref_clip(), .bandwidth = 2000000, .width = 192, .height = 108 },
    };
    VMAFAnalyzer a;
    a.start_manifest(variants);
    REQUIRE(wait_done(a));

    const auto results = a.results();
    REQUIRE(results.size() == 1);
    const VMAFResult& r = results[0];
    REQUIRE(r.error.empty());
    CHECK(r.label == "96x54  0.5 Mbps");
    CHECK(r.ref_url == ref_clip());
    CHECK(r.dis_url == dis_clip());
    CHECK(r.ref_pts.empty());
    REQUIRE(r.per_frame == VMAFAnalyzer::analyze(ref_clip(), dis_clip()).per_frame);

    // The GUI's export keeps its layout: 4 decimals, 10 scores per line.
    std::string per_frame;
    for (std::size_t j = 0; j < r.per_frame.size(); ++j) {
        if (j % 10 == 0) per_frame += "\n        ";
        per_frame += std::format("{:.4f}", r.per_frame[j]);
        if (j + 1 < r.per_frame.size()) per_frame += ", ";
    }
    const std::string expected = std::format(
        "{{\n  \"results\": [\n    {{\n"
        "      \"label\": \"96x54  0.5 Mbps\",\n"
        "      \"ref\":   \"{}\",\n"
        "      \"dis\":   \"{}\",\n"
        "      \"bandwidth\": 500000,\n"
        "      \"resolution\": \"192x108\",\n"
        "      \"vmaf_mean\": {:.4f},\n"
        "      \"vmaf_min\":  {:.4f},\n"
        "      \"vmaf_p5\":   {:.4f},\n"
        "      \"per_frame\": [{}\n      ]\n"
        "    }}\n  ]\n}}\n",
        ref_clip(), dis_clip(), r.vmaf_mean, r.vmaf_min, r.vmaf_p5, per_frame);

    const fs::path out = fs::temp_directory_path() / "mirage_test_vmaf_gui.json";
    REQUIRE(a.write_json(out.string()));
    std::ifstream f(out);
    std::ostringstream text;
    text << f.rdbuf();
    CHECK(text.str() == expected);
    fs::remove(out);
}
