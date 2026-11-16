#!/usr/bin/env bash
# Runs the Windows build under Wine on a Linux computer with a GPU: a test
# bench for the Windows build without a Windows machine. Refer to "Testing
# under Wine on Linux" in port/windows/README.md.
#
#   tools/wine_bench.sh setup
#   tools/wine_bench.sh fetch [latest | <tag> | <PR> | run:<run id> | <zip URL, zip or folder>] [--debug]
#   tools/wine_bench.sh run [--visible] [--build <name>] [--seconds <s>] [--map <map>]
#           [--bots <n>] [--size <w>x<h>] [--refresh <Hz>] [--vsync] [--menus]
#           [VAR=value...] [-- <halo.exe's arguments>]
#   tools/wine_bench.sh shot [run's options] [--every <frames>] [--max <count>]
#   tools/wine_bench.sh logs [<run>]
#   tools/wine_bench.sh doctor
#
# setup: the Wine prefix, the bench's folders and its link to the game data.
# fetch: a Windows build into the bench (it becomes the current one): the
#   latest release's OpenH1-Windows.zip (--debug: OpenH1-Windows-debug.zip),
#   a release's by its tag (build-515), a pull request's (or a workflow
#   run's) CI artifact, halo-windows-debug, with gh (logged in once: gh auth
#   login); or a zip's URL, a zip or a folder. Releases need no login.
# run: the current build (or --build's). Without --visible, on a display of
#   its own that nobody sees (gamescope's, on the GPU), with silent
#   sound and no internet play, a game of bots (--map, default bloodgulch;
#   --bots, default 7; local:<map>:slayer) for --seconds (default 30), then
#   it quits and prints the renderer and the frame rate (gamescope paces
#   the frames at --refresh Hz, default 1000). --visible: on this
#   computer's own display (someone at it, or over remote desktop), with
#   sound, at the menus unless --map is given. VAR=value sets a setting's
#   variable (HALO_*, port/linux/README.md) or Wine's for the run; they win
#   over the bench's.
# shot: run, saving a frame every --every frames (default 300), at most
#   --max (default 12), to the run's shots/ (debug.screenshot_*).
# logs: a run's game log (debug.txt), its frame rate and Wine's errors (the
#   last run's by default).
# doctor: what the bench needs, and the GPU the game draws with.
#
# The bench is OPENH1_BENCH (default ~/openh1-wine, of whoever runs it):
# prefix/ (Wine's), builds/<name>/ (current: the last fetched),
# runs/<date>-<build>/ (each run's data root, with maps/ linked to the game
# data, its debug.txt, wine.log and shots), xdg/ (the headless display's own
# settings). The game data is OPENH1_BENCH_MAPS, a maps folder: by default
# /srv/openh1/maps where the computer has one, else ~/halo-x86-maps/maps,
# else ~/openh1-wine/data/maps. Retail
# maps are for local tests: shots show the game's content, so they stay on
# this computer, out of the repository and out of public places.
set -euo pipefail

bench=${OPENH1_BENCH:-$HOME/openh1-wine}
# the game data: OPENH1_BENCH_MAPS, else the shared copy a computer's admin
# may put in /srv/openh1/maps for every account on it, else
# ~/halo-x86-maps/maps (where the x86 tools keep them), else the bench's own
if [ -n "${OPENH1_BENCH_MAPS:-}" ]; then
	maps=$OPENH1_BENCH_MAPS
elif [ -f /srv/openh1/maps/bloodgulch.map ]; then
	maps=/srv/openh1/maps
elif [ -f "$HOME/halo-x86-maps/maps/bloodgulch.map" ]; then
	maps=$HOME/halo-x86-maps/maps
else
	maps=$bench/data/maps
fi
custom_maps=${OPENH1_BENCH_CUSTOM_MAPS:-}
repository=${OPENH1_BENCH_REPO:-OpenH1/goath1}
prefix=$bench/prefix
self=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")

usage() {
	sed -n '6,13p' "$0" | sed 's/^# \{0,1\}//' >&2
	exit 2
}

fail() {
	echo "wine_bench: $*" >&2
	exit 1
}

# Wine's environment for every command: the bench's prefix, no menu entries
# or file associations made in the user's desktop (winemenubuilder), no
# Mono or Gecko installers
wine_environment() {
	export WINEPREFIX=$prefix
	export WINEDLLOVERRIDES=${WINEDLLOVERRIDES:-mscoree,mshtml=,winemenubuilder.exe=d}
	export WINEDEBUG=${WINEDEBUG:-err+all,fixme-all}
}

# a Linux path as the game takes it (Wine's Z: drive is /)
windows_path() {
	printf 'Z:%s' "${1//\//\\}"
}

command_setup() {
	command -v wine >/dev/null || fail "Wine is not installed (pacman -S wine, apt install wine)"
	mkdir -p "$bench/builds" "$bench/runs" "$bench/xdg"
	wine_environment
	if [ ! -f "$prefix/system.reg" ]; then
		echo "wine_bench: making the Wine prefix $prefix"
		WINEDEBUG=-all wineboot -i >/dev/null 2>&1
	fi
	# OpenGL through GLX, not EGL: with Wine 11's EGL (its default on X11),
	# SDL finds no pixel format for the game's window on NVIDIA's driver
	WINEDEBUG=-all wine reg add 'HKCU\Software\Wine\X11 Driver' /v UseEGL /t REG_SZ /d N /f >/dev/null
	# a crash ends the run instead of waiting on Wine's crash dialog
	WINEDEBUG=-all wine reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f >/dev/null
	wineserver -w
	mkdir -p "$bench/data"
	[ -f "$maps/bloodgulch.map" ] ||
		echo "wine_bench: no game data at $maps: put the maps folder there, or name it with OPENH1_BENCH_MAPS" >&2
	echo "wine_bench: the bench is $bench ($(wine --version))"
}

# the folder of a build's halo.exe
build_folder() {
	local name=${1:-current} folder
	folder=$bench/builds/$name
	[ -d "$folder" ] || fail "no build $name in $bench/builds (wine_bench.sh fetch)"
	folder=$(cd "$folder" && pwd -P)
	[ -f "$folder/halo.exe" ] || fail "no halo.exe in $folder"
	echo "$folder"
}

# puts a downloaded build (a folder or a zip, which can have its files in a
# folder of their own) in builds/<name>, and makes it the current one
install_build() {
	local name=$1 source=$2 staging executable
	staging=$bench/builds/.$name.$$
	rm -rf "$staging"
	mkdir -p "$staging"
	if [ -d "$source" ]; then
		cp -a "$source/." "$staging/"
	else
		unzip -q "$source" -d "$staging"
	fi
	executable=$(find "$staging" -name halo.exe -print -quit)
	[ -n "$executable" ] || { rm -rf "$staging"; fail "no halo.exe in $source"; }
	rm -rf "$bench/builds/$name"
	mv "$(dirname "$executable")" "$bench/builds/$name"
	rm -rf "$staging"
	ln -sfn "$name" "$bench/builds/current"
	echo "wine_bench: $bench/builds/$name is the current build"
}

command_fetch() {
	local what=latest asset=OpenH1-Windows artifact=halo-windows-debug download tag sha run name
	while [ $# -gt 0 ]; do
		case $1 in
		--debug) asset=OpenH1-Windows-debug; shift ;;
		--asset) [ $# -ge 2 ] || usage; asset=$2; shift 2 ;;
		--artifact) [ $# -ge 2 ] || usage; artifact=$2; shift 2 ;;
		-*) usage ;;
		*) what=$1; shift ;;
		esac
	done
	mkdir -p "$bench/builds"
	download=$(mktemp -d "$bench/builds/.download.XXXXXX")
	if [[ $what =~ ^https?:// ]]; then
		echo "wine_bench: $what"
		curl -fL --progress-bar -o "$download/build.zip" "$what" || { rm -rf "$download"; fail "cannot download $what"; }
		name=$(basename "${what%%\?*}")
		install_build "url-${name%.zip}" "$download/build.zip"
	elif [ -e "$what" ]; then
		install_build "local-$(basename "${what%.zip}")" "$(cd "$(dirname "$what")" && pwd)/$(basename "$what")"
	elif [[ $what =~ ^[0-9]+$ ]] || [[ $what =~ ^run:[0-9]+$ ]]; then
		command -v gh >/dev/null || fail "a CI artifact needs gh, logged in (gh auth login)"
		if [[ $what =~ ^run: ]]; then
			run=${what#run:}
			name=run-$run
		else
			# the pull request's newest run with the artifact (its Windows
			# job done, though the others may still be going): the head's, or
			# else an earlier commit's
			local branch head candidate
			head=$(gh pr view "$what" --repo "$repository" --json headRefOid -q .headRefOid)
			branch=$(gh pr view "$what" --repo "$repository" --json headRefName -q .headRefName)
			run=
			for candidate in $(gh run list --repo "$repository" --workflow build.yml --branch "$branch" --limit 10 \
				--json databaseId,headSha -q '.[] | "\(.databaseId):\(.headSha)"'); do
				if gh run download "${candidate%%:*}" --repo "$repository" --name "$artifact" \
					--dir "$download/build" >/dev/null 2>&1; then
					run=${candidate%%:*}
					sha=${candidate#*:}
					break
				fi
			done
			[ -n "$run" ] ||
				{ rm -rf "$download"; fail "no $artifact in pull request $what's last 10 runs (CI keeps artifacts 3 days)"; }
			[ "$sha" = "$head" ] ||
				echo "wine_bench: the head ${head:0:8} has no $artifact yet: taking ${sha:0:8}'s" >&2
			name=pr-$what-${sha:0:8}
		fi
		echo "wine_bench: $artifact of run $run (https://github.com/$repository/actions/runs/$run)"
		[ -d "$download/build" ] || gh run download "$run" --repo "$repository" --name "$artifact" --dir "$download/build" ||
			{ rm -rf "$download"; fail "no $artifact in run $run: not built yet, failed, or over 3 days old"; }
		install_build "$name" "$download/build"
	else
		if [ "$what" = latest ]; then
			tag=$(curl -fsSLI -o /dev/null -w '%{url_effective}' "https://github.com/$repository/releases/latest")
			tag=${tag##*/}
		else
			tag=$what
		fi
		[ -n "$tag" ] || fail "cannot find the latest release of $repository"
		echo "wine_bench: $asset.zip of $tag"
		curl -fL --progress-bar -o "$download/$asset.zip" \
			"https://github.com/$repository/releases/download/$tag/$asset.zip" ||
			{ rm -rf "$download"; fail "no $asset.zip in release $tag"; }
		name=$tag
		[ "$asset" = OpenH1-Windows ] || name=$tag-${asset#OpenH1-Windows-}
		install_build "$name" "$download/$asset.zip"
	fi
	rm -rf "$download"
}

# headless: a display of the run's own that no one sees: gamescope's
# headless backend (by default), whose Xwayland draws OpenGL with the GPU
# and paces the frames at <refresh> Hz even with vsync off (it crashes as it
# quits, after the game); or, with OPENH1_BENCH_DISPLAY=xvfb, an Xvfb of the
# run's own, whose OpenGL is Mesa's llvmpipe, drawn by the processor (Vulkan
# goes to the GPU's driver where it can present to Xvfb). The display, and
# the game, get settings folders of their own (xdg/), and gamescope a D-Bus
# of its own, so that they touch nothing of the desktop's session.
headless() {
	local width=$1 height=$2 refresh=$3 status
	shift 3
	unset DISPLAY WAYLAND_DISPLAY
	export XDG_CONFIG_HOME=$bench/xdg/config XDG_DATA_HOME=$bench/xdg/data
	export XDG_STATE_HOME=$bench/xdg/state XDG_CACHE_HOME=$bench/xdg/cache
	mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_STATE_HOME" "$XDG_CACHE_HOME"
	case ${OPENH1_BENCH_DISPLAY:-gamescope} in
	xvfb)
		command -v Xvfb >/dev/null || fail "no Xvfb (xorg-server-xvfb, xvfb)"
		local number xvfb fifo
		fifo=$(mktemp -u "$bench/.xvfb.XXXXXX")
		mkfifo "$fifo"
		Xvfb -displayfd 3 -screen 0 "${width}x${height}x24" -nolisten tcp +extension GLX 3>"$fifo" &
		xvfb=$!
		read -r number <"$fifo"
		rm -f "$fifo"
		[ -n "$number" ] || fail "Xvfb did not start"
		status=0
		DISPLAY=:$number "$@" || status=$?
		kill "$xvfb" 2>/dev/null
		wait "$xvfb" 2>/dev/null
		return $status
		;;
	gamescope)
		command -v gamescope >/dev/null || fail "no gamescope (or OPENH1_BENCH_DISPLAY=xvfb, with Xvfb)"
		(ulimit -c 0; dbus-run-session -- gamescope --backend headless -W "$width" -H "$height" -r "$refresh" -- "$@")
		;;
	*) fail "OPENH1_BENCH_DISPLAY is xvfb or gamescope" ;;
	esac
}

# (in the display, from run): the game, its output timestamped, then the
# prefix's Wine processes stopped
command_inner() {
	local log=$1
	shift
	set +e
	(
		wine halo.exe "$@"
		echo "wine_bench: halo.exe exited with $?"
	) 2>&1 | while IFS= read -r line; do printf '%s %s\n' "$EPOCHREALTIME" "$line"; done >>"$log"
	wineserver -k 2>/dev/null
	wineserver -w 2>/dev/null
	return 0
}

command_run() {
	local visible=0 build=current seconds= map= bots=7 size=1280x720 refresh=1000 vsync=0 menus=0 every=0 most=12
	local -a settings=() arguments=()
	while [ $# -gt 0 ]; do
		case $1 in
		--visible) visible=1; shift ;;
		--build) [ $# -ge 2 ] || usage; build=$2; shift 2 ;;
		--seconds) [ $# -ge 2 ] || usage; seconds=$2; shift 2 ;;
		--map) [ $# -ge 2 ] || usage; map=$2; shift 2 ;;
		--bots) [ $# -ge 2 ] || usage; bots=$2; shift 2 ;;
		--size) [ $# -ge 2 ] || usage; size=$2; shift 2 ;;
		--refresh) [ $# -ge 2 ] || usage; refresh=$2; shift 2 ;;
		--vsync) vsync=1; shift ;;
		--menus) menus=1; shift ;;
		--every) [ $# -ge 2 ] || usage; every=$2; shift 2 ;;
		--max) [ $# -ge 2 ] || usage; most=$2; shift 2 ;;
		--) shift; arguments+=("$@"); break ;;
		*=*) settings+=("$1"); shift ;;
		*) usage ;;
		esac
	done
	[[ $size =~ ^[0-9]+x[0-9]+$ ]] || fail "--size is <width>x<height>"
	[ -f "$prefix/system.reg" ] || fail "no Wine prefix: wine_bench.sh setup"
	[ -e "$maps" ] || fail "no game data at $maps (OPENH1_BENCH_MAPS)"

	local folder name run data started
	folder=$(build_folder "$build")
	name=$(basename "$folder")
	run=$bench/runs/$(date +%Y%m%d-%H%M%S)-$name
	data=$run/data
	mkdir -p "$data"
	ln -s "$(cd "$maps" && pwd -P)" "$data/maps"
	[ -z "$custom_maps" ] || ln -s "$(cd "$custom_maps" && pwd -P)" "$data/custom_maps"
	ln -sfn "$(basename "$run")" "$bench/runs/last"

	# one run at a time: they share the prefix, whose processes each stops
	exec 9>"$bench/lock"
	flock -n 9 || fail "another run of the bench is going (wait for it, or stop it)"

	wine_environment
	export HALO_DATA_ROOT=$(windows_path "$data")
	# the bench's settings: never an update, a crash report sent, an invite
	# put on the clipboard, a public listing, a port opened on the router
	export HALO_UPDATE_AUTO=false HALO_CRASH_REPORTS=no HALO_NO_CLIPBOARD=1 HALO_NET_ALLOW_UPNP=false
	export HALO_NET_PUBLIC_LOBBY=false HALO_FULLSCREEN=false HALO_WINDOW_SIZE=$size HALO_GPU_STATS=1
	[ $visible = 1 ] && [ -z "$map" ] && [ -z "$seconds" ] && menus=1
	if [ $visible = 0 ]; then
		case ${OPENH1_BENCH_DISPLAY:-gamescope} in
		gamescope) command -v gamescope >/dev/null || fail "no gamescope (or OPENH1_BENCH_DISPLAY=xvfb, with Xvfb)" ;;
		xvfb) command -v Xvfb >/dev/null || fail "no Xvfb (xorg-server-xvfb, xvfb)" ;;
		*) fail "OPENH1_BENCH_DISPLAY is gamescope or xvfb" ;;
		esac
		# no internet play, system link on the loopback, silent sound, the
		# game 3 seconds after it is hosted (not 15)
		export HALO_NET_ONLINE=false HALO_NET_ADDRESS=127.0.0.1 SDL_AUDIO_DRIVER=dummy HALO_NETWORK_TEST_START=3
		[ $vsync = 1 ] || export HALO_NO_VSYNC=1
		seconds=${seconds:-30}
	fi
	if [ $menus = 0 ]; then
		export HALO_NETWORK_TEST=local:${map:-bloodgulch}:slayer HALO_BOTS=$bots
	fi
	[ -z "$seconds" ] || export HALO_EXIT_AFTER=$seconds
	if [ "$every" -gt 0 ]; then
		mkdir -p "$run/shots"
		chmod 700 "$run/shots"
		export HALO_SCREENSHOT_DIR=$(windows_path "$run/shots") HALO_SCREENSHOT_EVERY=$every
	fi
	local setting
	for setting in "${settings[@]}"; do
		export "${setting?}"
	done
	env | grep -E '^(HALO_|SDL_|WINE)' | sort >"$run/environment.txt"

	cd "$folder"
	echo "wine_bench: $name in $run"
	started=$(date +%s)
	if [ $visible = 1 ]; then
		if [ -z "${DISPLAY:-}" ]; then
			# the desktop's Xwayland, from an ssh session
			export DISPLAY=:0
			XAUTHORITY=${XAUTHORITY:-$(ls /run/user/"$(id -u)"/xauth_* 2>/dev/null | head -1)}
			export XAUTHORITY
			echo "wine_bench: on the display :0 of this computer"
		fi
		command_inner "$run/wine.log" "${arguments[@]}"
	else
		# (the timeout leaves time for the loading: debug.exit_after counts
		# from the window's opening)
		printf '#!/bin/sh\nexec %q _inner %q' "$self" "$run/wine.log" >"$run/inner.sh"
		printf ' %q' "${arguments[@]}" >>"$run/inner.sh"
		echo >>"$run/inner.sh"
		chmod +x "$run/inner.sh"
		timeout -k 10 $((seconds + 120)) "$self" _headless "${size%x*}" "${size#*x}" "$refresh" "$run/inner.sh" \
			>"$run/display.log" 2>&1 ||
			echo "wine_bench: the display ended with $? (display.log)" >&2
	fi
	echo "wine_bench: $(($(date +%s) - started)) s"
	# the log of a release build (it has no console): halo.log beside halo.exe
	[ ! -f halo.log ] || cp halo.log "$run/halo.log"
	if [ "$every" -gt 0 ]; then
		prune_shots "$run/shots" "$most"
	fi
	summary "$run"
}

# at most <most> shots, spread over the run
prune_shots() {
	local folder=$1 most=$2 count index=0 keep
	count=$(find "$folder" -maxdepth 1 -type f | wc -l)
	if [ "$count" -gt "$most" ]; then
		find "$folder" -maxdepth 1 -type f | sort | while read -r file; do
			keep=$(((index * most) / count != ((index + 1) * most) / count))
			[ "$keep" = 1 ] || rm -f "$file"
			index=$((index + 1))
		done
	fi
	echo "wine_bench: $(find "$folder" -maxdepth 1 -type f | wc -l) shots in $folder (the game's content: keep them on this computer)"
}

# a run's renderer, frame rate (from debug.gpu_stats' line every 60 frames,
# timestamped as it came), long frames and Wine's errors
summary() {
	local run=$1
	[ -f "$run/wine.log" ] || fail "no wine.log in $run"
	echo "== $run"
	grep -hE 'halo-linux: (OpenGL|display:|screen:|SDL_|cannot|Direct3D)|vulkan: ([^ ]+ GPU|NVIDIA|AMD|Intel|llvmpipe|swapchain)|halo.exe exited' \
		"$run/wine.log" |
		cut -d' ' -f2- || true
	python3 -I - "$run/wine.log" <<'EOF'
import re, statistics, sys

times = []
loaded = None
for line in open(sys.argv[1], errors="replace"):
    stamp, _, text = line.partition(" ")
    try:
        stamp = float(stamp)
    except ValueError:
        continue
    # the game is playing from the network test's first tick
    if loaded is None and "network test: tick" in text:
        loaded = stamp
    match = re.search(r"halo-linux: frame (\d+):", text)
    if match:
        times.append((stamp, int(match.group(1))))
if loaded is None and times:
    loaded = times[0][0]
playing = [(t, f) for t, f in times if loaded is not None and t >= loaded + 2.0]
if len(playing) < 2:
    print("frame rate: too few frames logged while playing (%d)" % len(playing))
    sys.exit(0)
rates = [(f1 - f0) / (t1 - t0) for (t0, f0), (t1, f1) in zip(playing, playing[1:]) if t1 > t0]
span = playing[-1][0] - playing[0][0]
print("frame rate while playing: %.1f fps average over %.1f s (%d frames); each 60 frames: min %.1f, median %.1f, max %.1f"
      % ((playing[-1][1] - playing[0][1]) / span, span, playing[-1][1] - playing[0][1], min(rates),
         statistics.median(rates), max(rates)))
EOF
	if [ -f "$run/data/debug.txt" ]; then
		grep -E 'a frame of [0-9]+ ms|EXCEPTION|crash|assert' "$run/data/debug.txt" | head -5 || true
	fi
	local errors
	errors=$(grep -E ':err:' "$run/wine.log" | cut -d' ' -f2- | sed -E 's/^[0-9a-f]{4}://' | sort | uniq -c |
		sort -rn | head -15 || true)
	if [ -n "$errors" ]; then
		echo "Wine's errors (count, channel, message):"
		echo "$errors"
	else
		echo "Wine's errors: none"
	fi
}

command_logs() {
	local run=${1:-last}
	[ -d "$run" ] || run=$bench/runs/$run
	[ -d "$run" ] || fail "no run $1 in $bench/runs"
	run=$(cd "$run" && pwd -P)
	if [ -f "$run/data/debug.txt" ]; then
		echo "== debug.txt"
		cat "$run/data/debug.txt"
	fi
	if [ -f "$run/halo.log" ]; then
		echo "== halo.log"
		cat "$run/halo.log"
	fi
	summary "$run"
}

command_doctor() {
	local status=0
	echo "== Wine: $(wine --version 2>/dev/null || echo 'not installed')"
	[ -f "$prefix/system.reg" ] && echo "prefix: $prefix" || { echo "prefix: none (wine_bench.sh setup)"; status=1; }
	echo "== GPU"
	nvidia-smi --query-gpu=name,driver_version,memory.used,memory.total --format=csv,noheader 2>/dev/null ||
		lspci 2>/dev/null | grep -iE 'vga|3d' || true
	echo "== Vulkan (the host's: Wine's winevulkan goes to it)"
	vulkaninfo --summary 2>/dev/null | grep -E 'deviceName|driverName|apiVersion' | sort -u ||
		echo "vulkaninfo: none (vulkan-tools)"
	echo "== OpenGL in the headless display (${OPENH1_BENCH_DISPLAY:-gamescope})"
	if command -v glxinfo >/dev/null; then
		local renderer
		renderer=$(timeout -k 5 30 "$self" _headless 640 480 60 "$(command -v glxinfo)" 2>/dev/null || true)
		grep -E 'OpenGL (renderer|version) string' <<<"$renderer" ||
			{ echo "no OpenGL in the headless display"; status=1; }
	else
		echo "glxinfo: none (mesa-utils)"
	fi
	echo "== game data: $maps"
	[ -f "$maps/bloodgulch.map" ] && ls "$maps" | tr '\n' ' ' && echo || { echo "none"; status=1; }
	echo "== builds"
	ls -l "$bench/builds" 2>/dev/null | sed 1d || echo "none (wine_bench.sh fetch)"
	return $status
}

[ $# -ge 1 ] || usage
command=$1
shift
case $command in
setup) command_setup "$@" ;;
fetch) command_fetch "$@" ;;
run) command_run "$@" ;;
shot)
	case " $* " in *" --every "*) command_run "$@" ;; *) command_run --every 300 "$@" ;; esac ;;
logs) command_logs "$@" ;;
doctor) command_doctor ;;
_headless) headless "$@" ;;
_inner) wine_environment; command_inner "$@" ;;
-h|--help) usage ;;
*) usage ;;
esac
