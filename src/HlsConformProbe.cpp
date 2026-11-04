#include "HlsConformProbe.h"

#include "Logger.h"
#include "NetworkLogger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/dict.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

namespace {

std::string av_error(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

bool is_http(const std::string& url)
{
    return url.starts_with("http://") || url.starts_with("https://");
}

int abort_cb(void* opaque)
{
    return static_cast<const std::stop_token*>(opaque)->stop_requested() ? 1 : 0;
}

// ── Demuxing a segment from memory ───────────────────────────────────────────

struct MemIO {
    const std::uint8_t* d;
    std::size_t         n;
    std::size_t         pos = 0;
};

int mem_read(void* opaque, std::uint8_t* buf, int size)
{
    auto* m = static_cast<MemIO*>(opaque);
    if (m->pos >= m->n) return AVERROR_EOF;
    const std::size_t k = std::min(static_cast<std::size_t>(size), m->n - m->pos);
    std::memcpy(buf, m->d + m->pos, k);
    m->pos += k;
    return static_cast<int>(k);
}

std::int64_t mem_seek(void* opaque, std::int64_t off, int whence)
{
    auto* m = static_cast<MemIO*>(opaque);
    whence &= ~AVSEEK_FORCE;
    if (whence == AVSEEK_SIZE) return static_cast<std::int64_t>(m->n);
    const std::int64_t p = whence == SEEK_SET ? off
                         : whence == SEEK_CUR ? static_cast<std::int64_t>(m->pos) + off
                         : whence == SEEK_END ? static_cast<std::int64_t>(m->n) + off : -1;
    if (p < 0 || p > static_cast<std::int64_t>(m->n)) return AVERROR(EINVAL);
    m->pos = static_cast<std::size_t>(p);
    return p;
}

std::string audio_codec_string(AVCodecID id, int profile)
{
    switch (id) {
        case AV_CODEC_ID_AAC: {
            const int aot = profile == AV_PROFILE_AAC_LOW ? 2 : profile == AV_PROFILE_AAC_HE ? 5
                          : profile == AV_PROFILE_AAC_HE_V2 ? 29 : profile == AV_PROFILE_AAC_MAIN ? 1
                          : profile == AV_PROFILE_AAC_LD ? 23 : profile == AV_PROFILE_AAC_ELD ? 39 : -1;
            return aot > 0 ? std::format("mp4a.40.{}", aot) : std::string("mp4a.40");
        }
        case AV_CODEC_ID_MP3:  return "mp4a.40.34";
        case AV_CODEC_ID_AC3:  return "ac-3";
        case AV_CODEC_ID_EAC3: return "ec-3";
        case AV_CODEC_ID_AC4:  return "ac-4";
        case AV_CODEC_ID_OPUS: return "Opus";
        case AV_CODEC_ID_FLAC: return "fLaC";
        case AV_CODEC_ID_ALAC: return "alac";
        default:               break;
    }
    return {};
}

std::string video_codec_string(AVCodecID id, const std::uint8_t* config, std::size_t n, const std::string& entry)
{
    switch (id) {
        case AV_CODEC_ID_H264: return hls::avc_codec_string(config, n, entry);
        case AV_CODEC_ID_HEVC: return hls::hevc_codec_string(config, n, entry);
        case AV_CODEC_ID_AV1:  return hls::av1_codec_string(config, n);
        default:               break;
    }
    return {};
}

// The demuxer for a segment's bytes: probing alone can take an MPEG-TS
// segment that starts inside a GOP for an MPEG program stream.
const AVInputFormat* segment_format(const std::uint8_t* d, std::size_t n)
{
    if (n >= 188 && d[0] == 0x47 && (n < 376 || d[188] == 0x47)) return av_find_input_format("mpegts");
    if (n >= 8) {
        const std::string_view box(reinterpret_cast<const char*>(d + 4), 4);
        if (box == "ftyp" || box == "styp" || box == "moov" || box == "moof" || box == "sidx" || box == "emsg"
            || box == "prft")
            return av_find_input_format("mov");
    }
    return nullptr;   // packed audio (ID3, ADTS), WebVTT, …: probed
}

AVCodecContext* open_decoder(const AVCodecParameters* par)
{
    const AVCodec* dec = avcodec_find_decoder(par->codec_id);
    if (!dec) return nullptr;
    AVCodecContext* ctx = avcodec_alloc_context3(dec);
    if (!ctx) return nullptr;
    // Slice threads only: frame threads hold the first frame back until
    // they all have one.
    ctx->thread_type = FF_THREAD_SLICE;
    if (avcodec_parameters_to_context(ctx, par) < 0 || avcodec_open2(ctx, dec, nullptr) < 0)
        avcodec_free_context(&ctx);
    return ctx;
}

} // namespace

// ── conformprobe ─────────────────────────────────────────────────────────────

namespace conformprobe {

Fetched fetch(const std::string& url, std::int64_t offset, std::int64_t length, std::int64_t max_bytes,
              const std::stop_token& st, std::atomic<std::int64_t>& received)
{
    Fetched f;
    f.final_url = url;
    const AVIOInterruptCB cb{ abort_cb, const_cast<std::stop_token*>(&st) };
    AVDictionary* opts = nullptr;
    const bool http = is_http(url);
    const std::int64_t first = std::max<std::int64_t>(offset, 0);
    if (http) {
        av_dict_set(&opts, "timeout",    "5000000", 0);
        av_dict_set(&opts, "rw_timeout", "8000000", 0);
        // http.c sends "Range: bytes=offset-(end_offset-1)".
        if (first > 0)  av_dict_set_int(&opts, "offset", first, 0);
        if (length > 0) av_dict_set_int(&opts, "end_offset", first + length, 0);
    }
    const bool local_range = !http && (first > 0 || length > 0);
    NetworkLogger::track_open_begin(NetInitiator::Inspector, url.c_str(), local_range ? first : -1,
                                    local_range && length > 0 ? first + length - 1 : -1);
    AVIOContext* pb = nullptr;
    const int ret = avio_open2(&pb, url.c_str(), AVIO_FLAG_READ, &cb, &opts);
    av_dict_free(&opts);
    NetworkLogger::track_open_end(ret, pb);
    if (ret < 0) {
        f.error = st.stop_requested() ? std::string("stopped") : av_error(ret);
        return f;
    }
    if (http) {
        if (std::uint8_t* loc = nullptr; av_opt_get(pb, "location", AV_OPT_SEARCH_CHILDREN, &loc) >= 0 && loc) {
            if (*loc) f.final_url = reinterpret_cast<const char*>(loc);
            av_free(loc);
        }
    }
    f.total = avio_size(pb);
    if (!http && first > 0 && avio_seek(pb, first, SEEK_SET) < 0) {
        f.error = std::format("cannot seek to byte {}", first);
        NetworkLogger::track_close(pb);
        avio_closep(&pb);
        return f;
    }

    // A server that ignores Range sends everything: stop at what was asked.
    std::int64_t want = length > 0 ? length : -1;
    if (max_bytes >= 0) want = want < 0 ? max_bytes : std::min(want, max_bytes);
    std::vector<unsigned char> buf(64 * 1024);
    int n = 0;
    while (want != 0) {
        const int ask = want < 0 ? static_cast<int>(buf.size())
                                 : static_cast<int>(std::min<std::int64_t>(want, static_cast<std::int64_t>(buf.size())));
        n = avio_read(pb, buf.data(), ask);
        if (n <= 0) break;
        f.body.append(reinterpret_cast<const char*>(buf.data()), static_cast<std::size_t>(n));
        received += n;
        if (want > 0) want -= n;
    }
    const int read_err = pb->error < 0 ? pb->error : (n < 0 && n != AVERROR_EOF ? n : 0);
    NetworkLogger::track_close(pb);
    avio_closep(&pb);
    if (st.stop_requested())
        f.error = "stopped";
    else if (read_err < 0 && want != 0)
        f.error = std::format("reading failed after {} bytes: {}", f.body.size(), av_error(read_err));
    else if (length > 0 && max_bytes < 0 && static_cast<std::int64_t>(f.body.size()) < length)
        f.error = std::format("{} of the {} bytes of the range arrived", f.body.size(), length);
    return f;
}

std::vector<int> pick_segments(int n, int k)
{
    std::vector<int> out;
    if (n <= 0) return out;
    if (k <= 0 || k >= n) {
        for (int i = 0; i < n; ++i) out.push_back(i);
        return out;
    }
    if (k == 1) return { 0 };
    for (int j = 0; j < k; ++j) {
        const int i = static_cast<int>(std::lround(static_cast<double>(j) * (n - 1) / (k - 1)));
        if (out.empty() || out.back() != i) out.push_back(i);
    }
    return out;
}

bool readable_without_key(std::string_view method)
{
    return method == "SAMPLE-AES" || method == "SAMPLE-AES-CTR" || method == "ISO-23001-7";
}

hls::SegmentRead read_segment(const std::uint8_t* data, std::size_t n, int index, std::int64_t bytes,
                              bool encrypted)
{
    hls::SegmentRead r;
    r.index = index;
    r.bytes = bytes;
    r.encrypted = encrypted;

    MemIO mem{ data, n };
    constexpr int kBuf = 64 * 1024;
    auto* buf = static_cast<unsigned char*>(av_malloc(kBuf));
    AVIOContext* io = buf ? avio_alloc_context(buf, kBuf, 0, &mem, mem_read, nullptr, mem_seek) : nullptr;
    AVFormatContext* fc = io ? avformat_alloc_context() : nullptr;
    AVCodecContext *vdec = nullptr, *adec = nullptr;
    AVPacket* pkt = av_packet_alloc();
    AVFrame*  frame = av_frame_alloc();
    auto cleanup = [&] {
        avcodec_free_context(&vdec);
        avcodec_free_context(&adec);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avformat_close_input(&fc);   // custom I/O: the AVIOContext stays ours
        if (io) {
            av_freep(&io->buffer);   // FFmpeg may have replaced the buffer given
            avio_context_free(&io);
        } else {
            av_free(buf);
        }
    };
    if (!fc || !pkt || !frame) {
        r.error = "out of memory";
        cleanup();
        return r;
    }
    fc->pb = io;
    fc->flags |= AVFMT_FLAG_CUSTOM_IO;
    int ret = avformat_open_input(&fc, nullptr, segment_format(data, n), nullptr);
    if (ret < 0) {
        r.error = "not a media segment libavformat reads: " + av_error(ret);
        cleanup();
        return r;
    }
    // Probing decodes frames: of encrypted fMP4 that is ciphertext, and its
    // moov already gives the decoder configuration, size and timescales.
    const bool skip_probe = encrypted && std::string_view(fc->iformat->name).find("mp4") != std::string_view::npos;
    if (!skip_probe && (ret = avformat_find_stream_info(fc, nullptr)) < 0) {
        r.error = "probing failed: " + av_error(ret);
        cleanup();
        return r;
    }
    const int vi = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int ai = av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    for (unsigned i = 0; i < fc->nb_streams; ++i)
        if (static_cast<int>(i) != vi && static_cast<int>(i) != ai) fc->streams[i]->discard = AVDISCARD_ALL;
    const bool mp4 = std::string_view(fc->iformat->name).find("mp4") != std::string_view::npos;

    hls::VideoSeen v;
    int nal_len = 0;
    bool h264 = false, hevc = false;
    double tb = 0.0;
    if (vi >= 0) {
        const AVStream* vs = fc->streams[vi];
        const AVCodecParameters* par = vs->codecpar;
        tb = av_q2d(vs->time_base);
        h264 = par->codec_id == AV_CODEC_ID_H264;
        hevc = par->codec_id == AV_CODEC_ID_HEVC;
        v.codec = avcodec_get_name(par->codec_id);
        if (mp4 && par->codec_tag) {
            char tag[AV_FOURCC_MAX_STRING_SIZE] = {};
            av_fourcc_make_string(tag, par->codec_tag);
            v.sample_entry = tag;
        }
        const auto* ex = par->extradata;
        const auto  exn = static_cast<std::size_t>(std::max(par->extradata_size, 0));
        // A Dolby Vision sample entry (dvh1, dvhe, …) carries the base codec's
        // record: its string is the base codec's own, the DV configuration aside.
        const bool base_entry = v.sample_entry == "avc1" || v.sample_entry == "avc3" || v.sample_entry == "hvc1"
                             || v.sample_entry == "hev1";
        v.codec_string = video_codec_string(par->codec_id, ex, exn, base_entry ? v.sample_entry : std::string());
        if (h264 || hevc) nal_len = hls::nal_length_size(ex, exn, hevc);
        if (const AVPacketSideData* sd = av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data,
                                                                 AV_PKT_DATA_DOVI_CONF);
            sd && sd->size >= sizeof(AVDOVIDecoderConfigurationRecord)) {
            const auto* dv = reinterpret_cast<const AVDOVIDecoderConfigurationRecord*>(sd->data);
            v.dv_profile = dv->dv_profile;
            v.dv_level   = dv->dv_level;
            v.dv_compat  = dv->dv_bl_signal_compatibility_id;
        }
        v.width    = par->width;
        v.height   = par->height;
        v.transfer = par->color_trc;
        if (par->sample_aspect_ratio.num > 0) {
            v.sar_num = par->sample_aspect_ratio.num;
            v.sar_den = par->sample_aspect_ratio.den;
        }
        if (const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(par->format)))
            v.bit_depth = d->comp[0].depth;
        if (!encrypted) vdec = open_decoder(par);
    }
    if (ai >= 0 && !encrypted) adec = open_decoder(fc->streams[ai]->codecpar);

    // The first frame decoded: what the stream really is (cropped size,
    // aspect, transfer as the bitstream signals it).
    bool vdone = false;
    auto take_frame = [&](const AVFrame* f) {
        v.width  = f->width;
        v.height = f->height;
        if (f->sample_aspect_ratio.num > 0) {
            v.sar_num = f->sample_aspect_ratio.num;
            v.sar_den = f->sample_aspect_ratio.den;
        }
        if (f->color_trc != AVCOL_TRC_UNSPECIFIED) v.transfer = f->color_trc;
        if (const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(f->format)))
            v.bit_depth = d->comp[0].depth;
        vdone = true;
    };
    constexpr int kAudioFrames = 4;   // implicit SBR / PS is known after the first frames
    int adone = 0;

    double min_pts = std::numeric_limits<double>::infinity(), max_pts = -min_pts, max_end = -min_pts;
    double amin = min_pts, aend = -min_pts;
    const double atb = ai >= 0 ? av_q2d(fc->streams[ai]->time_base) : 0.0;
    bool first_video = true, vkey = false;
    double first_pts = std::numeric_limits<double>::quiet_NaN();   // the first video packet's
    while (av_read_frame(fc, pkt) >= 0) {
        if (pkt->stream_index == vi) {
            ++r.frames;
            if (pkt->flags & AV_PKT_FLAG_KEY) ++r.keyframes;
            if (first_video) {
                first_video = false;
                r.first_key = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
                const auto sz = static_cast<std::size_t>(pkt->size);
                if (h264) r.first = hls::first_picture_h264(pkt->data, sz, nal_len);
                if (hevc) r.first = hls::first_picture_hevc(pkt->data, sz, nal_len);
                // MPEG-TS without extradata: the parameter sets of the first access unit.
                if (v.codec_string.empty() && nal_len == 0)
                    v.codec_string = video_codec_string(fc->streams[vi]->codecpar->codec_id, pkt->data, sz, {});
            }
            if (pkt->pts != AV_NOPTS_VALUE) {
                const double t = static_cast<double>(pkt->pts) * tb;
                if (std::isnan(first_pts)) {
                    first_pts = t;
                } else if (t < first_pts) {
                    ++r.leading;
                    if (hevc && hls::hevc_rasl(pkt->data, static_cast<std::size_t>(pkt->size), nal_len)) ++r.rasl;
                }
                min_pts = std::min(min_pts, t);
                max_pts = std::max(max_pts, t);
                max_end = std::max(max_end, t + static_cast<double>(pkt->duration) * tb);
            }
            // Decoding starts at a keyframe: before it the parameter sets
            // may not have come yet (a segment cut inside a GOP).
            if (pkt->flags & AV_PKT_FLAG_KEY) vkey = true;
            if (vdec && vkey && !vdone && avcodec_send_packet(vdec, pkt) >= 0)
                while (!vdone && avcodec_receive_frame(vdec, frame) == 0) {
                    take_frame(frame);
                    av_frame_unref(frame);
                }
        } else if (pkt->stream_index == ai) {
            if (pkt->pts != AV_NOPTS_VALUE) {
                const double t = static_cast<double>(pkt->pts) * atb;
                amin = std::min(amin, t);
                aend = std::max(aend, t + static_cast<double>(pkt->duration) * atb);
            }
            if (adec && adone < kAudioFrames && avcodec_send_packet(adec, pkt) >= 0)
                while (avcodec_receive_frame(adec, frame) == 0) {
                    ++adone;
                    av_frame_unref(frame);
                }
        }
        av_packet_unref(pkt);
    }
    if (vdec && vkey && !vdone && avcodec_send_packet(vdec, nullptr) >= 0)
        while (!vdone && avcodec_receive_frame(vdec, frame) == 0) {
            take_frame(frame);
            av_frame_unref(frame);
        }

    if (vi >= 0) {
        if (r.frames >= 2 && max_pts > min_pts)
            r.frame_rate = static_cast<double>(r.frames - 1) / (max_pts - min_pts);
        if (std::isfinite(min_pts)) {
            const double end = max_end > max_pts ? max_end
                             : max_pts + (r.frame_rate > 0.0 ? 1.0 / r.frame_rate : 0.0);
            r.media_duration = end - min_pts;
        }
        r.video = std::move(v);
    }
    if (ai >= 0) {
        if (std::isfinite(amin) && aend > amin) r.audio_duration = aend - amin;
        const AVCodecParameters* par = fc->streams[ai]->codecpar;
        hls::AudioSeen a;
        a.codec = avcodec_get_name(par->codec_id);
        const int profile = adec && adone > 0 ? adec->profile : par->profile;
        a.codec_string = audio_codec_string(par->codec_id, profile);
        a.channels = adec && adone > 0 && adec->ch_layout.nb_channels > 0 ? adec->ch_layout.nb_channels
                                                                        : par->ch_layout.nb_channels;
        a.sample_rate = adec && adone > 0 && adec->sample_rate > 0 ? adec->sample_rate : par->sample_rate;
        r.audio = std::move(a);
    }
    if (vi < 0 && ai < 0) r.error = "no audio or video stream";
    cleanup();
    return r;
}

} // namespace conformprobe

// ── ConformProbe ─────────────────────────────────────────────────────────────

using conformprobe::Fetched;
using conformprobe::fetch;
using conformprobe::parallel_for;

ConformProbe::~ConformProbe() { stop(); }

void ConformProbe::start(const std::string& url, const Options& o)
{
    launch(url, {}, {}, o);
}

void ConformProbe::start(const std::string& source, const std::string& text, const std::string& final_url,
                         const Options& o)
{
    launch(source, text, final_url, o);
}

void ConformProbe::stop()
{
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
}

void ConformProbe::clear()
{
    stop();
    {
        std::lock_guard lk(mtx_);
        result_ = Result{};
    }
    state_.store(State::Idle);
    ++version_;
}

ConformProbe::Progress ConformProbe::progress() const
{
    Progress p;
    p.playlists = count_.playlists.load();
    p.playlists_total = count_.playlists_total.load();
    p.sizes = count_.sizes.load();
    p.sizes_total = count_.sizes_total.load();
    p.reads = count_.reads.load();
    p.reads_total = count_.reads_total.load();
    p.bytes = count_.bytes.load();
    return p;
}

ConformProbe::Result ConformProbe::result() const
{
    std::lock_guard lk(mtx_);
    return result_;
}

void ConformProbe::launch(std::string source, std::string text, std::string final_url, Options o)
{
    stop();
    {
        std::lock_guard lk(mtx_);
        result_ = Result{};
        result_.source = source;
    }
    for (auto* a : { &count_.playlists, &count_.playlists_total, &count_.sizes, &count_.sizes_total, &count_.reads,
                     &count_.reads_total })
        a->store(0);
    count_.bytes.store(0);
    t0_ = std::chrono::steady_clock::now();
    state_.store(State::Running);
    ++version_;
    thread_ = std::jthread([this, source = std::move(source), text = std::move(text), final_url = std::move(final_url),
                            o](std::stop_token st) mutable {
        check(st, source, std::move(text), std::move(final_url), o);
    });
}

void ConformProbe::finish(State s, std::string error)
{
    {
        std::lock_guard lk(mtx_);
        result_.error = std::move(error);
        result_.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
    }
    state_.store(s);
    ++version_;
}

void ConformProbe::check(const std::stop_token& st, const std::string& source, std::string text,
                         std::string final_url, const Options& o)
{
    std::atomic<std::int64_t>& received = count_.bytes;
    if (text.empty()) {
        Fetched f = fetch(source, -1, -1, static_cast<std::int64_t>(NetworkLogger::kMaxTextBody), st, received);
        if (st.stop_requested()) return finish(State::Stopped);
        if (!f.ok()) return finish(State::Failed, "cannot fetch the master playlist: " + f.error);
        text = std::move(f.body);
        final_url = std::move(f.final_url);
    }
    hls::MasterDecl m = hls::parse_master_decl(text, final_url.empty() ? source : final_url);
    {
        std::lock_guard lk(mtx_);
        result_.master = m;
    }
    ++version_;
    if (!m.ok) {
        std::string why;
        for (const auto& p : m.problems) why += (why.empty() ? "" : "; ") + p;
        return finish(State::Failed, why.empty() ? "not a master playlist" : why);
    }

    // ── Media playlists ──────────────────────────────────────────────────────
    const std::vector<std::string> uris = m.playlists();
    std::set<std::string> audio_uris;
    for (const auto& md : m.media)
        if (md.type == "AUDIO" && !md.uri.empty()) audio_uris.insert(md.uri);
    // Subtitle playlists after them, for the marker check only.
    std::vector<std::string> all = uris;
    {
        std::set<std::string> seen(uris.begin(), uris.end());
        for (const auto& md : m.media)
            if (md.type == "SUBTITLES" && !md.uri.empty() && seen.insert(md.uri).second) all.push_back(md.uri);
    }
    std::vector<hls::PlaylistRead> pls(all.size());
    count_.playlists_total = static_cast<int>(all.size());
    parallel_for(all.size(), o.workers, st, [&](std::size_t i) {
        pls[i].uri = all[i];
        Fetched f = fetch(all[i], -1, -1, static_cast<std::int64_t>(NetworkLogger::kMaxTextBody), st, received);
        if (!f.ok()) {
            pls[i].error = f.error;
        } else {
            pls[i].tl = hls::parse_media_timeline(f.body, f.final_url, o.marker_tags);
            pls[i].target_tag = f.body.find("#EXT-X-TARGETDURATION:") != std::string::npos;
            if (!pls[i].tl.ok) {
                std::string why;
                for (const auto& p : pls[i].tl.problems) why += (why.empty() ? "" : "; ") + p;
                pls[i].error = why.empty() ? "not a media playlist" : why;
            }
        }
        ++count_.playlists;
    });
    std::vector<hls::PlaylistRead> subs(std::make_move_iterator(pls.begin() + static_cast<std::ptrdiff_t>(uris.size())),
                                        std::make_move_iterator(pls.end()));
    pls.resize(uris.size());

    // ── What to request ──────────────────────────────────────────────────────
    struct Job { std::size_t pl; int seg; bool read; bool encrypted = false; };
    std::vector<Job> jobs;
    std::map<std::pair<std::string, std::int64_t>, std::size_t> init_index;   // (uri, offset) → slot
    std::vector<std::pair<const hls::InitMap*, Fetched>> inits;
    for (std::size_t p = 0; p < pls.size(); ++p) {
        hls::PlaylistRead& pr = pls[p];
        if (!pr.error.empty() || !pr.tl.ok) continue;
        const auto& segs = pr.tl.segments;
        pr.sizes.assign(segs.size(), -1);
        for (std::size_t i = 0; i < segs.size(); ++i)
            if (!segs[i].range.whole()) pr.sizes[i] = segs[i].range.length;
        if (o.markers_only) continue;
        const bool audio = audio_uris.contains(pr.uri);
        const std::vector<int> picks = conformprobe::pick_segments(static_cast<int>(segs.size()),
                                                                   audio ? o.audio_segments : o.segments);
        std::set<int> picked;
        for (const int i : picks) {
            const auto& s = segs[static_cast<std::size_t>(i)];
            const bool encrypted = !s.key.empty() && s.key != "NONE";
            if (encrypted && !conformprobe::readable_without_key(s.key)) {
                pr.key = s.key;
                continue;
            }
            if (encrypted) pr.encrypted = s.key;
            picked.insert(i);
            jobs.push_back({ p, i, true, encrypted });
            if (s.map >= 0) {
                const hls::InitMap& mp = pr.tl.maps[static_cast<std::size_t>(s.map)];
                const auto key = std::make_pair(mp.uri, mp.range.offset);
                if (!init_index.contains(key)) {
                    init_index.emplace(key, inits.size());
                    inits.emplace_back(&mp, Fetched{});
                }
            }
        }
        if (o.sizes)
            for (std::size_t i = 0; i < segs.size(); ++i)
                if (pr.sizes[i] < 0 && !picked.contains(static_cast<int>(i))) jobs.push_back({ p, static_cast<int>(i), false });
    }
    int nreads = 0;
    for (const auto& j : jobs) nreads += j.read ? 1 : 0;
    count_.reads_total = nreads;
    count_.sizes_total = static_cast<int>(jobs.size()) - nreads;

    // Init segments first: every read of a rendition needs its own.
    parallel_for(inits.size(), o.workers, st, [&](std::size_t i) {
        const hls::InitMap& mp = *inits[i].first;
        inits[i].second = fetch(mp.uri, mp.range.offset, mp.range.length, -1, st, received);
    });

    std::mutex reads_mtx;
    parallel_for(jobs.size(), o.workers, st, [&](std::size_t k) {
        const Job& j = jobs[k];
        hls::PlaylistRead& pr = pls[j.pl];
        const hls::Segment& s = pr.tl.segments[static_cast<std::size_t>(j.seg)];
        const auto si = static_cast<std::size_t>(j.seg);
        if (!j.read) {   // its size only: the first byte
            const Fetched f = fetch(s.uri, 0, 1, 1, st, received);
            if (f.ok() && f.total > 0) pr.sizes[si] = f.total;
            ++count_.sizes;
            return;
        }
        Fetched f = fetch(s.uri, s.range.offset, s.range.length, -1, st, received);
        hls::SegmentRead r;
        if (!f.ok()) {
            r.index = j.seg;
            r.error = f.error;
        } else {
            const auto bytes = static_cast<std::int64_t>(f.body.size());
            pr.sizes[si] = s.range.whole() ? (f.total > 0 ? f.total : bytes) : s.range.length;
            std::string data;
            if (s.map >= 0) {
                const hls::InitMap& mp = pr.tl.maps[static_cast<std::size_t>(s.map)];
                const Fetched& init = inits[init_index.at({ mp.uri, mp.range.offset })].second;
                if (!init.ok()) {
                    r.index = j.seg;
                    r.bytes = bytes;
                    r.error = "its init segment could not be fetched: " + init.error;
                }
                data = init.body;
            }
            if (r.error.empty()) {
                data += f.body;
                r = conformprobe::read_segment(reinterpret_cast<const std::uint8_t*>(data.data()), data.size(), j.seg,
                                               pr.sizes[si], j.encrypted);
            }
        }
        {
            std::lock_guard lk(reads_mtx);
            pr.reads.push_back(std::move(r));
        }
        ++count_.reads;
    });
    for (auto& pr : pls)
        std::sort(pr.reads.begin(), pr.reads.end(),
                  [](const hls::SegmentRead& a, const hls::SegmentRead& b) { return a.index < b.index; });

    // Markers only: nothing was read to compare the declarations with.
    hls::ConformReport rep = o.markers_only ? hls::ConformReport{} : hls::analyze_conformance(m, pls);

    // ── Markers: every variant, audio and subtitle playlist ──────────────────
    std::vector<hls::MarkerIn> marker_in;
    {
        const auto timeline_of = [&](const std::string& uri) -> const hls::PlaylistRead* {
            for (const auto* v : { &pls, &subs })
                for (const auto& p : *v)
                    if (p.uri == uri) return &p;
            return nullptr;
        };
        std::set<std::string> used;
        const auto push = [&](const std::string& uri, std::string label, hls::Track kind) {
            if (uri.empty() || !used.insert(uri).second) return;
            const hls::PlaylistRead* p = timeline_of(uri);
            if (!p || !p->error.empty() || !p->tl.ok) return;
            marker_in.push_back({ std::move(label), kind, p->tl });
        };
        for (std::size_t i = 0; i < m.variants.size(); ++i) {
            const auto& v = m.variants[i];
            push(v.uri, v.height > 0 ? std::format("v{} {}p", i, v.height) : std::format("v{}", i), hls::Track::Video);
        }
        for (const auto& md : m.media) {
            if (md.type != "AUDIO" && md.type != "SUBTITLES") continue;
            const bool audio = md.type == "AUDIO";
            std::string name = md.name.empty() ? md.language : md.name;
            push(md.uri, std::format("{} {}/{}", audio ? "audio" : "subtitles", md.group_id, name),
                 audio ? hls::Track::Audio : hls::Track::Text);
        }
    }
    bool any_marker = false;
    for (const auto& mi : marker_in) any_marker = any_marker || !mi.tl.markers.empty();
    hls::MarkerReport markers = any_marker ? hls::analyze_markers(marker_in) : hls::MarkerReport{};
    const bool stopped = st.stop_requested();
    logger::info("ConformProbe: {} — {} playlists{}, {} segments read, {} sized, {:.1f} MB in {:.1f} s: {} errors, "
                 "{} warnings{}", source, pls.size(),
                 subs.empty() ? std::string() : std::format(" (and {} subtitle, for the markers)", subs.size()), count_.reads.load(), count_.sizes.load(),
                 static_cast<double>(received.load()) / 1e6,
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count(), rep.errors,
                 rep.warnings, stopped ? " (stopped)" : "");
    {
        std::lock_guard lk(mtx_);
        result_.playlists = std::move(pls);
        result_.report = std::move(rep);
        result_.marker_in = std::move(marker_in);
        result_.markers = std::move(markers);
    }
    finish(stopped ? State::Stopped : State::Done);
}
