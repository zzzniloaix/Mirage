#!/bin/sh
# The player's GUI regressions outside ctest, run as they run today, with the player of this
# change and with the one before it, and their snapshots compared (work package WP13). See
# README.md.
#
# usage: sh eval/timeline/ui-qa-2026-11-16/regressions.sh <build dir> <before dir> <out> [pathb|engineer|loudness]
#
# <build dir> is a debug build of this checkout (mirage, snapshot_diff); <before dir> holds
# `mirage` and `snapshot_diff` built from the commit before the panel's; <out> a new or empty
# directory outside the repository; the last argument runs one part alone. The script runs
# - tests/pathb_snapshot_regression.sh once with each player. It passes the player no option of
#   the run rules below, so it runs a wrapper in each player's place (<out>.wrap/<which>/mirage):
#   the player with --qa-no-focus and --window 1280x720, the size it takes without one, with HOME
#   at <out>.home, under caffeinate -i and a perl alarm of 60 s, keeping a copy of each
#   snapshot the script would delete;
# - tests/engineer_tools_snapshots.sh, which passes those options itself, under caffeinate -i:
#   twice with the player before, once with this one. Its windows play audio: it waits for an
#   unlocked screen, as CoreAudio plays nothing while the screen is locked;
# - after the engineer part, by itself, its loudness part: that script's 5.1 loudness case
#   again, as it runs it but for the snapshot's count, at the render loop's 419th, 420th and
#   421st iteration (src/main.cpp's ui_frames), twice each with each player, under caffeinate -i.
#   The count is of iterations, which the loop makes as frames fall due and as events come, so
#   the source's frame then on screen, played on the clip's audio clock, follows the run's
#   timing: regressions/loudness.txt gives each snapshot's frame counter of the source (testsrc2,
#   the top left) and its picture outside the loudness card and the top bar's drop counter, whose
#   figures follow the timing too, by their pixels' MD5; each counter's pixels are kept once, at
#   regressions/loudness-counters/<MD5>.png.
# Then each snapshot of the two players is compared by its decoded pixels (FFmpeg's md5 muxer),
# and the engineer-tools snapshots of the player before with each other, which shows the ones
# that differ from run to run; where two differ, the box that holds every pixel that differs;
# regressions/compare.txt is the table. Each run's copy of an engineer-tools snapshot that
# differs from another's is kept at half size, for review, in regressions/engineer-differ/. The
# table is written at the end of every part's run, from the runs made so far. POSIX sh.
set -u

die() {
    echo "regressions.sh: $*" >&2
    exit 2
}

[ $# -eq 3 ] || [ $# -eq 4 ] || { echo "usage: sh regressions.sh <build dir> <before dir> <out> [pathb|engineer|loudness]" >&2; exit 2; }
here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P) || die "can't find the script's directory"
repo=$(git -C "$here" rev-parse --show-toplevel 2>/dev/null) || die "$here isn't in a git repository"
repo=$(CDPATH='' cd -- "$repo" && pwd -P) || die "can't enter $repo"
build=$(CDPATH='' cd -- "$1" 2>/dev/null && pwd -P) || die "no build directory $1"
before=$(CDPATH='' cd -- "$2" 2>/dev/null && pwd -P) || die "no directory $2"
part=${4:-all}
for prog in "$build/mirage" "$build/snapshot_diff" "$before/mirage" "$before/snapshot_diff"; do
    [ -x "$prog" ] || die "no $prog"
done
mkdir -p -- "$3" || die "can't make $3"
out=$(CDPATH='' cd -- "$3" && pwd -P) || die "can't enter $3"
case $out in "$repo"/*) die "$3 is inside the repository" ;; esac
home=$out.home
mkdir -p -- "$home" "$out/regressions" || die "can't make $home"
cd -- "$out" || die "can't enter $out"
cmd=regressions/cmd.txt
[ -f "$cmd" ] || printf '%s\n' "# The commands of regressions.sh's run, from <out>, each with its exit status." > "$cmd"

# A word as cmd.txt shows it: the run's directories as placeholders, quoted where a shell would
# need it (as run.sh shows them).
shown() {
    sw_w=$1
    for sw_pl in "out=$out" "build=$build" "before=$before" "repo=$repo"; do
        sw_nm=${sw_pl%%=*}
        sw_pp=${sw_pl#*=}
        case $sw_w in
            "$sw_pp") sw_w="<$sw_nm>"; break ;;
            "$sw_pp"/*) sw_w="<$sw_nm>${sw_w#"$sw_pp"}"; break ;;
            "$sw_pp".*) sw_w="<$sw_nm>${sw_w#"$sw_pp"}"; break ;;
            *"=$sw_pp"/* | *"=$sw_pp".*) sw_w="${sw_w%%=*}=<$sw_nm>${sw_w#*="$sw_pp"}"; break ;;
        esac
    done
    if [ -z "$sw_w" ] || printf '%s' "$sw_w" | LC_ALL=C grep -q '[^A-Za-z0-9_./:=@%+,<>-]'; then
        sw_w="'$(printf '%s' "$sw_w" | sed "s/'/'\\\\''/g")'"
    fi
    printf '%s' "$sw_w"
}

shown_line() {
    sl_l=''
    for sl_word in "$@"; do
        sl_l="$sl_l${sl_l:+ }$(shown "$sl_word")"
    done
    printf '%s' "$sl_l"
}

run() {
    run_lg=$1
    shift
    printf '$ %s > %s 2>&1\n' "$(shown_line "$@")" "$run_lg" >> "$cmd"
    "$@" > "$run_lg" 2>&1
    run_st=$?
    echo "# exit status $run_st" >> "$cmd"
    return $run_st
}

# A PNG's decoded pixels, as FFmpeg's md5 muxer hashes them.
pixels() {
    ffmpeg -v error -i "$1" -f md5 - 2>/dev/null | sed 's/^MD5=//'
}

# Where two PNGs of one size differ: the box that holds every pixel with a channel that differs,
# as FFmpeg's bbox filter finds it in their difference, each such pixel made white first.
box() {
    ffmpeg -v info -i "$1" -i "$2" -filter_complex \
        "[0][1]blend=all_mode=difference,lutrgb=r='if(val,255,0)':g='if(val,255,0)':b='if(val,255,0)',format=gray,bbox=min_val=1" \
        -f null - 2>&1 | sed -n 's/.* x1:\([0-9]*\) x2:\([0-9]*\) y1:\([0-9]*\) y2:\([0-9]*\) .*/x \1-\2, y \3-\4/p' |
        sed -n '1p'
}

# "same", or "differ in x <x1>-<x2>, y <y1>-<y2>" (window px, both ends included).
verdict() {
    if [ "$1" = "$2" ]; then
        echo same
    else
        echo "differ in $(box "$3" "$4")"
    fi
}

locked() {
    ioreg -n Root -d1 -a | plutil -extract IOConsoleUsers xml1 -o - - | grep -A1 CGSSessionScreenIsLocked |
        grep -q true
}

if [ "$part" = all ] || [ "$part" = pathb ]; then
    for pb_which in before after; do
        pb_real=$build/mirage
        pb_diff=$build/snapshot_diff
        [ "$pb_which" = before ] && pb_real=$before/mirage && pb_diff=$before/snapshot_diff
        pb_dir=$out.wrap/$pb_which
        mkdir -p -- "$pb_dir/kept"
        cp -- "$pb_diff" "$pb_dir/snapshot_diff"
        {
            echo '#!/bin/sh'
            echo "# tests/pathb_snapshot_regression.sh's player under the run rules; the snapshot kept."
            echo "snap=''; prev=''"
            echo 'for a in "$@"; do [ "$prev" = --snapshot ] && snap=$a; prev=$a; done'
            echo "HOME='$home' caffeinate -i perl -e 'alarm shift; exec @ARGV' 60 '$pb_real' --qa-no-focus --window 1280x720 \"\$@\""
            echo 'st=$?'
            echo "[ -n \"\$snap\" ] && [ -f \"\$snap\" ] && cp -- \"\$snap\" '$pb_dir/kept/'"
            echo 'exit $st'
        } > "$pb_dir/mirage"
        chmod +x "$pb_dir/mirage"
        echo "# <out>.wrap/$pb_which/mirage runs $(shown "$pb_real") with --qa-no-focus --window 1280x720, HOME at <out>.home, under caffeinate -i and perl's alarm 60, and keeps its --snapshot in <out>.wrap/$pb_which/kept" >> "$cmd"
        run "regressions/pathb-$pb_which.txt" bash "$repo/tests/pathb_snapshot_regression.sh" "$pb_dir"
    done
fi

if [ "$part" = all ] || [ "$part" = engineer ]; then
    if locked; then
        echo "regressions.sh: the screen is locked: CoreAudio plays nothing; run the engineer part later" >&2
        exit 4
    fi
    for en_which in before-1 before-2 after; do
        en_bin=$build
        case $en_which in before-*) en_bin=$before ;; esac
        run "regressions/engineer-$en_which.txt" caffeinate -i bash "$repo/tests/engineer_tools_snapshots.sh" \
            "$en_bin" "$out.eng/$en_which"
        mkdir -p "regressions/engineer-$en_which"
        for en_log in "$out.eng/$en_which"/*.log; do
            cp -- "$en_log" "regressions/engineer-$en_which/"
        done
    done
fi

if [ "$part" = loudness ]; then
    lo_clip=$out.eng/before-1/surround51.mkv
    [ -f "$lo_clip" ] || die "no $lo_clip: run the engineer part first"
    if locked; then
        echo "regressions.sh: the screen is locked: CoreAudio plays nothing; run the loudness part later" >&2
        exit 4
    fi
    # The counter: testsrc2's frame number and time, the top left of the 1600x1000 window at
    # its framebuffer's scale of 2; outside the card: the picture with the loudness card and the
    # drop counter's pill painted black.
    lo_counter="crop=260:100:0:100"
    lo_outside="drawbox=x=2360:y=930:w=820:h=890:color=black:t=fill,drawbox=x=2980:y=20:w=230:h=50:color=black:t=fill"
    mkdir -p -- "$out.loud" regressions/loudness-counters
    # lo_row <png> <what>: its row of the table, its counter's pixels kept once.
    lo_row() {
        lr_c=$(ffmpeg -v error -i "$1" -vf "$lo_counter" -f md5 - 2>/dev/null | sed 's/^MD5=//')
        lr_m=$(ffmpeg -v error -i "$1" -vf "$lo_outside" -f md5 - 2>/dev/null | sed 's/^MD5=//')
        [ -f "regressions/loudness-counters/$lr_c.png" ] ||
            ffmpeg -v error -y -i "$1" -vf "$lo_counter" "regressions/loudness-counters/$lr_c.png"
        echo "  $2  counter $lr_c  outside the card $lr_m" >> regressions/loudness.txt
    }
    {
        echo "# The 5.1 loudness case of tests/engineer_tools_snapshots.sh: its snapshots of the engineer"
        echo "# part, at the render loop's 420th iteration, then the case again at three counts of the"
        echo "# loop's iterations, twice each, with each player: each snapshot's source frame counter and"
        echo "# its picture outside the loudness card and the drop counter, by their pixels' MD5; each"
        echo "# counter's pixels are regressions/loudness-counters/<MD5>.png."
    } > regressions/loudness.txt
    for lo_which in before-1 before-2 after; do
        lo_row "$out.eng/$lo_which/loudness_51.png" "engineer part, $lo_which, iteration 420"
    done
    for lo_which in before after; do
        lo_bin=$build/mirage
        [ "$lo_which" = before ] && lo_bin=$before/mirage
        for lo_n in 419 420 421; do
            for lo_k in 1 2; do
                lo_png=$out.loud/$lo_which-$lo_n-$lo_k.png
                run "regressions/loudness-$lo_which-$lo_n-$lo_k.log" caffeinate -i env HOME="$home" \
                    perl -e 'alarm 60; exec @ARGV' "$lo_bin" --qa-no-focus --window 1600x1000 --ui-overlays L \
                    --ui-snapshot "$lo_png" "$lo_n" "$lo_clip"
                lo_row "$lo_png" "$lo_which, iteration $lo_n, run $lo_k"
            done
        done
    done
fi

{
    echo "# Each snapshot by its decoded pixels' MD5 (FFmpeg's md5 muxer): the player before this"
    echo "# change and this one. \"same\" where the pixels are identical."
    if [ -d "$out.wrap/before/kept" ] && [ -d "$out.wrap/after/kept" ]; then
        echo
        echo "tests/pathb_snapshot_regression.sh (the video viewport, --snapshot after 30 frames):"
        for cp_png in "$out.wrap/before/kept"/*.png; do
            cp_n=$(basename "$cp_png")
            cp_a=$(pixels "$cp_png")
            cp_b=$(pixels "$out.wrap/after/kept/$cp_n")
            echo "  $cp_n  before $cp_a  after $cp_b  $(verdict "$cp_a" "$cp_b" "$cp_png" "$out.wrap/after/kept/$cp_n")"
        done
    fi
    if [ -d "$out.eng/before-1" ] && [ -d "$out.eng/before-2" ] && [ -d "$out.eng/after" ]; then
        echo
        echo "tests/engineer_tools_snapshots.sh (the whole window): before, run 1 and run 2, and after:"
        for cp_png in "$out.eng/before-1"/*.png; do
            cp_n=$(basename "$cp_png")
            cp_1=$out.eng/before-2/$cp_n
            cp_2=$out.eng/after/$cp_n
            cp_a=$(pixels "$cp_png")
            cp_c=$(pixels "$cp_1")
            cp_b=$(pixels "$cp_2")
            cp_r=$(verdict "$cp_a" "$cp_c" "$cp_png" "$cp_1")
            cp_s=$(verdict "$cp_a" "$cp_b" "$cp_png" "$cp_2")
            cp_t=$(verdict "$cp_c" "$cp_b" "$cp_1" "$cp_2")
            echo "  $cp_n  before-1 vs before-2: $cp_r; after vs before-1: $cp_s; after vs before-2: $cp_t"
            echo "    $cp_a  $cp_c  $cp_b"
            if [ "$cp_r$cp_s$cp_t" != samesamesame ]; then
                mkdir -p regressions/engineer-differ
                for cp_w in before-1 before-2 after; do
                    ffmpeg -v error -y -i "$out.eng/$cp_w/$cp_n" -vf "scale=iw/2:ih/2:flags=lanczos" \
                        "regressions/engineer-differ/${cp_n%.png}-$cp_w.png"
                done
            fi
        done
    fi
} > regressions/compare.txt
echo "regressions.sh: done (regressions/compare.txt)"
