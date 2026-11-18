#!/bin/bash
# Builds the survey's probes into $OUT/probe/bin, against Homebrew's
# ffmpeg-full (the libraries the repository's presets use, CMakePresets.json)
# and, for manifest_probe, the repository's own sources at $REPO (read, not
# changed) and the pugixml its debug build fetched. Needs REPO, OUT, HOMEBREW.
set -eu
src="$OUT/probe"
bin="$OUT/probe/bin"
mkdir -p "$bin"
export PKG_CONFIG_PATH="$HOMEBREW/opt/ffmpeg-full/lib/pkgconfig"
ff="$(pkg-config --cflags --libs libavformat libavcodec libavutil libswscale libswresample)"
cxx="clang++ -std=c++20 -O1 -g -Wall -Wextra"
set -x
$cxx -o "$bin/make_fixture" "$src/make_fixture.cpp" $ff
$cxx -I"$REPO/src" -I"$REPO/tests" -o "$bin/logprobe" "$src/logprobe.cpp" $ff
$cxx -I"$REPO/src" -I"$REPO/build/macos-debug/_deps/pugixml-src/src" -o "$bin/manifest_probe" \
    "$src/manifest_probe.cpp" "$REPO/src/ManifestParser.cpp" "$REPO/src/HlsAlign.cpp" \
    "$REPO/src/HlsVariants.cpp" "$REPO/src/HlsMaster.cpp" "$REPO/src/NetworkLogger.cpp" \
    "$REPO/build/macos-debug/_deps/pugixml-src/src/pugixml.cpp" $ff
$cxx -I"$REPO/src" -I"$REPO/tests" -o "$bin/netlog_probe" "$src/netlog_probe.cpp" "$REPO/src/NetworkLogger.cpp" $ff
$cxx -o "$bin/demuxers" "$src/demuxers.cpp" $ff
