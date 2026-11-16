#!/bin/sh
# Install OpenH1's latest Linux release as net.goath1.com's relay
# (/opt/openh1-relay/current), checked against the release key, and restart
# the relay if the build changed. Run as root, by openh1-relay-update.timer
# or by hand; "--force" installs it again. Needs curl, unzip, openssl and
# python3 (Ubuntu's).
set -eu

REPO=OpenH1/goath1
ASSET=OpenH1-Linux.zip
ROOT=/opt/openh1-relay
# port/assets/release-key.pub: the key the game's self-updater checks with
RELEASE_KEY=38e00053ccb1c7fced2553635fec4c5598ac4531092ff82787b662dc8e129c74

tag=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" |
	python3 -c 'import json, sys; print(json.load(sys.stdin)["tag_name"])')
case "$tag" in build-[0-9]*) ;; *) echo "update-relay: no release tag ($tag)" >&2; exit 1 ;; esac
if [ "${1:-}" != "--force" ] && [ -e "$ROOT/$tag/bin/openh1" ] &&
	[ "$(readlink "$ROOT/current")" = "$ROOT/$tag" ]; then
	echo "update-relay: $tag already installed"
	exit 0
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
base="https://github.com/$REPO/releases/download/$tag"
curl -fsSL -o "$work/SHA256SUMS" "$base/SHA256SUMS"
curl -fsSL -o "$work/SHA256SUMS.sig" "$base/SHA256SUMS.sig"
curl -fsSL -o "$work/$ASSET" "$base/$ASSET"

# the signature (tools/release_sign.py: Ed25519 of SHA256SUMS, in hex)
hex() { python3 -c 'import sys; sys.stdout.buffer.write(bytes.fromhex(sys.stdin.read().strip()))'; }
printf '302a300506032b6570032100%s' "$RELEASE_KEY" | hex > "$work/key.der"
openssl pkey -pubin -inform DER -in "$work/key.der" -out "$work/key.pem"
hex < "$work/SHA256SUMS.sig" > "$work/sig.bin"
openssl pkeyutl -verify -pubin -inkey "$work/key.pem" -rawin -in "$work/SHA256SUMS" -sigfile "$work/sig.bin" > /dev/null
# ... and the download's sum in it
(cd "$work" && grep "  $ASSET\$" SHA256SUMS | sha256sum -c --quiet -)

rm -rf "$ROOT/$tag.new"
mkdir -p "$ROOT/$tag.new"
unzip -q "$work/$ASSET" -d "$ROOT/$tag.new"
# (the zip's one folder, if it has one)
tree="$ROOT/$tag.new"
if [ ! -e "$tree/bin/openh1" ] && [ "$(ls "$tree" | wc -l)" = 1 ]; then
	tree="$tree/$(ls "$tree")"
fi
[ -x "$tree/bin/openh1" ] || { echo "update-relay: no bin/openh1 in $ASSET" >&2; exit 1; }
"$tree/bin/openh1" --version
rm -rf "$ROOT/$tag"
mv "$tree" "$ROOT/$tag"
rm -rf "$ROOT/$tag.new"
chown -R root:root "$ROOT/$tag"
ln -sfn "$ROOT/$tag" "$ROOT/current.new"
mv -T "$ROOT/current.new" "$ROOT/current"
systemctl restart openh1-relay.service
echo "update-relay: $tag installed"
# (the three newest kept, to go back to: ln -sfn $ROOT/<tag> $ROOT/current)
ls -1d "$ROOT"/build-* | sort -t- -k2 -n | head -n -3 | xargs -r rm -rf
