#!/bin/bash
# tools/rig/install.sh <host>: installs (or updates) the test kit on a Linux test machine from this checkout:
# omarchy or 5900x (<name>.fabric, or the user@host ~/.config/goat-rig/hosts gives it), or any user@host. No root
# needed. On the host, in ~/goat-rig:
#   bin/goat-rig        this checkout's tools/rig/goat-rig (kit/ holds it, with tools/capture.py and relay_netns.py)
#   bin/ccache          ccache's official static release (its SHA-256 checked), bin/rig-ccache the launcher
#                       configure.py gets (--compiler-launcher), its cache ccache/ (30 GB)
#   repo.git            a bare clone of GitHub's OpenH1/goath1 (GitHub's branches as refs/remotes/origin/*)
#   venv                cmake (SDL's build), when the machine has none
#   maps                the machine's Halo maps (~/halo-x86-maps/maps)
#   locks/gpu.lock      the GPU lock: one game that draws at a time (older scripts link their lock to it)
# Run it again after changing tools/rig: it only copies the kit then.
set -euo pipefail
host=${1:?usage: tools/rig/install.sh <omarchy|5900x|user@host>}
case $host in
  omarchy|5900x) target=$(awk -v n="$host" '$1 == n { print $2 }' ~/.config/goat-rig/hosts 2>/dev/null); target=${target:-$host.fabric} ;;
  *) target=$host ;;
esac
root=$(cd "$(dirname "$0")/../.." && pwd)

# ccache 4.14.2's static Linux build (GitHub release, 2026-10-18) and its published SHA-256
ccache_version=4.14.2
ccache_sha256=b20ff6415151226ac5a10dfd6839700c694bd67d1871c8fddb266d366ff25bfb

# (to bash on its input: the 5900X's login shell is fish)
ssh "$target" bash -s -- "$ccache_version" "$ccache_sha256" <<'REMOTE'
set -euo pipefail
version=$1; sha=$2
rig=$HOME/goat-rig
mkdir -p "$rig"/{bin,kit/tools/rig,builds,runs,locks,ccache,tmp}

# ccache: the official static release, checked
if [ "$("$rig/bin/ccache" --version 2>/dev/null | head -1)" != "ccache version $version" ]; then
  name=ccache-$version-linux-x86_64-musl-static
  rm -rf "$rig/tmp/ccache" && mkdir -p "$rig/tmp/ccache"
  curl -fsSL -o "$rig/tmp/ccache/$name.tar.xz" \
    "https://github.com/ccache/ccache/releases/download/v$version/$name.tar.xz"
  echo "$sha  $rig/tmp/ccache/$name.tar.xz" | sha256sum -c --quiet
  tar -C "$rig/tmp/ccache" -xf "$rig/tmp/ccache/$name.tar.xz"
  install -m 755 "$rig/tmp/ccache/$name/ccache" "$rig/bin/ccache"
  rm -rf "$rig/tmp/ccache"
fi
cat > "$rig/bin/rig-ccache" <<EOF
#!/bin/sh
# configure.py's --compiler-launcher: the machine's one shared cache, wherever the build is run from
CCACHE_DIR=$rig/ccache exec $rig/bin/ccache "\$@"
EOF
chmod 755 "$rig/bin/rig-ccache"
# (base_dir and hash_dir: every worktree's objects are found again by the others)
cat > "$rig/ccache/ccache.conf" <<EOF
max_size = 30G
base_dir = $rig
hash_dir = false
sloppiness = include_file_mtime,include_file_ctime,time_macros
EOF

# cmake for SDL's build, when the machine has none
if ! command -v cmake >/dev/null && [ ! -x "$rig/venv/bin/cmake" ]; then
  python3 -m venv "$rig/venv"
  "$rig/venv/bin/pip" install --quiet cmake
fi

# the mirror
if [ ! -d "$rig/repo.git" ]; then
  git clone --quiet --bare https://github.com/OpenH1/goath1.git "$rig/repo.git"
  git --git-dir "$rig/repo.git" config remote.origin.fetch '+refs/heads/*:refs/remotes/origin/*'
  git --git-dir "$rig/repo.git" fetch --quiet origin
fi

# maps, the GPU lock shared with the older scripts
[ -e "$rig/maps" ] || ln -s "$HOME/halo-x86-maps/maps" "$rig/maps"
[ -e "$rig/locks/gpu.lock" ] || touch "$rig/locks/gpu.lock"
REMOTE

# the kit itself
COPYFILE_DISABLE=1 tar --no-xattrs -C "$root" -cf - tools/rig tools/capture.py tools/relay_netns.py |
  ssh "$target" bash -c "'tar -C \$HOME/goat-rig/kit -xf - && chmod 755 \$HOME/goat-rig/kit/tools/rig/goat-rig &&
    ln -sfn \$HOME/goat-rig/kit/tools/rig/goat-rig \$HOME/goat-rig/bin/goat-rig &&
    ln -sfn \$HOME/goat-rig/kit/tools/rig/README.md \$HOME/goat-rig/README.md &&
    if [ -d \$HOME/.local/bin ]; then ln -sfn \$HOME/goat-rig/bin/goat-rig \$HOME/.local/bin/goat-rig; fi &&
    \$HOME/goat-rig/bin/ccache --version | head -1'"
echo "installed on $target: ~/goat-rig/bin/goat-rig ($(git -C "$root" rev-parse --short HEAD))"
