#!/bin/bash
# collect_facts.sh <repo> <baseline-binary> <out-dir>
# Writes environment.txt (toolchain / dependency versions, machine) and
# code_facts.txt (where the VMAF code is, at the commit measured) — the raw
# outputs every number in docs/VMAF_GAP_REPORT.md comes from.
repo=$1; bin=$2; out=$3
run() { echo "\$ $*"; "$@" 2>&1; echo; }
{
  run date -u +%Y-%m-%dT%H:%M:%SZ
  run git -C "$repo" rev-parse HEAD
  run git -C "$repo" log -1 --format='%H %cd %s' --date=iso
  run sw_vers
  run uname -a
  run sysctl -n machdep.cpu.brand_string hw.physicalcpu hw.logicalcpu hw.memsize
  run clang --version
  run cmake --version
  run ninja --version
  run brew list --versions ffmpeg ffmpeg-full libvmaf glfw pkg-config python@3.13 python@3.14
  echo "\$ PKG_CONFIG_PATH=/opt/homebrew/opt/ffmpeg-full/lib/pkgconfig pkg-config --modversion libavformat libavcodec libavutil libswscale libswresample libavfilter libvmaf glfw3"
  PKG_CONFIG_PATH=/opt/homebrew/opt/ffmpeg-full/lib/pkgconfig pkg-config --modversion libavformat libavcodec libavutil libswscale libswresample libavfilter libvmaf glfw3 2>&1; echo
  run /opt/homebrew/opt/ffmpeg-full/bin/ffmpeg -hide_banner -version
  run /opt/homebrew/opt/ffmpeg-full/bin/ffmpeg -hide_banner -buildconf
  run /opt/homebrew/opt/ffmpeg/bin/ffmpeg -hide_banner -version
  run /opt/homebrew/opt/ffmpeg/bin/ffmpeg -hide_banner -buildconf
  run /opt/homebrew/opt/libvmaf/bin/vmaf --version
  run grep -E 'VMAF_API_VERSION_(MAJOR|MINOR|PATCH)' /opt/homebrew/opt/libvmaf/include/libvmaf/version.h
  run ls /opt/homebrew/opt/libvmaf/share/libvmaf/model
  run otool -L "$bin"
  echo '$ brew info --json=v2 ffmpeg ffmpeg-full libvmaf glfw  (name, formula version now, license)'
  brew info --json=v2 ffmpeg ffmpeg-full libvmaf glfw 2>/dev/null | python3 -I -c 'import json,sys; [print(f["name"], f["versions"]["stable"], f.get("license")) for f in json.load(sys.stdin)["formulae"]]'
  echo
  tmp=$(mktemp -d)
  clang "$(dirname "$0")/tools/vmaf_version.c" -o "$tmp/vmaf_version" $(pkg-config --cflags --libs libvmaf) && run "$tmp/vmaf_version"
  rm -rf "$tmp"
  # The GL loader (glad) is generated at build time with Python + jinja2.
  # (ElementTree imports pyexpat only when it parses: test pyexpat itself.)
  run /opt/homebrew/bin/python3.14 -c 'import sys; print(sys.version); import pyexpat; print("pyexpat", pyexpat.EXPAT_VERSION)'
  run /opt/homebrew/bin/python3.13 -c 'import sys; print(sys.version); import pyexpat; print("pyexpat", pyexpat.EXPAT_VERSION)'
  run otool -L /opt/homebrew/opt/python@3.14/Frameworks/Python.framework/Versions/3.14/lib/python3.14/lib-dynload/pyexpat.cpython-314-darwin.so
  for d in miniaudio stb glad imgui pugixml nfd catch2; do
    src="$(dirname "$bin")/_deps/$d-src"
    [ -d "$src" ] && run git -C "$src" log -1 --format="$d %H %cs"
  done
} > "$out/environment.txt"
{
  run git -C "$repo" rev-parse HEAD
  run git -C "$repo" ls-files 'LICENSE*' 'COPYING*' 'license*'
  run wc -l "$repo/src/VMAFAnalyzer.cpp" "$repo/src/VMAFAnalyzer.h"
  run grep -n -E 'n_threads|vmaf_model_load|SWS_BILINEAR|VMAF_POOL_METHOD|sorted\.size\(\) \* 5|next_video_frame\(\);|frame_idx\+\+|result\.width  = ref_w|std::ofstream f\(path\)' "$repo/src/VMAFAnalyzer.cpp"
  run grep -n -E 'vmaf\.json|write_json|start_manifest|auto_vmaf' "$repo/src/main.cpp"
  run grep -n -E 'draw_vmaf_results|pos_frac \* N|Export JSON' "$repo/src/PlayerUI.cpp"
  run grep -c -i 'vmaf' "$repo/src/main.cpp"
  run grep -n -E 'vmaf_analyzer\.results\(\)|VMAF report written|cli.auto_vmaf\)|vmaf_pos_frac =' "$repo/src/main.cpp"
  run grep -n 'vmaf_analyzer' "$repo/src/MediaSession.h"
  run grep -n -E 'cancel\(\);|void VMAFAnalyzer::cancel' "$repo/src/VMAFAnalyzer.cpp" "$repo/src/VMAFAnalyzer.h"
  echo '$ (line span of a function: from its signature to the brace that closes it)'
  python3 -I - "$repo" <<'PY'
import sys
def span(path, sig):
    lines = open(path).read().splitlines()
    for i, l in enumerate(lines):
        if sig in l:
            depth, started = 0, False
            for j in range(i, len(lines)):
                depth += lines[j].count('{') - lines[j].count('}')
                started = started or '{' in lines[j]
                if started and depth == 0:
                    return i + 1, j + 1
for path, sig in (('src/PlayerUI.cpp', 'bool PlayerUI::draw_vmaf_results'),
                  ('src/DebugHUD.cpp', 'void DebugHUD::draw_vmaf_panel'),
                  ('src/VMAFAnalyzer.cpp', 'void VMAFAnalyzer::analyze_pair'),
                  ('src/VMAFAnalyzer.cpp', 'void VMAFAnalyzer::start_manifest'),
                  ('src/VMAFAnalyzer.cpp', 'bool VMAFAnalyzer::write_json')):
    a, b = span(sys.argv[1] + '/' + path, sig)
    print(f'{path} {sig}: L{a}-L{b} ({b - a + 1} lines)')
PY
  echo
  run grep -rn -i -l 'vmaf' "$repo/tests"
  run grep -n -E 'FetchContent_Declare|GIT_TAG|pkg_check_modules' "$repo/CMakeLists.txt"
  run grep -n 'PKG_CONFIG_PATH' "$repo/CMakePresets.json"
  run grep -n -E 'brew install' "$repo/README.md" "$repo/CLAUDE.md"
} > "$out/code_facts.txt"
