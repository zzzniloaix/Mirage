#!/bin/bash
# Builds seek_probe.cpp against Homebrew's ffmpeg-full, the FFmpeg the player
# links (<repo>/CMakePresets.json sets PKG_CONFIG_PATH to it), and runs it on
# ./media. Everything it prints goes to stdout; the Homebrew prefix and this
# directory are written as <homebrew> and <here> so that no local path is kept.
# The generated media (./media) are deleted afterwards: seek_probe makes them
# again.
set -euo pipefail
cd "$(dirname "$0")"
here=$PWD
brew_prefix=$(brew --prefix)
export PKG_CONFIG_PATH="$(brew --prefix ffmpeg-full)/lib/pkgconfig"
redact() { sed -e "s|$brew_prefix|<homebrew>|g" -e "s|$here|<here>|g"; }
{
  echo "+ pkg-config --modversion libavformat libavcodec libavutil"
  pkg-config --modversion libavformat libavcodec libavutil
  echo "+ clang++ --version"
  clang++ --version | head -1
  echo "+ clang++ -std=c++20 -O1 -Wall -Wextra seek_probe.cpp \$(pkg-config --cflags --libs libavformat libavcodec libavutil) -o seek_probe"
  clang++ -std=c++20 -O1 -Wall -Wextra seek_probe.cpp $(pkg-config --cflags --libs libavformat libavcodec libavutil) -o seek_probe
  rm -rf media
  echo "+ ./seek_probe media"
  ./seek_probe media
  echo "+ exit status $?"
  rm -rf media seek_probe
} 2>&1 | redact
