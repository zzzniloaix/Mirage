# run_all.sh — reproduces the numbers of the render investigation (findings
# 1-3: 10-bit limited range with 8-bit constants, colour tags read once,
# 4:2:2 / 4:4:4 VideoToolbox buffers). Run it with bash:
#
#   WT=<Mirage worktree> [FF=<FFmpeg prefix>] [CATCH2_SRC=<Catch2 v3 source>] \
#       [OUT=<output dir>] [REV=<commit>]  bash run_all.sh > results.txt 2>&1
#
#   WT          required. Read through read-only git commands only (show, diff,
#               rev-parse, cat-file); nothing is built or written there and
#               its build directories are not read.
#   FF          FFmpeg prefix (default /opt/homebrew/opt/ffmpeg-full).
#   CATCH2_SRC  optional: a Catch2 v3 source tree. It is built into OUT to
#               compile proposal/test_proposal; without it that step is skipped.
#   OUT         where everything is built and written (default: run/ next to
#               this script).
#   REV         the commit whose sources the report's file:line references
#               were read at; the replicas are built from it too (default
#               a60e4bc). Missing REV: the worktree's files are used instead.
#
# Inputs next to this script: the probe sources, make_bars.py, read_bars.py,
# proposal/, and the encoded clips in clips/. The raw frames are regenerated
# with make_bars.py, tagswitch.ts with the recipe of
# tests/engineer_tools_snapshots.sh:38-43. No player window: the GL probes use
# an offscreen CGL context. Absolute paths in the output are replaced by
# <OUT>, <R> (this script's directory), <WT>, <CATCH2>, <FF>, <SDK>, <HOME>.

if [ -z "${BASH_VERSION:-}" ]; then echo "run_all.sh: run it with bash" >&2; exit 2; fi
set -u
: "${WT:?set WT to the Mirage worktree}"
FF="${FF:-/opt/homebrew/opt/ffmpeg-full}"
REV="${REV:-a60e4bc}"
CATCH2_SRC="${CATCH2_SRC:-}"
R="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)" || exit 2
WT="$(cd "$WT" && pwd)" || { echo "run_all.sh: WT=$WT is not a directory" >&2; exit 2; }
OUT="${OUT:-$R/run}"
mkdir -p "$OUT" && OUT="$(cd "$OUT" && pwd)" || exit 2
[ -n "$CATCH2_SRC" ] && { CATCH2_SRC="$(cd "$CATCH2_SRC" && pwd)" || exit 2; }
BIN="$OUT/bin"; RAW="$OUT/raw"; WORK="$OUT/work"; SRC="$OUT/work/rev-src"
# The repo test goes in a directory of its own: its #include "YuvMatrix.h" must
# find the proposal's header (-I proposal), not the one exported into SRC.
TST="$OUT/work/rev-tests"
mkdir -p "$BIN" "$RAW" "$WORK" "$SRC" "$TST" || exit 2
SDK="$(xcrun --show-sdk-path)"

# Placeholders for the absolute paths in everything printed (longest first).
esc() { printf '%s' "$1" | sed -e 's/[][\.*^$#]/\\&/g'; }
SEDP=""
add_ph() {
    [ -n "$1" ] || return 0
    SEDP="$SEDP;s#$(esc "$1")#$2#g"
    local p; p="$(cd "$1" 2>&- && pwd -P)" || return 0
    [ -n "$p" ] && [ "$p" != "$1" ] && SEDP="$SEDP;s#$(esc "$p")#$2#g"
    return 0
}
add_ph "$OUT" "<OUT>"; add_ph "$R" "<R>"; add_ph "$WT" "<WT>"; add_ph "$CATCH2_SRC" "<CATCH2>"
add_ph "$FF" "<FF>"; add_ph "$SDK" "<SDK>"; add_ph "$HOME" "<HOME>"
SEDP="${SEDP#;}"

say()  { printf '%s\n' "$*"; }
hdr()  { printf '\n### %s\n' "$*"; }
run()  {   # print the command (shell-quoted), run it, note a non-zero exit
    printf '+'; printf ' %q' "$@"; printf '\n'
    "$@"; local s=$?
    [ "$s" -eq 0 ] || say "(exit $s)"
    return 0
}
pipe() {   # the same for a pipeline given as one string (printed with its variables filled in)
    local shown="$1"
    shown="${shown//\$FFPROBE/${FFPROBE:-}}"; shown="${shown//\$FFMPEG/${FFMPEG:-}}"
    shown="${shown//\$BIN/$BIN}"; shown="${shown//\$WORK/$WORK}"; shown="${shown//\$RAW/$RAW}"
    shown="${shown//\$c/${c:-}}"
    printf '+ %s\n' "$shown"
    eval "$1"; local s=$?
    [ "$s" -eq 0 ] || say "(exit $s)"
    return 0
}
runlog() {   # run a command, printed here, with its output in a log file
    local log="$1"; shift
    printf '+'; printf ' %q' "$@"; printf '  > %s 2>&1\n' "$log"
    "$@" > "$log" 2>&1; local s=$?
    [ "$s" -eq 0 ] || say "(exit $s)"
    return 0
}
REVOK=""
src_at() {   # a file of the worktree as it reads at REV (or as it is, without REV)
    if [ -n "$REVOK" ]; then git -C "$WT" show "$REV:$1"; else cat "$WT/$1"; fi
}
x255_r() {   # gl_hw output file -> x255 R values of bars 1-4, RGBA32F section
    awk '/RGBA32F target/ {s=1; next} /RGBA8 target/ {s=0} s && /bar [1-4]:/ {sub(/.*x255: /, ""); print $1}' "$1" | tr '\n' ' '
}

main() {
FFMPEG="$FF/bin/ffmpeg"; FFPROBE="$FF/bin/ffprobe"

hdr "[C0] environment"
run date
run sysctl -n machdep.cpu.brand_string
run sw_vers
pipe '"$FFMPEG" -hide_banner -version | head -1'
pipe 'clang --version | head -1'
pipe 'python3 --version'
pipe 'cmake --version | head -1'
run git -C "$WT" rev-parse HEAD
if git -C "$WT" cat-file -e "$REV^{commit}" 2>&-; then
    REVOK=1
    run git -C "$WT" rev-parse "$REV"
    for f in src tests; do
        if git -C "$WT" diff --quiet "$REV" HEAD -- "$f"; then say "$f/: no change between $REV and HEAD"
        else say "$f/: CHANGED between $REV and HEAD:"; git -C "$WT" diff --stat "$REV" HEAD -- "$f"; fi
    done
else
    say "REV $REV not found in WT: using the worktree's files as they are"
fi

hdr "[C0] the report's file:line references, as they read at $REV"
while read -r f a b; do
    [ -n "$f" ] || continue
    if [ -n "$REVOK" ] && ! git -C "$WT" diff --quiet "$REV" HEAD -- "$f"; then say "($f differs at HEAD)"; fi
    src_at "$f" | awk -v f="$f" -v a="$a" -v b="$b" 'NR >= a && NR <= b {printf "%s:%d: %s\n", f, NR, $0}'
done <<'REFS'
src/YuvMatrix.h 18 19
src/YuvMatrix.h 52 62
src/YuvMatrix.h 78 80
src/VideoRenderer.cpp 100 100
src/VideoRenderer.cpp 123 127
src/VideoRenderer.cpp 217 221
src/VideoRenderer.cpp 301 357
src/VideoRenderer.cpp 379 445
src/VideoRenderer.cpp 466 471
src/VideoRenderer.cpp 497 499
src/VideoRenderer.cpp 542 558
src/VideoRenderer.cpp 566 628
src/VideoRenderer.h 53 55
src/MediaSession.cpp 175 175
src/MediaSession.cpp 526 531
src/MediaSession.cpp 694 704
src/main.cpp 541 541
src/main.cpp 1664 1670
src/main.cpp 2835 2835
src/main.cpp 3105 3106
src/main.cpp 3278 3285
src/main.cpp 3307 3326
src/main.cpp 3374 3383
src/FrameInfo.cpp 328 366
src/Scopes.cpp 102 104
src/PixelView.cpp 28 31
src/PixelView.cpp 102 125
src/ThumbnailStrip.cpp 560 563
src/Decoder.cpp 14 22
tests/test_yuv_matrix.cpp 52 55
tests/test_yuv_matrix.cpp 141 146
tests/snapshot_diff.cpp 7 8
tests/snapshot_diff.cpp 27 28
tests/engineer_tools_snapshots.sh 38 43
plan/engineer_tools_status.md 73 84
REFS
say "-- the upload_frame calls (src/main.cpp at $REV):"
src_at src/main.cpp | grep -n 'upload_frame('
say "-- the owner's log (plan/engineer_tools_runs/gui-2026-11-03/snapshots/frame_inspector_tag_switch.log at $REV), lines 1, 12, 465:"
src_at plan/engineer_tools_runs/gui-2026-11-03/snapshots/frame_inspector_tag_switch.log | awk 'NR == 1 || NR == 12 || NR == 465 {printf "%d: %s\n", NR, $0}'
say "-- CoreVideo's pixel formats (<SDK> CVPixelBuffer.h):"
grep -n "'420v'\|'422v'\|'444v'\|'x420'\|'x422'\|'x444'\|'xf20'\|'sv22'\|'sv44'\|'y416'" \
    "$SDK/System/Library/Frameworks/CoreVideo.framework/Headers/CVPixelBuffer.h"

hdr "[B] build: every probe from its source (sources from <R>; YuvMatrix.h and VideoRenderer.cpp exported from $REV into <OUT>/work/rev-src, test_yuv_matrix.cpp into <OUT>/work/rev-tests)"
src_at src/YuvMatrix.h          > "$SRC/YuvMatrix.h"
src_at src/VideoRenderer.cpp    > "$SRC/VideoRenderer.cpp"
src_at tests/test_yuv_matrix.cpp > "$TST/test_yuv_matrix.cpp"
cd "$R" || exit 1
run shasum -a 256 gl_hw.cpp vt_probe.c sws_check.c analytic.cpp gl_compile_time.cpp gl_y416.cpp sws_dst.c sws_inputs.c \
    make_bars.py read_bars.py proposal/YuvMatrix.h proposal/gl_hw_prop.cpp proposal/test_proposal.cpp \
    "$SRC/YuvMatrix.h" "$SRC/VideoRenderer.cpp" "$TST/test_yuv_matrix.cpp"
FWK="-framework OpenGL -framework CoreVideo -framework IOSurface -framework CoreFoundation"
run clang++ -std=c++20 -O1 -I"$SRC" -I"$FF/include" gl_hw.cpp -L"$FF/lib" -lavformat -lavcodec -lavutil $FWK -o "$BIN/gl_hw"
run clang -O1 -I"$FF/include" vt_probe.c -L"$FF/lib" -lavformat -lavcodec -lavutil -framework CoreVideo -framework IOSurface -framework CoreFoundation -o "$BIN/vt_probe"
run clang -O1 -I"$FF/include" sws_check.c -L"$FF/lib" -lswscale -lavutil -o "$BIN/sws_check"
run clang++ -std=c++20 -O1 -I"$SRC" -I"$FF/include" analytic.cpp -o "$BIN/analytic"
run clang++ -std=c++20 -O1 gl_compile_time.cpp -framework OpenGL -o "$BIN/gl_compile_time"
run clang++ -std=c++20 -O1 -I"$FF/include" gl_y416.cpp -L"$FF/lib" -lavformat -lavcodec -lavutil $FWK -o "$BIN/gl_y416"
run clang -O1 -I"$FF/include" sws_dst.c -L"$FF/lib" -lswscale -lavutil -o "$BIN/sws_dst"
run clang -I"$FF/include" sws_inputs.c -L"$FF/lib" -lswscale -lavutil -o "$BIN/sws_inputs"
run clang++ -std=c++20 -O1 -I"$R/proposal" -I"$FF/include" proposal/gl_hw_prop.cpp -L"$FF/lib" -lavformat -lavcodec -lavutil $FWK -o "$BIN/gl_hw_prop"
if [ -n "$CATCH2_SRC" ]; then
    C2B="$OUT/catch2"
    runlog "$WORK/catch2-configure.log" cmake -S "$CATCH2_SRC" -B "$C2B" -DCMAKE_BUILD_TYPE=Release -DCATCH_INSTALL_DOCS=OFF -DCATCH_INSTALL_EXTRAS=OFF -DBUILD_TESTING=OFF
    say "Catch2 source: $(grep -m1 -A1 '^project(Catch2' "$CATCH2_SRC/CMakeLists.txt" | tail -1 | sed 's/#.*//')"
    tail -2 "$WORK/catch2-configure.log"
    runlog "$WORK/catch2-build.log" cmake --build "$C2B" --target Catch2WithMain --parallel
    tail -1 "$WORK/catch2-build.log"
    run clang++ -std=c++20 -O0 -I"$R/proposal" -I"$CATCH2_SRC/src" -I"$C2B/generated-includes" -I"$FF/include" \
        proposal/test_proposal.cpp "$TST/test_yuv_matrix.cpp" "$C2B/src/libCatch2Main.a" "$C2B/src/libCatch2.a" -o "$BIN/test_proposal"
    run clang++ -std=c++20 -E -I"$R/proposal" -I"$CATCH2_SRC/src" -I"$C2B/generated-includes" -I"$FF/include" \
        "$TST/test_yuv_matrix.cpp" -o "$WORK/test_yuv_matrix.i"
    say "the repo's test_yuv_matrix.cpp is compiled against: $(grep -m1 'YuvMatrix.h' "$WORK/test_yuv_matrix.i")"
else
    say "proposal/test_proposal: not built (CATCH2_SRC unset)"
fi
pipe 'otool -L "$BIN/vt_probe" | grep libav'

hdr "[RAW] the raw frames, regenerated with make_bars.py"
cd "$RAW" || exit 1
run python3 -I "$R/make_bars.py" bars420.yuv 420
run python3 -I "$R/make_bars.py" bars422.yuv 422
run python3 -I "$R/make_bars.py" bars444.yuv 444
run python3 -I "$R/make_bars.py" bars420_75.yuv 420 75
run python3 -I "$R/make_bars.py" bars8_420.yuv 420 25 8
run python3 -I "$R/make_bars.py" bars420_8.yuv 420 1 10to8
run shasum -a 256 bars420.yuv bars422.yuv bars444.yuv bars420_75.yuv bars8_420.yuv bars420_8.yuv
for f in bars420.yuv bars422.yuv bars444.yuv bars420_75.yuv bars8_420.yuv bars420_8.yuv; do
    if [ -f "$R/clips/$f" ]; then
        cmp "$f" "$R/clips/$f" && say "$f: byte-identical to <R>/clips/$f, the file the clips were encoded from"
    fi
done
say "(bars8_420.yuv and bars420_8.yuv were first written by inline Python; make_bars.py's 8 and 10to8 modes reproduce them.)"

hdr "[CLIPS] the encoded clips used (<R>/clips), and the commands that made them"
cd "$R/clips" || exit 1
CLIPS="bars_pq.mp4 bars_sdr10.mp4 bars8_hevc.mp4 bars_prores422hq.mov bars_prores4444.mov bars_prores4444a.mov bars_hevc422.mp4 bars_hevc444.mp4 bars_h264_422.mp4 bars_hevc444_8.mp4 bsw_sdr.ts bsw_pq.ts bars_switch.ts bars_switch_rev.ts"
for c in $CLIPS; do [ -f "$c" ] || say "MISSING: $c"; done
run shasum -a 256 $CLIPS
say "Made in the raw frames' directory with <FF>/bin/ffmpeg (printed, not re-run):"
cat <<'CMDS'
  ffmpeg -v error -y -f rawvideo -pix_fmt yuv420p10le -s 448x128 -r 25 -i bars420.yuv -c:v libx265 -x265-params "log-level=error:lossless=1:colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc:range=limited" -tag:v hvc1 bars_pq.mp4
  ffmpeg -v error -y -f rawvideo -pix_fmt yuv420p10le -s 448x128 -r 25 -i bars420.yuv -c:v libx265 -x265-params "log-level=error:lossless=1:colorprim=bt709:transfer=bt709:colormatrix=bt709:range=limited" -tag:v hvc1 bars_sdr10.mp4
  ffmpeg -v error -y -f rawvideo -pix_fmt yuv420p -s 448x128 -r 25 -i bars8_420.yuv -c:v libx265 -x265-params "log-level=error:lossless=1:colorprim=bt709:transfer=bt709:colormatrix=bt709:range=limited" -tag:v hvc1 bars8_hevc.mp4
  ffmpeg -v error -y -f rawvideo -pix_fmt yuv422p10le -s 448x128 -r 25 -i bars422.yuv -c:v prores_ks -profile:v hq -color_primaries bt709 -color_trc bt709 -colorspace bt709 -color_range tv bars_prores422hq.mov
  ffmpeg -v error -y -f rawvideo -pix_fmt yuv444p10le -s 448x128 -r 25 -i bars444.yuv -c:v prores_ks -profile:v 4444 -pix_fmt yuv444p10le -color_primaries bt709 -color_trc bt709 -colorspace bt709 -color_range tv bars_prores4444.mov
  ffmpeg -v error -y -f rawvideo -pix_fmt yuv444p10le -s 448x128 -r 25 -i bars444.yuv -c:v prores_ks -profile:v 4444 -pix_fmt yuva444p10le -color_primaries bt709 -color_trc bt709 -colorspace bt709 -color_range tv bars_prores4444a.mov
  ffmpeg -v error -y -f rawvideo -pix_fmt yuv422p10le -s 448x128 -r 25 -i bars422.yuv -c:v libx265 -x265-params "log-level=error:lossless=1" -tag:v hvc1 bars_hevc422.mp4
  ffmpeg -v error -y -f rawvideo -pix_fmt yuv444p10le -s 448x128 -r 25 -i bars444.yuv -c:v libx265 -x265-params "log-level=error:lossless=1" -tag:v hvc1 bars_hevc444.mp4
  ffmpeg -v error -y -f rawvideo -pix_fmt yuv422p10le -s 448x128 -r 25 -i bars422.yuv -pix_fmt yuv422p -c:v libx264 -qp 0 bars_h264_422.mp4
  ffmpeg -v error -y -f rawvideo -pix_fmt yuv444p10le -s 448x128 -r 25 -i bars444.yuv -pix_fmt yuv444p -c:v libx265 -x265-params "log-level=error:lossless=1" -tag:v hvc1 bars_hevc444_8.mp4
  ffmpeg -v error -y -f rawvideo -pix_fmt yuv420p10le -s 448x128 -r 25 -i bars420_75.yuv -c:v libx265 -x265-params "log-level=error:lossless=1:keyint=25:colorprim=bt709:transfer=bt709:colormatrix=bt709:range=limited:repeat-headers=1" bsw_sdr.ts
  ffmpeg -v error -y -f rawvideo -pix_fmt yuv420p10le -s 448x128 -r 25 -i bars420_75.yuv -c:v libx265 -x265-params "log-level=error:lossless=1:keyint=25:colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc:range=limited:repeat-headers=1" bsw_pq.ts
  printf "file 'bsw_sdr.ts'\nfile 'bsw_pq.ts'\n" > bsw.txt && ffmpeg -v error -y -f concat -safe 0 -i bsw.txt -c copy bars_switch.ts
  printf "file 'bsw_pq.ts'\nfile 'bsw_sdr.ts'\n" > bsw_rev.txt && ffmpeg -v error -y -f concat -safe 0 -i bsw_rev.txt -c copy bars_switch_rev.ts
CMDS
say "-- software decodes: the lossless clips give back the raw frames; the others' codes at the bar centres"
run "$FFMPEG" -v error -y -i bars_pq.mp4 -frames:v 1 -f rawvideo -pix_fmt yuv420p10le "$WORK/dec_pq_sw.yuv"
pipe 'head -c 172032 "$RAW/bars420.yuv" | cmp "$WORK/dec_pq_sw.yuv" - && say "bars_pq.mp4 frame 0 == bars420.yuv frame 0"'
run "$FFMPEG" -v error -y -i bars_sdr10.mp4 -frames:v 1 -f rawvideo -pix_fmt yuv420p10le "$WORK/dec_sdr10_sw.yuv"
pipe 'head -c 172032 "$RAW/bars420.yuv" | cmp "$WORK/dec_sdr10_sw.yuv" - && say "bars_sdr10.mp4 frame 0 == bars420.yuv frame 0"'
run "$FFMPEG" -v error -y -i bars8_hevc.mp4 -frames:v 1 -f rawvideo -pix_fmt yuv420p "$WORK/dec_8hevc_sw.yuv"
pipe 'head -c 86016 "$RAW/bars8_420.yuv" | cmp "$WORK/dec_8hevc_sw.yuv" - && say "bars8_hevc.mp4 frame 0 == bars8_420.yuv frame 0"'
run "$FFMPEG" -v error -y -i bars_hevc422.mp4 -frames:v 1 -f rawvideo -pix_fmt yuv422p10le "$WORK/dec_hevc422_sw.yuv"
pipe 'head -c 229376 "$RAW/bars422.yuv" | cmp "$WORK/dec_hevc422_sw.yuv" - && say "bars_hevc422.mp4 frame 0 == bars422.yuv frame 0"'
run "$FFMPEG" -v error -y -i bars_hevc444.mp4 -frames:v 1 -f rawvideo -pix_fmt yuv444p10le "$WORK/dec_hevc444_sw.yuv"
pipe 'head -c 344064 "$RAW/bars444.yuv" | cmp "$WORK/dec_hevc444_sw.yuv" - && say "bars_hevc444.mp4 frame 0 == bars444.yuv frame 0"'
for t in sdr pq; do
    run "$FFMPEG" -v error -y -i "bsw_$t.ts" -f rawvideo -pix_fmt yuv420p10le "$WORK/dec_bsw_$t.yuv"
    pipe "cmp \"\$WORK/dec_bsw_$t.yuv\" \"\$RAW/bars420_75.yuv\" && say \"bsw_$t.ts: all 75 frames == bars420_75.yuv\""
done
run "$FFMPEG" -v error -y -i bars_prores422hq.mov -frames:v 1 -f rawvideo -pix_fmt yuv422p10le "$WORK/dec_p422_sw.yuv"
run python3 -I "$R/read_bars.py" "$WORK/dec_p422_sw.yuv" 422
run "$FFMPEG" -v error -y -i bars_prores4444.mov -frames:v 1 -f rawvideo -pix_fmt yuv444p12le "$WORK/dec_p4444_sw.yuv"
say "(12-bit codes:)"; run python3 -I "$R/read_bars.py" "$WORK/dec_p4444_sw.yuv" 444
run "$FFMPEG" -v error -y -i bars_h264_422.mp4 -frames:v 1 -f rawvideo -pix_fmt yuv422p "$WORK/dec_h264_422.yuv"
run python3 -I "$R/read_bars.py" "$WORK/dec_h264_422.yuv" 422 0 8
run "$FFMPEG" -v error -y -i bars_hevc444_8.mp4 -frames:v 1 -f rawvideo -pix_fmt yuv444p "$WORK/dec_hevc444_8.yuv"
run python3 -I "$R/read_bars.py" "$WORK/dec_hevc444_8.yuv" 444 0 8

hdr "[C1] what VideoToolbox returns (vt_probe), and each stream as ffprobe reads it"
for c in bars_pq.mp4 bars_sdr10.mp4 bars8_hevc.mp4 bars_prores422hq.mov bars_prores4444.mov bars_prores4444a.mov bars_hevc422.mp4 bars_hevc444.mp4 bars_h264_422.mp4 bars_hevc444_8.mp4; do
    say "-- $c"
    run "$FFPROBE" -v error -select_streams v:0 -show_entries stream=codec_name,codec_tag_string,profile,pix_fmt,color_range,color_space,color_transfer,color_primaries,bits_per_raw_sample -of default=nw=1 "$c"
    run "$BIN/vt_probe" "$c" 1
done
say "-- the CPU transfer a refused buffer would need (TRANSFER=1)"
run env TRANSFER=1 "$BIN/vt_probe" bars_prores4444a.mov 1
run env TRANSFER=1 "$BIN/vt_probe" bars_prores422hq.mov 1

V="$SRC/VideoRenderer.cpp"
hw() {   # gl_hw with a tag for its saved output
    local tag="$1"; shift
    run "$BIN/gl_hw" "$V" "$@" > "$WORK/hw_$tag.txt"
    cat "$WORK/hw_$tag.txt"
}
hdr "[C2] the HW replica (gl_hw), upload_hw as written"
hw sdr10_sdr      bars_sdr10.mp4 sdr 709
hw pq_hdr         bars_pq.mp4 hdr 2020
hw 8hevc_sdr      bars8_hevc.mp4 sdr 709
hw pq_as_sdr      bars_pq.mp4 sdr 709
hw sdr10_as_hdr   bars_sdr10.mp4 hdr 2020
hw p422_sdr       bars_prores422hq.mov sdr 709
hw p4444_sdr      bars_prores4444.mov sdr 709
hw p4444a_sdr     bars_prores4444a.mov sdr 709
hw hevc422_sdr    bars_hevc422.mp4 sdr 709
hw hevc444_sdr    bars_hevc444.mp4 sdr 709
hw h264_422_sdr   bars_h264_422.mp4 sdr 709
hw hevc444_8_sdr  bars_hevc444_8.mp4 sdr 709
hdr "[C2] raw texels (RAWSAMPLE=1): what the GPU reads from the P010 planes"
run env RAWSAMPLE=1 "$BIN/gl_hw" "$V" bars_pq.mp4 sdr 709
hdr "[C2] bound as 16-bit (force16)"
hw p422_f16       bars_prores422hq.mov sdr 709 force16
hw p4444_f16      bars_prores4444.mov sdr 709 force16
hw hevc422_f16    bars_hevc422.mp4 sdr 709 force16
hw hevc444_f16    bars_hevc444.mp4 sdr 709 force16
hdr "[C2] with the 16-bit-container constants (fixed: gl_hw's fixed16)"
hw sdr10_fixed    bars_sdr10.mp4 sdr 709 fixed
hw pq_hdr_fixed   bars_pq.mp4 hdr 2020 fixed
hw sdr10_as_hdr_fixed bars_sdr10.mp4 hdr 2020 fixed
hw p422_f16_fixed  bars_prores422hq.mov sdr 709 force16 fixed
hw p4444_f16_fixed bars_prores4444.mov sdr 709 force16 fixed
hw hevc422_f16_fixed bars_hevc422.mp4 sdr 709 force16 fixed
hw hevc444_f16_fixed bars_hevc444.mp4 sdr 709 force16 fixed

hdr "[C3] the shader's float math on make_yuv_matrix (analytic)"
run "$BIN/analytic"
say "-- the SW path's E' (sws_check, RGB48LE / 65535, bars 1-4 = codes 300 509 723 940) through the same chain"
run "$BIN/sws_check" "$RAW/bars420.yuv" yuv420p10le hdr 2020 > "$WORK/sws_hdr.txt"
cat "$WORK/sws_hdr.txt"
SWE="$(awk '/bar [1-4]:/ {sub(/.*\/65535: /, ""); print $1}' "$WORK/sws_hdr.txt" | tr '\n' ' ')"
run "$BIN/analytic" $SWE > "$WORK/analytic_sw.txt"
cat "$WORK/analytic_sw.txt"
say "-- the constants, as fractions (the matrix values printed above):"
awk 'BEGIN { printf "16/255 = %.7f  255/219 = %.7f  128/255 = %.7f | 4096/65535 = %.7f  65535/56064 = %.7f  32768/65535 = %.7f  65535/57344 = %.7f | 65535/65280 = %.7f  65472/65535 = %.6f\n", 16/255, 255/219, 128/255, 4096/65535, 65535/56064, 32768/65535, 65535/57344, 65535/65280, 65472/65535 }'
say "-- exact E' = (code - 64) / 876 for 300 509 723 940:"
EXACT="$(awk 'BEGIN { printf "%.6f %.6f %.6f %.6f", (300-64)/876, (509-64)/876, (723-64)/876, (940-64)/876 }')"
run "$BIN/analytic" $EXACT
say "-- computed from the files above: on-screen PQ gaps (8-bit codes) at bars 1-4"
HWNOW="$(x255_r "$WORK/hw_pq_hdr.txt")"; HWFIX="$(x255_r "$WORK/hw_pq_hdr_fixed.txt")"
SWD="$(awk '/displayed/ {print $NF}' "$WORK/analytic_sw.txt" | tr '\n' ' ')"
awk -v a="$HWNOW" -v b="$SWD" -v c="$HWFIX" 'function ab(x) { return x < 0 ? -x : x }
BEGIN { n = split(a, A, " "); split(b, B, " "); split(c, C, " ")
  for (i = 1; i <= n; ++i) {
    d1 = ab(A[i] - B[i]); d2 = ab(C[i] - B[i]); d3 = ab(C[i] - A[i])
    printf "bar %d: HW now %s | SW %s | HW fixed %s -> |now-SW| %.2f  |fixed-SW| %.2f  |fixed-now| %.2f\n", i, A[i], B[i], C[i], d1, d2, d3
    if (d1 > m1) m1 = d1; if (d2 > m2) m2 = d2; if (d3 > m3) m3 = d3 }
  printf "max |HW now - SW| %.2f, max |HW fixed - SW| %.2f, max |HW fixed - HW now| %.2f\n", m1, m2, m3 }'

hdr "[C4] the SW path (sws_check: build_sws's settings; SWSF overrides the flags)"
run "$BIN/sws_check" --vt
for f in 0x2 0x2002; do run env SWSF=$f "$BIN/sws_check" "$RAW/bars420.yuv" yuv420p10le sdr 709; done
for f in 0x2 0x40002 0x42002 0x2002; do run env SWSF=$f "$BIN/sws_check" "$RAW/bars420.yuv" yuv420p10le hdr 2020; done
for f in 0x2 0x2002; do run env SWSF=$f "$BIN/sws_check" "$RAW/bars420_8.yuv" yuv420p sdr 709; done
run "$BIN/sws_check" "$RAW/bars8_420.yuv" yuv420p sdr 709
run "$BIN/sws_check" "$WORK/dec_p422_sw.yuv" yuv422p10le sdr 709
hdr "[C4] --time (context build and one conversion; timings vary run to run)"
run "$BIN/sws_check" --time 1920 1080 yuv420p10le hdr
run "$BIN/sws_check" --time 3840 2160 yuv420p10le hdr
for f in 0x2 0x2002; do
    run env SWSF=$f "$BIN/sws_check" --time 1920 1080 yuv420p10le sdr
    run env SWSF=$f "$BIN/sws_check" --time 1920 1080 yuv420p sdr
done

hdr "[C5] the same conversion through the ffmpeg CLI's scale filter (rgb48le; bar 4 = Y 940, bar 6 = the colour bar)"
for fl in bilinear bilinear+full_chroma_int; do
    run "$FFMPEG" -v error -y -f rawvideo -pix_fmt yuv420p10le -s 448x128 -i "$RAW/bars420.yuv" -frames:v 1 \
        -vf "scale=flags=$fl:in_range=tv:out_range=pc:in_color_matrix=bt2020,format=rgb48le" -f rawvideo "$WORK/cli_$fl.rgb"
    run python3 -I -c 'import struct, sys; d = open(sys.argv[1], "rb").read(); W, y = 448, 64; print(sys.argv[2], [struct.unpack_from("<3H", d, 2 * 3 * (y * W + 64 * b + 32)) for b in (4, 6)])' "$WORK/cli_$fl.rgb" "$fl"
done

hdr "[C6] the cost of (re)building each program (gl_compile_time)"
run "$BIN/gl_compile_time" "$V" 3

hdr "[C7] the tag switch: tagswitch.ts regenerated with the recipe of tests/engineer_tools_snapshots.sh:38-43"
mkdir -p "$WORK/tagswitch" && cd "$WORK/tagswitch" || exit 1
run "$FFMPEG" -v error -y -f lavfi -i testsrc2=size=1280x720:rate=25 -t 3 -pix_fmt yuv420p10le -c:v libx265 \
    -x265-params "log-level=error:colorprim=bt709:transfer=bt709:colormatrix=bt709:repeat-headers=1" part_sdr.ts
run "$FFMPEG" -v error -y -f lavfi -i testsrc2=size=1280x720:rate=25 -ss 3 -t 3 -pix_fmt yuv420p10le -c:v libx265 \
    -x265-params "log-level=error:colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc:master-display=G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(10000000,50):max-cll=1000,400:repeat-headers=1" part_pq.ts
say "+ printf \"file 'part_sdr.ts'\\nfile 'part_pq.ts'\\n\" > tagswitch.txt"
printf "file 'part_sdr.ts'\nfile 'part_pq.ts'\n" > tagswitch.txt
run "$FFMPEG" -v error -y -f concat -safe 0 -i tagswitch.txt -c copy tagswitch.ts
for c in "$WORK/tagswitch/tagswitch.ts" "$R/clips/bars_switch.ts" "$R/clips/bars_switch_rev.ts"; do
    say "-- $c"
    run "$FFPROBE" -v error -select_streams v:0 -show_entries stream=pix_fmt,color_range,color_space,color_transfer,color_primaries:format=start_time,duration -of default=nw=1 "$c"
    pipe '"$FFPROBE" -v error -select_streams v:0 -show_entries frame=pts_time,color_range,color_space,color_primaries,color_transfer -of compact=p=0 "$c" | awk -F"|" '\''{t = $2 "|" $3 "|" $4 "|" $5} t != p {print "frame " NR " (" $1 "): " t; p = t} END {print NR " frames"}'\'''
    pipe '"$BIN/vt_probe" "$c" 1000 1 | grep "^frame" | sed -E "s/^frame [0-9]+ pts=[0-9.]+ //" | uniq -c'
    pipe '"$BIN/vt_probe" "$c" 1000 1 | grep CVPixelBuffer | sort | uniq -c'
done
run "$BIN/vt_probe" "$WORK/tagswitch/tagswitch.ts" 8 20
cd "$R/clips" || exit 1

hdr "[C8] 'y416' bound as one RGBA16 texture (gl_y416); sws's inputs (sws_inputs)"
run "$BIN/gl_y416" bars_prores4444a.mov
run "$BIN/sws_inputs"

hdr "[C9] the scratch proposal: its unit tests with the repo's (test_proposal), and the HW replica with its matrix (gl_hw_prop)"
if [ -x "$BIN/test_proposal" ]; then
    run "$BIN/test_proposal" --list-tests
    run "$BIN/test_proposal"
else
    say "test_proposal not built"
fi
say "-- gl_hw_prop: gl_hw built against proposal/YuvMatrix.h; 'fixed' = make_yuv_matrix(cs, MPEG, 16 or 8, 10 or 8)"
while read -r a; do
    [ -n "$a" ] || continue
    run "$BIN/gl_hw_prop" "$V" $a fixed
done <<'PROPRUNS'
bars_sdr10.mp4 sdr 709
bars_pq.mp4 hdr 2020
bars8_hevc.mp4 sdr 709
bars_h264_422.mp4 sdr 709
bars_hevc444_8.mp4 sdr 709
bars_prores422hq.mov sdr 709 force16
bars_prores4444.mov sdr 709 force16
bars_hevc422.mp4 sdr 709 force16
bars_hevc444.mp4 sdr 709 force16
PROPRUNS

hdr "[C10] sws's 16-bit RGB destinations (sws_dst; bar 4 = Y 940, bar 6 = the colour bar)"
for d in rgb48le rgba64le gbrp16le; do
    for f in 0x2 0x2002; do run "$BIN/sws_dst" "$RAW/bars420.yuv" "$d" "$f"; done
done

hdr "done"
run date
}

main 2>&1 | sed -e "$SEDP"
