#!/bin/sh
# Reads stdin, writes it with the local absolute paths replaced by the
# placeholders the survey's files use: the repository worktree <repo>, the
# survey's output directory <out>, the session's scratch directory <scratch>
# and Homebrew's prefix <homebrew>. The paths come from the environment
# (REPO, OUT, SCRATCH, HOMEBREW), so this file names none of them. macOS gives
# /tmp also under its /private prefix: both spellings are replaced.
set -eu
sed \
  -e "s#/private${REPO#/private}#<repo>#g" -e "s#${REPO#/private}#<repo>#g" \
  -e "s#/private${OUT#/private}#<out>#g"   -e "s#${OUT#/private}#<out>#g" \
  -e "s#/private${SCRATCH#/private}#<scratch>#g" -e "s#${SCRATCH#/private}#<scratch>#g" \
  -e "s#${HOMEBREW}#<homebrew>#g" \
  -e "s#${HOME}#<home>#g"
