// demuxers: whether the linked libavformat has the demuxers the timeline's
// inputs need, by av_find_input_format(), and its run-time version.
//
//   demuxers

extern "C" {
#include <libavformat/avformat.h>
}

#include <cstdio>
#include <initializer_list>

int main()
{
    const unsigned v = avformat_version();
    std::printf("libavformat %u.%u.%u\n", AV_VERSION_MAJOR(v), AV_VERSION_MINOR(v), AV_VERSION_MICRO(v));
    for (const char* name : { "hls", "dash", "mpegts", "mov", "webvtt" })
        std::printf("%-7s %s\n", name, av_find_input_format(name) ? "present" : "absent");
    return 0;
}
