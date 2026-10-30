#!/bin/bash
# Regenerates glad's OpenGL 4.1 core loader the way the build generated it
# before the files were committed (docs/DECISIONS.md D-031), and compares the
# bytes with the files committed next to this script: glad 2.0.8, from its
# git tag's commit, run in a venv of Python 3.13 with Jinja2 and MarkupSafe
# pinned (docs/BUILD.md), with the arguments CMakeLists.txt's
# glad_add_library(glad_gl_core_41 STATIC REPRODUCIBLE API gl:core=4.1) gave it.
#
# usage: third_party/glad/regenerate.sh <python3.13> <scratch>
#   <python3.13>  the Python 3.13 interpreter the venv is created from
#   <scratch>     an empty directory for the venv, glad's clone, the generated
#                 files and the logs
#
# Needs the network: PyPI for Jinja2 and MarkupSafe, GitHub for glad.
# Exit status: 0 the three files are identical to the committed ones;
# 1 a file differs or is missing; 2 bad arguments, or a step failed.

set -u

GLAD_URL=https://github.com/Dav1dde/glad.git
GLAD_TAG=v2.0.8
# The commit the tag named when the build fetched glad
# (eval/build_check/macos-2026-10-23-6a6661f.txt, "Dependencies fetched").
GLAD_COMMIT=73db193f853e2ee079bf3ca8a64aa2eaf6459043
PACKAGES=(jinja2==3.1.6 markupsafe==3.0.4)
GLAD_ARGS=(--api gl:core=4.1 --reproducible c)
FILES=(include/KHR/khrplatform.h include/glad/gl.h src/gl.c)

if [ $# -ne 2 ]; then
    echo "usage: $0 <python3.13> <scratch>" >&2
    exit 2
fi
here=$(cd "$(dirname "$0")" && pwd -P) || exit 2
py=$1
scratch=$(cd "$2" && pwd -P) || exit 2
venv=$scratch/venv
src=$scratch/glad
out=$scratch/out
logs=$scratch/logs
if [ -e "$venv" ] || [ -e "$src" ] || [ -e "$out" ]; then
    echo "$scratch already has a venv, glad or out directory" >&2
    exit 2
fi
mkdir -p "$logs" || exit 2

sha256() {
    if command -v sha256sum > /dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi |
        cut -d ' ' -f 1
}

# A command, then its output.
fact() {
    echo "\$ $*"
    "$@" 2>&1
    echo
}

# A command whose output goes to <scratch>/logs/<log>; stops the script if it fails.
step() {
    local log=$1
    shift
    echo "\$ $*"
    "$@" > "$logs/$log" 2>&1
    local rc=$?
    echo "exit code: $rc (log: $logs/$log)"
    if [ $rc -ne 0 ]; then
        tail -n 20 "$logs/$log"
        exit 2
    fi
}

echo "# glad's OpenGL 4.1 core loader, regenerated and compared with $here"
echo
fact "$py" --version
step venv.log "$py" -m venv "$venv"
step pip.log "$venv/bin/pip" install --disable-pip-version-check "${PACKAGES[@]}"
fact "$venv/bin/python" --version
fact "$venv/bin/pip" list --format=freeze --disable-pip-version-check

step clone.log git clone --quiet --no-checkout "$GLAD_URL" "$src"
tagged=$(git -C "$src" rev-parse --verify --quiet "$GLAD_TAG^{commit}")
echo "\$ git -C $src rev-parse $GLAD_TAG^{commit}"
echo "${tagged:-(no such tag)}"
if [ "$tagged" != "$GLAD_COMMIT" ]; then
    echo "the tag $GLAD_TAG doesn't name $GLAD_COMMIT"
    exit 2
fi
step checkout.log git -C "$src" checkout --quiet --detach "$GLAD_COMMIT"

# As glad_add_library runs it: in glad's source directory, `python -m glad`.
echo "\$ cd $src && $venv/bin/python -m glad --out-path $out ${GLAD_ARGS[*]}"
(cd "$src" && "$venv/bin/python" -m glad --out-path "$out" "${GLAD_ARGS[@]}") \
    > "$logs/generate.log" 2>&1
rc=$?
echo "exit code: $rc (log: $logs/generate.log)"
[ $rc -eq 0 ] || exit 2
echo

status=0
for f in "${FILES[@]}"; do
    if [ ! -f "$out/$f" ]; then
        echo "$f: not generated"
        status=1
        continue
    fi
    if [ ! -f "$here/$f" ]; then
        same="missing"
        status=1
    elif cmp -s "$out/$f" "$here/$f"; then
        same="identical"
    else
        same="differs"
        status=1
    fi
    echo "$f: $(wc -c < "$out/$f" | tr -d ' ') bytes, sha256 $(sha256 "$out/$f");" \
         "the committed file: $same"
done
echo
if [ $status -eq 0 ]; then
    echo "The generated files are identical to the committed ones."
else
    echo "The generated files are not all identical to the committed ones."
fi
exit $status
