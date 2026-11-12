#!/bin/sh
# Builds the prototype without optimization (-O0), as the macos-debug preset's
# Debug build type compiles mirage_tests without optimization, to time the
# generator there. Without -g, so that no .dSYM with local paths is written.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
export PKG_CONFIG_PATH="$(brew --prefix)/opt/ffmpeg-full/lib/pkgconfig"
flags=$(pkg-config --cflags --libs libavformat libavcodec libavutil libswscale)
set -x
clang++ -std=c++20 -O0 -Wall -Wextra -o "$here/testgen-O0" "$here/src/testgen.cpp" $flags
