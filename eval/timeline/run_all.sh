#!/bin/sh
# Reproduces Phase 3's three end-to-end results from one command (eval/phase3-survey/design.md
# §9.4, E4): §3.2's two acceptance tests, the degraded segment and the ad boundary in D-080's
# three arrangements of the ad's timestamps, and the work order's example on a scripted session
# (design §9.2, §9.3). See eval/timeline/README.md.
#
# usage: eval/timeline/run_all.sh <build dir> <out>
#
# <build dir> is a build of this repository with mirage_vmaf and mirage_tests (build/macos-debug,
# say); <out> a new or empty directory. The script
# 0. notes whether the build was configured from this checkout and had nothing left to build
#    (ninja -n), before the tests run, since only then does the record's commit name the code
#    that ran;
# 1. runs the acceptance tests' two Catch2 test cases (tests/test_attribution_acceptance.cpp,
#    tests/test_attribution_session.cpp) with MIRAGE_KEEP_MEDIA=<out>/media, which keeps their
#    ladders, ladder reports, plan and documents there (the design's P-U);
# 2. builds a timeline document from each acceptance ladder's report with that build's
#    mirage_vmaf, detection's parameters pinned as the tests pin them, T = 6, W = 50 and N = 12
#    (D-077; the design's P-R), and queries it on each curve with attribute --auto, as JSON and as
#    text;
# 3. builds the composite example's document from its report and plan, fetching the session's
#    files from slow_server.py, which serves the ladder's directory and holds the low rung's first
#    ad segment's response head for the example's 2.3 s (design §6.4, §9.3; the design's P-S),
#    and queries it on each curve, the session's among them;
# 4. replaces the local directories in every file of <out> but the media segments by
#    placeholders and searches them for anything local left, as P-011 has the eval scripts do
#    (check.py redact);
# 5. checks every document and answer against its ladder's layout with check.py;
# 6. searches every file of <out> but the media segments once more, the check's report and the
#    logs' last lines included, once they are written (check.py redact --search-only).
# Each record directory (tests/, one per acceptance ladder, session/) keeps cmd.txt, the commands
# with their exit statuses, and environment.txt, the machine, the programs and the commit, git
# rev-parse HEAD's, at the run's start and end (P-AF, as P-006 has the eval tooling record it),
# with a warning where HEAD changed during the run, the working tree wasn't clean, or the build
# wasn't this checkout's or wasn't current; the script prints each warning too.
#
# Exit status: 0 the tests passed, every command exited 0, check.py found no difference and
# nothing local is left; 1 a test, a command or the check failed (cmd.txt, the logs and
# check.txt say what); 2 bad arguments, or the run could not start; 3 everything passed, but a
# file of <out> still holds local information (search.txt says which): don't commit it.
# POSIX sh.
set -u

# Detection's parameters, as the acceptance tests pin them (D-077; the design's P-R).
T=6
W=50
N=12
# How long slow_server.py holds the late segment's response head: the work order's example's
# 2.3 s (design §6.4, §9.3), under the analyzer's 5 s limit on a network read (D-036).
DELAY=2.3
# The Catch2 test cases of §3.2's tests and of the composite example: both carry these tags.
TESTS='[attribution][acceptance]'

die() {
    echo "run_all.sh: $*" >&2
    exit 2
}

[ $# -eq 2 ] || { echo "usage: eval/timeline/run_all.sh <build dir> <out>" >&2; exit 2; }
here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P) || die "can't find the script's directory"
repo=$(git -C "$here" rev-parse --show-toplevel 2>/dev/null) ||
    die "$here isn't in a git repository"
repo=$(CDPATH='' cd -- "$repo" && pwd -P) || die "can't enter $repo"
build=$(CDPATH='' cd -- "$1" 2>/dev/null && pwd -P) || die "no build directory $1"
for prog in mirage_vmaf mirage_tests; do
    [ -x "$build/$prog" ] || die "$1 has no $prog: build it first (docs/BUILD.md)"
done
command -v python3 >/dev/null 2>&1 || die "no python3 on PATH"
if [ -e "$2" ]; then
    [ -d "$2" ] || die "$2 isn't a directory"
    [ -z "$(ls -A -- "$2")" ] || die "$2 isn't empty: give a new or empty directory"
fi
mkdir -p -- "$2" || die "can't make $2"
out=$(CDPATH='' cd -- "$2" && pwd -P) || die "can't enter $2"
check="$here/check.py"
server=''
failed=''
warnings=''

# A word as cmd.txt shows it: the run's directories as <out>, <build>, <repo> and <home>, and
# quoted where a shell would need it.
shown() {
    _w=$1
    for _pl in "out=$out" "build=$build" "repo=$repo" "home=${HOME:-}"; do
        _nm=${_pl%%=*}
        _pp=${_pl#*=}
        if [ -n "$_pp" ] && [ "$_pp" != / ]; then
            case $_w in
                "$_pp") _w="<$_nm>"; break ;;
                "$_pp"/*) _w="<$_nm>${_w#"$_pp"}"; break ;;
            esac
        fi
    done
    if [ -z "$_w" ] || printf '%s' "$_w" | LC_ALL=C grep -q '[^A-Za-z0-9_./:=@%+,<>-]'; then
        _w="'$(printf '%s' "$_w" | sed "s/'/'\\\\''/g")'"
    fi
    printf '%s' "$_w"
}

# A command's words, as one line of cmd.txt.
shown_line() {
    _sl=''
    for _word in "$@"; do
        _sl="$_sl${_sl:+ }$(shown "$_word")"
    done
    printf '$ %s\n' "$_sl"
}

# run <record dir> <log> <command>...: runs the command from <out>, its stdout and stderr into
# <log>, and writes it and its exit status into the record's cmd.txt; returns the status.
run() {
    _rd=$1
    _lg=$2
    shift 2
    shown_line "$@" >> "$_rd/cmd.txt"
    (cd -- "$out" && "$@") > "$_lg" 2>&1
    _st=$?
    echo "# exit status $_st" >> "$_rd/cmd.txt"
    return $_st
}

# A new record directory under <out>, with its cmd.txt's first line.
record() {
    mkdir -- "$out/$1" || return 1
    printf '%s\n' "# The commands of this record, as eval/timeline/run_all.sh ran them from" \
        "# <out>, each with its exit status; <build> is the build directory environment.txt" \
        "# names." > "$out/$1/cmd.txt"
}

fail() {
    failed="$failed${failed:+; }$*"
}

# A warning, written into environment.txt and printed at the end; it doesn't change the exit
# status.
warn() {
    warnings="$warnings$*
"
}

# Stops slow_server.py with SIGTERM and writes its exit status into session/cmd.txt: 0 after
# SIGTERM, anything else a failure.
stop_server() {
    if [ -n "$server" ]; then
        kill "$server" 2>/dev/null
        wait "$server"
        _ss=$?
        server=''
        echo "# exit status $_ss of slow_server.py (0 once SIGTERM has stopped it)" \
            >> "$out/session/cmd.txt"
        [ "$_ss" -eq 0 ] ||
            fail "session: slow_server.py exited with status $_ss (session/server.stderr.txt)"
    fi
}
trap stop_server EXIT
trap 'stop_server; exit 130' INT
trap 'stop_server; exit 143' TERM

git_status() {
    case $out in
        "$repo"/*) git -C "$repo" status --porcelain -- . ":(exclude)${out#"$repo"/}" ;;
        *) git -C "$repo" status --porcelain ;;
    esac
}

load() {
    if [ "$(uname -s)" = Darwin ]; then sysctl -n vm.loadavg; else cat /proc/loadavg; fi
}

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum -- "$1" | cut -d ' ' -f 1
    else
        shasum -a 256 -- "$1" | cut -d ' ' -f 1
    fi
}

# The first line of standard input that isn't empty.
first_line() {
    sed -n '/./{p;q;}'
}

head_start=$(git -C "$repo" rev-parse HEAD)
status_start=$(git_status)
load_start=$(load)

# Whether the build was configured from this checkout and had nothing left to build, before the
# tests run: only then does the commit the record gives name the code that ran (eval/run_all.py
# builds a clean worktree of the commit for this reason, P-001). Each is a warning.
build_source=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$build/CMakeCache.txt" 2>/dev/null)
if [ -z "$build_source" ]; then
    build_source_note="(not known: no CMAKE_HOME_DIRECTORY in <build>/CMakeCache.txt)"
    warn "the build's source directory isn't known (no CMAKE_HOME_DIRECTORY in its CMakeCache.txt)"
elif [ "$(CDPATH='' cd -- "$build_source" 2>/dev/null && pwd -P)" = "$repo" ]; then
    build_source_note="<repo>"
else
    build_source_note=$build_source
    warn "the build was configured from another source directory than this checkout's" \
        "(environment.txt), so its programs may not be this commit's code"
fi
if [ -f "$build/build.ninja" ] && command -v ninja >/dev/null 2>&1; then
    ninja_out=$(ninja -C "$build" -n 2>&1)
    ninja_status=$?
    ninja_note=$(printf '%s\n' "$ninja_out" | sed -n '$p')
    ninja_steps=$(printf '%s\n' "$ninja_out" | grep -c '^\[')
    steps=steps
    [ "$ninja_steps" -eq 1 ] && steps=step
    if [ "$ninja_status" -ne 0 ]; then
        ninja_note="exit status $ninja_status: $ninja_note"
        warn "whether the build was current isn't known: ninja -C <build> -n exited with status" \
            "$ninja_status (environment.txt)"
    elif [ "$ninja_steps" -gt 0 ]; then
        ninja_note="$ninja_steps $steps to do, the last: $ninja_note"
        warn "the build wasn't current before the tests: ninja -C <build> -n had $ninja_steps" \
            "$steps to do, so its programs may not be this commit's code"
    fi
else
    ninja_note="(not run: no build.ninja in <build>, or no ninja on PATH)"
    warn "whether the build was current isn't known: no build.ninja in it, or no ninja on PATH"
fi

# A command and its output, in environment.txt's form.
note() {
    printf '$ %s\n%s\n\n' "$1" "$2"
}

environment() {
    echo "# Environment of an eval/timeline/run_all.sh run. <build> is the build directory below,"
    echo "# <out> the output directory, <repo> the repository the script is in and <home> the home"
    echo "# directory; the commit is git rev-parse HEAD's, at the run's start and end."
    home=${HOME:-}
    case $build in
        "$repo"/*) echo "<build> = <repo>/${build#"$repo"/}" ;;
        *) if [ -n "$home" ] && [ "$home" != / ] && [ "${build#"$home"/}" != "$build" ]; then
               echo "<build> = <home>/${build#"$home"/}"
           else
               echo "<build> = $build"
           fi ;;
    esac
    echo
    note "git -C <repo> rev-parse HEAD   (start of the run)" "$head_start"
    note "git -C <repo> status --porcelain   (start of the run)" \
        "${status_start:-(no output: the working tree is clean)}"
    note "sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' <build>/CMakeCache.txt   (its source)" \
        "$build_source_note"
    note "ninja -C <build> -n   (before the tests: its last line)" "$ninja_note"
    if [ "$(uname -s)" = Darwin ]; then
        note "sw_vers" "$(sw_vers)"
        note "sysctl -n machdep.cpu.brand_string" "$(sysctl -n machdep.cpu.brand_string)"
        note "sysctl -n hw.ncpu" "$(sysctl -n hw.ncpu)"
        note "sysctl -n vm.loadavg   (start of the run)" "$load_start"
    else
        note "grep PRETTY_NAME /etc/os-release" "$(grep PRETTY_NAME /etc/os-release)"
        note "sed -n '/model name/{p;q;}' /proc/cpuinfo" \
            "$(sed -n '/model name/{p;q;}' /proc/cpuinfo)"
        note "nproc" "$(nproc)"
        note "cat /proc/loadavg   (start of the run)" "$load_start"
    fi
    note "uname -srm" "$(uname -srm)"
    note "python3 -I -B (the interpreter of check.py and slow_server.py)" \
        "$(python3 -I -B -c 'import sys; print(sys.version)')"
    for prog in mirage_vmaf mirage_tests; do
        note "<build>/$prog" "sha256: $(sha256 "$build/$prog")
size: $(wc -c < "$build/$prog" | tr -d ' ') bytes
--help, its first line that isn't empty: $("$build/$prog" --help 2>&1 | first_line)"
    done
    note "git -C <repo> rev-parse HEAD   (end of the run)" "$head_end"
    note "git -C <repo> status --porcelain   (end of the run)" \
        "${status_end:-(no output: the working tree is clean)}"
    if [ "$(uname -s)" = Darwin ]; then
        note "sysctl -n vm.loadavg   (end of the run)" "$load_end"
    else
        note "cat /proc/loadavg   (end of the run)" "$load_end"
    fi
    echo "# The run's warnings, which run_all.sh printed too:"
    if [ -n "$warnings" ]; then
        printf '%s' "$warnings" | sed 's/^/warning: /'
    else
        echo "(none)"
    fi
}

# The curves of a record's document, then attribute --auto on each, as JSON and as text.
answers() {
    _rec=$1
    _report=$2
    if ! run "$out/$_rec" "$out/$_rec/curves.txt" python3 -I -B "$check" curves \
        "$_rec/timeline.json"; then
        fail "$_rec: no curves (check.py curves)"
        return
    fi
    for curve in $(cat "$out/$_rec/curves.txt"); do
        a=$(printf '%s' "$curve" | tr ':' '-')
        for format in json text; do
            ext=$format
            [ "$format" = text ] && ext=txt
            run "$out/$_rec" "$out/$_rec/answer.$a.$ext.log" "$build/mirage_vmaf" attribute \
                --timeline "$_rec/timeline.json" --report "$_report" --auto --curve "$curve" \
                --threshold "$T" --window "$W" --min-points "$N" --format "$format" \
                --out "$_rec/answer.$a.$ext" || fail "$_rec: attribute --curve $curve failed"
        done
    done
}

# §3.2's ladders and the composite example's, kept by the tests (the design's P-U).
record tests || die "can't write into $2"
run "$out/tests" "$out/tests/mirage_tests.txt" env MIRAGE_KEEP_MEDIA=media "$build/mirage_tests" \
    "$TESTS" --order decl || fail "mirage_tests failed (tests/mirage_tests.txt)"

found=0
for layout in "$out"/media/*/*/layout.json; do
    [ -f "$layout" ] || continue
    ladder=$(dirname -- "$layout")
    name=$(basename -- "$ladder")
    kept=$(dirname -- "$ladder")
    rel=${kept#"$out"/}
    if [ -f "$kept/$name.session.json" ] && [ -f "$kept/$name.session.plan.json" ]; then
        rec=session
        report="$rel/$name.session.json"
    elif [ -f "$kept/$name.json" ]; then
        rec=$name
        report="$rel/$name.json"
    else
        continue
    fi
    if ! record "$rec"; then
        fail "two ladders for the record $rec"
        continue
    fi
    found=$((found + 1))
    run "$out/$rec" /dev/null cp -- "$rel/$name/layout.json" "$rec/layout.json" ||
        fail "$rec: can't copy the ladder's layout.json"
    if [ "$rec" != session ]; then
        # An acceptance case: its document, detection's parameters pinned, and its answers.
        run "$out/$rec" "$out/$rec/timeline.log" "$build/mirage_vmaf" timeline --report "$report" \
            --out "$rec/timeline.json" --threshold "$T" --window "$W" --min-points "$N" ||
            fail "$rec: timeline failed"
        answers "$rec" "$report"
        continue
    fi
    # The composite example: the tests' plan, the session's files fetched from slow_server.py.
    run "$out/session" /dev/null cp -- "$rel/$name.session.plan.json" session/plan.json ||
        fail "session: can't copy the tests' plan"
    if ! run "$out/session" "$out/session/late.txt" python3 -I -B "$check" late \
        session/layout.json; then
        fail "session: no late segment (check.py late)"
        continue
    fi
    late=$(cat "$out/session/late.txt")
    shown_line python3 -I -B "$here/slow_server.py" "$rel/$name" session/server.port \
        session/server.log --delay "$late=$DELAY" >> "$out/session/cmd.txt"
    echo "# in the background, until the build below has exited; then stopped with SIGTERM," \
        "its exit status after the build's" >> "$out/session/cmd.txt"
    (cd -- "$out" && exec python3 -I -B "$here/slow_server.py" "$rel/$name" session/server.port \
        session/server.log --delay "$late=$DELAY") > "$out/session/server.stderr.txt" 2>&1 &
    server=$!
    # It writes its port once it listens: whole seconds, as POSIX sleep takes them.
    tries=0
    while [ ! -s "$out/session/server.port" ] && kill -0 "$server" 2>/dev/null &&
        [ "$tries" -lt 10 ]; do
        tries=$((tries + 1))
        sleep 1
    done
    if [ ! -s "$out/session/server.port" ]; then
        stop_server
        fail "session: slow_server.py didn't start (session/server.stderr.txt)"
        continue
    fi
    port=$(cat "$out/session/server.port")
    run "$out/session" "$out/session/timeline.log" "$build/mirage_vmaf" timeline \
        --report "$report" --out session/timeline.json --session session/plan.json --fetch on \
        --fetch-base "http://127.0.0.1:$port" --threshold "$T" --window "$W" --min-points "$N" ||
        fail "session: timeline failed"
    stop_server
    answers session "$report"
done
[ "$found" -gt 0 ] || fail "the tests kept no ladder (tests/mirage_tests.txt)"

# The run's environment, at its end, with its warnings; then, from <out>, the local directories
# replaced by placeholders in every file but the media segments, with a search for anything
# local left (P-011), the check of every record as it is kept, and a last search of every file,
# the check's report and the logs included, once they are written.
head_end=$(git -C "$repo" rev-parse HEAD)
status_end=$(git_status)
load_end=$(load)
[ "$head_start" = "$head_end" ] || warn "HEAD changed during the run: $head_start -> $head_end"
[ -z "$status_start$status_end" ] ||
    warn "the working tree was not clean during the run (git status --porcelain in environment.txt)"
printf '%s\n' "# eval/timeline/run_all.sh's run, then the replacement of the local directories" \
    "# in its files, the check of its records and the last search of its files, from <out>." \
    "$(shown_line "$here/run_all.sh" "$build" "$out")" > "$out/cmd.txt"
environment > "$out/environment.txt"
for dir in "$out"/*/; do
    [ -f "$dir/cmd.txt" ] && cp -- "$out/environment.txt" "$dir/environment.txt"
done
run "$out" "$out/redact.txt" python3 -I -B "$check" redact . --build "$build" --repo "$repo"
redact_status=$?
run "$out" "$out/check.txt" python3 -I -B "$check" check . --threshold "$T" --window "$W" \
    --min-points "$N" --delay "$DELAY" --json check.json
check_status=$?
run "$out" "$out/search.txt" python3 -I -B "$check" redact . --search-only --build "$build" \
    --repo "$repo"
search_status=$?

tail -n 1 "$out/check.txt"
tail -n 1 "$out/redact.txt"
tail -n 1 "$out/search.txt"
if [ "$check_status" -ne 0 ]; then
    fail "check.py found differences or couldn't check (check.txt)"
fi
case $redact_status in
    0 | 3) ;;
    *) fail "check.py redact couldn't replace the local directories (redact.txt)" ;;
esac
case $search_status in
    0 | 3) ;;
    *) fail "check.py redact --search-only couldn't search <out> (search.txt)" ;;
esac
printf '%s' "$warnings" | while IFS= read -r w; do
    echo "run_all.sh: warning: $w" >&2
done
if [ -n "$failed" ] && [ "$search_status" -eq 3 ]; then
    fail "files of <out> still hold local information (search.txt): don't commit them"
fi
if [ -n "$failed" ]; then
    echo "run_all.sh: FAIL: $failed"
    exit 1
fi
if [ "$search_status" -eq 3 ]; then
    echo "run_all.sh: PASS, but files of <out> still hold local information (search.txt):" \
        "don't commit them"
    exit 3
fi
echo "run_all.sh: PASS"
exit 0
