#!/bin/sh
# The timeline panel's scripted QA run (eval/phase3-survey/ui.md §5.3; DECISIONS.md D-114; work
# package WP13), as this directory's record was made. See README.md.
#
# usage: sh eval/timeline/ui-qa-2026-11-16/run.sh <build dir> <before dir> <out>
#
# <build dir> is a debug build of this checkout with mirage, mirage_vmaf and mirage_tests;
# <before dir> holds `mirage`, the player built from the commit before the panel's (the GL tag
# ruler's last commit), for the ruler's before-and-after pair; <out> a new or empty directory
# outside the repository. The script
# 1. runs the acceptance test case of §3.2's two tests and the composite example's case (test 3)
#    with MIRAGE_KEEP_MEDIA=<out>/media, which keeps D-069's ladders, their reports and their
#    documents there, and the composite example's ladder with its session's documents (the
#    design's P-U);
# 2. builds each acceptance ladder's timeline document with mirage_vmaf timeline, detection's
#    parameters as the tests and eval/timeline/run_all.sh pin them, T = 6, W = 50, N = 12 (D-077);
# 3. plays each ladder twice with the panel open on its document: run 1 picks the document's
#    attribution 0 (its detected drop) while paused, on the variant the player starts on; run 2
#    switches to the drop's rung first (--qa-switch), zooms to 2-6 s, shows that rung's curve
#    alone and picks one frame by its position; each writes the panel's report
#    (--timeline-report) and a snapshot (--speed-seq snap); the ladder whose ad restarts at the
#    main content's origin a third time, its drop picked in the ad, then switched to the drop's
#    rung inside the ad and paused after the switch, before the ad plays again;
# 4. plays the composite example once, its document the session's, picking the session's drop,
#    the document's attribution 1, while paused;
# 5. answers the query of each pick's card with mirage_vmaf attribute, the document's drop the
#    card names (--drop, counted along its curve) or the frames of the card's interval
#    (--frames), for check_ui.py to compare with the card line by line;
# 6. makes six runs more: E and Esc on the panel, the overlays popover and the help card with
#    their new rows, a pick while a seek-bar drag holds the seeks, and a variant's media playlist
#    opened alone with the network sidebar open, with this player and with <before dir>'s, whose
#    GL tag ruler this one retires (D-114 (e));
# 7. keeps each snapshot's lower half, the panel, the seek bar and the controls, at half size
#    (the popover and the help card whole, at half size), and moves the full-size captures to
#    <out>.full;
# 8. checks the reports and the logs (check_ui.py), notes the checkout's state at the end and,
#    last, replaces the local directories in every file of <out> but the media segments and
#    searches them, as P-011 has the eval scripts do (eval/timeline/check.py redact).
# Every player run passes --qa-no-focus and --window 1280x720, with HOME at <out>.home (the
# player keeps its recent list there), under caffeinate -i and a perl alarm of 60 s, as
# eval/baseline/run_baseline.sh starts it; each runs in the foreground, from <out>, and ends with
# its last snapshot, or the alarm. --timeline-report counts seconds after launch, --speed-seq,
# --qa-switch and --ui-timeline's picks seconds after the first frame: each report is due before
# its run's last snapshot, which comes at least as late after launch. POSIX sh.
set -u

die() {
    echo "run.sh: $*" >&2
    exit 2
}

[ $# -eq 3 ] || { echo "usage: sh run.sh <build dir> <before dir> <out>" >&2; exit 2; }
here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P) || die "can't find the script's directory"
repo=$(git -C "$here" rev-parse --show-toplevel 2>/dev/null) || die "$here isn't in a git repository"
repo=$(CDPATH='' cd -- "$repo" && pwd -P) || die "can't enter $repo"
build=$(CDPATH='' cd -- "$1" 2>/dev/null && pwd -P) || die "no build directory $1"
before=$(CDPATH='' cd -- "$2" 2>/dev/null && pwd -P) || die "no directory $2"
for prog in "$build/mirage" "$build/mirage_vmaf" "$build/mirage_tests" "$before/mirage"; do
    [ -x "$prog" ] || die "no $prog"
done
if [ -e "$3" ]; then
    [ -d "$3" ] || die "$3 isn't a directory"
    [ -z "$(ls -A -- "$3")" ] || die "$3 isn't empty"
fi
mkdir -p -- "$3" || die "can't make $3"
out=$(CDPATH='' cd -- "$3" && pwd -P) || die "can't enter $3"
case $out in "$repo"/*) die "$3 is inside the repository" ;; esac
home=$out.home
full=$out.full
mkdir -p -- "$home" "$full" || die "can't make $home or $full"
cd -- "$out" || die "can't enter $out"
cases=media/attribution_acceptance__a_degraded_segment_and_an_ad_boundary
example=media/attribution_acceptance__test_3__the_work_order_s_example_on_a_scripted_session_whose_ad_segment_is_answered_late
failed=''

# A word as cmd.txt shows it: the run's directories as placeholders, quoted where a shell would
# need it (as eval/timeline/run_all.sh shows them).
shown() {
    sw_w=$1
    for sw_pl in "out=$out" "build=$build" "before=$before" "repo=$repo"; do
        sw_nm=${sw_pl%%=*}
        sw_pp=${sw_pl#*=}
        case $sw_w in
            "$sw_pp") sw_w="<$sw_nm>"; break ;;
            "$sw_pp"/*) sw_w="<$sw_nm>${sw_w#"$sw_pp"}"; break ;;
            *"=$sw_pp"/*) sw_w="${sw_w%%=*}=<$sw_nm>${sw_w#*="$sw_pp"}"; break ;;
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

# run <log> <command>...: the command from <out>, its output into <log>, and the command and its
# exit status into cmd.txt.
run() {
    run_lg=$1
    shift
    printf '$ %s > %s 2>&1\n' "$(shown_line "$@")" "$run_lg" >> cmd.txt
    "$@" > "$run_lg" 2>&1
    run_st=$?
    echo "# exit status $run_st" >> cmd.txt
    [ "$run_st" -eq 0 ] || failed="$failed${failed:+; }$run_lg"
    return $run_st
}

# player <binary> <log> <arguments>...: one window, under the run rules above.
player() {
    pl_bin=$1
    pl_lg=$2
    shift 2
    printf '$ HOME=%s caffeinate -i perl -e %s 60 %s --qa-no-focus --window 1280x720 %s > %s 2>&1\n' \
        "$(shown "$home")" "'alarm shift; exec @ARGV'" "$(shown "$pl_bin")" "$(shown_line "$@")" "$pl_lg" \
        >> cmd.txt
    HOME=$home caffeinate -i perl -e 'alarm shift; exec @ARGV' 60 "$pl_bin" --qa-no-focus --window 1280x720 \
        "$@" > "$pl_lg" 2>&1
    pl_st=$?
    echo "# exit status $pl_st" >> cmd.txt
    [ "$pl_st" -eq 0 ] || failed="$failed${failed:+; }$pl_lg"
}

# answer <report> <document>: mirage_vmaf attribute's answer to the query of the report's card,
# into the report's name with .attribute.txt for .txt: the document's drop the card names,
# counted along the picked curve as --drop counts with --curve, from the report's list of the
# document's attributions; else the frames of the card's interval.
answer() {
    an_rep=$1
    an_doc=$2
    an_out=${an_rep%.txt}.attribute.txt
    an_curve=$(sed -n 's/^  Curve \([^ ]*\) (.*/\1/p' "$an_rep" | sed -n '1p')
    an_n=$(sed -n "s/^  In the document's attribution \([0-9]*\), a detected drop:\$/\1/p" "$an_rep" | sed -n '1p')
    if [ -z "$an_curve" ]; then
        failed="$failed${failed:+; }$an_out (no pick in $an_rep)"
    elif [ -n "$an_n" ]; then
        an_k=$(awk -v n="$an_n" -v c="$an_curve," '/^  [0-9]+: a detected drop of / {
                i = $1; sub(":", "", i); if ($6 == c && i + 0 <= n + 0) k++ } END { print k + 0 }' "$an_rep")
        run "$an_out" "$build/mirage_vmaf" attribute --timeline "$an_doc" --curve "$an_curve" --drop "$an_k"
    else
        an_f=$(sed -n 's/^  reference frames \([0-9]*-[0-9]*\) (.*/\1/p' "$an_rep" | sed -n '1p')
        run "$an_out" "$build/mirage_vmaf" attribute --timeline "$an_doc" --curve "$an_curve" --frames "$an_f"
    fi
}

sha256() { shasum -a 256 -- "$1" | cut -d ' ' -f 1; }

# What the run is made with: the checkout, the machine, the programs. The change is in the
# working tree on the package's commit: each changed file's git hash-object is its blob in the
# commit that brings this record up to date, which is folded into the package's commit.
environment() {
    echo "# The environment of run.sh's run: <repo> is the checkout, <build> its debug build,"
    echo "# <before> the player of the commit before, <out> the output directory."
    echo "\$ git -C <repo> rev-parse HEAD   (start of the run)"
    git -C "$repo" rev-parse HEAD
    echo
    echo "\$ git -C <repo> status --porcelain   (start of the run: the change, in the working tree)"
    git -C "$repo" status --porcelain
    echo
    echo "\$ git -C <repo> hash-object <each changed or new file of src/>"
    git -C "$repo" status --porcelain -- src | cut -c 4- | while read -r env_f; do
        printf '%s  %s\n' "$(git -C "$repo" hash-object -- "$env_f")" "$env_f"
    done
    echo
    echo "\$ sw_vers"
    sw_vers
    echo
    echo "\$ uname -srm"
    uname -srm
    echo
    echo "\$ sysctl -n machdep.cpu.brand_string hw.ncpu vm.loadavg"
    sysctl -n machdep.cpu.brand_string hw.ncpu vm.loadavg
    echo
    echo "\$ python3 -I -B --version   (the interpreter of check_ui.py and check.py)"
    python3 -I -B --version
    echo
    echo "\$ ffmpeg -version   (its first line)"
    ffmpeg -version | sed -n '1p'
    echo
    for env_p in "$build/mirage" "$build/mirage_vmaf" "$build/mirage_tests" "$before/mirage"; do
        echo "\$ $(shown "$env_p")"
        echo "sha256: $(sha256 "$env_p")"
        echo "size: $(wc -c < "$env_p" | tr -d ' ') bytes"
        echo
    done
    echo "\$ pgrep -f VTDecoderXPCService | wc -l   (before the windows)"
    pgrep -f VTDecoderXPCService | wc -l | tr -d ' '
    echo
    echo "\$ ioreg -n Root -d1 -a | plutil -extract IOConsoleUsers xml1 -o - - | grep -A1 CGSSessionScreenIsLocked"
    ioreg -n Root -d1 -a | plutil -extract IOConsoleUsers xml1 -o - - | grep -A1 CGSSessionScreenIsLocked |
        sed -n '2p' | tr -d '\t '
    echo "(no audio in D-069's ladders: the video follows its own clock, locked screen or not)"
}

printf '%s\n' "# The commands of run.sh's run, from <out>, each with its exit status; <home> is" \
    "# <out>.home, the players' HOME." > cmd.txt
environment > environment.txt 2>&1

# 1. The ladders, their reports and the acceptance tests' documents (the design's P-U). The
# composite example's case by a wildcard: Catch2 takes a comma in a test spec for an "or".
mkdir -p tests
for ts_case in "attribution acceptance: a degraded segment and an ad boundary" "attribution acceptance: test 3*"; do
    ts_log=tests/mirage_tests.txt
    case $ts_case in *"test 3"*) ts_log=tests/mirage_tests_example.txt ;; esac
    printf '$ MIRAGE_KEEP_MEDIA=<out>/media %s > %s 2>&1\n' "$(shown_line "$build/mirage_tests" "$ts_case")" \
        "$ts_log" >> cmd.txt
    MIRAGE_KEEP_MEDIA=$out/media "$build/mirage_tests" "$ts_case" > "$ts_log" 2>&1
    ts_st=$?
    echo "# exit status $ts_st" >> cmd.txt
    [ "$ts_st" -eq 0 ] || failed="$failed${failed:+; }$ts_log"
done

# 2. Their timeline documents (D-077's parameters, as run_all.sh pins them).
for ladder in degraded ad-main-origin ad-from-1s ad-continuous; do
    mkdir -p "$ladder"
    run "$ladder/timeline.log" "$build/mirage_vmaf" timeline --report "$cases/$ladder.json" \
        --out "$ladder/timeline.json" --threshold 6 --window 50 --min-points 12
done

# 3. Two runs a ladder: run 1 on the variant the player starts on, its attribution 0 picked at
# 3 s, paused from 2 s, so that a seek shows its frame and logs where it landed; run 2 switched
# at 0.5 s to the rung whose curve holds the drop (the entry its variants= names), zoomed, a
# frame picked by its position, the middle of its span, at 3.5 s, paused from 2.5 s. The report
# at 7 s after launch, before the snapshot, 8 s after the first frame, ends the run. Each pick's
# query answered by mirage_vmaf attribute (5.).
ladder_runs() {
    lr_l=$1
    lr_h=$2
    lr_e=$3
    lr_p=$4
    player "$build/mirage" "$lr_l/run1.log" "$cases/$lr_l/master.m3u8" --timeline "$lr_l/timeline.json" \
        --ui-timeline "fit=1,variants=all,pick=attribution:0@3" --speed-seq "pause@2,snap@8" \
        --ui-snapshot "$lr_l/run1.png" 0 --timeline-report "$lr_l/run1.txt" 7
    answer "$lr_l/run1.txt" "$lr_l/timeline.json"
    player "$build/mirage" "$lr_l/run2.log" "$cases/$lr_l/master.m3u8" --timeline "$lr_l/timeline.json" \
        --qa-switch "v$lr_h@0.5" --ui-timeline "zoom=2:6,variants=$lr_e,pick=$lr_p@3.5" \
        --speed-seq "pause@2.5,snap@8" --ui-snapshot "$lr_l/run2.png" 0 --timeline-report "$lr_l/run2.txt" 7
    answer "$lr_l/run2.txt" "$lr_l/timeline.json"
}
# The degraded segment is v270's (entry 1) segment 4, frames 100-124; the ads are frames 75-124,
# the low rung's (v180, entry 2) drop. Run 2's frames: 124, the degraded segment's last; 130,
# after the ad; 74, the last frame before it.
ladder_runs degraded 270 1 4.98
ladder_runs ad-main-origin 180 2 5.22
ladder_runs ad-from-1s 180 2 5.22
ladder_runs ad-continuous 180 2 2.98

# Run 3 of the ad from the main content's origin: its drop picked at 2.5 s, in the ad, on the
# top rung, the decision an offer; at 3.3 s the switch to the drop's rung, inside the ad, which
# the Demuxer repositions by PTS into the main content, the decision then made again for that
# rung; paused at 5 s, after the switch's first frame and before the ad plays again. The report
# at 10 s after launch, the snapshot at 11 s after the first frame.
player "$build/mirage" ad-main-origin/run3.log "$cases/ad-main-origin/master.m3u8" \
    --timeline ad-main-origin/timeline.json --qa-switch v180@3.3 \
    --ui-timeline "fit=1,variants=all,pick=attribution:0@2.5" --speed-seq "pause@5,snap@11" \
    --ui-snapshot ad-main-origin/run3.png 0 --timeline-report ad-main-origin/run3.txt 10
answer ad-main-origin/run3.txt ad-main-origin/timeline.json

# 4. The composite example: the session's drop, the document's attribution 1, picked at 3 s,
# paused from 2 s, on the variant the player starts on; its query answered (5.).
mkdir -p session
player "$build/mirage" session/run1.log "$example/ad-main-origin/master.m3u8" \
    --timeline "$example/ad-main-origin.session.timeline.json" \
    --ui-timeline "fit=1,variants=all,pick=attribution:1@3" --speed-seq "pause@2,snap@8" \
    --ui-snapshot session/run1.png 0 --timeline-report session/run1.txt 7
answer session/run1.txt "$example/ad-main-origin.session.timeline.json"

# 6. The keys, the menus, a pick under a seek-bar drag, and the ruler.
mkdir -p extras
player "$build/mirage" extras/keys.log "$cases/ad-main-origin/master.m3u8" --timeline ad-main-origin/timeline.json \
    --speed-seq "panel:E@1,panel:E@2,esc@3,snap@4" --ui-snapshot extras/keys.png 0
player "$build/mirage" extras/overlays.log "$cases/ad-main-origin/master.m3u8" \
    --timeline ad-main-origin/timeline.json --ui-menu overlays --ui-snapshot extras/overlays.png 30
player "$build/mirage" extras/help.log "$cases/ad-main-origin/master.m3u8" \
    --timeline ad-main-origin/timeline.json --ui-menu help --ui-snapshot extras/help.png 30
# The drop of the ad with continuous timestamps, which a seek reaches, picked at 3 s while a
# seek-bar drag, from 2.5 s to its release at 4 s, holds the seeks: the panel asks for none.
player "$build/mirage" extras/drag.log "$cases/ad-continuous/master.m3u8" --timeline ad-continuous/timeline.json \
    --ui-timeline "variants=all,pick=attribution:0@3" --speed-seq "pause@2,scrub:6@2.5,release@4,snap@8" \
    --ui-snapshot extras/drag.png 0 --timeline-report extras/drag.txt 7.5
for rl_bin in "$before" "$build"; do
    rl_n=after
    [ "$rl_bin" = "$before" ] && rl_n=before
    # A media playlist opened alone, by the URL the report gives its variant: the manifest's
    # tags are this playlist's, so the old player draws its ruler with the sidebar open.
    if [ "$rl_n" = before ]; then
        player "$rl_bin/mirage" "extras/ruler-$rl_n.log" "$out/$cases/ad-main-origin/v180/index.m3u8" \
            --timeline ad-main-origin/timeline.json --ui-net tab=manifest --speed-seq "pause@2,snap@3" \
            --ui-snapshot "extras/ruler-$rl_n.png" 0
    else
        player "$rl_bin/mirage" "extras/ruler-$rl_n.log" "$out/$cases/ad-main-origin/v180/index.m3u8" \
            --timeline ad-main-origin/timeline.json --ui-net tab=manifest --speed-seq "pause@2,snap@3" \
            --ui-snapshot "extras/ruler-$rl_n.png" 0 --timeline-report "extras/ruler-$rl_n.txt" 3
    fi
done
{
    echo
    echo "\$ pgrep -f VTDecoderXPCService | wc -l   (after the windows)"
    pgrep -f VTDecoderXPCService | wc -l | tr -d ' '
} >> environment.txt

# 7. The snapshots for review: lower halves at half size, the popover and the card whole.
mkdir -p snapshots
for sn_p in degraded/run1 degraded/run2 ad-main-origin/run1 ad-main-origin/run2 ad-main-origin/run3 \
    ad-from-1s/run1 ad-from-1s/run2 ad-continuous/run1 ad-continuous/run2 session/run1 extras/keys \
    extras/drag extras/ruler-before extras/ruler-after; do
    sn_n=$(printf '%s' "$sn_p" | tr / -)
    run "snapshots/$sn_n.ffmpeg.log" ffmpeg -v error -y -i "$sn_p.png" \
        -vf "crop=iw:ih/2:0:ih/2,scale=iw/2:ih/2:flags=lanczos" "snapshots/$sn_n.png"
done
for sn_p in extras/overlays extras/help; do
    sn_n=$(printf '%s' "$sn_p" | tr / -)
    run "snapshots/$sn_n.ffmpeg.log" ffmpeg -v error -y -i "$sn_p.png" -vf "scale=iw/2:ih/2:flags=lanczos" \
        "snapshots/$sn_n.png"
done
for mv_p in */run1.png */run2.png */run3.png extras/*.png; do
    [ -f "$mv_p" ] || continue
    mkdir -p "$full/$(dirname "$mv_p")"
    mv -- "$mv_p" "$full/$mv_p"
done
echo "# the full-size captures moved to <out>.full" >> cmd.txt

# 8. The checks, the checkout's state at the end, then the replacement of the local directories
# and the last search.
printf '$ python3 -I -B %s %s > checks.txt 2>&1\n' "$(shown "$here/check_ui.py")" "$(shown "$out")" >> cmd.txt
python3 -I -B "$here/check_ui.py" "$out" > checks.txt 2>&1
check_st=$?
echo "# exit status $check_st" >> cmd.txt
{
    echo
    echo "\$ git -C <repo> rev-parse HEAD   (end of the run)"
    git -C "$repo" rev-parse HEAD
    echo
    echo "\$ git -C <repo> status --porcelain   (end of the run)"
    git -C "$repo" status --porcelain
} >> environment.txt
printf '$ python3 -I -B %s redact %s --build %s --repo %s > redact.txt 2>&1\n' "$(shown "$repo/eval/timeline/check.py")" \
    "$(shown "$out")" "$(shown "$build")" "$(shown "$repo")" >> cmd.txt
python3 -I -B "$repo/eval/timeline/check.py" redact "$out" --build "$build" --repo "$repo" > redact.txt 2>&1
echo "# exit status $?" >> cmd.txt
printf '$ python3 -I -B %s redact %s --search-only --build %s --repo %s > search.txt 2>&1\n' \
    "$(shown "$repo/eval/timeline/check.py")" "$(shown "$out")" "$(shown "$build")" "$(shown "$repo")" >> cmd.txt
python3 -I -B "$repo/eval/timeline/check.py" redact "$out" --search-only --build "$build" --repo "$repo" \
    > search.txt 2>&1
search_st=$?
echo "# exit status $search_st (the last line of this file: the search ran before it was written)" >> cmd.txt

[ -z "$failed" ] || echo "run.sh: these commands failed: $failed" >&2
[ "$check_st" -eq 0 ] || { echo "run.sh: check_ui.py found a difference (checks.txt)" >&2; exit 1; }
[ -z "$failed" ] || exit 1
[ "$search_st" -eq 0 ] || { echo "run.sh: something local is left (search.txt): don't commit it" >&2; exit 3; }
echo "run.sh: PASS"
