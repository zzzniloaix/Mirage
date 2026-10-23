// vmafreport::to_json / write_file. The report is read back with a small
// strict JSON parser (RFC 8259 grammar) so that the tests check what a reader
// gets, not how the text happens to be laid out.

#include <catch2/catch_test_macros.hpp>

#include "TestTemp.h"
#include "VMAFReport.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ── Minimal JSON reader ───────────────────────────────────────────────────────

struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    bool                         b = false;
    double                       num = 0.0;
    std::string                  str;
    std::vector<Value>           arr;
    std::map<std::string, Value> obj;

    const Value& operator[](const std::string& key) const
    {
        static const Value missing;
        const auto it = obj.find(key);
        return it == obj.end() ? missing : it->second;
    }
    bool has(const std::string& key) const { return obj.count(key) != 0; }
};

class Reader {
public:
    explicit Reader(const std::string& text) : s_(text) {}

    // The document, or nullptr when `text` is not exactly one JSON value.
    std::unique_ptr<Value> document()
    {
        auto v = std::make_unique<Value>();
        if (!value(*v)) return nullptr;
        skip_ws();
        return i_ == s_.size() ? std::move(v) : nullptr;
    }

private:
    const std::string& s_;
    std::size_t        i_ = 0;

    void skip_ws()
    {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r'))
            ++i_;
    }
    bool eat(char c)
    {
        skip_ws();
        if (i_ < s_.size() && s_[i_] == c) { ++i_; return true; }
        return false;
    }
    bool literal(const char* word)
    {
        const std::string w(word);
        if (s_.compare(i_, w.size(), w) != 0) return false;
        i_ += w.size();
        return true;
    }

    bool value(Value& v)
    {
        skip_ws();
        if (i_ >= s_.size()) return false;
        const char c = s_[i_];
        if (c == '{') return object(v);
        if (c == '[') return array(v);
        if (c == '"') { v.kind = Value::Kind::String; return string(v.str); }
        if (c == 't') { v.kind = Value::Kind::Bool; v.b = true;  return literal("true"); }
        if (c == 'f') { v.kind = Value::Kind::Bool; v.b = false; return literal("false"); }
        if (c == 'n') { v.kind = Value::Kind::Null; return literal("null"); }
        return number(v);
    }

    bool object(Value& v)
    {
        v.kind = Value::Kind::Object;
        ++i_;
        if (eat('}')) return true;
        do {
            skip_ws();
            std::string key;
            if (i_ >= s_.size() || s_[i_] != '"' || !string(key)) return false;
            if (v.obj.count(key)) return false;   // duplicate keys are not wanted in a report
            if (!eat(':') || !value(v.obj[key])) return false;
        } while (eat(','));
        return eat('}');
    }

    bool array(Value& v)
    {
        v.kind = Value::Kind::Array;
        ++i_;
        if (eat(']')) return true;
        do {
            v.arr.emplace_back();
            if (!value(v.arr.back())) return false;
        } while (eat(','));
        return eat(']');
    }

    static void put_utf8(std::string& out, unsigned cp)
    {
        if (cp < 0x80) { out += static_cast<char>(cp); return; }
        if (cp < 0x800) {
            out += static_cast<char>(0xc0 | (cp >> 6));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xe0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        }
        out += static_cast<char>(0x80 | (cp & 0x3f));
    }

    bool hex4(unsigned& cp)
    {
        if (i_ + 4 > s_.size()) return false;
        cp = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = s_[i_++];
            cp <<= 4;
            if      (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool string(std::string& out)
    {
        ++i_;   // opening quote
        while (i_ < s_.size()) {
            const auto c = static_cast<unsigned char>(s_[i_++]);
            if (c == '"') return true;
            if (c < 0x20) return false;   // raw control characters are invalid
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (i_ >= s_.size()) return false;
            const char e = s_[i_++];
            switch (e) {
            case '"': out += '"';  break;
            case '\\': out += '\\'; break;
            case '/': out += '/';  break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    unsigned lo = 0;
                    if (!literal("\\u") || !hex4(lo) || lo < 0xdc00 || lo > 0xdfff) return false;
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                    return false;
                }
                put_utf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool number(Value& v)
    {
        const std::size_t start = i_;
        auto digit = [&] { return i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9'; };
        if (i_ < s_.size() && s_[i_] == '-') ++i_;
        if (!digit()) return false;
        if (s_[i_] == '0') ++i_;
        else while (digit()) ++i_;
        if (i_ < s_.size() && s_[i_] == '.') {
            ++i_;
            if (!digit()) return false;
            while (digit()) ++i_;
        }
        if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            ++i_;
            if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
            if (!digit()) return false;
            while (digit()) ++i_;
        }
        v.kind = Value::Kind::Number;
        v.num  = std::strtod(s_.substr(start, i_ - start).c_str(), nullptr);
        return true;
    }
};

std::unique_ptr<Value> parse(const std::string& text) { return Reader(text).document(); }

// ── Fixtures ──────────────────────────────────────────────────────────────────

VMAFInputInfo input(int w, int h, const char* space)
{
    VMAFInputInfo in;
    in.width = w;
    in.height = h;
    in.pix_fmt = "yuv420p";
    in.color_space = space;
    in.color_range = "tv";
    in.color_primaries = "unknown";
    in.color_trc = "bt709";
    in.time_base_num = 1;
    in.time_base_den = 90000;
    return in;
}

VMAFResult sample_result()
{
    VMAFResult r;
    r.ref_url   = "https://example.com/gear5/prog_index.m3u8";
    r.dis_url   = "dir with \"quotes\"\\and\ttab/caf\xc3\xa9.ts";
    r.width     = 1920;
    r.height    = 1080;
    r.pooled    = { .mean = 12.345678901234567, .harmonic_mean = 9.87654321,
                    .min = 0.1 + 0.2,   // 0.30000000000000004: needs all 17 digits
                    .min_without_first_frame = 1.0 / 3.0,
                    .p1 = 0.5, .p5 = 1.25, .p10 = 2.0, .p25 = 50.0, .p50 = 99.875 };
    r.per_frame = { 0.1 + 0.2, 100.0, 1.0 / 3.0 };
    r.done      = true;
    r.settings  = { .model = "vmaf_v0.6.1", .model_source = "built-in", .scaler = "bilinear",
                    .pix_fmt = "yuv420p", .bit_depth = 8, .n_threads = 4, .n_subsample = 1 };
    r.ref_info  = input(1920, 1080, "bt709");
    r.dis_info  = input(960, 540, "smpte170m");
    r.dis_info.ended = true;
    r.ref_pts   = { 10.0, 903003.0 / 90000.0, 906006.0 / 90000.0 };
    r.dis_pts   = { 10.0, std::numeric_limits<double>::quiet_NaN(), 906006.0 / 90000.0 };
    r.ref_index = { 0, 1, 3 };
    r.dis_index = { 0, 1, 2 };
    r.alignment.grid           = "reference";
    r.alignment.tolerance      = 1001.0 / 60000.0;
    r.alignment.ref_frame_rate = "30000/1001";
    r.alignment.dis_frame_rate = "30000/1001";
    r.alignment.ref_start      = 10.0;
    r.alignment.ref_start_from = "container";
    r.alignment.dis_start      = 10.0;
    r.alignment.dis_start_from = "first-frame";
    r.alignment.paired         = 3;
    r.alignment.unmatched_reference = 1;
    r.alignment.failed         = 1;
    r.alignment.failure_rate   = 0.25;
    r.alignment.threshold      = 0.01;
    r.alignment.exceeded       = true;
    r.alignment.failures = { { .input = "reference", .index = 2, .pts = 906006.0 / 90000.0 - 1001.0 / 30000.0,
                               .reason = "unmatched_reference" } };
    r.alignment.contexts = { { 0, 2 }, { 2, 1 } };
    r.alignment.warning  = "25.00% of the frames failed to pair by timestamp";
    r.dis_conversions = {
        { .from_space = "smpte170m", .from_range = "tv", .to_space = "bt709", .to_range = "tv",
          .matrix_changed = true, .range_changed = false, .first_index = 0, .pairs = 2 },
        { .from_space = "unknown", .from_range = "pc", .to_space = "bt709", .to_range = "tv",
          .matrix_changed = true, .range_changed = true, .first_index = 2, .pairs = 1 },
    };
    return r;
}

const vmafreport::RunInfo kRun{
    .tool = "mirage_vmaf", .reference_mode = "explicit-pair", .libvmaf_version = "3.0.0",
    .libvmaf_package_version = "3.1.0",
    .ffmpeg_libraries = { { "libavutil", "60.26.101" }, { "libavcodec", "62.28.101" },
                          { "libavformat", "62.12.101" }, { "libswscale", "9.5.101" },
                          { "libavfilter", "11.14.101" } },
    .frame_limit = 30,
};

std::string read_file(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

fs::path fresh_dir(const char* name)
{
    const fs::path dir = testtemp::path(name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

} // namespace

// ── incomplete ────────────────────────────────────────────────────────────────

TEST_CASE("vmafreport::incomplete: a complete run, and each reason it is not", "[vmafreport]")
{
    CHECK(vmafreport::incomplete(sample_result()).empty());

    VMAFResult failed = sample_result();
    failed.error = "cannot open reference: x";
    CHECK(vmafreport::incomplete(failed) == "cannot open reference: x");

    VMAFResult ref_err = sample_result();
    ref_err.ref_info.end_error = "Input/output error";
    CHECK(vmafreport::incomplete(ref_err) ==
          "reading the reference stopped on an error: Input/output error");

    VMAFResult dis_err = sample_result();
    dis_err.dis_info.end_error = "Invalid data found when processing input";
    CHECK(vmafreport::incomplete(dis_err) ==
          "reading the distorted input stopped on an error: Invalid data found when processing input");

    VMAFResult none = sample_result();
    none.per_frame.clear();
    none.score_error = "vmaf_read_pictures (flush) failed: Invalid argument";
    CHECK(vmafreport::incomplete(none) == "no frame pair was scored");

    VMAFResult score = sample_result();
    score.score_error = "vmaf_score_at_index failed: Invalid argument";
    CHECK(vmafreport::incomplete(score) == "vmaf_score_at_index failed: Invalid argument");
}

// ── to_json ───────────────────────────────────────────────────────────────────

TEST_CASE("vmafreport::to_json: run settings and pooled values", "[vmafreport]")
{
    const std::string text = vmafreport::to_json(sample_result(), kRun);
    const auto doc = parse(text);
    REQUIRE(doc);
    const Value& d = *doc;
    CHECK(d["tool"].str == "mirage_vmaf");
    CHECK(d["reference_mode"].str == "explicit-pair");
    CHECK(d["libvmaf_version"].str == "3.0.0");
    CHECK(d["libvmaf_package_version"].str == "3.1.0");
    const Value& libs = d["ffmpeg_libraries"];
    REQUIRE(libs.kind == Value::Kind::Object);
    CHECK(libs.obj.size() == 5);
    CHECK(libs["libavutil"].str == "60.26.101");
    CHECK(libs["libavcodec"].str == "62.28.101");
    CHECK(libs["libavformat"].str == "62.12.101");
    CHECK(libs["libswscale"].str == "9.5.101");
    CHECK(libs["libavfilter"].str == "11.14.101");
    CHECK(d["model"].str == "vmaf_v0.6.1");
    CHECK(d["model_source"].str == "built-in");
    CHECK(d["model_sha256"].kind == Value::Kind::Null);   // a built-in model has no file
    CHECK(d["scaler"].str == "bilinear");
    CHECK(d["n_threads"].num == 4);
    CHECK(d["n_subsample"].num == 1);
    CHECK(d["scored_as"]["width"].num == 1920);
    CHECK(d["scored_as"]["height"].num == 1080);
    CHECK(d["scored_as"]["pix_fmt"].str == "yuv420p");
    CHECK(d["scored_as"]["bit_depth"].num == 8);
    CHECK(d["frame_limit"].num == 30);
    CHECK(d["frames_scored"].num == 3);
    const Value& pooled = d["pooled"];
    REQUIRE(pooled.kind == Value::Kind::Object);
    CHECK(pooled.obj.size() == 9);
    CHECK(pooled["mean"].num == 12.345678901234567);   // full precision
    CHECK(pooled["harmonic_mean"].num == 9.87654321);
    CHECK(pooled["min"].num == 0.1 + 0.2);
    CHECK(pooled["min_without_first_frame"].num == 1.0 / 3.0);
    CHECK(pooled["p1"].num == 0.5);
    CHECK(pooled["p5"].num == 1.25);
    CHECK(pooled["p10"].num == 2.0);
    CHECK(pooled["p25"].num == 50.0);
    CHECK(pooled["p50"].num == 99.875);
}

TEST_CASE("vmafreport::to_json: the config file (D-030)", "[vmafreport]")
{
    const auto none = parse(vmafreport::to_json(sample_result(), kRun));
    REQUIRE(none);
    CHECK(none->has("config_file"));
    CHECK((*none)["config_file"].kind == Value::Kind::Null);

    vmafreport::RunInfo run = kRun;
    run.config_path   = "configs/run \"1\".cfg";
    run.config_sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    const auto doc = parse(vmafreport::to_json(sample_result(), run));
    REQUIRE(doc);
    const Value& c = (*doc)["config_file"];
    REQUIRE(c.kind == Value::Kind::Object);
    CHECK(c.obj.size() == 2);
    CHECK(c["path"].str == run.config_path);   // as given, escaped and read back intact
    CHECK(c["sha256"].str == run.config_sha256);
}

TEST_CASE("vmafreport::to_json: a model file and another scaler", "[vmafreport]")
{
    VMAFResult r = sample_result();
    r.settings.model        = "models/my \"model\".json";
    r.settings.model_source = "file";
    r.settings.model_sha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    r.settings.scaler       = "lanczos";
    const auto doc = parse(vmafreport::to_json(r, kRun));
    REQUIRE(doc);
    CHECK((*doc)["model"].str == "models/my \"model\".json");
    CHECK((*doc)["model_source"].str == "file");
    CHECK((*doc)["model_sha256"].str == r.settings.model_sha256);
    CHECK((*doc)["scaler"].str == "lanczos");
}

TEST_CASE("vmafreport::to_json: inputs, escaped strings included", "[vmafreport]")
{
    const VMAFResult r = sample_result();
    const auto doc = parse(vmafreport::to_json(r, kRun));
    REQUIRE(doc);
    const Value& ref = (*doc)["ref"];
    const Value& dis = (*doc)["dis"];
    CHECK(ref["url"].str == r.ref_url);
    CHECK(dis["url"].str == r.dis_url);   // quotes, backslash, tab, UTF-8 read back intact
    CHECK(ref["width"].num == 1920);
    CHECK(dis["height"].num == 540);
    CHECK(ref["color_space"].str == "bt709");
    CHECK(dis["color_space"].str == "smpte170m");
    CHECK(dis["color_range"].str == "tv");
    CHECK(dis["color_primaries"].str == "unknown");
    CHECK(dis["color_trc"].str == "bt709");
    CHECK(dis["pix_fmt"].str == "yuv420p");
    CHECK(ref["time_base"].str == "1/90000");
    CHECK(ref["ended"].kind == Value::Kind::Bool);
    CHECK_FALSE(ref["ended"].b);
    CHECK(dis["ended"].b);
}

TEST_CASE("vmafreport::to_json: the distorted frames' color conversions", "[vmafreport]")
{
    const auto doc = parse(vmafreport::to_json(sample_result(), kRun));
    REQUIRE(doc);
    const Value& conv = (*doc)["dis_conversion"];
    REQUIRE(conv.kind == Value::Kind::Array);
    REQUIRE(conv.arr.size() == 2);
    const Value& a = conv.arr[0];
    CHECK(a["from"]["color_space"].str == "smpte170m");
    CHECK(a["from"]["color_range"].str == "tv");
    CHECK(a["to"]["color_space"].str == "bt709");
    CHECK(a["to"]["color_range"].str == "tv");
    CHECK(a["matrix_changed"].kind == Value::Kind::Bool);
    CHECK(a["matrix_changed"].b);
    CHECK_FALSE(a["range_changed"].b);
    CHECK(a["first_index"].num == 0);
    CHECK(a["pairs"].num == 2);
    const Value& b = conv.arr[1];
    CHECK(b["from"]["color_space"].str == "unknown");
    CHECK(b["range_changed"].b);
    CHECK(b["first_index"].num == 2);
    CHECK(b["pairs"].num == 1);

    VMAFResult none = sample_result();
    none.dis_conversions.clear();
    const auto empty = parse(vmafreport::to_json(none, kRun));
    REQUIRE(empty);
    CHECK((*empty)["dis_conversion"].kind == Value::Kind::Array);
    CHECK((*empty)["dis_conversion"].arr.empty());
}

TEST_CASE("vmafreport::to_json: the reference's color conversions", "[vmafreport]")
{
    // A yuv420p reference: none.
    const auto plain = parse(vmafreport::to_json(sample_result(), kRun));
    REQUIRE(plain);
    CHECK((*plain)["ref_conversion"].kind == Value::Kind::Array);
    CHECK((*plain)["ref_conversion"].arr.empty());

    VMAFResult r = sample_result();
    r.ref_conversions = {
        { .from_space = "bt709", .from_range = "pc", .to_space = "smpte170m", .to_range = "tv",
          .matrix_changed = true, .range_changed = true, .first_index = 0, .pairs = 3 },
    };
    const auto doc = parse(vmafreport::to_json(r, kRun));
    REQUIRE(doc);
    const Value& conv = (*doc)["ref_conversion"];
    REQUIRE(conv.arr.size() == 1);
    const Value& a = conv.arr[0];
    CHECK(a["from"]["color_space"].str == "bt709");
    CHECK(a["from"]["color_range"].str == "pc");
    CHECK(a["to"]["color_space"].str == "smpte170m");
    CHECK(a["to"]["color_range"].str == "tv");
    CHECK(a["matrix_changed"].b);
    CHECK(a["range_changed"].b);
    CHECK(a["first_index"].num == 0);
    CHECK(a["pairs"].num == 3);
    CHECK((*doc)["dis_conversion"].arr.size() == 2);   // the distorted frames' stay apart
}

TEST_CASE("vmafreport::to_json: warnings", "[vmafreport]")
{
    const auto none = parse(vmafreport::to_json(sample_result(), kRun));
    REQUIRE(none);
    CHECK((*none)["warnings"].kind == Value::Kind::Array);
    CHECK((*none)["warnings"].arr.empty());

    VMAFResult r = sample_result();
    r.warnings = { "the reference's frames are yuv422p, not yuv420p", "a \"quoted\"\tline" };
    const auto doc = parse(vmafreport::to_json(r, kRun));
    REQUIRE(doc);
    const Value& w = (*doc)["warnings"];
    REQUIRE(w.kind == Value::Kind::Array);
    REQUIRE(w.arr.size() == 2);
    CHECK(w.arr[0].str == r.warnings[0]);
    CHECK(w.arr[1].str == r.warnings[1]);   // escaped and read back intact
}

TEST_CASE("vmafreport::to_json: how the frames were paired by timestamp (D-028)", "[vmafreport]")
{
    const VMAFResult r = sample_result();
    const auto doc = parse(vmafreport::to_json(r, kRun));
    REQUIRE(doc);
    const Value& a = (*doc)["alignment"];
    REQUIRE(a.kind == Value::Kind::Object);
    CHECK(a["rule"].str == "reference-timeline");
    CHECK(a["timestamp_origin"].str == "input-start");
    CHECK(a["grid"].str == "reference");
    CHECK(a["tolerance"].num == 1001.0 / 60000.0);   // full precision
    CHECK(a["reference"]["frame_rate"].str == "30000/1001");
    CHECK(a["reference"]["start"].num == 10.0);
    CHECK(a["reference"]["start_from"].str == "container");
    CHECK(a["distorted"]["start_from"].str == "first-frame");
    CHECK(a["paired"].num == 3);
    CHECK(a["failed"].num == 1);
    CHECK(a["failure_rate"].num == 0.25);
    CHECK(a["threshold"].num == 0.01);
    CHECK(a["exceeded"].kind == Value::Kind::Bool);
    CHECK(a["exceeded"].b);
    for (const char* key : { "unmatched_reference", "extra_distorted", "duplicate_distorted",
                             "no_timestamp", "after_discontinuity", "discontinuities",
                             "off_grid_reference", "off_grid_distorted" }) {
        INFO(key);
        CHECK(a[key].kind == Value::Kind::Number);
    }
    CHECK(a["unmatched_reference"].num == 1);
    CHECK(a["extra_distorted"].num == 0);
    const Value& ctx = a["vmaf_contexts"];
    REQUIRE(ctx.arr.size() == 2);
    CHECK(ctx.arr[0]["first_index"].num == 0);
    CHECK(ctx.arr[0]["pairs"].num == 2);
    CHECK(ctx.arr[1]["first_index"].num == 2);
    CHECK(ctx.arr[1]["pairs"].num == 1);
    const Value& f = a["failures"];
    REQUIRE(f.arr.size() == 1);
    CHECK(f.arr[0]["input"].str == "reference");
    CHECK(f.arr[0]["index"].num == 2);
    CHECK(f.arr[0]["pts"].num == r.alignment.failures[0].pts);
    CHECK(f.arr[0]["reason"].str == "unmatched_reference");
    CHECK_FALSE(a.has("warning"));   // the warning is in "warnings", when the analysis put it there

    // Nothing paired or failed: no failure rate; a start not known: null.
    VMAFResult none = sample_result();
    none.alignment = VMAFAlignment{};
    const auto empty = parse(vmafreport::to_json(none, kRun));
    REQUIRE(empty);
    const Value& e = (*empty)["alignment"];
    CHECK(e["failure_rate"].kind == Value::Kind::Null);
    CHECK(e["reference"]["start"].kind == Value::Kind::Null);
    CHECK(e["reference"]["start_from"].kind == Value::Kind::Null);
    CHECK(e["failures"].kind == Value::Kind::Array);
    CHECK(e["failures"].arr.empty());
    CHECK(e["vmaf_contexts"].arr.empty());
}

TEST_CASE("vmafreport::to_json: one entry per frame pair, full precision", "[vmafreport]")
{
    const VMAFResult r = sample_result();
    const auto doc = parse(vmafreport::to_json(r, kRun));
    REQUIRE(doc);
    const Value& frames = (*doc)["frames"];
    REQUIRE(frames.kind == Value::Kind::Array);
    REQUIRE(frames.arr.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        INFO(i);
        const Value& f = frames.arr[i];
        CHECK(f["index"].num == static_cast<double>(i));
        CHECK(f["vmaf"].num == r.per_frame[i]);
        CHECK(f["ref_pts"].num == r.ref_pts[i]);
        CHECK(f["ref_index"].num == static_cast<double>(r.ref_index[i]));
        CHECK(f["dis_index"].num == static_cast<double>(r.dis_index[i]));
    }
    CHECK(frames.arr[1]["dis_pts"].kind == Value::Kind::Null);   // NaN: the frame had no PTS
    CHECK(frames.arr[2]["dis_pts"].num == r.dis_pts[2]);
}

TEST_CASE("vmafreport::to_json: random scores survive the round trip exactly", "[vmafreport]")
{
    VMAFResult r = sample_result();
    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> score(0.0, 100.0);
    r.per_frame.resize(500);
    for (double& s : r.per_frame) s = score(rng);
    const auto doc = parse(vmafreport::to_json(r, kRun));
    REQUIRE(doc);
    const Value& frames = (*doc)["frames"];
    REQUIRE(frames.arr.size() == r.per_frame.size());
    for (std::size_t i = 0; i < r.per_frame.size(); ++i)
        REQUIRE(frames.arr[i]["vmaf"].num == r.per_frame[i]);
    CHECK(frames.arr[499]["ref_pts"].kind == Value::Kind::Null);   // no PTS recorded that far
    CHECK(frames.arr[499]["ref_index"].kind == Value::Kind::Null);
}

TEST_CASE("vmafreport::to_json: no frame limit, no frames, NaN pooled values", "[vmafreport]")
{
    VMAFResult r = sample_result();
    r.per_frame.clear();
    r.ref_pts.clear();
    r.dis_pts.clear();
    r.pooled = VMAFPooled{};   // all NaN
    vmafreport::RunInfo run = kRun;
    run.frame_limit = 0;
    run.libvmaf_package_version = "unknown";
    run.ffmpeg_libraries.clear();
    const auto doc = parse(vmafreport::to_json(r, run));
    REQUIRE(doc);
    CHECK((*doc)["ffmpeg_libraries"].kind == Value::Kind::Object);
    CHECK((*doc)["ffmpeg_libraries"].obj.empty());
    CHECK((*doc)["frame_limit"].kind == Value::Kind::Null);
    CHECK((*doc)["frames_scored"].num == 0);
    CHECK((*doc)["frames"].kind == Value::Kind::Array);
    CHECK((*doc)["frames"].arr.empty());
    for (const char* key : { "mean", "harmonic_mean", "min", "min_without_first_frame", "p1", "p5",
                             "p10", "p25", "p50" }) {
        INFO(key);
        CHECK((*doc)["pooled"].has(key));
        CHECK((*doc)["pooled"][key].kind == Value::Kind::Null);
    }
    CHECK((*doc)["libvmaf_package_version"].str == "unknown");
}

TEST_CASE("JSON reader used by these tests rejects invalid documents", "[vmafreport]")
{
    CHECK(parse(R"({"a": 1})"));
    CHECK_FALSE(parse(R"({"a": 1,})"));
    CHECK_FALSE(parse(R"({"a": nan})"));
    CHECK_FALSE(parse(R"({"a": 01})"));
    CHECK_FALSE(parse(R"({"a": "x"} trailing)"));
    CHECK_FALSE(parse("{\"a\": \"raw\ttab\"}"));
    CHECK_FALSE(parse(R"({"a": 1, "a": 2})"));
}

// ── write_file ────────────────────────────────────────────────────────────────

TEST_CASE("vmafreport::write_file: writes the text and leaves no temporary file", "[vmafreport]")
{
    const fs::path dir = fresh_dir("mirage_test_vmaf_report");
    const fs::path out = dir / "report.json";
    std::string err;
    REQUIRE(vmafreport::write_file(out.string(), "{\"a\": 1}\n", err));
    CHECK(err.empty());
    CHECK(read_file(out) == "{\"a\": 1}\n");

    // Replaces an existing report.
    REQUIRE(vmafreport::write_file(out.string(), "{}\n", err));
    CHECK(read_file(out) == "{}\n");

    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir)) files.push_back(e.path().filename());
    CHECK(files == std::vector<fs::path>{ "report.json" });
    fs::remove_all(dir);
}

TEST_CASE("vmafreport::write_file: failures are reported and leave nothing behind", "[vmafreport]")
{
    const fs::path dir = fresh_dir("mirage_test_vmaf_report_fail");

    std::string err;
    const fs::path no_dir = dir / "missing" / "report.json";
    CHECK_FALSE(vmafreport::write_file(no_dir.string(), "{}\n", err));
    CHECK_FALSE(err.empty());
    CHECK_FALSE(fs::exists(no_dir));

    // The target is a directory: the rename fails, the temporary file is removed.
    const fs::path is_dir = dir / "taken";
    fs::create_directories(is_dir / "child");
    err.clear();
    CHECK_FALSE(vmafreport::write_file(is_dir.string(), "{}\n", err));
    CHECK_FALSE(err.empty());
    CHECK(fs::is_directory(is_dir));
    CHECK_FALSE(fs::exists(dir / "taken.partial"));

    fs::remove_all(dir);
}
