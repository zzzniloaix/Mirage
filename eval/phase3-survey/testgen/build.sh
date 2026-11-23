#!/bin/sh
# Builds the prototype: testgen next to this script, from src/testgen.cpp,
# against Homebrew's ffmpeg-full (docs/BUILD.md), with clang++ -std=c++20.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
export PKG_CONFIG_PATH="$(brew --prefix)/opt/ffmpeg-full/lib/pkgconfig"
flags=$(pkg-config --cflags --libs libavformat libavcodec libavutil libswscale)
echo "pkg-config --cflags --libs libavformat libavcodec libavutil libswscale: $flags"
clang++ --version | head -1
set -x
clang++ -std=c++20 -O2 -Wall -Wextra -o "$here/testgen" "$here/src/testgen.cpp" $flags
