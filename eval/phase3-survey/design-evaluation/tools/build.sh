#!/bin/sh
# Builds the design's two probes into <out>/design-evaluation/bin against Homebrew's ffmpeg-full,
# with clang++ -std=c++20, as the repository's presets use it (docs/BUILD.md). The binaries are
# deleted after the runs: their debug information would name local paths.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
bin="$(dirname "$here")/bin"
mkdir -p "$bin"
export PKG_CONFIG_PATH="$(brew --prefix)/opt/ffmpeg-full/lib/pkgconfig"
ff=$(pkg-config --cflags --libs libavformat libavcodec libavutil)
pkg-config --modversion libavformat libavcodec libavutil
clang++ --version | head -1
set -x
clang++ -std=c++20 -O1 -Wall -Wextra -o "$bin/segmap" "$here/segmap.cpp" $ff
clang++ -std=c++20 -O1 -Wall -Wextra -o "$bin/fetch" "$here/fetch.cpp" $ff
