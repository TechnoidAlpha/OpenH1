#!/bin/sh
# Ships a release (docs/releasing.md): tags master's newest commit build-<N>,
# N the next number after the latest release's (and any build-* tag's), once
# master's full run for that commit has passed. The pushed tag starts the
# release run (.github/workflows/build.yml), which builds, signs and
# publishes build-<N>, the release the self-updaters install.
#
#     tools/ship.sh            check, then tag and push
#     tools/ship.sh --dry-run  check and say what it would tag
set -eu

repository="${SHIP_REPOSITORY:-OpenH1/goath1}"
branch="${SHIP_BRANCH:-master}"
dry_run=false
[ "${1:-}" = --dry-run ] && dry_run=true

remote="https://github.com/$repository.git"
sha="$(git ls-remote "$remote" "refs/heads/$branch" | cut -f1)"
[ -n "$sha" ] || { echo "no $branch in $repository" >&2; exit 1; }

# master's full run for that commit (a push run of the Build workflow)
run="$(gh run list --repo "$repository" --workflow build.yml --branch "$branch" --event push --commit "$sha" \
	--limit 1 --json status,conclusion,url --jq '.[0] | "\(.status) \(.conclusion) \(.url)"')"
[ -n "$run" ] || { echo "$branch's newest commit ${sha%"${sha#???????}"} has no full run yet" >&2; exit 1; }
# shellcheck disable=SC2086 # (three words)
set -- $run
if [ "$1" != completed ] || [ "$2" != success ]; then
	echo "$branch's full run for ${sha%"${sha#???????}"} is not green ($1 ${2:-}): $3" >&2
	exit 1
fi

# the next number: above the latest release's and every build-* tag's
latest="$(gh release view --repo "$repository" --json tagName --jq .tagName 2>/dev/null | sed -n 's/^build-//p')"
highest="$(git ls-remote --tags "$remote" 'refs/tags/build-*' | sed -n 's#.*refs/tags/build-\([0-9]*\)$#\1#p' |
	sort -n | tail -n 1)"
number=$(( ${latest:-0} > ${highest:-0} ? ${latest:-0} : ${highest:-0} ))
number=$((number + 1))

echo "$branch at ${sha%"${sha#???????}"} is green ($3)"
if [ "$dry_run" = true ]; then
	echo "would tag build-$number"
	exit 0
fi
git fetch --quiet "$remote" "refs/heads/$branch"
git tag -a "build-$number" "$sha" -m "Build $number"
git push "$remote" "refs/tags/build-$number"
echo "tagged build-$number: the release run is https://github.com/$repository/actions/workflows/build.yml"
