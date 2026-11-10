#include "FrameInfo.h"

extern "C" {
#include <libavcodec/packet.h>
#include <libavutil/ambient_viewing_environment.h>
#include <libavutil/csp.h>
#include <libavutil/display.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/film_grain_params.h>
#include <libavutil/hdr_dynamic_metadata.h>
#include <libavutil/hwcontext.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/motion_vector.h>
#include <libavutil/pixdesc.h>
#include <libavutil/spherical.h>
#include <libavutil/stereo3d.h>
#include <libavutil/timecode.h>
#include <libavutil/video_enc_params.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

namespace frameinfo {

namespace {

const char* name_or(const char* s, const char* fallback) { return s ? s : fallback; }

double q2d(AVRational r) { return r.den != 0 ? av_q2d(r) : 0.0; }

bool hdr_trc(AVColorTransferCharacteristic t)
{
    return t == AVCOL_TRC_SMPTE2084 || t == AVCOL_TRC_ARIB_STD_B67;
}

// BT.470BG and SMPTE 170M carry the same (BT.601) matrix.
bool same_matrix(AVColorSpace a, AVColorSpace b)
{
    auto norm = [](AVColorSpace c) { return c == AVCOL_SPC_SMPTE170M ? AVCOL_SPC_BT470BG : c; };
    return norm(a) == norm(b);
}

// "yuv420p10le · 10-bit 4:2:0"; VideoToolbox frames name the format of the
// pixel buffer behind them.
std::string pixel_text(const AVFrame* f)
{
    AVPixelFormat fmt = static_cast<AVPixelFormat>(f->format);
    std::string   prefix;
    if (fmt == AV_PIX_FMT_VIDEOTOOLBOX && f->hw_frames_ctx) {
        prefix = "VideoToolbox ";
        fmt    = reinterpret_cast<const AVHWFramesContext*>(f->hw_frames_ctx->data)->sw_format;
    }
    const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(fmt);
    if (!d) return prefix + "unknown";
    std::string s = prefix + d->name;
    s += std::format(" · {}-bit", d->comp[0].depth);
    if (d->flags & AV_PIX_FMT_FLAG_RGB) s += " RGB";
    else if (d->nb_components <= 2)     s += " grey";
    else if (d->log2_chroma_w == 1 && d->log2_chroma_h == 1) s += " 4:2:0";
    else if (d->log2_chroma_w == 1 && d->log2_chroma_h == 0) s += " 4:2:2";
    else if (d->log2_chroma_w == 0 && d->log2_chroma_h == 0) s += " 4:4:4";
    else if (d->log2_chroma_w == 2 && d->log2_chroma_h == 0) s += " 4:1:1";
    return s;
}

std::string primaries_text(const AVColorPrimariesDesc& p)
{
    const AVColorPrimaries id = av_csp_primaries_id_from_desc(&p);
    if (id != AVCOL_PRI_UNSPECIFIED) {
        const char* n = av_color_primaries_name(id);
        if (id == AVCOL_PRI_SMPTE432) return "P3-D65 (smpte432)";
        if (id == AVCOL_PRI_SMPTE431) return "DCI-P3 (smpte431)";
        return name_or(n, "?");
    }
    return std::format("R({:.4f},{:.4f}) G({:.4f},{:.4f}) B({:.4f},{:.4f}) W({:.4f},{:.4f})",
                       q2d(p.prim.r.x), q2d(p.prim.r.y), q2d(p.prim.g.x), q2d(p.prim.g.y),
                       q2d(p.prim.b.x), q2d(p.prim.b.y), q2d(p.wp.x), q2d(p.wp.y));
}

void add_mastering(std::vector<Row>& rows, const AVMasteringDisplayMetadata& m, const char* where)
{
    if (m.has_primaries) {
        AVColorPrimariesDesc p{};
        p.prim.r = { m.display_primaries[0][0], m.display_primaries[0][1] };
        p.prim.g = { m.display_primaries[1][0], m.display_primaries[1][1] };
        p.prim.b = { m.display_primaries[2][0], m.display_primaries[2][1] };
        p.wp     = { m.white_point[0], m.white_point[1] };
        rows.push_back({ "Mastering", primaries_text(p), Level::Normal,
                         std::format("SMPTE ST 2086 display primaries ({})", where) });
    }
    if (m.has_luminance)
        rows.push_back({ "Mastering lum", std::format("{:.4f} – {:.0f} cd/m²", q2d(m.min_luminance),
                                                      q2d(m.max_luminance)),
                         Level::Normal, std::format("SMPTE ST 2086 min / max luminance ({})", where) });
}

// 12-bit PQ code → cd/m² (Dolby Vision L1 values).
double pq12_nits(unsigned code) { return pq_to_nits(static_cast<double>(code) / 4095.0); }

std::string nits_text(double v)
{
    if (v < 1.0)   return std::format("{:.4f}", v);
    if (v < 100.0) return std::format("{:.1f}", v);
    return std::format("{:.0f}", v);
}

std::string decode_errors_text(int e)
{
    std::string s;
    auto add = [&](int bit, const char* n) {
        if (e & bit) { if (!s.empty()) s += ", "; s += n; }
    };
    add(FF_DECODE_ERROR_INVALID_BITSTREAM, "invalid bitstream");
    add(FF_DECODE_ERROR_MISSING_REFERENCE, "missing reference");
    add(FF_DECODE_ERROR_CONCEALMENT_ACTIVE, "concealment");
    add(FF_DECODE_ERROR_DECODE_SLICES, "slices lost");
    return s;
}

// Average / min / max QP over the blocks: H.264 and VP9 store a frame base
// plus per-block deltas (H.264's base is the PPS init QP, not a frame QP).
std::string enc_params_text(const AVFrameSideData* sd)
{
    auto* p = reinterpret_cast<AVVideoEncParams*>(sd->data);
    const char* kind = p->type == AV_VIDEO_ENC_PARAMS_H264 ? "H.264 QP"
                     : p->type == AV_VIDEO_ENC_PARAMS_VP9  ? "VP9 qindex"
                     : p->type == AV_VIDEO_ENC_PARAMS_MPEG2 ? "MPEG-2 qscale" : "QP";
    if (p->nb_blocks == 0) return std::format("{} {} (frame)", kind, p->qp);
    double sum = 0.0;
    int lo = INT32_MAX, hi = INT32_MIN;
    for (unsigned i = 0; i < p->nb_blocks; ++i) {
        const int v = p->qp + av_video_enc_params_block(p, i)->delta_qp;
        sum += v; lo = std::min(lo, v); hi = std::max(hi, v);
    }
    return std::format("{} avg {:.1f}, min {}, max {} over {} blocks", kind,
                       sum / p->nb_blocks, lo, hi, p->nb_blocks);
}

std::string hex_uuid(const std::uint8_t* d)
{
    std::string s;
    for (int i = 0; i < 16; ++i) {
        s += std::format("{:02x}", d[i]);
        if (i == 3 || i == 5 || i == 7 || i == 9) s += '-';
    }
    return s;
}

// A label that fits the inspector's label column; FFmpeg's own name (e.g.
// "H.26[45] User Data Unregistered SEI message") goes in the row's tooltip.
std::string short_side_name(AVFrameSideDataType t, const char* ffmpeg_name)
{
    switch (t) {
    case AV_FRAME_DATA_SEI_UNREGISTERED:            return "User data SEI";
    case AV_FRAME_DATA_MASTERING_DISPLAY_METADATA:  return "Mastering display";
    case AV_FRAME_DATA_CONTENT_LIGHT_LEVEL:         return "Light level";
    case AV_FRAME_DATA_DYNAMIC_HDR_PLUS:            return "HDR10+";
    case AV_FRAME_DATA_DOVI_METADATA:               return "Dolby Vision";
    case AV_FRAME_DATA_DOVI_RPU_BUFFER:             return "DV RPU";
    case AV_FRAME_DATA_A53_CC:                      return "Captions (A53)";
    case AV_FRAME_DATA_S12M_TIMECODE:               return "Timecode";
    case AV_FRAME_DATA_GOP_TIMECODE:                return "GOP timecode";
    case AV_FRAME_DATA_VIDEO_ENC_PARAMS:            return "Encode params";
    case AV_FRAME_DATA_MOTION_VECTORS:              return "Motion vectors";
    case AV_FRAME_DATA_FILM_GRAIN_PARAMS:           return "Film grain";
    case AV_FRAME_DATA_DISPLAYMATRIX:               return "Display matrix";
    case AV_FRAME_DATA_AMBIENT_VIEWING_ENVIRONMENT: return "Ambient viewing";
    case AV_FRAME_DATA_REGIONS_OF_INTEREST:         return "Regions of interest";
    default: return ffmpeg_name ? ffmpeg_name : std::format("type {}", static_cast<int>(t));
    }
}

const AVPacketSideData* stream_side(const Context& ctx, AVPacketSideDataType t)
{
    if (!ctx.par) return nullptr;
    return av_packet_side_data_get(ctx.par->coded_side_data, ctx.par->nb_coded_side_data, t);
}

// The value the renderer uses vs the frame's own tag: dim when the frame
// says nothing, a warning when the two differ.
Row tag_row(const char* label, const char* frame_name, const char* used_name, bool specified, bool differs)
{
    if (!specified)
        return { label, std::format("unspecified (rendered as {})", used_name), Level::Dim };
    if (differs)
        return { label, std::format("{} (rendered as {})", frame_name, used_name), Level::Warn,
                 "The renderer's interpretation differs from this frame's own tag" };
    return { label, frame_name };
}

} // namespace

double pq_to_nits(double e)
{
    constexpr double m1 = 2610.0 / 16384.0;
    constexpr double m2 = 2523.0 / 4096.0 * 128.0;
    constexpr double c1 = 3424.0 / 4096.0;
    constexpr double c2 = 2413.0 / 4096.0 * 32.0;
    constexpr double c3 = 2392.0 / 4096.0 * 32.0;
    e = std::clamp(e, 0.0, 1.0);
    const double p = std::pow(e, 1.0 / m2);
    return 10000.0 * std::pow(std::max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1);
}

std::optional<EncoderInfo> parse_encoder_sei(const std::uint8_t* data, std::size_t size)
{
    if (!data || size <= 16) return std::nullopt;
    const char* p = reinterpret_cast<const char*>(data + 16);
    std::size_t n = 0;
    while (n < size - 16 && p[n] != '\0') {
        const auto c = static_cast<unsigned char>(p[n]);
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') return std::nullopt;   // binary payload
        ++n;
    }
    const std::string text(p, n);
    static constexpr std::string_view kOpts = " - options: ";
    const auto at = text.find(kOpts);
    if (at == std::string::npos) return std::nullopt;

    EncoderInfo info;
    // "x264 - core 164 r3108 31e19f9 - H.264/MPEG-4 AVC codec - Copyleft ..."
    // → the first two " - " fields name the encoder and its build.
    const std::string head = text.substr(0, at);
    const auto d1 = head.find(" - ");
    const auto d2 = d1 == std::string::npos ? std::string::npos : head.find(" - ", d1 + 3);
    info.banner = head.substr(0, d2);

    std::size_t i = at + kOpts.size();
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') ++i;
        std::size_t j = i;
        while (j < text.size() && text[j] != ' ') ++j;
        if (j > i) {
            const std::string tok = text.substr(i, j - i);
            const auto eq = tok.find('=');
            if (eq == std::string::npos) info.options.emplace_back(tok, std::string());
            else                         info.options.emplace_back(tok.substr(0, eq), tok.substr(eq + 1));
        }
        i = j;
    }
    return info;
}

void remember(const AVFrame* f, Memory& m)
{
    if (!f) return;
    for (int i = 0; i < f->nb_side_data; ++i) {
        const AVFrameSideData* sd = f->side_data[i];
        if (sd->type != AV_FRAME_DATA_SEI_UNREGISTERED) continue;
        if (auto e = parse_encoder_sei(sd->data, sd->size)) {
            m.encoder = std::move(e);
            const int64_t ts = f->pts != AV_NOPTS_VALUE ? f->pts : f->best_effort_timestamp;
            m.encoder_pts = (ts != AV_NOPTS_VALUE && f->time_base.den > 0) ? ts * av_q2d(f->time_base) : 0.0;
            return;
        }
    }
}

Report describe(const AVFrame* f, const Context& ctx, const Memory* mem)
{
    Report rep;
    if (!f) return rep;

    // ── Frame ───────────────────────────────────────────────────────────────
    Section frame{ "Frame", {} };
    {
        std::string type(1, av_get_picture_type_char(f->pict_type));
        if (f->flags & AV_FRAME_FLAG_KEY)      type += " · key";
        if (f->flags & AV_FRAME_FLAG_LOSSLESS) type += " · lossless";
        frame.rows.push_back({ "Type", type });
    }
    const AVRational tb = f->time_base;
    const int64_t    ts = f->pts != AV_NOPTS_VALUE ? f->pts : f->best_effort_timestamp;
    if (ts != AV_NOPTS_VALUE && tb.num > 0 && tb.den > 0) {
        const double sec = ts * av_q2d(tb);
        if (ctx.fps > 0.0) {
            const long long n = std::llround((sec - ctx.start_pts) * ctx.fps);
            frame.rows.push_back({ "Number", std::format("{}", n), Level::Normal,
                                   std::format("Counted from the stream start ({:.3f} s) at {:.3f} fps",
                                               ctx.start_pts, ctx.fps) });
        }
        frame.rows.push_back({ "PTS", std::format("{:.6f} s", sec), Level::Normal,
                               std::format("{} × {}/{}{}", ts, tb.num, tb.den,
                                           f->pts == AV_NOPTS_VALUE ? " (best-effort timestamp: no pts)" : "") });
        if (f->duration > 0)
            frame.rows.push_back({ "Duration", std::format("{:.3f} ms", f->duration * av_q2d(tb) * 1000.0) });
    } else {
        frame.rows.push_back({ "PTS", "none", Level::Warn });
    }
    {
        std::string size = std::format("{}×{}", f->width, f->height);
        if (f->sample_aspect_ratio.num > 0 && f->sample_aspect_ratio.den > 0
            && f->sample_aspect_ratio.num != f->sample_aspect_ratio.den)
            size += std::format(" · SAR {}:{}", f->sample_aspect_ratio.num, f->sample_aspect_ratio.den);
        frame.rows.push_back({ "Size", size });
    }
    if (f->crop_top || f->crop_bottom || f->crop_left || f->crop_right) {
        const auto w = f->width  - static_cast<long long>(f->crop_left + f->crop_right);
        const auto h = f->height - static_cast<long long>(f->crop_top + f->crop_bottom);
        frame.rows.push_back({ "Crop", std::format("t{} b{} l{} r{} → {}×{}", f->crop_top, f->crop_bottom,
                                                   f->crop_left, f->crop_right, w, h) });
    }
    {
        std::string scan = (f->flags & AV_FRAME_FLAG_INTERLACED)
            ? ((f->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST) ? "interlaced, top field first"
                                                          : "interlaced, bottom field first")
            : "progressive";
        if (f->repeat_pict > 0) scan += std::format(" · repeat {} field{}", f->repeat_pict, f->repeat_pict > 1 ? "s" : "");
        frame.rows.push_back({ "Scan", scan });
        if (f->flags & AV_FRAME_FLAG_INTERLACED)
            rep.findings.push_back({ "Interlaced", "shown without deinterlacing", Level::Info });
    }
    if ((f->flags & AV_FRAME_FLAG_CORRUPT) || f->decode_error_flags) {
        std::string e = decode_errors_text(f->decode_error_flags);
        if (f->flags & AV_FRAME_FLAG_CORRUPT) e = e.empty() ? "corrupt" : "corrupt: " + e;
        frame.rows.push_back({ "Errors", e, Level::Error });
        rep.findings.push_back({ "Decode", e, Level::Error });
    }
    if (f->flags & AV_FRAME_FLAG_DISCARD) frame.rows.push_back({ "Discard", "flagged for discard", Level::Info });
    rep.sections.push_back(std::move(frame));

    // ── Format and colour ──────────────────────────────────────────────────
    Section fmt{ "Format", {} };
    fmt.rows.push_back({ "Pixels", pixel_text(f) });
    {
        const AVColorRange used = ctx.range == AVCOL_RANGE_JPEG ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
        const bool spec = f->color_range != AVCOL_RANGE_UNSPECIFIED;
        fmt.rows.push_back(tag_row("Range", av_color_range_name(f->color_range), av_color_range_name(used),
                                   spec, spec && f->color_range != used));
    }
    {
        const bool spec = f->colorspace != AVCOL_SPC_UNSPECIFIED;
        fmt.rows.push_back(tag_row("Matrix", av_color_space_name(f->colorspace),
                                   name_or(av_color_space_name(ctx.cs), "?"),
                                   spec, spec && !same_matrix(f->colorspace, ctx.cs)));
    }
    {
        const bool spec = f->color_trc != AVCOL_TRC_UNSPECIFIED;
        const bool used_hdr = hdr_trc(ctx.trc), frame_hdr = hdr_trc(f->color_trc);
        const char* used = ctx.trc != AVCOL_TRC_UNSPECIFIED ? av_color_transfer_name(ctx.trc) : "SDR";
        Row r = tag_row("Transfer", av_color_transfer_name(f->color_trc), used, spec,
                        spec && (used_hdr != frame_hdr || (frame_hdr && f->color_trc != ctx.trc)));
        fmt.rows.push_back(r);
        if (r.level == Level::Warn)
            rep.findings.push_back({ "Transfer", std::format("frame is {}, rendered as {}",
                                                             av_color_transfer_name(f->color_trc), used),
                                     Level::Warn, r.tip });
    }
    {
        const bool spec = f->color_primaries != AVCOL_PRI_UNSPECIFIED;
        const bool differs = spec && ctx.primaries != AVCOL_PRI_UNSPECIFIED && f->color_primaries != ctx.primaries;
        fmt.rows.push_back(spec ? Row{ "Primaries",
                                       differs ? std::format("{} (renderer: {})",
                                                             av_color_primaries_name(f->color_primaries),
                                                             av_color_primaries_name(ctx.primaries))
                                               : std::string(av_color_primaries_name(f->color_primaries)),
                                       differs ? Level::Warn : Level::Normal,
                                       differs ? "The renderer's primaries differ from this frame's"
                                               : std::string() }
                                : Row{ "Primaries", "unspecified", Level::Dim });
        if (spec && f->color_primaries == AVCOL_PRI_BT2020 && !hdr_trc(ctx.trc))
            rep.findings.push_back({ "Gamut", "BT.2020 primaries on the SDR path: shown without gamut conversion",
                                     Level::Info });
    }
    for (const auto& r : fmt.rows)
        if (r.level == Level::Warn && r.label != "Transfer")
            rep.findings.push_back({ r.label, r.value, Level::Warn, r.tip });
    if (f->chroma_location != AVCHROMA_LOC_UNSPECIFIED)
        fmt.rows.push_back({ "Chroma siting", name_or(av_chroma_location_name(f->chroma_location), "?") });
    rep.sections.push_back(std::move(fmt));

    // ── HDR signalling ──────────────────────────────────────────────────────
    Section hdr{ "HDR", {} };
    bool has_mdcv = false, has_cll = false;
    if (const AVFrameSideData* sd = av_frame_get_side_data(f, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA)) {
        add_mastering(hdr.rows, *reinterpret_cast<const AVMasteringDisplayMetadata*>(sd->data), "frame");
        has_mdcv = true;
    } else if (const AVPacketSideData* ps = stream_side(ctx, AV_PKT_DATA_MASTERING_DISPLAY_METADATA)) {
        add_mastering(hdr.rows, *reinterpret_cast<const AVMasteringDisplayMetadata*>(ps->data), "stream");
        has_mdcv = true;
    }
    {
        const AVContentLightMetadata* cll = nullptr;
        const char* where = "frame";
        if (const AVFrameSideData* sd = av_frame_get_side_data(f, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL))
            cll = reinterpret_cast<const AVContentLightMetadata*>(sd->data);
        else if (const AVPacketSideData* ps = stream_side(ctx, AV_PKT_DATA_CONTENT_LIGHT_LEVEL)) {
            cll = reinterpret_cast<const AVContentLightMetadata*>(ps->data);
            where = "stream";
        }
        if (cll) {
            has_cll = true;
            hdr.rows.push_back({ "Light level", std::format("MaxCLL {} · MaxFALL {} cd/m²", cll->MaxCLL, cll->MaxFALL),
                                 Level::Normal, std::format("CTA-861.3 content light level ({})", where) });
            if (cll->MaxCLL == 0 && cll->MaxFALL == 0)
                rep.findings.push_back({ "Light level", "MaxCLL / MaxFALL 0 / 0 (unknown)", Level::Info });
        }
    }
    if (const AVFrameSideData* sd = av_frame_get_side_data(f, AV_FRAME_DATA_DYNAMIC_HDR_PLUS)) {
        const auto* p = reinterpret_cast<const AVDynamicHDRPlus*>(sd->data);
        hdr.rows.push_back({ "HDR10+", std::format("app v{}, {} window{}, target {:.0f} cd/m²",
                                                   p->application_version, p->num_windows,
                                                   p->num_windows == 1 ? "" : "s",
                                                   q2d(p->targeted_system_display_maximum_luminance)),
                             Level::Normal, "SMPTE ST 2094-40 dynamic metadata (this frame)" });
        if (p->num_windows > 0) {
            const AVHDRPlusColorTransformParams& w = p->params[0];
            // Linearised values are normalised to 10,000 cd/m².
            hdr.rows.push_back({ "Scene maxRGB", std::format("R {} · G {} · B {} cd/m²",
                                                             nits_text(10000.0 * q2d(w.maxscl[0])),
                                                             nits_text(10000.0 * q2d(w.maxscl[1])),
                                                             nits_text(10000.0 * q2d(w.maxscl[2]))) });
            hdr.rows.push_back({ "Average maxRGB", nits_text(10000.0 * q2d(w.average_maxrgb)) + " cd/m²" });
            std::string pct;
            for (int i = 0; i < w.num_distribution_maxrgb_percentiles && i < 15; ++i) {
                if (!pct.empty()) pct += " · ";
                pct += std::format("P{} {}", w.distribution_maxrgb[i].percentage,
                                   nits_text(10000.0 * q2d(w.distribution_maxrgb[i].percentile)));
            }
            if (!pct.empty()) hdr.rows.push_back({ "Percentiles", pct, Level::Dim });
        }
    }
    if (const AVPacketSideData* ps = stream_side(ctx, AV_PKT_DATA_DOVI_CONF)) {
        const auto* c = reinterpret_cast<const AVDOVIDecoderConfigurationRecord*>(ps->data);
        hdr.rows.push_back({ "Dolby Vision", std::format("profile {}.{} level {} · RPU {} · EL {} · BL {}",
                                                         c->dv_profile, c->dv_bl_signal_compatibility_id,
                                                         c->dv_level, c->rpu_present_flag ? "yes" : "no",
                                                         c->el_present_flag ? "yes" : "no",
                                                         c->bl_present_flag ? "yes" : "no"),
                             Level::Normal, "Stream configuration record (profile.compatibility)" });
        if (c->dv_profile == 5 || (c->dv_bl_signal_compatibility_id == 0 && c->dv_profile != 0))
            rep.findings.push_back({ "Dolby Vision",
                                     "no backward-compatible base layer: colours are wrong without RPU processing",
                                     Level::Warn });
    }
    if (const AVFrameSideData* sd = av_frame_get_side_data(f, AV_FRAME_DATA_DOVI_METADATA)) {
        const auto* m = reinterpret_cast<const AVDOVIMetadata*>(sd->data);
        const AVDOVIRpuDataHeader* h = av_dovi_get_header(m);
        hdr.rows.push_back({ "DV RPU", std::format("profile {} · BL {}-bit · EL {}-bit · VDR {}-bit",
                                                   h->vdr_rpu_profile, h->bl_bit_depth, h->el_bit_depth,
                                                   h->vdr_bit_depth) });
        if (const AVDOVIDmData* l1 = av_dovi_find_level(m, 1))
            hdr.rows.push_back({ "DV L1", std::format("min {} · avg {} · max {} cd/m²",
                                                      nits_text(pq12_nits(l1->l1.min_pq)),
                                                      nits_text(pq12_nits(l1->l1.avg_pq)),
                                                      nits_text(pq12_nits(l1->l1.max_pq))),
                                 Level::Normal, "Dolby Vision level 1: this shot's luminance (PQ codes)" });
    }
    if (const AVFrameSideData* sd = av_frame_get_side_data(f, AV_FRAME_DATA_AMBIENT_VIEWING_ENVIRONMENT)) {
        const auto* a = reinterpret_cast<const AVAmbientViewingEnvironment*>(sd->data);
        hdr.rows.push_back({ "Ambient", std::format("{:.1f} lx at ({:.4f}, {:.4f})", q2d(a->ambient_illuminance),
                                                    q2d(a->ambient_light_x), q2d(a->ambient_light_y)) });
    }
    const bool pq = f->color_trc == AVCOL_TRC_SMPTE2084 || ctx.trc == AVCOL_TRC_SMPTE2084;
    if (pq && !has_mdcv)
        rep.findings.push_back({ "HDR10", "PQ without mastering-display metadata (ST 2086)", Level::Warn });
    if (pq && !has_cll)
        rep.findings.push_back({ "HDR10", "PQ without content light level (MaxCLL / MaxFALL)", Level::Info });
    if (!hdr.rows.empty()) rep.sections.push_back(std::move(hdr));

    // ── Side data, as the decoder attached it ───────────────────────────────
    Section side{ "Side data", {} };
    std::optional<EncoderInfo> enc;
    for (int i = 0; i < f->nb_side_data; ++i) {
        const AVFrameSideData* sd = f->side_data[i];
        const char* name = av_frame_side_data_name(sd->type);
        Row r{ short_side_name(sd->type, name), std::format("{} bytes", sd->size), Level::Dim,
               name ? name : std::string() };
        switch (sd->type) {
        case AV_FRAME_DATA_A53_CC:
            r.value = std::format("{} cc_data triplets (CEA-608/708)", sd->size / 3);
            r.level = Level::Normal;
            break;
        case AV_FRAME_DATA_S12M_TIMECODE: {
            const auto* tc = reinterpret_cast<const std::uint32_t*>(sd->data);
            const unsigned n = std::min<unsigned>(tc[0], 3);
            const AVRational rate = ctx.fps > 0.0 ? av_d2q(ctx.fps, 1001000) : AVRational{ 30, 1 };
            std::string s;
            for (unsigned k = 1; k <= n; ++k) {
                char buf[AV_TIMECODE_STR_SIZE];
                if (!s.empty()) s += ", ";
                s += av_timecode_make_smpte_tc_string2(buf, rate, tc[k], 0, 0);
            }
            r.value = s.empty() ? "none" : s;
            r.level = Level::Normal;
            break;
        }
        case AV_FRAME_DATA_GOP_TIMECODE: {
            char buf[AV_TIMECODE_STR_SIZE];
            std::int64_t v = 0;
            std::memcpy(&v, sd->data, std::min<std::size_t>(sizeof v, sd->size));
            r.value = av_timecode_make_mpeg_tc_string(buf, static_cast<std::uint32_t>(v));
            r.level = Level::Normal;
            break;
        }
        case AV_FRAME_DATA_SEI_UNREGISTERED:
            if (auto e = parse_encoder_sei(sd->data, sd->size)) {
                r.value = e->banner;
                r.level = Level::Normal;
                if (!enc) enc = std::move(e);
            } else if (sd->size >= 16) {
                r.value = std::format("{} · {} bytes", hex_uuid(sd->data), sd->size - 16);
            }
            break;
        case AV_FRAME_DATA_AFD:
            r.value = std::format("active format {}", sd->size ? sd->data[0] : 0);
            r.level = Level::Normal;
            break;
        case AV_FRAME_DATA_DISPLAYMATRIX:
            r.value = std::format("rotation {:.0f}°", av_display_rotation_get(reinterpret_cast<const int32_t*>(sd->data)));
            r.level = Level::Normal;
            break;
        case AV_FRAME_DATA_FILM_GRAIN_PARAMS: {
            const auto* g = reinterpret_cast<const AVFilmGrainParams*>(sd->data);
            r.value = std::format("{} · seed {}", g->type == AV_FILM_GRAIN_PARAMS_AV1 ? "AV1" : "H.274", g->seed);
            r.level = Level::Normal;
            break;
        }
        case AV_FRAME_DATA_VIDEO_ENC_PARAMS:
            r.value = enc_params_text(sd);
            r.level = Level::Normal;
            break;
        case AV_FRAME_DATA_MOTION_VECTORS:
            r.value = std::format("{} vectors", sd->size / sizeof(AVMotionVector));
            r.level = Level::Normal;
            break;
        case AV_FRAME_DATA_STEREO3D:
            r.value = av_stereo3d_type_name(reinterpret_cast<const AVStereo3D*>(sd->data)->type);
            r.level = Level::Normal;
            break;
        case AV_FRAME_DATA_SPHERICAL:
            r.value = av_spherical_projection_name(reinterpret_cast<const AVSphericalMapping*>(sd->data)->projection);
            r.level = Level::Normal;
            break;
        case AV_FRAME_DATA_VIEW_ID:
            if (sd->size >= sizeof(int)) {
                int v = 0;
                std::memcpy(&v, sd->data, sizeof v);
                r.value = std::format("view {}", v);
                r.level = Level::Normal;
            }
            break;
        case AV_FRAME_DATA_MASTERING_DISPLAY_METADATA:
        case AV_FRAME_DATA_CONTENT_LIGHT_LEVEL:
        case AV_FRAME_DATA_DYNAMIC_HDR_PLUS:
        case AV_FRAME_DATA_DOVI_METADATA:
        case AV_FRAME_DATA_AMBIENT_VIEWING_ENVIRONMENT:
            r.value = "decoded under HDR";
            break;
        case AV_FRAME_DATA_DOVI_RPU_BUFFER:
            r.value = std::format("{} bytes (raw RPU)", sd->size);
            break;
        default:
            break;
        }
        side.rows.push_back(std::move(r));
    }
    if (side.rows.empty()) side.rows.push_back({ "", "none on this frame", Level::Dim });
    rep.sections.push_back(std::move(side));

    const bool earlier = !enc && mem && mem->encoder;
    if (earlier) enc = mem->encoder;
    if (enc) {
        Section es{ "Encoder settings", {} };
        es.rows.push_back({ "Encoder", enc->banner });
        if (earlier)
            es.rows.push_back({ "Seen at", std::format("{:.3f} s (sent once, not on this frame)", mem->encoder_pts),
                                Level::Dim, "From the user-data SEI of an earlier frame of this session" });
        for (auto& [k, v] : enc->options) es.rows.push_back({ k, v.empty() ? "(set)" : v });
        rep.sections.push_back(std::move(es));
    }

    // Most severe first, stable within a level.
    std::stable_sort(rep.findings.begin(), rep.findings.end(),
                     [](const Row& a, const Row& b) { return a.level > b.level; });

    // ── Plain text, for the clipboard ───────────────────────────────────────
    for (const auto& r : rep.findings)
        rep.text += std::format("! {}: {}\n", r.label, r.value);
    for (const auto& s : rep.sections) {
        rep.text += "[" + s.title + "]\n";
        for (const auto& r : s.rows)
            rep.text += r.label.empty() ? std::format("  {}\n", r.value)
                                        : std::format("  {}: {}\n", r.label, r.value);
    }
    return rep;
}

} // namespace frameinfo
