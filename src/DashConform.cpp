#include "DashConform.h"

#include "JsonText.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <set>

namespace dash {

using hls::Cell;
using hls::CodecFamily;
using hls::CodecId;
using hls::Finding;

namespace {

std::string join(const std::vector<std::string>& v, std::string_view sep)
{
    std::string out;
    for (const auto& s : v) {
        if (!out.empty()) out += sep;
        out += s;
    }
    return out;
}

Severity worse(Severity a, Severity b) { return static_cast<int>(a) < static_cast<int>(b) ? a : b; }

std::string secs(double s) { return std::format("{:.3f} s", s); }
std::string millis(double s) { return std::format("{:+.0f} ms", s * 1000.0); }

// "#12" ($Number$), else its MPD time.
std::string seg_name(const Representation& r, int i)
{
    if (i < 0 || static_cast<std::size_t>(i) >= r.segments.size()) return std::format("[{}]", i);
    const SegmentRef& s = r.segments[static_cast<std::size_t>(i)];
    return s.number >= 0 ? std::format("#{}", s.number) : std::format("at {}", secs(s.start));
}

// At most `k` names, then how many more.
std::string seg_list(const Representation& r, const std::vector<int>& idx, std::size_t k = 8)
{
    std::vector<std::string> names;
    for (std::size_t i = 0; i < idx.size() && i < k; ++i) names.push_back(seg_name(r, idx[i]));
    std::string out = join(names, ", ");
    if (idx.size() > k) out += std::format(" and {} more", idx.size() - k);
    return out;
}

std::string rate_text(std::int64_t bps) { return hls::format_bitrate(static_cast<double>(bps)); }

std::vector<std::string> split_codecs(const std::string& s)
{
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i <= s.size()) {
        const std::size_t j = std::min(s.find(',', i), s.size());
        std::string part = s.substr(i, j - i);
        part.erase(0, part.find_first_not_of(" \t"));
        part.erase(part.find_last_not_of(" \t") + 1);
        if (!part.empty()) out.push_back(part);
        i = j + 1;
    }
    return out;
}

int transfer_declared(const Representation& r, std::string* where)
{
    for (const auto* list : { &r.essential, &r.supplemental })
        for (const Descriptor& d : *list)
            if (d.scheme == "urn:mpeg:mpegB:cicp:TransferCharacteristics") {
                if (where) *where = list == &r.essential ? "EssentialProperty" : "SupplementalProperty";
                char* end = nullptr;
                const long v = std::strtol(d.value.c_str(), &end, 10);
                return end != d.value.c_str() && !*end ? static_cast<int>(v) : -1;
            }
    return -1;
}

bool hdr(int t) { return t == 16 || t == 18; }

// The largest of (bits of segments k..i) − B × (end_i − start_k) over k ≤ i,
// and where: end_i is segment i's end (by_end) or start. B covers the
// segments when it is at most B × @minBufferTime.
struct Excess {
    double bits = -std::numeric_limits<double>::infinity();
    int    from = -1, to = -1;
};

Excess max_excess(const std::vector<double>& start, const std::vector<double>& dur, const std::vector<double>& bits,
                  double b, bool by_end)
{
    Excess e;
    double best = -std::numeric_limits<double>::infinity();   // max over k <= i of b × start_k − bits before k
    int    best_k = -1;
    double prefix = 0.0;
    for (std::size_t i = 0; i < bits.size(); ++i) {
        if (const double cand = b * start[i] - prefix; cand > best) {
            best = cand;
            best_k = static_cast<int>(i);
        }
        prefix += bits[i];
        const double end = by_end ? start[i] + dur[i] : start[i];
        if (const double x = prefix - b * end + best; x > e.bits) {
            e.bits = x;
            e.from = best_k;
            e.to = static_cast<int>(i);
        }
    }
    return e;
}

// The smallest bit rate that covers the segments with buffer `t`.
double least_rate(const std::vector<double>& start, const std::vector<double>& dur, const std::vector<double>& bits,
                  double t, bool by_end)
{
    double total = 0.0;
    for (const double b : bits) total += b;
    double lo = 0.0, hi = total / t + 1.0;
    for (int it = 0; it < 200 && hi - lo > 1e-9 * std::max(1.0, hi); ++it) {
        const double mid = (lo + hi) / 2.0;
        (max_excess(start, dur, bits, mid, by_end).bits <= mid * t ? hi : lo) = mid;
    }
    return hi;
}

} // namespace

// ── Public helpers ───────────────────────────────────────────────────────────

Bucket bucket_model(const std::vector<double>& starts, const std::vector<double>& seconds,
                    const std::vector<std::int64_t>& bytes, double min_buffer, double bandwidth)
{
    Bucket b;
    const std::size_t n = std::min({ starts.size(), seconds.size(), bytes.size() });
    b.total = static_cast<int>(n);
    std::vector<double> bits(n), st(starts.begin(), starts.begin() + static_cast<std::ptrdiff_t>(n)),
                        du(seconds.begin(), seconds.begin() + static_cast<std::ptrdiff_t>(n));
    for (std::size_t i = 0; i < n; ++i) {
        bits[i] = bytes[i] > 0 ? 8.0 * static_cast<double>(bytes[i]) : 0.0;
        b.sized += bytes[i] >= 0 ? 1 : 0;
    }
    if (n == 0 || b.sized == 0 || !(min_buffer > 0.0)) return b;
    b.need = least_rate(st, du, bits, min_buffer, true);
    b.safe = least_rate(st, du, bits, min_buffer, false);
    const Excess e = max_excess(st, du, bits, bandwidth > 0.0 ? bandwidth : b.need, true);
    b.from = e.from;
    b.to = e.to;
    if (bandwidth > 0.0) b.buffer = std::max(0.0, e.bits / bandwidth);
    return b;
}

int channel_count(const Descriptor& d)
{
    char* end = nullptr;
    const long v = std::strtol(d.value.c_str(), &end, d.scheme.find("dolby") != std::string::npos ? 16 : 10);
    if (end == d.value.c_str() || *end || v < 0) return -1;
    if (d.scheme == "urn:mpeg:dash:23003:3:audio_channel_configuration:2011") return static_cast<int>(v);
    if (d.scheme == "urn:mpeg:mpegB:cicp:ChannelConfiguration") {
        // ISO/IEC 23091-3 ChannelConfiguration 1–14, 16, 19.
        static constexpr int kCount[] = { -1, 1, 2, 3, 4, 5, 6, 8, 2, 3, 4, 7, 8, 24, 8, -1, 10, -1, -1, 12 };
        return v < static_cast<long>(std::size(kCount)) ? kCount[v] : -1;
    }
    if (d.scheme == "tag:dolby.com,2014:dash:audio_channel_configuration:2011"
        || d.scheme == "urn:dolby:dash:audio_channel_configuration:2011") {
        // 16 bits from L (bit 15) to LFE (bit 0); bits 10, 9, 6, 5, 4 and 2
        // are pairs (Lc/Rc, Lrs/Rrs, Lsd/Rsd, Lw/Rw, Lvh/Rvh, Lts/Rts).
        if (v > 0xFFFF) return -1;
        int n = 0;
        for (int bit = 0; bit < 16; ++bit)
            if (v & (1L << bit)) n += (bit == 10 || bit == 9 || bit == 6 || bit == 5 || bit == 4 || bit == 2) ? 2 : 1;
        return n;
    }
    return -1;
}

int sap_type(const hls::SegmentRead& s)
{
    using hls::FirstPicture;
    if (!s.video) return s.audio ? 1 : -1;
    switch (s.first) {
        case FirstPicture::Idr:      return s.leading == 0 ? 1 : 2;
        case FirstPicture::Cra:
        case FirstPicture::Bla:      return s.leading == 0 ? 1 : s.rasl > 0 ? 3 : 2;
        case FirstPicture::Recovery: return s.leading == 0 ? 1 : 3;
        case FirstPicture::Intra:    return -1;
        case FirstPicture::Other:    return 0;
        case FirstPicture::Unknown:  break;
    }
    // Not H.264 / HEVC (AV1, VP9, …): the keyframe flag.
    return s.frames == 0 ? -1 : s.first_key ? 1 : 0;
}

const char* check_name(Check c)
{
    switch (c) {
        case Check::Bandwidth:  return "@bandwidth";
        case Check::Codecs:     return "@codecs";
        case Check::Resolution: return "@width/@height";
        case Check::FrameRate:  return "@frameRate";
        case Check::Audio:      return "audio";
        case Check::Colour:     return "colour";
        case Check::Sap:        return "SAP";
        case Check::Timeline:   return "timeline";
        case Check::Alignment:  return "alignment";
        case Check::Count:      break;
    }
    return "?";
}

std::string rep_label(const Mpd& m, int i)
{
    if (i < 0 || static_cast<std::size_t>(i) >= m.reps.size()) return "?";
    const Representation& r = m.reps[static_cast<std::size_t>(i)];
    std::string label = r.id;
    if (m.periods.size() > 1) {
        const Period& p = m.periods[static_cast<std::size_t>(r.period)];
        label = std::format("{}/{}", p.id.empty() ? std::to_string(r.period) : p.id, label);
    }
    std::string what = hls::track_name(r.kind);
    if (r.width > 0) what += std::format(", {}×{}", r.width, r.height);
    if (r.kind == Track::Audio && !r.lang.empty()) what += ", " + r.lang;
    return std::format("{} ({})", label, what);
}

// ── The verdict ──────────────────────────────────────────────────────────────

Report analyze_conformance(const Mpd& m, const std::vector<RepRead>& reads)
{
    Report rep;
    // The same text for several Representations is one finding naming them
    // all. Returns the finding, to name more on (before the next add).
    std::vector<Finding> found;
    auto name = [](Finding& f, int r) {
        if (r >= 0 && std::find(f.renditions.begin(), f.renditions.end(), r) == f.renditions.end())
            f.renditions.push_back(r);
    };
    auto add = [&](Severity sev, int r, std::string title, std::string detail) -> Finding& {
        for (auto& f : found)
            if (f.sev == sev && f.title == title && f.detail == detail) {
                name(f, r);
                return f;
            }
        Finding& f = found.emplace_back();
        f.sev = sev;
        f.title = std::move(title);
        f.detail = std::move(detail);
        name(f, r);
        return f;
    };

    for (const auto& p : m.problems) add(Severity::Error, -1, "The MPD: " + p, {});
    if (m.dynamic)
        add(Severity::Info, -1, "A dynamic MPD: checked over the segments listed at the time of the check",
            "Segments addressed by @duration are the ones available then, at most the last "
                + std::to_string(kLiveSegments) + " of each Representation.");

    // @bandwidths short of what the segments need by kPeakSlack at most: one note for all.
    struct Slight { int rep; double frac, bps; };
    std::vector<Slight> slight;

    rep.rows.resize(m.reps.size());
    for (std::size_t ri = 0; ri < m.reps.size(); ++ri) {
        const Representation& r = m.reps[ri];
        RepRow&        row = rep.rows[ri];
        const int      idx = static_cast<int>(ri);
        const RepRead* rr = ri < reads.size() ? &reads[ri] : nullptr;
        const double   ts = static_cast<double>(r.timescale);

        for (const auto& p : r.problems)
            add(p.find("mandatory") != std::string::npos ? Severity::Error : Severity::Warning, idx, p, {});
        if (!rr) {
            add(Severity::Info, idx, "not read", {});
            continue;
        }
        if (!rr->error.empty()) add(Severity::Warning, idx, "could not be read", rr->error);
        if (!rr->skipped.empty()) add(Severity::Info, idx, "segments not read: " + rr->skipped, {});
        row.read = static_cast<int>(rr->reads.size());

        std::vector<const SegmentSeen*> seen;
        for (const auto& s : rr->reads) {
            if (s.read.error.empty()) seen.push_back(&s);
            else add(Severity::Warning, idx, std::format("segment {} could not be read", seg_name(r, s.read.index)),
                     s.read.error);
        }
        const hls::VideoSeen* video = nullptr;
        const hls::AudioSeen* audio = nullptr;
        for (const SegmentSeen* s : seen) {
            if (!video && s->read.video) video = &*s->read.video;
            if (!audio && s->read.audio) audio = &*s->read.audio;
        }
        if (rr->init) {
            if (!video && rr->init->video) video = &*rr->init->video;
            if (!audio && rr->init->audio) audio = &*rr->init->audio;
        }

        // ── @bandwidth ───────────────────────────────────────────────────────
        {
            Cell& c = row.cell(Check::Bandwidth);
            c.declared = r.bandwidth >= 0 ? rate_text(r.bandwidth) : "absent";
            const double t = m.min_buffer_time;
            std::vector<double> starts, durs;
            for (const auto& s : r.segments) {
                starts.push_back(s.start);
                durs.push_back(s.seconds);
            }
            std::vector<std::int64_t> sizes = rr->sizes;
            sizes.resize(r.segments.size(), -1);
            if (r.kind == Track::Text) {
                c.note = "Text: not checked.";
            } else if (r.segments.empty()) {
                c.note = "No segments listed.";
            } else if (!(t > 0.0)) {
                c.note = "No @minBufferTime to check it with.";
            } else {
                const double bw = r.bandwidth > 0 ? static_cast<double>(r.bandwidth) : 0.0;
                const Bucket b = bucket_model(starts, durs, sizes, t, bw);
                row.bucket = b;
                if (b.sized == 0) {
                    c.note = "No segment size is known.";
                } else {
                    c.checked = true;
                    c.measured = hls::format_bitrate(b.need);
                    const std::string scope = b.complete() ? std::string()
                        : std::format(" Over the {} of {} segments whose size is known: a lower bound.", b.sized, b.total);
                    const std::string need_text =
                        std::format("{} (every segment in by the time it ends playing; {} would hold even if each had "
                                    "to be in before it starts)", hls::format_bitrate(b.need), hls::format_bitrate(b.safe));
                    if (r.bandwidth < 0) {
                        c.sev = Severity::Error;
                        c.note = "Representation@bandwidth is mandatory. The segments need " + need_text + "." + scope;
                    } else if (const double frac = (b.need - bw) / b.need; frac > 1e-9 && frac <= hls::kPeakSlack) {
                        c.sev = Severity::Info;
                        c.note = std::format("@bandwidth {} is {:.2f}% ({:.0f} b/s) below the {} with @minBufferTime {}: "
                                             "within {:g}%, as close as a @bandwidth taken from the encoder's bit rate, "
                                             "without the bytes of the segments' boxes, can come.{}", c.declared,
                                             frac * 100.0, b.need - bw, need_text, secs(t), hls::kPeakSlack * 100.0, scope);
                        slight.push_back({ idx, frac, b.need - bw });
                    } else if (frac > 1e-9) {
                        std::int64_t run_bytes = 0;
                        for (int i = b.from; i >= 0 && i <= b.to; ++i)
                            run_bytes += std::max<std::int64_t>(sizes[static_cast<std::size_t>(i)], 0);
                        const auto f = static_cast<std::size_t>(b.from), l = static_cast<std::size_t>(b.to);
                        const double span = starts[l] + durs[l] - starts[f];
                        const std::string run = b.from == b.to ? "segment " + seg_name(r, b.from)
                                              : std::format("segments {}–{}", seg_name(r, b.from), seg_name(r, b.to));
                        c.sev = Severity::Error;
                        c.note = std::format(
                            "With @minBufferTime {}, delivered at {} from the start of segment {}, {} ({:.2f} MB, {:.3f} s "
                            "of media) {} not all in until {:.2f} s after {} finished playing; at {} playout needs a "
                            "@minBufferTime of {}. They need {}.{}",
                            secs(t), c.declared, seg_name(r, b.from), run, static_cast<double>(run_bytes) / 1e6, span,
                            b.from == b.to ? "is" : "are", b.buffer - t, b.from == b.to ? "it has" : "the last of them has",
                            c.declared, secs(b.buffer), need_text, scope);
                        add(Severity::Error, idx,
                            std::format("@bandwidth {} below the {} its segments need with @minBufferTime {}", c.declared,
                                        hls::format_bitrate(b.need), secs(t)),
                            c.note + " ISO/IEC 23009-1 defines @bandwidth by this: delivered at @bandwidth from any "
                                     "segment start, playout begun @minBufferTime after the first bit never runs out. A "
                                     "player that fits @bandwidth to its link can stall on this Representation.");
                    } else if (b.complete() && bw > hls::kOverDeclared * b.safe) {
                        c.sev = Severity::Info;
                        c.note = std::format("@bandwidth {} is {:.2f} × the {} that holds even if every segment had to "
                                             "be in before it starts playing (they need {}): players wait for a faster "
                                             "link than this Representation needs.", c.declared, bw / b.safe,
                                             hls::format_bitrate(b.safe), hls::format_bitrate(b.need));
                        add(Severity::Info, idx, std::format("@bandwidth {:.2f} × what its segments need at most", bw / b.safe),
                            c.note);
                    } else {
                        c.sev = Severity::Ok;
                        c.note = std::format("@bandwidth {} covers the {} with @minBufferTime {}.{}", c.declared, need_text,
                                             secs(t), scope);
                    }
                }
            }
        }

        // ── @codecs ──────────────────────────────────────────────────────────
        {
            Cell& c = row.cell(Check::Codecs);
            c.declared = r.codecs.empty() ? "absent" : r.codecs;
            std::vector<CodecId> decl;
            for (const auto& s : split_codecs(r.codecs)) decl.push_back(hls::parse_codec(s));
            std::vector<std::string> seen_strings, notes;
            Severity sev = Severity::Ok;
            auto issue = [&](Severity s, std::string title, std::string detail) {
                sev = worse(sev, s);
                notes.push_back(title + (detail.empty() ? "" : " — " + detail));
                add(s, idx, std::move(title), std::move(detail));
            };
            auto find_decl = [&](CodecFamily fam) {
                for (std::size_t j = 0; j < decl.size(); ++j)
                    if (hls::family_of(decl[j]) == fam) return static_cast<int>(j);
                return -1;
            };
            if (r.codecs.empty() && r.kind != Track::Text)
                issue(Severity::Warning, "no @codecs",
                      "Players choose a Representation by it before downloading anything (with MSE, isTypeSupported).");
            if (video) {
                const CodecId act = hls::parse_codec(video->codec_string);
                const CodecFamily fam = act.ok ? hls::family_of(act) : hls::family_of_codec(video->codec);
                std::string shown = video->codec_string.empty() ? video->codec : video->codec_string;
                if (video->dv_profile >= 0) shown += std::format(" + Dolby Vision {}.{}", video->dv_profile, video->dv_level);
                seen_strings.push_back(shown);
                const std::string what = act.ok ? std::format("{} ({})", hls::describe_codec(act), video->codec_string)
                                                : hls::family_name(fam);
                if (const int dv = video->dv_profile >= 0 ? find_decl(CodecFamily::DolbyVision) : -1; dv >= 0) {
                    const CodecId& d = decl[static_cast<std::size_t>(dv)];
                    if (d.ok && d.profile != video->dv_profile)
                        issue(Severity::Error, std::format("@codecs Dolby Vision profile {}, the stream's is {}", d.profile,
                                                           video->dv_profile), {});
                    else if (d.ok && d.level < video->dv_level)
                        issue(Severity::Error, std::format("@codecs Dolby Vision level {} below the stream's {}", d.level,
                                                           video->dv_level), {});
                    else if (d.ok && d.level > video->dv_level)
                        issue(Severity::Info, std::format("@codecs Dolby Vision level {} above the stream's {}", d.level,
                                                          video->dv_level), {});
                } else if (!decl.empty()) {
                    const int j = find_decl(fam);
                    if (j < 0) {
                        issue(Severity::Error, "@codecs does not name the video's format",
                              std::format("The stream is {}; @codecs is \"{}\".", what, r.codecs));
                    } else if (const CodecId& d = decl[static_cast<std::size_t>(j)]; !d.ok) {
                        issue(Severity::Warning, std::format("@codecs \"{}\" does not parse", d.text),
                              std::format("The stream is {}.", what));
                    } else if (!act.ok) {
                        notes.push_back(std::format("The stream's {} configuration could not be read: the format alone was "
                                                    "compared.", hls::family_name(fam)));
                    } else {
                        for (hls::CodecIssue& ci : hls::compare_video_codec(d, act, video->sample_entry, "@codecs",
                                                                            "Representation"))
                            issue(ci.sev, std::move(ci.title), std::move(ci.detail));
                    }
                    if (video->dv_profile >= 0)
                        issue(Severity::Info, std::format("the stream carries a Dolby Vision {}.{} configuration @codecs "
                                                          "does not name", video->dv_profile, video->dv_level), {});
                }
            }
            if (audio) {
                const CodecId act = hls::parse_codec(audio->codec_string);
                const CodecFamily fam = act.ok ? hls::family_of(act) : hls::family_of_codec(audio->codec);
                seen_strings.push_back(audio->codec_string.empty() ? audio->codec : audio->codec_string);
                if (!decl.empty()) {
                    const int j = find_decl(fam);
                    if (j < 0)
                        issue(Severity::Error, "@codecs does not name the audio's format",
                              std::format("The audio is {} ({}); @codecs is \"{}\".",
                                          act.ok ? hls::describe_codec(act) : hls::family_name(fam), seen_strings.back(),
                                          r.codecs));
                    else if (const CodecId& d = decl[static_cast<std::size_t>(j)];
                             fam == CodecFamily::Aac && d.ok && act.ok && d.object_type != act.object_type)
                        issue(Severity::Warning, std::format("@codecs {} but the audio is {}", hls::describe_codec(d),
                                                             hls::describe_codec(act)),
                              std::format("@codecs says {}; the audio decodes as {}.", d.text, audio->codec_string));
                }
            }
            for (const CodecId& d : decl)
                if (d.kind == Track::Unknown)
                    issue(Severity::Warning, std::format("@codecs format \"{}\" is not one this check knows", d.text), {});
            // Checked: compared with what was read, or wrong in itself.
            c.checked = r.kind != Track::Text && (!seen_strings.empty() || sev != Severity::Ok);
            c.sev = c.checked ? sev : Severity::Info;
            c.measured = join(seen_strings, ",");
            c.note = notes.empty() ? (seen_strings.empty() ? "Nothing was read to compare with."
                                                           : "The format read is the one declared, at the profile and level read.")
                                   : join(notes, "\n");
        }

        // ── @width / @height / @sar ──────────────────────────────────────────
        if (r.kind == Track::Video) {
            Cell& c = row.cell(Check::Resolution);
            c.declared = r.width > 0 ? std::format("{}×{}", r.width, r.height) : "absent";
            if (r.sar_num > 0) c.declared += std::format(" sar {}:{}", r.sar_num, r.sar_den);
            if (!video || video->width <= 0) {
                c.note = "No video was read.";
            } else {
                c.measured = std::format("{}×{}", video->width, video->height);
                if (video->sar_num > 0) c.measured += std::format(" sar {}:{}", video->sar_num, video->sar_den);
                std::vector<std::string> notes;
                Severity sev = Severity::Ok;
                if (r.width <= 0) {
                    sev = Severity::Info;
                    notes.push_back("@width / @height not declared.");
                    add(Severity::Info, idx, "no @width / @height", "Players size their choice by them.");
                } else if (r.width != video->width || r.height != video->height) {
                    sev = Severity::Warning;
                    const std::string t = std::format("@width×@height {}×{}, the video decodes at {}×{}", r.width, r.height,
                                                      video->width, video->height);
                    notes.push_back(t);
                    add(Severity::Warning, idx, t, "DASH's @width and @height count samples; @sar gives their shape.");
                }
                if (r.sar_num > 0 && video->sar_num > 0
                    && static_cast<std::int64_t>(r.sar_num) * video->sar_den != static_cast<std::int64_t>(r.sar_den) * video->sar_num) {
                    sev = worse(sev, Severity::Warning);
                    const std::string t = std::format("@sar {}:{}, the stream's is {}:{}", r.sar_num, r.sar_den, video->sar_num,
                                                      video->sar_den);
                    notes.push_back(t);
                    add(Severity::Warning, idx, t, "The picture is shown at another shape than the MPD says.");
                }
                c.checked = r.width > 0 || r.sar_num > 0;
                c.sev = sev;
                c.note = notes.empty() ? std::format("The video decodes at {}.", c.measured) : join(notes, "\n");
            }
        }

        // ── @frameRate ───────────────────────────────────────────────────────
        if (r.kind == Track::Video) {
            Cell& c = row.cell(Check::FrameRate);
            c.declared = r.frame_rate > 0.0 ? r.frame_rate_text : "absent";
            double fps = 0.0;
            for (const SegmentSeen* s : seen) fps = std::max(fps, s->read.frame_rate);
            if (fps > 0.0) c.measured = std::format("{:.3f}", fps);
            if (fps <= 0.0) {
                c.note = "No frame rate was measured.";
            } else if (r.frame_rate <= 0.0) {
                c.note = "Not declared.";
            } else {
                c.checked = true;
                const bool off = std::fabs(r.frame_rate - fps) > hls::kRateSlack;
                c.sev = off ? Severity::Warning : Severity::Ok;
                c.note = std::format("@frameRate {} ({:.3f}); the segments run at {:.3f}.", r.frame_rate_text, r.frame_rate, fps);
                if (off)
                    add(Severity::Warning, idx, std::format("@frameRate {} but the video runs at {:.3f} fps", r.frame_rate_text, fps),
                        c.note);
            }
        }

        // ── Audio ────────────────────────────────────────────────────────────
        if (r.kind == Track::Audio) {
            Cell& c = row.cell(Check::Audio);
            int declared_ch = -1;
            std::string ch_text;
            for (const Descriptor& d : r.channels) {
                ch_text = d.value;
                if ((declared_ch = channel_count(d)) >= 0) break;
            }
            c.declared = std::format("{} Hz, {} ch", r.sampling_rate > 0 ? std::to_string(r.sampling_rate) : "?",
                                     declared_ch >= 0 ? std::to_string(declared_ch) : r.channels.empty() ? "?" : ch_text);
            if (!audio) {
                c.note = "No audio was read.";
            } else {
                c.measured = std::format("{} Hz, {} ch", audio->sample_rate, audio->channels);
                std::vector<std::string> notes;
                Severity sev = Severity::Ok;
                if (r.sampling_rate > 0 && audio->sample_rate > 0 && r.sampling_rate != audio->sample_rate) {
                    sev = Severity::Warning;
                    notes.push_back(std::format("@audioSamplingRate {}, the audio decodes at {} Hz", r.sampling_rate,
                                                audio->sample_rate));
                    add(Severity::Warning, idx, notes.back(), {});
                }
                if (r.channels.empty()) {
                    sev = worse(sev, Severity::Info);
                    notes.push_back("No AudioChannelConfiguration.");
                    add(Severity::Info, idx, "no AudioChannelConfiguration", "Players and devices choose audio by its channels.");
                } else if (declared_ch < 0) {
                    notes.push_back(std::format("AudioChannelConfiguration {} \"{}\" is not read here.",
                                                r.channels.front().scheme, r.channels.front().value));
                } else if (audio->channels > 0 && declared_ch != audio->channels) {
                    sev = Severity::Warning;
                    notes.push_back(std::format("AudioChannelConfiguration says {} channels, the audio decodes to {}",
                                                declared_ch, audio->channels));
                    add(Severity::Warning, idx, notes.back(),
                        std::format("{} = \"{}\".", r.channels.front().scheme, r.channels.front().value));
                }
                c.checked = r.sampling_rate > 0 || declared_ch >= 0;
                c.sev = sev;
                c.note = notes.empty() ? "The rate and channels decoded are the ones declared." : join(notes, "\n");
            }
        }

        // ── Colour ───────────────────────────────────────────────────────────
        if (r.kind == Track::Video) {
            Cell& c = row.cell(Check::Colour);
            std::string where;
            const int decl = transfer_declared(r, &where);
            c.declared = decl >= 0 ? std::format("{} ({})", decl, hls::transfer_name(decl)) : "absent";
            // Dolby Vision without a compatible base layer (profile 5) is PQ
            // whatever its VUI says.
            const int tc = !video ? -1 : video->dv_profile >= 0 && video->dv_compat == 0 ? 16 : video->transfer;
            if (tc < 0) {
                c.note = "No video was read.";
            } else {
                c.measured = std::format("{} ({})", tc, hls::transfer_name(tc));
                c.checked = true;
                if (tc == 2) {
                    c.checked = false;
                    c.note = "The stream does not say (transfer characteristics 2, unspecified).";
                } else if (decl < 0) {
                    c.sev = tc == 16 ? Severity::Error : tc == 18 ? Severity::Warning : Severity::Ok;
                    c.note = hdr(tc) ? std::format("The video is {} and no urn:mpeg:mpegB:cicp:TransferCharacteristics "
                                                   "descriptor says so: a player or device that cannot show it takes it "
                                                   "for SDR.", hls::transfer_name(tc))
                                     : "SDR, and nothing declared: the default.";
                    if (hdr(tc))
                        add(c.sev, idx, std::format("{} video without a TransferCharacteristics descriptor",
                                                    tc == 16 ? "PQ" : "HLG"), c.note);
                } else if (decl == tc) {
                    c.sev = Severity::Ok;
                    c.note = std::format("{} TransferCharacteristics {}, as the stream signals.", where, decl);
                } else {
                    c.sev = hdr(decl) || hdr(tc) ? Severity::Error : Severity::Info;
                    c.note = std::format("{} TransferCharacteristics {} ({}), the stream's is {} ({}).", where, decl,
                                         hls::transfer_name(decl), tc, hls::transfer_name(tc));
                    add(c.sev, idx, std::format("TransferCharacteristics {} but the stream's is {}", decl, tc), c.note);
                }
            }
        }

        // ── SAP ──────────────────────────────────────────────────────────────
        {
            Cell& c = row.cell(Check::Sap);
            // SegmentBase: its subsegments' (the Segment Index's); else the
            // segments', and a segment starts a subsegment too.
            const bool sub = r.subsegment_sap >= 0
                          && (r.addressing == Addressing::Base || r.start_with_sap < 0);
            const int declared = sub ? r.subsegment_sap : r.start_with_sap;
            const char* attr = sub ? "@subsegmentStartsWithSAP" : "@startWithSAP";
            c.declared = declared >= 0 ? std::format("{} {}", attr, declared) : "absent";
            std::map<int, std::vector<int>> by_type;
            std::vector<std::string> index_off;
            for (const SegmentSeen* s : seen) {
                const int t = sap_type(s->read);
                by_type[t].push_back(s->read.index);
                const auto si = static_cast<std::size_t>(s->read.index);
                if (si < r.segments.size() && r.segments[si].sap_type >= 0 && t >= 0
                    && (r.segments[si].sap_type == 0 ? t != 0 : (t == 0 || t > r.segments[si].sap_type)))
                    index_off.push_back(std::format("{}: the index says {}, it starts with {}", seg_name(r, s->read.index),
                                                    r.segments[si].sap_type ? std::format("SAP type {}", r.segments[si].sap_type)
                                                                            : std::string("no SAP"),
                                                    t ? std::format("SAP type {}", t) : std::string("no SAP")));
            }
            std::vector<std::string> types, notes;
            for (const auto& [t, segs] : by_type) types.push_back(t < 0 ? "?" : std::to_string(t));
            c.measured = join(types, ", ");
            Severity sev = Severity::Ok;
            auto issue = [&](Severity s, std::string title, std::string detail) {
                sev = worse(sev, s);
                notes.push_back(title);
                add(s, idx, std::move(title), std::move(detail));
            };
            for (const auto& [t, segs] : by_type) {
                const std::string list = seg_list(r, segs);
                if (t == 0) {
                    issue(declared >= 1 ? Severity::Error : Severity::Warning,
                          declared >= 1 ? std::format("{} {} but {} of the segments read start with a P / B picture", attr,
                                                      declared, segs.size())
                                        : std::format("{} of the segments read start with a P / B picture", segs.size()),
                          list + ": a player starting or switching there has nothing to decode until the next random "
                                 "access point.");
                } else if (t < 0 && !segs.empty() && r.kind == Track::Video) {
                    issue(Severity::Info, std::format("segments start with an I picture that is neither IDR nor a recovery "
                                                      "point"), list + ": whether later pictures reference across it is not "
                                                                       "told by the picture itself.");
                } else if (declared >= 1 && t > declared) {
                    issue(Severity::Error, std::format("{} {} but segments start with SAP type {}", attr, declared, t),
                          list + (t == 3 ? ": open GOPs — the leading pictures after the CRA / recovery point reference the "
                                           "segment before and are dropped when playback starts or switches there."
                                         : ": leading pictures are presented before the IDR."));
                }
            }
            if (!index_off.empty())
                issue(Severity::Error, "the Segment Index's SAP flags differ from the segments", join(index_off, "; "));
            c.checked = !by_type.empty();
            c.sev = c.checked ? sev : Severity::Info;
            c.note = !c.checked ? "Nothing was read."
                   : notes.empty() ? std::format("Every segment read starts with SAP type {}.", c.measured)
                                   : join(notes, "\n");
        }

        // ── Timeline ─────────────────────────────────────────────────────────
        {
            Cell& c = row.cell(Check::Timeline);
            c.declared = addressing_name(r.addressing);
            const bool nominal = r.addressing == Addressing::Template;   // @duration: approximate starts
            struct Off { int seg; double by; };
            std::vector<Off> start_warn, start_bad, start_note, start_edit, dur_warn, dur_note;
            double worst_start = 0.0, worst_dur = 0.0;
            for (const SegmentSeen* s : seen) {
                const auto si = static_cast<std::size_t>(s->read.index);
                if (si >= r.segments.size()) continue;
                const SegmentRef& g = r.segments[si];
                if (std::isfinite(s->start)) {
                    const double d = s->start - static_cast<double>(g.time) / ts;
                    worst_start = std::max(worst_start, std::fabs(d));
                    if (nominal) {
                        if (std::fabs(d) > 0.5 * g.seconds) start_warn.push_back({ s->read.index, d });
                        else if (std::fabs(d) > 0.25 * g.seconds) start_note.push_back({ s->read.index, d });
                    } else if (std::fabs(d) > kTimeSlack) {
                        if (s->edit != 0.0 && std::fabs(d + s->edit) <= kTimeSlack) start_edit.push_back({ s->read.index, d });
                        else if (std::fabs(d) > kTimeBad) start_bad.push_back({ s->read.index, d });
                        else start_warn.push_back({ s->read.index, d });
                    }
                }
                const double media = s->read.video ? s->read.media_duration : s->read.audio_duration;
                if (std::isfinite(media) && g.seconds > 0.0) {
                    const double d = media - g.seconds;
                    worst_dur = std::max(worst_dur, std::fabs(d));
                    if (std::fabs(d) > kTimeSlack) (nominal || s->read.leading > 0 ? dur_note : dur_warn).push_back({ s->read.index, d });
                }
            }
            auto list = [&](const std::vector<Off>& v) {
                std::vector<std::string> parts;
                for (std::size_t i = 0; i < v.size() && i < 8; ++i)
                    parts.push_back(std::format("{} {}", seg_name(r, v[i].seg), millis(v[i].by)));
                std::string out = join(parts, ", ");
                if (v.size() > 8) out += std::format(" and {} more", v.size() - 8);
                return out;
            };
            auto worst = [](const std::vector<Off>& v) {   // "600 ms": the largest, unsigned
                double w = 0.0;
                for (const auto& o : v) w = std::max(w, std::fabs(o.by));
                return std::format("{:.0f} ms", w * 1000.0);
            };
            std::vector<std::string> notes;
            Severity sev = Severity::Ok;
            auto issue = [&](Severity s, std::string title, std::string detail) {
                sev = worse(sev, s);
                notes.push_back(title);
                add(s, idx, std::move(title), std::move(detail));
            };
            const char* where = r.addressing == Addressing::Timeline ? "S@t" : r.addressing == Addressing::Base
                              ? "the Segment Index" : "the MPD";
            if (!start_bad.empty())
                issue(Severity::Error, std::format("segments start up to {} off where {} puts them", worst(start_bad), where),
                      list(start_bad) + " (earliest presentation time in the media − the timeline's). A player places each "
                                        "segment by the MPD: this far off, playback jumps or stalls at it.");
            if (!start_warn.empty())
                issue(Severity::Warning,
                      nominal ? std::format("segments start up to {} from their @duration slot, over half a segment",
                                            worst(start_warn))
                              : std::format("segments start up to {} off where {} puts them", worst(start_warn), where),
                      list(start_warn) + (nominal ? " — a player that computes the segment number from a time fetches the "
                                                    "neighbouring segment."
                                                  : " (earliest presentation time in the media − the timeline's): a gap or "
                                                    "an overlap at every switch there."));
            if (!start_note.empty())
                issue(Severity::Info, std::format("segments start up to {} from their @duration slot", worst(start_note)),
                      list(start_note));
            if (!start_edit.empty())
                issue(Severity::Info, "segments match the timeline only when the init segment's edit list is ignored",
                      list(start_edit) + " off with the edit list applied (ISO/IEC 14496-12); players that ignore it see "
                                         "the segments where the MPD says.");
            if (!dur_warn.empty())
                issue(Severity::Warning, std::format("segments last up to {} more or less than the MPD says", worst(dur_warn)),
                      list(dur_warn) + " (media duration − the timeline's).");
            if (!dur_note.empty())
                issue(Severity::Info, std::format("segment durations up to {} off the MPD's", worst(dur_note)),
                      list(dur_note) + (nominal ? " (@duration is nominal)." : " (they have leading pictures)."));
            // A static timeline fills its Period.
            const Period& pd = m.periods[static_cast<std::size_t>(r.period)];
            if (!m.dynamic && !r.segments.empty() && std::isfinite(pd.duration) && r.kind != Track::Text) {
                const double end = r.segments.back().start + r.segments.back().seconds, period_end = pd.start + pd.duration;
                if (end < period_end - kTimeSlack)
                    issue(Severity::Warning, std::format("the segments end {} before the Period does", secs(period_end - end)),
                          std::format("The last segment ends at {}, the Period at {}: players stall there or end early.",
                                      secs(end), secs(period_end)));
                else if (end > period_end + kTimeSlack)
                    issue(Severity::Info, std::format("the segments run {} past the Period's end", secs(end - period_end)),
                          std::format("The last segment ends at {}, the Period at {}: what is past it is not played.",
                                      secs(end), secs(period_end)));
            }
            // @maxSegmentDuration covers every segment.
            if (std::isfinite(m.max_segment_duration)) {
                double longest = 0.0;
                int over = 0;
                for (const auto& s : r.segments) {
                    longest = std::max(longest, s.seconds);
                    over += s.seconds > m.max_segment_duration + 0.0005 ? 1 : 0;
                }
                if (over > 0)
                    issue(longest > m.max_segment_duration + kTimeSlack ? Severity::Error : Severity::Warning,
                          std::format("segments up to {}, above @maxSegmentDuration {}", secs(longest),
                                      secs(m.max_segment_duration)),
                          std::format("{} of its {} segments. @maxSegmentDuration is the longest segment of any "
                                      "Representation; live players size their windows by it.", over, r.segments.size()));
            }
            c.checked = !seen.empty();
            c.sev = c.checked ? sev : Severity::Info;
            if (c.checked) c.measured = std::format("start ±{:.0f} ms, length ±{:.0f} ms", worst_start * 1000.0, worst_dur * 1000.0);
            c.note = !c.checked ? "Nothing was read." : notes.empty() ? "Every segment read starts and ends where the MPD says."
                                                                       : join(notes, "\n");
        }
    }

    // ── Alignment, AdaptationSet by AdaptationSet ────────────────────────────
    for (const AdaptationSet& as : m.sets) {
        std::vector<int> reps;
        for (const int i : as.reps)
            if (m.reps[static_cast<std::size_t>(i)].kind != Track::Text) reps.push_back(i);
        auto set_cells = [&](Severity sev, bool checked, const std::string& note) {
            for (const int i : reps) {
                Cell& c = rep.rows[static_cast<std::size_t>(i)].cell(Check::Alignment);
                c.declared = !as.segment_alignment.empty() ? "segment " + as.segment_alignment
                           : !as.subsegment_alignment.empty() ? "subsegment " + as.subsegment_alignment : "absent";
                c.checked = checked;
                c.sev = sev;
                c.note = note;
            }
        };
        if (reps.size() < 2) {
            set_cells(Severity::Info, false, "Alone in its AdaptationSet.");
            continue;
        }
        if (!as.aligned()) {
            set_cells(Severity::Info, false, "Not declared aligned.");
            Finding& f = add(Severity::Info, reps.front(), "@segmentAlignment / @subsegmentAlignment not set",
                             std::format("{} Representations in AdaptationSet {}: a player switching between them cannot "
                                         "count on their segment boundaries coinciding.", reps.size(),
                                         as.id.empty() ? "(no @id)" : as.id));
            for (const int i : reps) name(f, i);
            continue;
        }
        // The timelines as declared.
        const Representation& ref = m.reps[static_cast<std::size_t>(reps.front())];
        std::vector<std::string> diffs;
        std::vector<int> named{ reps.front() };
        for (std::size_t k = 1; k < reps.size(); ++k) {
            const Representation& o = m.reps[static_cast<std::size_t>(reps[k])];
            if (o.segments.empty() || ref.segments.empty()) continue;
            if (o.segments.size() != ref.segments.size()) {
                diffs.push_back(std::format("Representation {} lists {} segments, {} lists {}", o.id, o.segments.size(), ref.id,
                                            ref.segments.size()));
                named.push_back(reps[k]);
                continue;
            }
            for (std::size_t i = 0; i < o.segments.size(); ++i)
                if (std::fabs(o.segments[i].start - ref.segments[i].start) > 0.001
                    || std::fabs(o.segments[i].seconds - ref.segments[i].seconds) > 0.001) {
                    diffs.push_back(std::format("segment {} starts at {} in Representation {}, at {} in {}",
                                                seg_name(o, static_cast<int>(i)), secs(o.segments[i].start), o.id,
                                                secs(ref.segments[i].start), ref.id));
                    named.push_back(reps[k]);
                    break;
                }
        }
        // The segments read: the same index, in MPD time.
        std::map<int, std::vector<std::pair<int, double>>> at;   // segment index → (Representation, start)
        for (const int i : reps) {
            const Representation& r = m.reps[static_cast<std::size_t>(i)];
            if (static_cast<std::size_t>(i) >= reads.size()) continue;
            for (const SegmentSeen& s : reads[static_cast<std::size_t>(i)].reads) {
                const auto si = static_cast<std::size_t>(s.read.index);
                if (!s.read.error.empty() || !std::isfinite(s.start) || si >= r.segments.size()) continue;
                const SegmentRef& g = r.segments[si];
                at[s.read.index].emplace_back(i, g.start + s.start - static_cast<double>(g.time) / static_cast<double>(r.timescale));
            }
        }
        double spread = 0.0;
        std::string spread_at;
        for (const auto& [seg, v] : at) {
            if (v.size() < 2) continue;
            const auto [lo, hi] = std::minmax_element(v.begin(), v.end(),
                                                      [](const auto& a, const auto& b) { return a.second < b.second; });
            if (hi->second - lo->second > spread) {
                spread = hi->second - lo->second;
                const Representation& rl = m.reps[static_cast<std::size_t>(lo->first)];
                const Representation& rh = m.reps[static_cast<std::size_t>(hi->first)];
                spread_at = std::format("segment {}: {} in {}, {} in {}", seg_name(rl, seg), secs(lo->second), rl.id,
                                        secs(hi->second), rh.id);
            }
        }
        Severity sev = Severity::Ok;
        std::string note = std::format("Declared aligned; the timelines agree{}.",
                                       at.empty() ? "" : std::format(", and the segments read start within {:.0f} ms of each other",
                                                                     spread * 1000.0));
        if (!diffs.empty()) {
            sev = Severity::Error;
            note = "The timelines differ: " + join(diffs, "; ") + ".";
            Finding& f = add(Severity::Error, named.front(), "declared aligned, but the Representations' timelines differ",
                             note + " With @segmentAlignment, the n-th segment of every Representation of the "
                                    "AdaptationSet must not overlap any other segment of the others.");
            for (const int i : named) name(f, i);
        }
        if (diffs.empty() && spread > kAlignSlack) {   // differing timelines say it already
            const Severity s = spread > kAlignBad ? Severity::Error : Severity::Warning;
            sev = worse(sev, s);
            note += std::format(" The same segment starts up to {:.0f} ms apart in the media ({}).", spread * 1000.0, spread_at);
            Finding& f = add(s, reps.front(), std::format("aligned segments start up to {:.0f} ms apart in the media",
                                                          spread * 1000.0),
                             spread_at + ": a player switching at that boundary repeats or skips that much.");
            for (const int i : reps) name(f, i);
        }
        set_cells(sev, true, note);
    }

    if (!slight.empty()) {
        const auto most = std::max_element(slight.begin(), slight.end(),
                                           [](const Slight& a, const Slight& b) { return a.frac < b.frac; });
        Finding f;
        f.sev = Severity::Info;
        f.title = std::format("@bandwidth of {} Representation{} up to {:.2g}% below what the segments need", slight.size(),
                              slight.size() == 1 ? "" : "s", most->frac * 100.0);
        f.detail = std::format("Short by no more than {:g}% of the bit rate their segments need with @minBufferTime, as "
                               "close as a @bandwidth taken from the encoder's bit rate, without the bytes of the "
                               "segments' boxes, can come. The most: {}, {:.0f} b/s.",
                               hls::kPeakSlack * 100.0, m.reps[static_cast<std::size_t>(most->rep)].id, most->bps);
        for (const auto& x : slight) f.renditions.push_back(x.rep);
        found.push_back(std::move(f));
    }

    std::stable_sort(found.begin(), found.end(),
                     [](const Finding& a, const Finding& b) { return static_cast<int>(a.sev) < static_cast<int>(b.sev); });
    for (auto& f : found) {
        std::sort(f.renditions.begin(), f.renditions.end());
        if (f.sev == Severity::Error) ++rep.errors;
        if (f.sev == Severity::Warning) ++rep.warnings;
        for (const int i : f.renditions) {
            Severity& w = rep.rows[static_cast<std::size_t>(i)].worst;
            w = worse(w, f.sev);
        }
    }
    rep.findings = std::move(found);
    return rep;
}

// ── JSON ─────────────────────────────────────────────────────────────────────

std::string conformance_json(const Mpd& m, const std::vector<RepRead>& reads, const Report& r, const std::string& source)
{
    using json::number;
    using json::quote;
    auto num = [](double v) { return std::isfinite(v) ? number(v) : std::string("null"); };
    std::string o = "{\n";
    o += std::format("  \"source\": {},\n", quote(source));
    o += std::format("  \"summary\": {{\"representations\": {}, \"errors\": {}, \"warnings\": {}}},\n", m.reps.size(),
                     r.errors, r.warnings);
    o += std::format("  \"mpd\": {{\"type\": {}, \"profiles\": {}, \"min_buffer_time\": {}, "
                     "\"media_presentation_duration\": {}, \"max_segment_duration\": {}, \"listed_at\": {}, \"periods\": [",
                     quote(m.dynamic ? "dynamic" : "static"), quote(m.profiles), num(m.min_buffer_time), num(m.duration),
                     num(m.max_segment_duration), num(m.dynamic ? m.now : kNaN));
    for (std::size_t i = 0; i < m.periods.size(); ++i)
        o += std::format("{}{{\"id\": {}, \"start\": {}, \"duration\": {}}}", i ? ", " : "", quote(m.periods[i].id),
                         num(m.periods[i].start), num(m.periods[i].duration));
    o += "]},\n";

    o += "  \"representations\": [";
    for (std::size_t i = 0; i < m.reps.size(); ++i) {
        const Representation& p = m.reps[i];
        const RepRow* row = i < r.rows.size() ? &r.rows[i] : nullptr;
        const RepRead* rr = i < reads.size() ? &reads[i] : nullptr;
        std::string channels;
        for (const auto& d : p.channels) channels += std::format("{}{{\"scheme\": {}, \"value\": {}}}", channels.empty() ? "" : ", ",
                                                                 quote(d.scheme), quote(d.value));
        o += i ? ",\n    {" : "\n    {";
        o += std::format("\"index\": {}, \"id\": {}, \"period\": {}, \"adaptation_set\": {}, \"kind\": {}, \"addressing\": {},\n",
                         i, quote(p.id), p.period, p.set, quote(hls::track_name(p.kind)), quote(addressing_name(p.addressing)));
        o += std::format("     \"declared\": {{\"bandwidth\": {}, \"codecs\": {}, \"mime_type\": {}, \"width\": {}, \"height\": {}, "
                         "\"sar\": {}, \"frame_rate\": {}, \"audio_sampling_rate\": {}, \"audio_channels\": [{}], "
                         "\"start_with_sap\": {}, \"subsegment_starts_with_sap\": {}, \"transfer_characteristics\": {}, "
                         "\"protected\": {}, \"timescale\": {}, \"presentation_time_offset\": {}, \"segments\": {}, "
                         "\"duration\": {}}},\n",
                         p.bandwidth >= 0 ? std::to_string(p.bandwidth) : "null", p.codecs.empty() ? "null" : quote(p.codecs),
                         quote(p.mime_type), p.width, p.height,
                         p.sar_num > 0 ? quote(std::format("{}:{}", p.sar_num, p.sar_den)) : "null",
                         p.frame_rate_text.empty() ? "null" : quote(p.frame_rate_text), p.sampling_rate, channels,
                         p.start_with_sap, p.subsegment_sap, transfer_declared(p, nullptr), p.protection.empty() ? "false" : "true",
                         p.timescale, p.pto, p.segments.size(), num(p.total()));
        if (row && rr) {
            int sized = 0;
            std::int64_t bytes = 0;
            for (const auto b : rr->sizes) if (b >= 0) { ++sized; bytes += b; }
            o += std::format("     \"measured\": {{\"sized\": {}, \"bytes\": {}, \"need_bps\": {}, \"safe_bps\": {}, "
                             "\"min_buffer_time_at_bandwidth\": {}, \"segments_read\": {}, \"error\": {}, \"skipped\": {}}},\n",
                             sized, bytes, row->bucket.sized ? number(std::round(row->bucket.need)) : "null",
                             row->bucket.sized ? number(std::round(row->bucket.safe)) : "null", num(row->bucket.buffer), row->read,
                             rr->error.empty() ? "null" : quote(rr->error), rr->skipped.empty() ? "null" : quote(rr->skipped));
            // Every segment's [MPD start, duration, bytes]: what need_bps and safe_bps come from.
            o += "     \"segment_list\": [";
            for (std::size_t k = 0; k < p.segments.size(); ++k) {
                const std::int64_t b = k < rr->sizes.size() ? rr->sizes[k] : -1;
                o += std::format("{}[{}, {}, {}]", k ? ", " : "", num(p.segments[k].start), num(p.segments[k].seconds),
                                 b >= 0 ? std::to_string(b) : "null");
            }
            o += "],\n     \"checks\": {";
            for (std::size_t k = 0; k < row->cells.size(); ++k) {
                const Cell& c = row->cells[k];
                o += std::format("{}\n       {}: {{\"checked\": {}, \"severity\": {}, \"declared\": {}, \"measured\": {}, "
                                 "\"note\": {}}}", k ? "," : "", quote(check_name(static_cast<Check>(k))),
                                 c.checked ? "true" : "false", quote(c.checked ? hls::severity_name(c.sev) : "not checked"),
                                 quote(c.declared), quote(c.measured), quote(c.note));
            }
            o += std::format("}},\n     \"worst\": {},\n     \"reads\": [", quote(hls::severity_name(row->worst)));
            for (std::size_t k = 0; k < rr->reads.size(); ++k) {
                const SegmentSeen& s = rr->reads[k];
                const auto si = static_cast<std::size_t>(s.read.index);
                const SegmentRef* g = si < p.segments.size() ? &p.segments[si] : nullptr;
                o += std::format("{}\n       {{\"index\": {}, \"number\": {}, \"start\": {}, \"media_time\": {}, \"duration\": {}, "
                                 "\"bytes\": {}, \"earliest_presentation_time\": {}, \"edit_list\": {}",
                                 k ? "," : "", s.read.index, g && g->number >= 0 ? std::to_string(g->number) : "null",
                                 num(g ? g->start : kNaN), num(g ? static_cast<double>(g->time) / static_cast<double>(p.timescale) : kNaN),
                                 num(g ? g->seconds : kNaN), s.read.bytes, num(s.start), num(s.edit));
                if (!s.read.error.empty()) o += std::format(", \"error\": {}", quote(s.read.error));
                o += std::format(", \"sap_type\": {}", sap_type(s.read));
                if (s.read.video) {
                    const hls::VideoSeen& v = *s.read.video;
                    o += std::format(", \"first_picture\": {}, \"frames\": {}, \"keyframes\": {}, \"leading\": {}, \"rasl\": {}, "
                                     "\"media_duration\": {}, \"frame_rate\": {},\n        \"video\": {{\"codec\": {}, "
                                     "\"codec_string\": {}, \"sample_entry\": {}, \"width\": {}, \"height\": {}, \"sar\": {}, "
                                     "\"transfer\": {}, \"bit_depth\": {}, \"dolby_vision\": {}}}",
                                     quote(hls::first_picture_name(s.read.first)), s.read.frames, s.read.keyframes,
                                     s.read.leading, s.read.rasl, num(s.read.media_duration),
                                     s.read.frame_rate > 0.0 ? number(s.read.frame_rate) : "null", quote(v.codec),
                                     quote(v.codec_string), v.sample_entry.empty() ? "null" : quote(v.sample_entry), v.width,
                                     v.height, v.sar_num > 0 ? quote(std::format("{}:{}", v.sar_num, v.sar_den)) : "null",
                                     v.transfer, v.bit_depth,
                                     v.dv_profile >= 0 ? quote(std::format("{}.{} compatibility {}", v.dv_profile, v.dv_level,
                                                                           v.dv_compat))
                                                       : "null");
                }
                if (s.read.audio)
                    o += std::format(", \"audio_duration\": {},\n        \"audio\": {{\"codec\": {}, \"codec_string\": {}, "
                                     "\"channels\": {}, \"sample_rate\": {}}}", num(s.read.audio_duration),
                                     quote(s.read.audio->codec), quote(s.read.audio->codec_string), s.read.audio->channels,
                                     s.read.audio->sample_rate);
                o += "}";
            }
            o += rr->reads.empty() ? "]}" : "\n     ]}";
        } else {
            o += "     \"measured\": null}";
        }
    }
    o += "\n  ],\n";

    o += "  \"findings\": [";
    for (std::size_t i = 0; i < r.findings.size(); ++i) {
        const Finding& f = r.findings[i];
        std::string ids;
        for (const int k : f.renditions)
            ids += (ids.empty() ? "" : ", ") + quote(k >= 0 && static_cast<std::size_t>(k) < m.reps.size()
                                                         ? m.reps[static_cast<std::size_t>(k)].id : std::string("?"));
        o += std::format("{}\n    {{\"severity\": {}, \"title\": {}, \"detail\": {}, \"representations\": [{}]}}", i ? "," : "",
                         quote(hls::severity_name(f.sev)), quote(f.title), quote(f.detail), ids);
    }
    o += "\n  ]\n}\n";
    return o;
}

} // namespace dash
