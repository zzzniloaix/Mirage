#!/bin/sh
# The timeline panel's scripted QA cases for the owner's answers that WP13b and WP13c put into the
# panel (DECISIONS.md D-124 (a) and (c), D-125 (b) 6; eval/phase3-survey/ui.md §5.3), in the
# method of eval/timeline/ui-qa-2026-11-16/run.sh, which made WP13's record. Written, not run: its
# cases open the player's windows, and the owner gives the time for such runs.
#
# usage: sh eval/timeline/ui-qa-followups.sh <build dir> <out>
#
# <build dir> is a debug build of this checkout with mirage, mirage_vmaf and mirage_tests; <out> a
# new or empty directory outside the repository, beside which the script makes <out>.home and
# <out>.full; it refuses any of the three that exists and isn't an empty directory, so that a run
# never mixes with another's. The script
# 1. runs the acceptance test case of §3.2's two tests and the composite example's case (test 3)
#    with MIRAGE_KEEP_MEDIA=<out>/media, as run.sh's step 1 does, which keeps D-069's ladders,
#    their reports and their documents there, and the composite example's ladder with its
#    session's files (the design's P-U);
# 2. case inside-ad (D-124 (c), Q93 (c)): on D-069's ladder whose ad, segments 3 and 4, restarts
#    its timestamps at the main content's origin, so that its PTS repeat those of the main
#    content's first 2 s, builds the ladder's document as run.sh does (T = 6, W = 50, N = 12,
#    D-077), and plays the master with --timeline, which opens the panel (D-124 (d)); the panel
#    is closed 0.5 s after the first frame and opened again at 3.4 s (--speed-seq panel:E, as the
#    key does), inside the ad, which plays from 3 s to 5 s after the first frame at 1x, so that it
#    first sees the player's frames there; paused at 3.8 s, still inside the ad. The report, 7 s
#    after launch, and the snapshot, 8 s after the first frame, show no playhead and the line that
#    says why: the frame's PTS lies in two sequences, and nothing has told the panel which;
# 3. case unscored (D-124 (a), Q91 (b)): the composite example has no span that isn't scored: its
#    plan plays entries 1, 2 and 1, each a pair's variant in top-rung mode. A span that plays
#    entry 0, the top rung, which is the reference in top-rung mode and has no score, would make
#    one (D-105 (b)). So the case writes a plan of its own, unscored/plan.json, the composite
#    example's with its ad, segments 3 and 4, played by entry 0, builds the session's document
#    from the composite example's ladder report with it, its files not fetched (--fetch off), and
#    plays that ladder's master with the document: the session's curve alone (--ui-timeline
#    curves=session:0), the view zoomed to 2-6 s, the span, 3-5 s, in its middle; paused at 2.5 s;
#    at 3 s a pick at 4 s, inside the span, which picks nothing, and at 3.5 s one at 5.5 s, after
#    it, which picks the session's frame there and seeks to it. The report lists where the curve
#    was drawn and the span that isn't scored, and the snapshot shows no curve over the span;
# 4. case step-back (D-125 (b) 6, Q99 6 (a)): on inside-ad's ladder, its document built the same
#    way, the panel open from the launch; paused at 0.5 s; at 1 s a seek by --speed-seq's seek:
#    to position 2 s, the start of the segment before the ad, which the panel offers for the ad's
#    frames (D-114 (b)), since no seek by PTS reaches them, and from which the playhead is known;
#    25 frame steps forward from 2 s, a tenth of a second apart, to the ad's first frame, where
#    the PTS step back and the playhead follows into the ad's sequence (D-046); a step back at
#    5 s, from the ad's first frame, for which main.cpp asks the scrub decoder for a PTS before
#    the stream's start. Run run: the report at 8 s after launch shows the playhead on the frame
#    on screen, or none where the step's landing can't be told, as the tracker gives it for that
#    landing; the snapshot at 9 s after the first frame. Run fwd: the same, then 50 frame steps
#    forward from 6 s, 0.08 s apart, which show the playback pipeline's next frames, the step
#    back having left it where it was, to the main content's first frame after the ad; the
#    report at 13 s after launch shows the playhead on that frame; the snapshot at 14 s;
# 5. answers the query of the unscored case's card, its second pick's, with mirage_vmaf
#    attribute, the frames of the card's interval (--frames), for check_ui_followups.py to
#    compare with the card line by line, as run.sh's step 5 does;
# 6. keeps each snapshot's lower half, the panel, the seek bar and the controls, at half size, and
#    moves the full-size captures to <out>.full, as run.sh's step 7 does;
# 7. checks the reports and the logs (check_ui_followups.py), notes the checkout's state at the
#    end and, last, replaces the local directories in every file of <out> but the media segments
#    and searches them, as P-011 has the eval scripts do (eval/timeline/check.py redact).
# Every player run passes --qa-no-focus and --window 1280x720, with HOME at <out>.home (the player
# keeps its recent list there; a snapshot run neither reads nor writes prefs.txt), under caffeinate
# -i and a perl alarm of 60 s, as run.sh's player() starts it; each runs in the foreground, from
# <out>, and ends with its last snapshot, or the alarm. D-069's ladders carry no audio, so the
# video follows its own clock, the screen locked or not. --timeline-report counts seconds after
# launch, --speed-seq and --ui-timeline's picks seconds after the first frame: each report is due
# before its run's last snapshot, which comes at least as late after launch. POSIX sh.
set -u

die() {
    echo "ui-qa-followups.sh: $*" >&2
    exit 2
}

[ $# -eq 2 ] || { echo "usage: sh ui-qa-followups.sh <build dir> <out>" >&2; exit 2; }
here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P) || die "can't find the script's directory"
repo=$(git -C "$here" rev-parse --show-toplevel 2>/dev/null) || die "$here isn't in a git repository"
repo=$(CDPATH='' cd -- "$repo" && pwd -P) || die "can't enter $repo"
build=$(CDPATH='' cd -- "$1" 2>/dev/null && pwd -P) || die "no build directory $1"
for prog in "$build/mirage" "$build/mirage_vmaf" "$build/mirage_tests"; do
    [ -x "$prog" ] || die "no $prog"
done
# A new or empty directory, never one a run has used.
fresh() {
    if [ -e "$1" ]; then
        [ -d "$1" ] || die "$1 isn't a directory"
        [ -z "$(ls -A -- "$1")" ] || die "$1 isn't empty: the script doesn't reuse a directory"
    fi
}
fresh "$2"
mkdir -p -- "$2" || die "can't make $2"
out=$(CDPATH='' cd -- "$2" && pwd -P) || die "can't enter $2"
case $out in "$repo" | "$repo"/*) die "$2 is inside the repository" ;; esac
home=$out.home
full=$out.full
fresh "$home"
fresh "$full"
mkdir -p -- "$home" "$full" || die "can't make $home or $full"
cd -- "$out" || die "can't enter $out"
cases=media/attribution_acceptance__a_degraded_segment_and_an_ad_boundary
example=media/attribution_acceptance__test_3__the_work_order_s_example_on_a_scripted_session_whose_ad_segment_is_answered_late
failed=''

# A word as cmd.txt shows it: the run's directories as placeholders, quoted where a shell would
# need it (as run.sh shows them).
shown() {
    sw_w=$1
    for sw_pl in "out=$out" "build=$build" "repo=$repo"; do
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

# player <binary> <log> <arguments>...: one window, under the run rules above (run.sh's player()).
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
# into the report's name with .attribute.txt for .txt: the frames of the card's interval, the
# pick being outside the document's drops (run.sh's answer(), its --frames branch).
answer() {
    an_rep=$1
    an_doc=$2
    an_out=${an_rep%.txt}.attribute.txt
    an_curve=$(sed -n 's/^  Curve \([^ ]*\) (.*/\1/p' "$an_rep" | sed -n '1p')
    an_f=$(sed -n 's/^  reference frames \([0-9]*-[0-9]*\) (.*/\1/p' "$an_rep" | sed -n '1p')
    if [ -z "$an_curve" ] || [ -z "$an_f" ]; then
        failed="$failed${failed:+; }$an_out (no pick or no interval in $an_rep)"
    else
        run "$an_out" "$build/mirage_vmaf" attribute --timeline "$an_doc" --curve "$an_curve" --frames "$an_f"
    fi
}

sha256() { shasum -a 256 -- "$1" | cut -d ' ' -f 1; }

# What the run is made with: the checkout, the machine, the programs (run.sh's environment()).
environment() {
    echo "# The environment of ui-qa-followups.sh's run: <repo> is the checkout, <build> its debug"
    echo "# build, <out> the output directory."
    echo "\$ git -C <repo> rev-parse HEAD   (start of the run)"
    git -C "$repo" rev-parse HEAD
    echo
    echo "\$ git -C <repo> status --porcelain   (start of the run)"
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
    echo "\$ python3 -I -B --version   (the interpreter of check_ui_followups.py and check.py)"
    python3 -I -B --version
    echo
    echo "\$ ffmpeg -version   (its first line)"
    ffmpeg -version | sed -n '1p'
    echo
    for env_p in "$build/mirage" "$build/mirage_vmaf" "$build/mirage_tests"; do
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

printf '%s\n' "# The commands of ui-qa-followups.sh's run, from <out>, each with its exit status; <home> is" \
    "# <out>.home, the players' HOME." > cmd.txt
environment > environment.txt 2>&1

# 1. The ladders, their reports and documents, as run.sh's step 1. The composite example's case
# by a wildcard: Catch2 takes a comma in a test spec for an "or".
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

# 2. Case inside-ad: the panel first sees the player's frames inside the ad whose timestamps
# restart at the main content's origin. --timeline opens it at launch; panel:E closes it at
# 0.5 s, before the main content's first 2 s are out, and opens it again at 3.4 s, inside the ad;
# paused at 3.8 s, inside the ad; the report at 7 s after launch, the snapshot at 8 s after the
# first frame. No seek, no switch, no jump of the PTS meanwhile.
mkdir -p inside-ad
run inside-ad/timeline.log "$build/mirage_vmaf" timeline --report "$cases/ad-main-origin.json" \
    --out inside-ad/timeline.json --threshold 6 --window 50 --min-points 12
player "$build/mirage" inside-ad/run.log "$cases/ad-main-origin/master.m3u8" --timeline inside-ad/timeline.json \
    --speed-seq "panel:E@0.5,panel:E@3.4,pause@3.8,snap@8" --ui-snapshot inside-ad/run.png 0 \
    --timeline-report inside-ad/run.txt 7

# 3. Case unscored: the composite example's plan with its ad played by entry 0, the reference in
# top-rung mode, which makes the span over the ad one that isn't scored (D-105 (b)); its document
# from the composite example's ladder report, the session's files not fetched. Then the session's
# curve alone, zoomed to 2-6 s; paused at 2.5 s; picks at 4 s (inside the span) at 3 s and at
# 5.5 s (after it) at 3.5 s; the report at 7 s after launch, the snapshot at 8 s after the first
# frame.
mkdir -p unscored
cat > unscored/plan.json <<'EOF'
{"schema": "mirage-session-plan", "schema_version": 1, "name": "the composite example, its ad played by the reference",
 "spans": [{"entry": 1, "first_seq": 0, "last_seq": 2},
           {"entry": 0, "first_seq": 3, "last_seq": 4},
           {"entry": 1, "first_seq": 5, "last_seq": 7}]}
EOF
echo "# unscored/plan.json written by the script: the composite example's plan, its ad played by entry 0" >> cmd.txt
run unscored/timeline.log "$build/mirage_vmaf" timeline --report "$example/ad-main-origin.session.json" \
    --out unscored/timeline.json --session unscored/plan.json --fetch off --threshold 6 --window 50 --min-points 12
player "$build/mirage" unscored/run.log "$example/ad-main-origin/master.m3u8" --timeline unscored/timeline.json \
    --ui-timeline "zoom=2:6,curves=session:0,pick=4@3,pick=5.5@3.5" --speed-seq "pause@2.5,snap@8" \
    --ui-snapshot unscored/run.png 0 --timeline-report unscored/run.txt 7

# 4. Case step-back: paused, a seek to the segment before the ad, frame steps forward to the ad's
# first frame, then a step back from it; run fwd, frame steps forward after it. --speed-seq's own
# events alone (src/main.cpp, SpeedStep): pause, seek:, fwd, back and snap.
mkdir -p step-back
run step-back/timeline.log "$build/mirage_vmaf" timeline --report "$cases/ad-main-origin.json" \
    --out step-back/timeline.json --threshold 6 --window 50 --min-points 12
# fwd_steps <count> <first> <apart>: <count> frame steps forward, the first <first> seconds after the
# first frame, <apart> seconds apart, as --speed-seq's events.
fwd_steps() {
    awk -v n="$1" -v t0="$2" -v dt="$3" \
        'BEGIN { for (i = 0; i < n; i++) printf "%sfwd@%.2f", (i ? "," : ""), t0 + i * dt }'
}
to_ad=$(fwd_steps 25 2 0.1)
after=$(fwd_steps 50 6 0.08)
player "$build/mirage" step-back/run.log "$cases/ad-main-origin/master.m3u8" --timeline step-back/timeline.json \
    --speed-seq "pause@0.5,seek:2@1,$to_ad,back@5,snap@9" --ui-snapshot step-back/run.png 0 \
    --timeline-report step-back/run.txt 8
player "$build/mirage" step-back/fwd.log "$cases/ad-main-origin/master.m3u8" --timeline step-back/timeline.json \
    --speed-seq "pause@0.5,seek:2@1,$to_ad,back@5,$after,snap@14" --ui-snapshot step-back/fwd.png 0 \
    --timeline-report step-back/fwd.txt 13

# 5. The query of the unscored case's card.
answer unscored/run.txt unscored/timeline.json
{
    echo
    echo "\$ pgrep -f VTDecoderXPCService | wc -l   (after the windows)"
    pgrep -f VTDecoderXPCService | wc -l | tr -d ' '
} >> environment.txt

# 6. The snapshots for review: lower halves at half size, the full-size captures moved.
mkdir -p snapshots
for sn_p in inside-ad/run unscored/run step-back/run step-back/fwd; do
    sn_n=$(printf '%s' "$sn_p" | tr / -)
    run "snapshots/$sn_n.ffmpeg.log" ffmpeg -v error -y -i "$sn_p.png" \
        -vf "crop=iw:ih/2:0:ih/2,scale=iw/2:ih/2:flags=lanczos" "snapshots/$sn_n.png"
done
for mv_p in inside-ad/run.png unscored/run.png step-back/run.png step-back/fwd.png; do
    [ -f "$mv_p" ] || continue
    mkdir -p "$full/$(dirname "$mv_p")"
    mv -- "$mv_p" "$full/$mv_p"
done
echo "# the full-size captures moved to <out>.full" >> cmd.txt

# 7. The checks, the checkout's state at the end, then the replacement of the local directories
# and the last search.
printf '$ python3 -I -B %s %s > checks.txt 2>&1\n' "$(shown "$here/check_ui_followups.py")" "$(shown "$out")" \
    >> cmd.txt
python3 -I -B "$here/check_ui_followups.py" "$out" > checks.txt 2>&1
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

[ -z "$failed" ] || echo "ui-qa-followups.sh: these commands failed: $failed" >&2
[ "$check_st" -eq 0 ] || { echo "ui-qa-followups.sh: check_ui_followups.py found a difference (checks.txt)" >&2; exit 1; }
[ -z "$failed" ] || exit 1
[ "$search_st" -eq 0 ] || { echo "ui-qa-followups.sh: something local is left (search.txt): don't commit it" >&2; exit 3; }
echo "ui-qa-followups.sh: PASS"
