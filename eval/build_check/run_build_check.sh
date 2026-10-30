#!/bin/bash
# Build check behind docs/BUILD.md: builds the committed HEAD of <repo> in a
# clean worktree the way BUILD.md describes, and prints what happened. Local
# paths in the output are replaced by placeholders; the full logs stay in
# <scratch>/logs. It also records what the build's dependencies were pinned
# to and what was fetched, that neither the configure nor the build looks
# for Python, glad's committed loader regenerated and compared, the sources
# Homebrew built the installed FFmpeg, libvmaf and x264 from (the pins of
# docker/Dockerfile), and where LICENSE's text stands among the GPLv3 texts
# Homebrew's packages ship.
#
# usage: eval/build_check/run_build_check.sh <repo> <scratch> <python3.13> > <record.txt>
#   <repo>        the repository; its working tree must be clean
#   <scratch>     an empty directory for the worktree, glad's regeneration and the logs
#   <python3.13>  the Python 3.13 interpreter third_party/glad/regenerate.sh
#                 creates its venv from; the build itself runs no Python
#
# Placeholders: <worktree> <scratch> <repo> <home> <tmp> <hostname>

set -u

if [ $# -ne 3 ]; then
    echo "usage: $0 <repo> <scratch> <python3.13>" >&2
    exit 2
fi
repo=$(cd "$1" && pwd -P) || exit 2
scratch=$(cd "$2" && pwd -P) || exit 2
py=$3
wt=$scratch/worktree
logs=$scratch/logs
mkdir -p "$logs"

tmp=${TMPDIR:-/tmp}
tmp=${tmp%/}
host=$(hostname -s)

# Longest paths first: the worktree lives in <scratch>.
subst=(-e "s|$wt|<worktree>|g" -e "s|$scratch|<scratch>|g"
       -e "s|$repo|<repo>|g" -e "s|/private$tmp|<tmp>|g" -e "s|$tmp|<tmp>|g")
[ -n "${HOME:-}" ] && subst+=(-e "s|$HOME|<home>|g")
[ -n "$host" ] && subst+=(-e "s|$host|<hostname>|g")
redact() { sed "${subst[@]}"; }

section() { printf '\n## %s\n\n' "$*"; }

# The command line, with quotes where the shell would need them.
show() {
    local out='$' a
    for a in "$@"; do
        case $a in
            *\'*) out="$out $(printf '%q' "$a")" ;;
            '' | *[!A-Za-z0-9_./:=@%+,-]*) out="$out '$a'" ;;
            *) out="$out $a" ;;
        esac
    done
    echo "$out" | redact
}

# A command and its output.
fact() {
    show "$@"
    "$@" 2>&1 | redact
    echo
}

# A command whose output goes to <scratch>/logs/<log>; prints its exit code.
run() {
    local log=$1
    shift
    show "$@"
    "$@" > "$logs/$log" 2>&1
    local rc=$?
    echo "exit code: $rc (log: <scratch>/logs/$log)"
    return $rc
}

# The lines of a file that name Python, and how many there are.
python_lines() {
    local n
    n=$(grep -c -i python "$1")
    echo "$1: $n line(s) naming Python" | redact
    grep -i python "$1" | cut -c 1-160 | redact
}

echo "# Build check for docs/BUILD.md"

section "Machine and tools"
fact date -u +%Y-%m-%dT%H:%M:%SZ
fact sw_vers
fact uname -m
fact sysctl -n machdep.cpu.brand_string
fact clang --version
fact cmake --version
fact ninja --version
fact env HOMEBREW_NO_AUTO_UPDATE=1 brew list --versions cmake ninja pkgconf ffmpeg ffmpeg-full libvmaf glfw x264 python@3.13 python@3.14
echo "\$ brew info --json=v2 ffmpeg-full libvmaf glfw x264  (installed vs current stable)"
HOMEBREW_NO_AUTO_UPDATE=1 brew info --json=v2 ffmpeg-full libvmaf glfw x264 2>&1 |
    python3 -I -c 'import json, sys
for f in json.load(sys.stdin)["formulae"]:
    print(f["name"], "installed", " ".join(i["version"] for i in f.get("installed", [])),
          "| current stable", f["versions"]["stable"])' 2>&1 | redact
echo
fact env PKG_CONFIG_PATH=/opt/homebrew/opt/ffmpeg-full/lib/pkgconfig pkg-config --modversion \
    libavformat libavcodec libavutil libswscale libswresample libavfilter libvmaf glfw3
fact "$py" --version
fact env HOMEBREW_NO_AUTO_UPDATE=1 brew --version
echo "What Homebrew offers for versions:"
for c in pin extract; do
    echo "\$ brew help $c"
    HOMEBREW_NO_AUTO_UPDATE=1 brew help "$c" 2>&1 | redact
    echo
done

section "The sources of the installed ffmpeg-full, libvmaf and x264"
echo "Each formula as it was installed (the copy Homebrew keeps in the keg): the"
echo "source it names and its checksum or commit, which docker/Dockerfile pins; and"
echo "which x264 and libvmaf ffmpeg-full's libraries link."
echo
for f in ffmpeg-full libvmaf x264; do
    rb=/opt/homebrew/opt/$f/.brew/$f.rb
    echo "\$ grep -n -E '^  (url|sha256|version) |revision:' $rb"
    grep -n -E '^  (url|sha256|version) |revision:' "$rb" 2>&1 | redact
    echo
done
for lib in libavcodec libavfilter; do
    echo "\$ otool -L /opt/homebrew/opt/ffmpeg-full/lib/$lib.dylib | grep -E 'x264|vmaf'"
    otool -L "/opt/homebrew/opt/ffmpeg-full/lib/$lib.dylib" 2>&1 | grep -E 'x264|vmaf' | redact
    echo
done

section "Commit"
fact git -C "$repo" rev-parse HEAD
echo "\$ git -C <repo> status --porcelain"
dirty=$(git -C "$repo" status --porcelain)
if [ -n "$dirty" ]; then
    echo "$dirty" | redact
    echo "the working tree is not clean: stopping"
    exit 1
fi
echo "(no output: the working tree is clean)"
echo
fact git -C "$repo" worktree add --detach "$wt" HEAD
fact git -C "$wt" rev-parse HEAD
cd "$wt" || exit 1
echo "The presets CMake offers on this machine:"
fact cmake --list-presets

section "Release preset"
run configure_release.log cmake --preset macos-release
grep -E 'libvmaf package version' "$logs/configure_release.log" | redact
run build_release.log cmake --build --preset macos-release
grep -E -m 1 '^FAILED:' "$logs/build_release.log" | cut -c 1-160 | redact

section "Debug preset"
run configure_debug.log cmake --preset macos-debug
run build_debug.log cmake --build --preset macos-debug
grep -E -m 1 '^FAILED:' "$logs/build_debug.log" | cut -c 1-160 | redact

section "Headless preset (MIRAGE_HEADLESS)"
run configure_headless.log cmake --preset macos-headless
fact grep -E '^MIRAGE_HEADLESS' build/macos-headless/CMakeCache.txt
run build_headless.log cmake --build --preset macos-headless
grep -E -m 1 '^FAILED:' "$logs/build_headless.log" | cut -c 1-160 | redact
echo "The dependencies each configuration fetched (FetchContent's _deps directories):"
fact ls build/macos-release/_deps build/macos-headless/_deps

section "Python in the configure and the build"
echo "Every line that names Python in each configuration's configure log, CMake cache"
echo "and Ninja build file."
echo
for c in release debug headless; do
    python_lines "$logs/configure_$c.log"
    python_lines "build/macos-$c/CMakeCache.txt"
    python_lines "build/macos-$c/build.ninja"
done

section "Unit tests (debug build)"
run tests_debug.log perl -e 'alarm 900; exec @ARGV' build/macos-debug/mirage_tests '~[audio]'
grep -E -B 6 -A 3 'FAILED:' "$logs/tests_debug.log" | redact
tail -n 2 "$logs/tests_debug.log" | redact

section "Unit tests (headless build)"
run tests_headless.log perl -e 'alarm 900; exec @ARGV' build/macos-headless/mirage_tests '~[audio]'
grep -E -B 6 -A 3 'FAILED:' "$logs/tests_headless.log" | redact
tail -n 2 "$logs/tests_headless.log" | redact

section "Built programs"
fact ls build/macos-release/mirage build/macos-release/mirage_vmaf build/macos-release/mirage_tests \
    build/macos-release/snapshot_diff
echo "\$ ls build/macos-headless | grep -x -E 'mirage|mirage_vmaf|mirage_tests|snapshot_diff'"
ls build/macos-headless | grep -x -E 'mirage|mirage_vmaf|mirage_tests|snapshot_diff'
echo
fact otool -L build/macos-release/mirage_vmaf
run vmaf_help.log build/macos-release/mirage_vmaf --help
head -n 1 "$logs/vmaf_help.log"
fact shasum -a 256 build/macos-release/mirage_vmaf build/macos-headless/mirage_vmaf \
    build/macos-debug/mirage_vmaf
if cmp -s build/macos-release/mirage_vmaf build/macos-headless/mirage_vmaf; then
    echo "The release and headless builds' mirage_vmaf are identical (cmp)."
else
    echo "The release and headless builds' mirage_vmaf differ (cmp)."
fi

section "Dependencies fetched at configure time (release build)"
echo "For each FetchContent_Declare in CMakeLists.txt: the GIT_TAG it asks for, the commit"
echo "the release build fetched and its commit date, the release tag at that commit, and"
echo "origin/HEAD of the fetched clone: the upstream default branch when this check fetched"
echo "it. For a dependency pinned by commit, the upstream commits after the pinned one"
echo "follow, each with the files it changes."
echo
awk '
/FetchContent_Declare\(/ { s = $0; sub(/.*FetchContent_Declare\(/, "", s)
                           split(s, a, /[ \t)]+/); name = a[1]; repo = ""; tag = "" }
name != "" && $1 == "GIT_REPOSITORY" { repo = $2 }
name != "" && $1 == "GIT_TAG" { tag = $2 }
name != "" && /^[ \t]*\)/ { print name, repo, tag; name = "" }
' CMakeLists.txt | while read -r name url tag; do
    d=build/macos-release/_deps/$(echo "$name" | tr '[:upper:]' '[:lower:]')-src
    echo "$name ($url)"
    echo "  GIT_TAG:            $tag"
    echo "  fetched:            $(git -C "$d" rev-parse HEAD) ($(git -C "$d" log -1 --format=%cs HEAD))"
    echo "  tag at that commit: $(git -C "$d" describe --tags --exact-match HEAD 2>/dev/null || echo none)"
    echo "  origin/HEAD:        $(git -C "$d" rev-parse origin/HEAD 2>/dev/null || echo unknown)," \
         "$(git -C "$d" rev-list --count HEAD..origin/HEAD 2>/dev/null || echo '?') commits after the fetched one"
    case $tag in
        [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]*)
            git -C "$d" log --format='    %h %cs %s' --name-only HEAD..origin/HEAD |
                grep -v '^$' | sed -E 's/^([^ ])/      \1/' | redact ;;
    esac
done

section "glad's committed loader"
for f in include/KHR/khrplatform.h include/glad/gl.h src/gl.c; do
    p=third_party/glad/$f
    echo "$f: $(wc -c < "$p" | tr -d ' ') bytes, sha256 $(shasum -a 256 "$p" | cut -d ' ' -f 1)"
done
echo
echo "\$ grep -m 2 -E 'Loader generated by|SPDX-License-Identifier' third_party/glad/include/glad/gl.h"
grep -m 2 -E 'Loader generated by|SPDX-License-Identifier' third_party/glad/include/glad/gl.h
echo
mkdir -p "$scratch/glad"
show third_party/glad/regenerate.sh "$py" "$scratch/glad"
third_party/glad/regenerate.sh "$py" "$scratch/glad" 2>&1 | redact
echo "exit code: ${PIPESTATUS[0]}"

section "LICENSE"
fact shasum -a 256 LICENSE
echo "GPLv3 texts in Homebrew's packages: files named COPYING* or LICENSE* up to three"
echo "levels below /opt/homebrew/Cellar whose first five lines name the GNU General Public"
echo "License, Version 3, 29 June 2007, by SHA-256:"
find /opt/homebrew/Cellar -maxdepth 3 -type f \( -iname 'COPYING*' -o -iname 'LICENSE*' \) \
        2>/dev/null | sort | while read -r f; do
    top=$(head -n 5 "$f")
    case $top in
        *'GNU GENERAL PUBLIC LICENSE'*'Version 3, 29 June 2007'*)
            echo "$(shasum -a 256 "$f" | cut -d ' ' -f 1) $f" ;;
    esac
done > "$logs/gpl3_texts.txt"
license_sha=$(shasum -a 256 LICENSE | cut -d ' ' -f 1)
sort -k 1,1 -s "$logs/gpl3_texts.txt" | awk -v l="$license_sha" '
    { tag = ($1 == l) ? "  identical to LICENSE" : "  other text"; print tag ": " $1 " " $2 }' | redact
cut -d ' ' -f 1 "$logs/gpl3_texts.txt" | sort -u | while read -r sha; do
    [ "$sha" = "$license_sha" ] && continue
    other=$(awk -v s="$sha" '$1 == s { print $2; exit }' "$logs/gpl3_texts.txt")
    echo
    echo "\$ diff LICENSE $other"
    diff LICENSE "$other" | redact
done

cd "$repo" || exit 1
git worktree remove --force "$wt"
