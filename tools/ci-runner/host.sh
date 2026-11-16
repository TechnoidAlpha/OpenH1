#!/bin/bash
# tools/ci-runner/host.sh: a test machine's GitHub Actions runners of OpenH1/goath1, installed by
# tools/ci-runner/install.sh into ~/goat-rig/ci (beside goat-rig's compile cache, which they share):
#   rootfs/           the jobs' file system: .github/ci-image's image, what GitHub's runners run the Linux jobs in
#   runners/<name>/   one runner each: its files, its jobs' folder (_work), home, tmp and cache (the guests' LLVM)
#   bin/host.sh       this script
# Each runner runs in a sandbox (bubblewrap, no root needed): the image's file system read-only, its own folder,
# and goat-rig's compile cache, and nothing else of the machine's: no home folder, games, Steam, docker, sudo or
# desktop session, its processes unseen. It runs as an unprivileged user (root inside, without capabilities) and
# in a systemd user service, goath1-runner@<name>, in the slice goath1-ci (the machine's limits: install.sh).
#   host.sh rootfs <id>            the image (docker export, on stdin) unpacked as the next rootfs
#   host.sh runner <version> <sha> the runner's release downloaded and checked
#   host.sh add <name> <version> <label>  a runner unpacked and registered (its registration token on stdin):
#                                  goath1 (the build and test jobs) or goath1-plan (the changes job alone)
#   host.sh remove <name>          a runner unregistered and deleted (its removal token on stdin)
#   host.sh list                   the runners' names
#   host.sh tidy                   the previous rootfs deleted, once the runners restarted
#   host.sh run <name>             the runner, in its sandbox (the service's command)
#   host.sh guard                  on the 5900X: runners stopped while a game plays, started again after
#   host.sh status                 the number of idle goath1 runners (what the changes job asks, through serve)
#   host.sh serve <address> [linux:<host>|windows:<host>...]  on omarchy: every machine's free runners, as JSON
#                                  over HTTP on <address>:8790 (status.py)
set -euo pipefail
ci=$HOME/goat-rig/ci
rootfs=$ci/rootfs
repo=OpenH1/goath1

sandbox() {  # sandbox <name> <command...>: the command in the runner's sandbox
  local dir=$ci/runners/$1; shift
  [ -d "$rootfs/usr" ] || { echo "no file system at $rootfs: run tools/ci-runner/install.sh" >&2; exit 1; }
  [ -d "$HOME/goat-rig/ccache" ] || { echo "no compile cache: run tools/rig/install.sh first" >&2; exit 1; }
  mkdir -p "$dir/home" "$dir/tmp" "$dir/cache"
  exec bwrap --ro-bind "$rootfs" / \
    --dev /dev --tmpfs /dev/shm --proc /proc --ro-bind /sys /sys --tmpfs /run --tmpfs /var/tmp \
    --ro-bind /etc/resolv.conf /etc/resolv.conf \
    --bind "$dir" /runner --bind "$dir/tmp" /tmp --bind "$HOME/goat-rig/ccache" /goat-rig/ccache \
    --unshare-user --uid 0 --gid 0 --unshare-pid --unshare-ipc --unshare-uts --unshare-cgroup-try \
    --hostname goath1-ci --cap-drop ALL --die-with-parent --new-session \
    --clearenv --setenv HOME /runner/home --setenv TMPDIR /tmp --setenv LANG C.UTF-8 \
    --setenv PATH /usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    --setenv RUNNER_ALLOW_RUNASROOT 1 --setenv DOTNET_SYSTEM_GLOBALIZATION_INVARIANT 1 \
    --chdir /runner "$@"
}

busy() {  # busy <name>: whether the runner is running a job
  local cg pid
  cg=$(systemctl --user show -p ControlGroup --value "goath1-runner@$1.service")
  [ -n "$cg" ] || return 1
  for pid in $(cat /sys/fs/cgroup"$cg"/cgroup.procs 2>/dev/null); do
    tr '\0' ' ' < /proc/"$pid"/cmdline 2>/dev/null | grep -q Runner.Worker && return 0
  done
  return 1
}

playing() {  # whether a game plays: the installed GOAT H1, a Steam game, or one in gamemode
  pgrep -f "^$HOME/Applications/OpenH1/bin/openh1" > /dev/null ||
    pgrep -f 'SteamLaunch AppId=' > /dev/null ||
    { command -v gamemoded > /dev/null && gamemoded -s 2>/dev/null | grep -q 'is active'; }
}

case ${1:-} in
  rootfs)
    id=${2:?image id}
    rm -rf "$ci/rootfs.new" && mkdir -p "$ci/rootfs.new"
    tar -C "$ci/rootfs.new" --no-same-owner --anchored --exclude='dev/*' --exclude='proc/*' --exclude='sys/*' -xf -
    mkdir -p "$ci/rootfs.new"/{runner,goat-rig/ccache,tmp,run}
    : > "$ci/rootfs.new/etc/resolv.conf"
    printf '127.0.0.1 localhost goath1-ci\n::1 localhost ip6-localhost ip6-loopback\n' > "$ci/rootfs.new/etc/hosts"
    echo "$id" > "$ci/rootfs.new/.image-id"
    # (the old one kept until the runners restart on the new one: their sandboxes still use it)
    if [ -d "$rootfs" ]; then
      [ -d "$ci/rootfs.old" ] && chmod -R u+w "$ci/rootfs.old" && rm -rf "$ci/rootfs.old"
      mv "$rootfs" "$ci/rootfs.old"
    fi
    mv "$ci/rootfs.new" "$rootfs"
    ;;
  runner)
    version=${2:?version} sha=${3:?sha256}
    tarball=$ci/actions-runner-linux-x64-$version.tar.gz
    if [ ! -f "$tarball" ]; then
      curl -fsSL -o "$tarball.part" \
        "https://github.com/actions/runner/releases/download/v$version/actions-runner-linux-x64-$version.tar.gz"
      echo "$sha  $tarball.part" | sha256sum -c --quiet
      mv "$tarball.part" "$tarball"
    fi
    ;;
  add)
    name=${2:?name} version=${3:?version} label=${4:-goath1}
    dir=$ci/runners/$name
    mkdir -p "$dir"
    [ -x "$dir/config.sh" ] || tar -C "$dir" -xzf "$ci/actions-runner-linux-x64-$version.tar.gz"
    echo "$label" > "$dir/.label"
    # (the token read from stdin inside the sandbox: never on a command line or in a file)
    sandbox "$name" /bin/bash -c 'IFS= read -r token; ACTIONS_RUNNER_INPUT_TOKEN=$token exec ./config.sh \
      --unattended --replace --url "https://github.com/$1" --name "$2" --labels "$3" --work _work' _ "$repo" "$name" "$label"
    ;;
  remove)
    name=${2:?name}
    systemctl --user disable --now "goath1-runner@$name.service" 2>/dev/null || true
    ( sandbox "$name" /bin/bash -c 'IFS= read -r token; ACTIONS_RUNNER_INPUT_TOKEN=$token exec ./config.sh remove' )
    chmod -R u+w "$ci/runners/$name" && rm -rf "${ci:?}/runners/$name"
    ;;
  list)
    for dir in "$ci"/runners/*/; do [ -d "$dir" ] && basename "$dir"; done
    ;;
  tidy)
    [ ! -d "$ci/rootfs.old" ] || { chmod -R u+w "$ci/rootfs.old" && rm -rf "$ci/rootfs.old"; }
    ;;
  run)
    name=${2:?name}
    rm -rf "$ci/runners/$name/tmp"
    sandbox "$name" ./run.sh
    ;;
  guard)
    for dir in "$ci"/runners/*/; do
      name=$(basename "$dir")
      [ -f "$dir/.runner" ] || continue
      if playing; then
        if systemctl --user is-active -q "goath1-runner@$name.service" && ! busy "$name"; then
          echo "a game plays: $name stopped"
          systemctl --user stop "goath1-runner@$name.service"
        fi
      elif ! systemctl --user is-active -q "goath1-runner@$name.service"; then
        echo "no game plays: $name started"
        systemctl --user start "goath1-runner@$name.service"
      fi
    done
    ;;
  status)
    idle=0
    for dir in "$ci"/runners/*/; do
      name=$(basename "$dir")
      [ -f "$dir/.runner" ] && [ "$(cat "$dir/.label" 2>/dev/null || echo goath1)" = goath1 ] || continue
      if systemctl --user is-active -q "goath1-runner@$name.service" && ! busy "$name"; then idle=$((idle + 1)); fi
    done
    echo "$idle"
    ;;
  serve)
    exec python3 "$(dirname "$0")/status.py" "${2:?address}" "$0" "${@:3}"
    ;;
  *) sed -n '2,25p' "$0" >&2; exit 2 ;;
esac
