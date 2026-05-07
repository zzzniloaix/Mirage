#include "SubtitleDecoder.h"
#include "Logger.h"

#include <algorithm>
#include <cstring>

extern "C" {
#include <libavutil/avutil.h>
}

// Cap on retained events; older events get evicted FIFO-style.
// 200 events ≈ 5–10 minutes of dialogue; sufficient for scrub-back lookups.
static constexpr std::size_t kMaxEvents = 200;

// ── ASS / SSA Dialogue line text extraction ───────────────────────────────────
//
// ASS Dialogue format (one line):
//   Dialogue: Layer,Start,End,Style,Name,MarginL,MarginR,MarginV,Effect,Text
// FFmpeg's older ass format prepends a ReadOrder int field (10 commas before
// text instead of 9). We tolerate either by skipping commas until we hit text.
// Override blocks (e.g. {\b1}) and ASS line breaks (\N or \n) are handled.
static std::string extract_ass_text(const char* ass)
{
    if (!ass) return {};

    // Detect "Dialogue:" prefix and skip it.
    const char* p = ass;
    if (std::strncmp(p, "Dialogue:", 9) == 0) p += 9;

    // FFmpeg-converted ASS lines have 9 fields before text on the modern format
    // (no ReadOrder); legacy ffsubdec may emit 10. Skip commas until we've
    // consumed at least 8 — the 9th is Effect (often empty) and the 10th is text.
    // To handle both, we scan for the last comma after the typical prefix length:
    // assume text is everything after the 9th comma, but if the field count is
    // unusual we still get reasonable output.
    int commas = 0;
    while (*p && commas < 9) {
        if (*p == ',') ++commas;
        ++p;
    }
    // For some ASS variants (with ReadOrder), text starts after the 10th comma.
    // Heuristic: if the substring up to the next \N looks like a number followed
    // by a comma, it's the legacy format; advance past the next comma.
    if (*p) {
        const char* q = p;
        bool digits = false;
        while (*q && *q != ',' && *q != '\n') {
            if (*q < '0' || *q > '9') { digits = false; break; }
            digits = true;
            ++q;
        }
        if (digits && *q == ',') p = q + 1;
    }

    std::string out;
    out.reserve(64);
    while (*p && *p != '\n' && *p != '\r') {
        if (*p == '{') {
            // Override block — skip to closing '}'.
            while (*p && *p != '}') ++p;
            if (*p == '}') ++p;
            continue;
        }
        if (*p == '\\' && (p[1] == 'N' || p[1] == 'n')) {
            out.push_back('\n');
            p += 2;
            continue;
        }
        if (*p == '\\' && p[1] == 'h') {  // hard space
            out.push_back(' ');
            p += 2;
            continue;
        }
        out.push_back(*p++);
    }
    return out;
}

// ── SubtitleDecoder ───────────────────────────────────────────────────────────

SubtitleDecoder::~SubtitleDecoder()
{
    close();
}

void SubtitleDecoder::close()
{
    if (ctx_) avcodec_free_context(&ctx_);
    is_text_ = false;
    flush();
}

bool SubtitleDecoder::open(AVCodecParameters* par, AVRational time_base)
{
    time_base_ = time_base;

    const AVCodec* codec = avcodec_find_decoder(par->codec_id);
    if (!codec) {
        logger::error("SubtitleDecoder: no decoder for codec id {}", static_cast<int>(par->codec_id));
        return false;
    }

    ctx_ = avcodec_alloc_context3(codec);
    if (!ctx_) return false;

    if (avcodec_parameters_to_context(ctx_, par) < 0) {
        logger::error("SubtitleDecoder: avcodec_parameters_to_context failed");
        return false;
    }

    if (avcodec_open2(ctx_, codec, nullptr) < 0) {
        logger::error("SubtitleDecoder: avcodec_open2 failed for {}", codec->name);
        return false;
    }

    // Text-based codecs we render. Anything else is bitmap or unsupported.
    switch (par->codec_id) {
        case AV_CODEC_ID_SUBRIP:
        case AV_CODEC_ID_TEXT:
        case AV_CODEC_ID_ASS:
        case AV_CODEC_ID_SSA:
        case AV_CODEC_ID_WEBVTT:
        case AV_CODEC_ID_MOV_TEXT:
            is_text_ = true;
            break;
        default:
            is_text_ = false;
            break;
    }

    logger::info("SubtitleDecoder opened: {} ({})", codec->name,
                 is_text_ ? "text" : "bitmap — not rendered");
    return true;
}

bool SubtitleDecoder::reopen(AVCodecParameters* par, AVRational time_base)
{
    close();
    return open(par, time_base);
}

void SubtitleDecoder::flush()
{
    std::lock_guard g(ev_mtx_);
    events_.clear();
}

void SubtitleDecoder::store_event(SubtitleEvent ev)
{
    std::lock_guard g(ev_mtx_);
    events_.push_back(std::move(ev));
    // Keep events sorted by start_pts so at() can binary-search if needed.
    std::sort(events_.begin(), events_.end(),
              [](const SubtitleEvent& a, const SubtitleEvent& b) {
                  return a.start_pts < b.start_pts;
              });
    if (events_.size() > kMaxEvents)
        events_.erase(events_.begin(), events_.begin() + (events_.size() - kMaxEvents));
}

void SubtitleDecoder::decode_packet(AVPacket* pkt)
{
    if (!ctx_) return;

    AVSubtitle sub{};
    int got = 0;

    if (pkt == nullptr) {
        // Drain: feed an empty packet to flush any pending events.
        AVPacket empty{};
        if (avcodec_decode_subtitle2(ctx_, &sub, &got, &empty) >= 0 && got)
            avsubtitle_free(&sub);
        return;
    }

    int ret = avcodec_decode_subtitle2(ctx_, &sub, &got, pkt);
    if (ret < 0 || !got) return;

    // pkt->pts is in stream time_base; sub start/end_display_time are in ms
    // relative to that pts.
    const double pkt_pts =
        (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts * av_q2d(time_base_) : 0.0;
    const double start = pkt_pts + sub.start_display_time / 1000.0;
    double       end   = pkt_pts + sub.end_display_time   / 1000.0;
    if (end <= start) end = start + 5.0;       // fallback when end is unset

    if (is_text_) {
        std::string combined;
        for (unsigned i = 0; i < sub.num_rects; ++i) {
            AVSubtitleRect* r = sub.rects[i];
            std::string line;
            if (r->ass)        line = extract_ass_text(r->ass);
            else if (r->text)  line = r->text;
            if (line.empty()) continue;
            if (!combined.empty()) combined.push_back('\n');
            combined += line;
        }
        if (!combined.empty())
            store_event({ start, end, std::move(combined) });
    } else if (!bitmap_warned_.exchange(true)) {
        logger::warn("SubtitleDecoder: bitmap subtitles not yet rendered");
    }

    avsubtitle_free(&sub);
}

std::string SubtitleDecoder::at(double pts) const
{
    std::lock_guard g(ev_mtx_);
    std::string out;
    for (const auto& e : events_) {
        if (e.start_pts > pts) break;             // sorted by start_pts
        if (pts < e.end_pts) {
            if (!out.empty()) out.push_back('\n');
            out += e.text;
        }
    }
    return out;
}
