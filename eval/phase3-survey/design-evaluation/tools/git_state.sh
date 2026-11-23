#!/bin/sh
# Read-only: the worktree's commit, branch and status (no command that changes state).
# usage: sh git_state.sh <repo>
set -eu
repo="$1"
git -C "$repo" rev-parse HEAD
git -C "$repo" branch --show-current
echo "status --short (empty = clean):"
git -C "$repo" status --short
git -C "$repo" log -1 --format='%H %s'
