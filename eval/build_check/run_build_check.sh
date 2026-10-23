#!/bin/bash
# Build check behind docs/BUILD.md: builds the committed HEAD of <repo> in a
# clean worktree the way BUILD.md describes, and prints what happened. Local
# paths in the output are replaced by placeholders; the full logs stay in
# <scratch>/logs.
#
# usage: eval/build_check/run_build_check.sh <repo> <scratch> <python3.13> > <record.txt>
#   <repo>        the repository; its working tree must be clean
#   <scratch>     an empty directory for the worktree, the venv and the logs
#   <python3.13>  the Python 3.13 interpreter the glad venv is created from
#
# Placeholders: <worktree> <venv> <scratch> <repo> <home> <tmp> <hostname>

set -u

if [ $# -ne 3 ]; then
    echo "usage: $0 <repo> <scratch> <python3.13>" >&2
    exit 2
fi
repo=$(cd "$1" && pwd -P) || exit 2
scratch=$(cd "$2" && pwd -P) || exit 2
py=$3
wt=$scratch/worktree
venv=$scratch/venv
logs=$scratch/logs
mkdir -p "$logs"

tmp=${TMPDIR:-/tmp}
tmp=${tmp%/}
host=$(hostname -s)

# Longest paths first: the worktree and the venv live in <scratch>.
subst=(-e "s|$wt|<worktree>|g" -e "s|$venv|<venv>|g" -e "s|$scratch|<scratch>|g"
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

echo "# Build check for docs/BUILD.md"

section "Machine and tools"
fact date -u +%Y-%m-%dT%H:%M:%SZ
fact sw_vers
fact uname -m
fact sysctl -n machdep.cpu.brand_string
fact clang --version
fact cmake --version
fact ninja --version
fact env HOMEBREW_NO_AUTO_UPDATE=1 brew list --versions cmake ninja pkgconf ffmpeg ffmpeg-full libvmaf glfw python@3.13 python@3.14
echo "\$ brew info --json=v2 ffmpeg-full libvmaf glfw  (installed vs current stable)"
HOMEBREW_NO_AUTO_UPDATE=1 brew info --json=v2 ffmpeg-full libvmaf glfw 2>&1 |
    python3 -I -c 'import json, sys
for f in json.load(sys.stdin)["formulae"]:
    print(f["name"], "installed", " ".join(i["version"] for i in f.get("installed", [])),
          "| current stable", f["versions"]["stable"])' 2>&1 | redact
echo
fact env PKG_CONFIG_PATH=/opt/homebrew/opt/ffmpeg-full/lib/pkgconfig pkg-config --modversion \
    libavformat libavcodec libavutil libswscale libswresample libavfilter libvmaf glfw3
fact "$py" --version

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

section "Release preset, default Python"
run configure_release_default.log cmake --preset macos-release
grep -E 'libvmaf package version|Found Python' "$logs/configure_release_default.log" | redact
run build_release_default.log cmake --build --preset macos-release
grep -E -m 2 'Symbol not found|ImportError: No module named expat' "$logs/build_release_default.log" | redact
grep -E -m 1 '^FAILED:' "$logs/build_release_default.log" | cut -c 1-160 | redact

section "mirage_vmaf and mirage_tests alone, default Python"
run build_release_vmaf_default.log cmake --build --preset macos-release --target mirage_vmaf
run build_release_tests_default.log cmake --build --preset macos-release --target mirage_tests

section "glad venv"
run venv.log "$py" -m venv "$venv"
run pip.log "$venv/bin/pip" install jinja2==3.1.6
fact "$venv/bin/python" --version
fact "$venv/bin/pip" list --format=freeze

section "Release preset, venv Python"
run configure_release_venv.log cmake --preset macos-release -DPython_EXECUTABLE="$venv/bin/python"
grep -E 'libvmaf package version|Found Python' "$logs/configure_release_venv.log" | redact
run build_release_venv.log cmake --build --preset macos-release
echo "A later configure without -DPython_EXECUTABLE keeps the cached interpreter:"
run reconfigure_release.log cmake --preset macos-release
grep -E '^_?Python_EXECUTABLE' build/macos-release/CMakeCache.txt | redact

section "Debug preset, venv Python"
run configure_debug_venv.log cmake --preset macos-debug -DPython_EXECUTABLE="$venv/bin/python"
run build_debug_venv.log cmake --build --preset macos-debug

section "Unit tests (debug build)"
run tests_debug.log perl -e 'alarm 900; exec @ARGV' build/macos-debug/mirage_tests '~[audio]'
grep -E -B 6 -A 3 'FAILED:' "$logs/tests_debug.log" | redact
tail -n 2 "$logs/tests_debug.log" | redact

section "Built programs"
fact ls build/macos-release/mirage build/macos-release/mirage_vmaf build/macos-release/mirage_tests \
    build/macos-release/snapshot_diff
fact otool -L build/macos-release/mirage_vmaf
run vmaf_help.log build/macos-release/mirage_vmaf --help
head -n 1 "$logs/vmaf_help.log"

section "Dependencies fetched at configure time (release build)"
for d in build/macos-release/_deps/*-src; do
    echo "$(basename "$d") $(git -C "$d" rev-parse HEAD)"
done

section "Generated glad files, release vs debug"
for f in include/KHR/khrplatform.h include/glad/gl.h src/gl.c; do
    if cmp -s "build/macos-release/gladsources/glad_gl_core_41/$f" \
              "build/macos-debug/gladsources/glad_gl_core_41/$f"; then
        echo "identical: $f"
    else
        echo "differ: $f"
    fi
done

cd "$repo" || exit 1
git worktree remove --force "$wt"
