#!/usr/bin/env bash
# Sets up a clone (and all its worktrees) for merging generated files:
#
#   tools/git_setup.sh
#
# - the menus-generated merge driver (.gitattributes): a file that
#   tools/ce_menus.py writes, changed on both sides of a merge, takes our
#   side's without a conflict;
# - a post-merge hook that then writes the menus again from the merged
#   sources and commits them (tools/menus_regen.sh --after-merge), when both
#   sides changed them.
# Run it again at any time: it changes nothing already set up, and keeps a
# post-merge hook of your own (adding its line to it).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
git -C "$ROOT" config merge.menus-generated.name "generated menus: ours, written again after the merge"
git -C "$ROOT" config merge.menus-generated.driver true

hooks=$(git -C "$ROOT" config core.hooksPath || true)
if [ -z "$hooks" ]; then
    hooks=$(cd "$ROOT" && cd "$(git rev-parse --git-common-dir)" && pwd)/hooks
fi
mkdir -p "$hooks"
hook=$hooks/post-merge
line='top=$(git rev-parse --show-toplevel); grep -q -- --after-merge "$top/tools/menus_regen.sh" 2>/dev/null && "$top/tools/menus_regen.sh" --after-merge  # tools/git_setup.sh'
if [ ! -e "$hook" ]; then
    printf '#!/bin/sh\n%s\nexit 0\n' "$line" > "$hook"
elif ! grep -qF 'tools/git_setup.sh' "$hook"; then
    printf '\n%s\n' "$line" >> "$hook"
fi
chmod +x "$hook"
echo "Set up: the menus-generated merge driver, and $hook"
