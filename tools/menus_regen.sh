#!/usr/bin/env bash
# Writes every menu file again (tools/ce_menus.py) from the inputs in one
# folder, outside the repository ($MENU_INPUTS, else ~/dev/menu-inputs):
#
#   tools/menus_regen.sh --fetch     # gets the inputs, once (git, python3)
#   tools/menus_regen.sh             # writes the menus; git status shows what changed
#   tools/menus_regen.sh --resolve   # in a merge: the menus written again and staged
#   tools/menus_regen.sh --check     # fails if the committed menus are not what it writes
#
# The folder holds (port/assets/menus/README.md, "Writing them again"):
#   halopc-restored/  the PC version's tags, at HALOPC_RESTORED_COMMIT (only
#                     tags/ui is read: --fetch checks out that folder alone)
#   invader/          Invader's tag definitions, at INVADER_COMMIT
#   redraws/          the hand-made redraws: port/assets/menus/svg copied in
#                     before each run (ce_menus.py writes that folder again),
#                     with any new redraw put here
#   venv/             tools/menus_requirements.txt's Pillow, NumPy, SciPy, fontTools
# rsvg-convert comes from the system (librsvg2-bin, Homebrew's librsvg).
# Other arguments are passed to ce_menus.py. The tags are the game's data:
# never commit the folder.
#
# The files it writes (GENERATED below; .gitattributes marks them
# merge=menus-generated) are committed, so that no build needs the tags. A
# merge of two branches that both changed them takes our side's (the merge
# driver tools/git_setup.sh sets up), then the post-merge hook it installs
# writes them again (--after-merge). A merge stopped by a conflict in a
# source: resolve the source, then --resolve.
set -euo pipefail

HALOPC_RESTORED=https://github.com/Aerocatia/halopc-restored.git
HALOPC_RESTORED_COMMIT=eb1c8f73ba387fb0844e9716f4e41072f09299e4
INVADER=https://github.com/SnowyMouse/invader.git
INVADER_COMMIT=20a23018f34cecb2fa33f046a6b2c29cd8341f34

ROOT=$(cd "$(dirname "$0")/.." && pwd)
INPUTS=${MENU_INPUTS:-$HOME/dev/menu-inputs}
PYTHON=$INPUTS/venv/bin/python
# what ce_menus.py writes, in port/assets/menus (.gitattributes'
# merge=menus-generated lists the same: tools/test_merge_friction.py)
GENERATED=(ce port_svg model menus.json NON_HANDDRAWN.md UNWIRED.md)
GENERATED_PATHS=("${GENERATED[@]/#/port/assets/menus/}")

fetch_repository() {  # <folder> <url> <commit> [folder]: a shallow clone of one commit (of that folder alone)
    if [ "$(git -C "$1" rev-parse HEAD 2>/dev/null)" = "$3" ]; then
        return
    fi
    if [ ! -d "$1/.git" ]; then
        git init -q "$1"
        git -C "$1" remote add origin "$2"
        if [ -n "${4:-}" ]; then
            # (the PC version's tags are 3 GB; the menus read tags/ui, 47 MB)
            git -C "$1" config remote.origin.promisor true
            git -C "$1" config remote.origin.partialclonefilter blob:none
            git -C "$1" sparse-checkout set --no-cone "/$4/"
        fi
    fi
    if [ "$(git -C "$1" config remote.origin.promisor || true)" = true ]; then
        git -C "$1" fetch -q --depth 1 --filter=blob:none origin "$3"
    else
        git -C "$1" fetch -q --depth 1 origin "$3"
    fi
    git -C "$1" checkout -q --detach FETCH_HEAD
}

fetch() {
    mkdir -p "$INPUTS"
    fetch_repository "$INPUTS/halopc-restored" "$HALOPC_RESTORED" "$HALOPC_RESTORED_COMMIT" tags/ui
    fetch_repository "$INPUTS/invader" "$INVADER" "$INVADER_COMMIT"
    if [ ! -x "$PYTHON" ]; then
        python3 -m venv "$INPUTS/venv"
        "$PYTHON" -m pip install -q -r "$ROOT/tools/menus_requirements.txt"
    fi
}

regenerate() {  # [ce_menus.py's options]
    local input
    for input in halopc-restored/tags/ui invader/src/tag/hek/definition venv/bin/python; do
        if [ ! -e "$INPUTS/$input" ]; then
            echo "$INPUTS/$input is missing: run $0 --fetch" >&2
            return 1
        fi
    done
    command -v rsvg-convert >/dev/null || { echo "rsvg-convert is missing (librsvg)" >&2; return 1; }
    mkdir -p "$INPUTS/redraws"
    cp -R "$ROOT/port/assets/menus/svg/." "$INPUTS/redraws/"
    "$PYTHON" -W ignore::DeprecationWarning "$ROOT/tools/ce_menus.py" \
        --tags "$INPUTS/halopc-restored/tags" \
        --definitions "$INPUTS/invader/src/tag/hek/definition" \
        --redraws "$INPUTS/redraws" "$@"
}

is_generated() {  # <path>: whether ce_menus.py writes it
    local generated
    for generated in "${GENERATED_PATHS[@]}"; do
        case "$1" in "$generated" | "$generated"/*) return 0 ;; esac
    done
    return 1
}

changed_generated() {  # <from> <to>: whether the generated files differ between the commits
    ! git -C "$ROOT" diff --quiet "$1" "$2" -- "${GENERATED_PATHS[@]}"
}

case "${1:-}" in
--fetch)
    shift
    fetch
    [ $# -eq 0 ] && exit 0
    ;;
--resolve)
    # a merge (or a rebase, a cherry-pick) stopped: the generated files
    # written again from the merged sources and staged, whatever their
    # conflicts. The sources' conflicts are resolved first.
    unresolved=()
    while IFS= read -r path; do
        is_generated "$path" || unresolved+=("$path")
    done < <(git -C "$ROOT" diff --name-only --diff-filter=U)
    if [ ${#unresolved[@]} -gt 0 ]; then
        echo "Resolve these first (and git add them), then run $0 --resolve again:" >&2
        printf '  %s\n' "${unresolved[@]}" >&2
        exit 1
    fi
    fetch
    regenerate
    git -C "$ROOT" add -A -- "${GENERATED_PATHS[@]}"
    echo "The menus are written again and staged: git commit (or git rebase --continue) to finish."
    exit 0
    ;;
--after-merge)
    # (the post-merge hook's, tools/git_setup.sh) after a merge commit whose
    # sides both changed the generated files, which the merge driver took
    # from our side: they are written again and committed. Never fails the
    # merge: CI's --check catches what it could not do.
    git -C "$ROOT" rev-parse -q --verify HEAD^2 >/dev/null || exit 0
    base=$(git -C "$ROOT" merge-base HEAD^1 HEAD^2) || exit 0
    if ! changed_generated "$base" HEAD^1 || ! changed_generated "$base" HEAD^2; then
        exit 0
    fi
    echo "Both sides of the merge changed the menus: writing them again (tools/menus_regen.sh)"
    if ! { fetch && regenerate > /dev/null; }; then
        echo "Could not write the menus again: run tools/menus_regen.sh --resolve, then git commit" >&2
        exit 0
    fi
    git -C "$ROOT" add -A -- "${GENERATED_PATHS[@]}"
    if git -C "$ROOT" diff --cached --quiet HEAD -- "${GENERATED_PATHS[@]}"; then
        echo "The merged menus were already what the generator writes."
        exit 0
    fi
    # (the merge commit made again with them, by plumbing: git still counts
    # the merge as under way while its hook runs, and refuses an amend)
    merge=$(git -C "$ROOT" rev-parse HEAD)
    commit=$(git -C "$ROOT" log -1 --format=%B HEAD |
        git -C "$ROOT" commit-tree "$(git -C "$ROOT" write-tree)" -p HEAD^1 -p HEAD^2)
    git -C "$ROOT" update-ref -m "menus_regen.sh --after-merge: the menus written again" HEAD "$commit" "$merge"
    echo "The merge commit now has the menus written again: $(git -C "$ROOT" log -1 --format='%h %s')"
    exit 0
    ;;
--check)
    # (CI's) fails if the committed files are not what the generator writes
    shift
    regenerate "$@"
    if [ -n "$(git -C "$ROOT" status --porcelain -- "${GENERATED_PATHS[@]}")" ]; then
        echo "The committed menus are not what tools/ce_menus.py writes from the committed sources:" >&2
        git -C "$ROOT" status --short -- "${GENERATED_PATHS[@]}" | head -50 >&2
        echo "Run tools/menus_regen.sh (tools/menus_regen.sh --resolve in a merge) and commit what it writes." >&2
        exit 1
    fi
    echo "The committed menus are what the generator writes."
    exit 0
    ;;
esac

regenerate "$@"
