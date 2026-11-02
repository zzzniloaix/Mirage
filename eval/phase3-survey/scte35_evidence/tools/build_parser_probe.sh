#!/bin/sh
# Builds build/parser_probe from tools/parser_probe.cpp and the debug build's
# objects of the repository (read only). usage: sh tools/build_parser_probe.sh <repo> <ffmpeg-prefix>
set -eu
R=$1; F=$2
O=$R/build/macos-debug/CMakeFiles/mirage_tests.dir/src
mkdir -p build
c++ -g -std=gnu++20 -arch arm64 -I"$R/src" -I"$R/build/macos-debug/_deps/pugixml-src/src" -isystem "$F/include" \
    tools/parser_probe.cpp "$O/ManifestParser.cpp.o" "$O/HlsMaster.cpp.o" "$O/HlsAlign.cpp.o" \
    "$R/build/macos-debug/_deps/pugixml-build/libpugixml.a" -L"$F/lib" -lavformat -lavutil -o build/parser_probe
echo "built build/parser_probe"
