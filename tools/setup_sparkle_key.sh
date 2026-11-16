#!/bin/bash
# The macOS app's update key (port/macos/README.md, "Updates"): the EdDSA
# key pair Sparkle checks each update with.
#
#   1. makes the key pair with Sparkle's generate_keys, its private half in
#      this Mac's login Keychain (account "openh1"), or finds the one there
#   2. sets the repository secret SPARKLE_PRIVATE_KEY on OpenH1/goath1, with
#      which CI signs each release's OpenH1.dmg for the appcast
#   3. sets the repository variable SPARKLE_PUBLIC_KEY, which CI puts in the
#      release app's Info.plist (SUPublicEDKey), and prints it
#
# Nothing secret is printed or kept outside the Keychain: the exported key
# lives in a private temporary folder that is deleted on exit. Run it again
# on the same Mac and it sets the same key. Keep the Keychain's entry (or a
# backup of it: generate_keys -x): the apps already out accept only updates
# signed with it.
#
#   bash tools/setup_sparkle_key.sh [--force]
#
# --force sets a new key though the repository has one and this Mac's
# Keychain does not: the apps already out will then refuse the updates
# until they are updated by hand.
set -euo pipefail

GH_REPO="OpenH1/goath1"
ACCOUNT="openh1"
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
FORCE=0
[ "${1:-}" = "--force" ] && FORCE=1

step() { printf '\n\033[1;32m==> %s\033[0m\n' "$*"; }
fail() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

work="$(mktemp -d)"
chmod 700 "$work"
trap 'rm -rf "$work"' EXIT

step "Checking what's needed"
[ "$(uname -s)" = "Darwin" ] || fail "run it on a Mac (Sparkle's generate_keys and the Keychain)"
command -v gh > /dev/null || fail "gh is not installed"
gh auth status > /dev/null 2>&1 || fail "gh is not signed in (gh auth login)"
gh secret list --repo "$GH_REPO" > /dev/null || fail "gh cannot read $GH_REPO's secrets (an admin's token is needed)"

step "Sparkle's tools (the build's pinned release: tools/macos_build.py)"
sparkle="$(cd "$REPO_DIR" && python3 -c 'import sys; sys.path.insert(0, "."); from tools.macos_build import fetch_sparkle; print(fetch_sparkle().resolve())' | tail -n 1)"
generate_keys="$sparkle/bin/generate_keys"
[ -x "$generate_keys" ] || fail "no generate_keys in $sparkle/bin"
echo "$generate_keys"

step "The key pair in the Keychain (account $ACCOUNT; macOS may ask to allow the Keychain's use: choose Allow)"
had_key=1
public="$("$generate_keys" --account "$ACCOUNT" -p 2> /dev/null || true)"
if [ -z "$public" ]; then
    had_key=0
    if gh secret list --repo "$GH_REPO" | grep -q '^SPARKLE_PRIVATE_KEY'; then
        [ "$FORCE" = 1 ] || fail "$GH_REPO has a SPARKLE_PRIVATE_KEY, but this Mac's Keychain has no key: run this on the Mac that made it, or import its backup (generate_keys --account $ACCOUNT -f <file>), or give --force to replace it (the apps already out will refuse the updates)"
    fi
    # (its output shown: the Keychain's prompt can make it fail, and says so)
    status=0
    "$generate_keys" --account "$ACCOUNT" || status=$?
    [ "$status" = 0 ] || fail "generate_keys failed (status $status): allow the Keychain prompt, or run $generate_keys --account $ACCOUNT yourself, then run this again"
    public="$("$generate_keys" --account "$ACCOUNT" -p)" || fail "generate_keys -p found no key after making it: allow the Keychain prompt, or run $generate_keys --account $ACCOUNT yourself, then run this again"
fi
[[ "$public" =~ ^[A-Za-z0-9+/]{43}=$ ]] || fail "generate_keys gave no public key ('$public'): allow the Keychain prompt, or run $generate_keys --account $ACCOUNT yourself, then run this again"
[ "$had_key" = 1 ] && echo "found the Keychain's key" || echo "made a new key"

step "Setting the secret and the variable on $GH_REPO"
"$generate_keys" --account "$ACCOUNT" -x "$work/private-key" > /dev/null
[ -s "$work/private-key" ] || fail "the private key's export failed (was it allowed?)"
gh secret set SPARKLE_PRIVATE_KEY --repo "$GH_REPO" < "$work/private-key"
gh variable set SPARKLE_PUBLIC_KEY --repo "$GH_REPO" --body "$public"
gh secret list --repo "$GH_REPO" | grep '^SPARKLE_PRIVATE_KEY'
gh variable list --repo "$GH_REPO" | grep '^SPARKLE_PUBLIC_KEY'

step "Done"
echo "Public key (SUPublicEDKey): $public"
echo "The next master release's app has it, and the release an appcast.xml: from then on the Mac app updates itself."
