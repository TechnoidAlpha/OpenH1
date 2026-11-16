#!/bin/sh
# Install the master's release (the latest, or the tag given: v0.1.10801) of
# OpenH1's fork of opence-master as /usr/local/bin/opence-master, checked
# against the fork's master release key, and restart the master. By hand
# only: the master says on its admin pages when a release is out
# (MASTER_UPDATES=notify), and we look at it before installing it. The
# binary it replaces is kept as opence-master.bak-<its version>; if the new
# one does not answer /healthz, it is put back. Needs curl, openssl and
# python3.
set -eu

REPO=OpenH1/opence-master-server
ASSET=opence-master-linux-amd64
# the fork's master release key (its internal/update/key.go; the seed is the
# maintainer's, and the fork's RELEASE_SIGNING_KEY secret). Not the game's
# release key.
RELEASE_KEY=3607ccfaa3ebb0c1cbbfdb8b1578522cb281b250799f9f28b529c2bb1e6e16ca
BIN=/usr/local/bin/opence-master
HEALTH=http://127.0.0.1:8090/healthz

tag=${1:-$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" |
	python3 -c 'import json, sys; print(json.load(sys.stdin)["tag_name"])')}
case "$tag" in v[0-9]*) ;; *) echo "install-master: no release tag ($tag)" >&2; exit 1 ;; esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
base="https://github.com/$REPO/releases/download/$tag"
curl -fsSL -o "$work/SHA256SUMS" "$base/SHA256SUMS"
curl -fsSL -o "$work/SHA256SUMS.sig" "$base/SHA256SUMS.sig"
curl -fsSL -o "$work/$ASSET" "$base/$ASSET"

hex() { python3 -c 'import sys; sys.stdout.buffer.write(bytes.fromhex(sys.stdin.read().strip()))'; }
printf '302a300506032b6570032100%s' "$RELEASE_KEY" | hex > "$work/key.der"
openssl pkey -pubin -inform DER -in "$work/key.der" -out "$work/key.pem"
# (the signature: hex, as release-sign writes it, or base64)
if grep -qE '^[0-9a-fA-F]{128}$' "$work/SHA256SUMS.sig"; then
	hex < "$work/SHA256SUMS.sig" > "$work/sig.bin"
else
	base64 -d < "$work/SHA256SUMS.sig" > "$work/sig.bin"
fi
openssl pkeyutl -verify -pubin -inkey "$work/key.pem" -rawin -in "$work/SHA256SUMS" -sigfile "$work/sig.bin" > /dev/null
(cd "$work" && grep " \*\?$ASSET\$" SHA256SUMS | sha256sum -c --quiet -)
chmod 755 "$work/$ASSET"
"$work/$ASSET" version | grep -q " $tag " || { echo "install-master: the binary is not $tag" >&2; exit 1; }

# the binary it replaces, kept
backup=
if [ -x "$BIN" ]; then
	old=$("$BIN" version 2> /dev/null | cut -d' ' -f2)
	backup="$BIN.bak-${old:-unknown}"
	cp -p "$BIN" "$backup"
	echo "install-master: the old binary is $backup"
fi
install -m 755 "$work/$ASSET" "$BIN.new"
mv "$BIN.new" "$BIN"
# (a release the master installed itself, in its bin/, would be run instead
# of an older /usr/local/bin one: it is removed, so this one runs)
rm -rf /var/lib/private/opence-master/bin
if systemctl is-enabled --quiet opence-master 2> /dev/null; then
	systemctl restart opence-master
	for _ in 1 2 3 4 5 6 7 8 9 10; do
		sleep 2
		if curl -fsS -o /dev/null "$HEALTH"; then
			echo "install-master: $tag installed and answering"
			exit 0
		fi
	done
	echo "install-master: $tag does not answer $HEALTH: going back" >&2
	if [ -n "$backup" ]; then
		cp -p "$backup" "$BIN"
		systemctl restart opence-master
	fi
	exit 1
fi
echo "install-master: $tag installed"
