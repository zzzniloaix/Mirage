#include "PipelineState.h"

#include <format>

const char* to_string(DemuxState s)
{
    switch (s) {
        case DemuxState::Starting:     return "starting";
        case DemuxState::Reading:      return "reading";
        case DemuxState::PushVideo:    return "push-video";
        case DemuxState::PushAudio:    return "push-audio";
        case DemuxState::PushSubtitle: return "push-subtitle";
        case DemuxState::Retrying:     return "retrying";
        case DemuxState::AtEnd:        return "at-end";
        case DemuxState::Stopped:      return "stopped";
    }
    return "?";
}

const char* to_string(DecodeState s)
{
    switch (s) {
        case DecodeState::Starting:   return "starting";
        case DecodeState::WaitPacket: return "wait-packet";
        case DecodeState::Decoding:   return "decoding";
        case DecodeState::WaitOutput: return "wait-output";
        case DecodeState::Stopped:    return "stopped";
    }
    return "?";
}

std::string describe_pipeline(const PipelineSnapshot& s)
{
    std::string out = std::format(
        "t={:.1f}s paused={} scrubbing={} focused={} | demux={} pkts v={} a={} s={} ended={}{} | "
        "videoq={}/{} audioq={}/{} subq={} frameq={}/{} | video={} in={} out={} hw={}",
        s.since_start_s, s.paused ? 1 : 0, s.scrubbing ? 1 : 0, s.focused ? 1 : 0,
        to_string(s.demux), s.video_pkts, s.audio_pkts, s.sub_pkts, s.demux_ended ? 1 : 0,
        s.demux_error.empty() ? std::string() : std::format(" error=\"{}\"", s.demux_error),
        s.videoq, s.videoq_cap, s.audioq, s.audioq_cap, s.subq, s.frameq, s.frameq_cap,
        to_string(s.video), s.video_in, s.video_out, s.hw ? 1 : 0);
    if (s.has_audio)
        out += std::format(" | audio={} in={} device={} callbacks={} buffered={:.2f}s clock={:.3f}",
                           to_string(s.audio), s.audio_in, s.audio_device ? 1 : 0, s.audio_callbacks,
                           s.audio_buffered_s, s.audio_clock);
    else
        out += " | audio=none";
    return out;
}

// Why the demuxer delivers nothing more, as a clause ("" when it is just reading).
static std::string demux_blocker(const PipelineSnapshot& s)
{
    switch (s.demux) {
        case DemuxState::PushAudio:
            return std::format("the demuxer is blocked on a full audio queue ({}/{}) — {}",
                               s.audioq, s.audioq_cap,
                               s.audio == DecodeState::WaitOutput
                                   ? std::format("the audio ring is full and the device is not draining it "
                                                 "(callbacks: {})", s.audio_callbacks)
                                   : std::string("the audio decoder is not keeping up"));
        case DemuxState::PushSubtitle:
            return "the demuxer is blocked on a full subtitle queue";
        case DemuxState::PushVideo:
            return std::format("the demuxer is blocked on a full video queue ({}/{})", s.videoq, s.videoq_cap);
        case DemuxState::Retrying:
            return "network reads are failing (the demuxer is retrying)";
        case DemuxState::AtEnd:
            return "the demuxer reached the end of the media";
        case DemuxState::Stopped:
            return s.demux_error.empty() ? std::string("the demuxer stopped")
                                         : "the demuxer stopped: " + s.demux_error;
        case DemuxState::Starting:
            return "the demux thread has not started";
        case DemuxState::Reading:
            break;
    }
    return {};
}

std::string diagnose_no_frame(const PipelineSnapshot& s)
{
    std::string why;
    bool said_paused = false;
    if (s.frameq > 0) {
        // Decoded frames are ready: only the render loop can be holding them.
        why = std::format("{} decoded frame(s) wait in the frame queue but none was presented", s.frameq);
        if (s.paused)         { why += " — playback is paused (Space / play button / menu)"; said_paused = true; }
        else if (s.scrubbing) why += " — a seek-bar drag is in progress";
        else                  why += " — the render loop is not taking them";
    } else if (s.video_out > 0) {
        why = std::format("the decoder produced {} frame(s) but none is queued "
                          "(dropped by a seek / track switch, or before a switch's take-over point)", s.video_out);
    } else if (s.video_in > 0) {
        if (s.video == DecodeState::Decoding) {
            why = std::format("the video decoder is in the codec with {} packet(s) in and no frame out{}",
                              s.video_in, s.hw ? " (VideoToolbox)" : "");
        } else {
            const std::string b = demux_blocker(s);
            why = std::format("the video decoder has {} packet(s) and no frame yet; it waits for more input{}",
                              s.video_in, b.empty() ? std::string(" (the demuxer is reading — slow input?)")
                                                    : ": " + b);
        }
    } else if (s.video_pkts > 0) {
        why = s.video == DecodeState::Starting
            ? std::string("the video decode thread has not started")
            : std::format("{} video packet(s) are queued but the decode thread has not taken one ({})",
                          s.videoq, to_string(s.video));
    } else {
        const std::string b = demux_blocker(s);
        why = b.empty() ? std::string("no video packet read yet — waiting on the input (network / disk)")
                        : "no video packet yet: " + b;
    }
    if (s.paused && !said_paused) why += "; playback is paused";
    if (s.scrubbing && s.frameq == 0) why += "; a seek-bar drag is in progress";
    if (s.has_audio && s.audio_device && s.audio_callbacks == 0)
        why += "; the audio device has not called back yet";
    return why;
}
