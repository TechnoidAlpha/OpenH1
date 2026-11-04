#!/system/bin/sh
# OpenH1 Dev build test runner, run ON the phone with adb's rights:
#   from a computer:   adb push fold_test.sh /data/local/tmp/ && adb shell sh /data/local/tmp/fold_test.sh discards
#   from Termux:       rish -c 'sh /sdcard/Download/fold_test.sh discards'   (Shizuku)
#                   or adb shell sh ... after pairing Termux's adb with Wireless debugging
#
# Each run writes a test config.toml (and init.txt for the map) into the app's
# folder, starts the app, waits for debug.exit_after to quit it, and keeps the
# log and, for picture runs, the screenshots' checksums. Your own config.toml
# and init.txt are put back at the end, even when the script is stopped.
#
# Results: /sdcard/Download/openh1-tests/<plan>-<time>/summary.txt
#
# Plans:
#   smoke               one short run with stats, to check the setup works
#   discards [rounds]   #302 / #133: HALO_VULKAN_DISCARDS off vs on, SMAA and
#                       MSAA 4x; GPU time per frame, then the pictures compared
#   ao [rounds]         ambient occlusion (display.ambient_occlusion): off vs
#                       strong timed at SMAA and MSAA 4x on the first of
#                       AO_MAPS, then pictures off, subtle, strong and the
#                       occlusion alone (debug.ambient_occlusion_view) on each
#
# Settings you can change in front of the command, as NAME=value:
#   MAP=... SEED=3 TIMING_SECONDS=90 PICTURE_SECONDS=35 KEEP=2 (every KEEPth
#   screenshot copied out) AO_MAPS='levels\b30\b30 levels\a10\a10'

PKG=${PKG:-com.openh1.openh1.fixtest}
DATA=/sdcard/Android/data/$PKG/files
PLAN=${1:-discards}
ROUNDS=${2:-2}
MAP=${MAP:-'levels\a10\a10'}
SEED=${SEED:-3}
TIMING_SECONDS=${TIMING_SECONDS:-90}
PICTURE_SECONDS=${PICTURE_SECONDS:-35}
AO_MAPS=${AO_MAPS:-'levels\b30\b30 levels\a10\a10'}
if [ "$PLAN" = ao ]; then KEEP=${KEEP:-4}; else KEEP=${KEEP:-2}; fi
STAMP=$(date +%Y%m%d-%H%M%S)
OUT=/sdcard/Download/openh1-tests/$PLAN-$STAMP
BACKUP=$OUT/backup
SHOTS=$DATA/test_shots
LOGPID=

say() { echo "$(date +%H:%M:%S) $*" | tee -a "$OUT/run.txt"; }
# a map's file name: levels\b30\b30 -> b30 (printf, as mksh's echo reads \b)
short() { printf '%s\n' "$1" | sed 's/.*\\//'; }
die() { say "STOP: $*"; exit 1; }

mkdir -p "$BACKUP" || { echo "cannot make $OUT"; exit 1; }
[ -d "$DATA/maps" ] || die "$DATA/maps not found: is $PKG installed and has it run once? (needs adb's rights: rish or adb shell)"
touch "$DATA/.write_test" 2>/dev/null && rm -f "$DATA/.write_test" || die "cannot write to $DATA (run it through rish or adb shell, not plain Termux)"

# ---- your files kept and put back
had_config=0; had_init=0
[ -f "$DATA/config.toml" ] && cp "$DATA/config.toml" "$BACKUP/config.toml" && had_config=1
[ -f "$DATA/init.txt" ] && cp "$DATA/init.txt" "$BACKUP/init.txt" && had_init=1
restore() {
	am force-stop "$PKG" 2>/dev/null
	[ -n "$LOGPID" ] && kill "$LOGPID" 2>/dev/null
	if [ $had_config = 1 ]; then cp "$BACKUP/config.toml" "$DATA/config.toml"; else rm -f "$DATA/config.toml"; fi
	if [ $had_init = 1 ]; then cp "$BACKUP/init.txt" "$DATA/init.txt"; else rm -f "$DATA/init.txt"; fi
	rm -rf "$SHOTS"
	say "your config.toml and init.txt are back"
}
trap restore EXIT
trap 'exit 130' INT TERM HUP

# ---- the device
{
	echo "model: $(getprop ro.product.model) ($(getprop ro.product.device)), Android $(getprop ro.build.version.release)"
	echo "soc: $(getprop ro.soc.model) $(getprop ro.hardware.vulkan)"
	echo "screen: $(wm size | tr '\n' ' ')"
	echo "app: $(dumpsys package "$PKG" | grep -m1 versionName | tr -d ' ')"
	echo "battery: $(dumpsys battery | grep -m1 level | tr -d ' '), saver $(settings get global low_power 2>/dev/null)"
	echo "thermal: $(dumpsys thermalservice 2>/dev/null | grep -m1 'Thermal Status' | tr -d ' ')"
} > "$OUT/device.txt"
tee -a "$OUT/run.txt" < "$OUT/device.txt"
case "$(dumpsys power | grep -m1 mWakefulness=)" in
	*Awake*) ;;
	*) die "the screen is off: unlock the phone, unfold it, and run again" ;;
esac

# ---- write_config <aa> <discards 0|1|"" (the build's default)> <timing|pictures> <seconds> [ao] [ao view 0|1]
write_config() {
	{
		echo "[display]"
		echo "anti_aliasing = \"$1\""
		[ -n "$5" ] && echo "ambient_occlusion = \"$5\""
		# timing runs unpaced, as #302 measured: a paced GPU clocks down instead
		[ "$3" = timing ] && echo "vsync = false"
		echo ""
		echo "[debug]"
		echo "gpu_stats = true"
		echo "skip_cinematics = true"
		echo "test_input = \"bot:$SEED\""
		echo "exit_after = $4"
		echo "host_environment = \"HALO_VULKAN_STATS=1${2:+ HALO_VULKAN_DISCARDS=$2}\""
		[ "$6" = 1 ] && echo "ambient_occlusion_view = true"
		if [ "$3" = pictures ]; then
			echo "frame_time = 0.0166667"
			echo "screenshot_directory = \"$SHOTS\""
			echo "screenshot_every = 60"
			echo "screenshot_from = 3.0"
			echo "screenshot_until = 20.0"
		fi
	} > "$DATA/config.toml"
	printf 'map_name %s\n' "$MAP" > "$DATA/init.txt"
}

# ---- run <name> <aa> <discards> <timing|pictures> <seconds> [ao] [ao view 0|1]
run() {
	name=$1; dir=$OUT/$name
	mkdir -p "$dir"
	am force-stop "$PKG"; sleep 2
	rm -rf "$SHOTS"; mkdir -p "$SHOTS"
	write_config "$2" "$3" "$4" "$5" "$6" "$7"
	cp "$DATA/config.toml" "$dir/config.toml"
	logcat -c
	logcat -v time -s 'halo:*' > "$dir/log.txt" 2>&1 &
	LOGPID=$!
	say "run $name: $(short "$MAP"), $2, discards ${3:-default}, ao ${6:-default}${7:+ view $7}, $4, ${5}s"
	am start -n "$PKG/com.halo.decomp.LauncherActivity" > /dev/null 2>&1
	waited=0
	while [ -z "$(pidof "$PKG:game")" ] && [ $waited -lt 40 ]; do sleep 1; waited=$((waited + 1)); done
	if [ -z "$(pidof "$PKG:game")" ]; then
		say "  the game did not start"; kill $LOGPID; LOGPID=; return 1
	fi
	limit=$(($5 + 120)); waited=0
	while [ -n "$(pidof "$PKG:game")" ] && [ $waited -lt $limit ]; do sleep 2; waited=$((waited + 2)); done
	if [ -n "$(pidof "$PKG:game")" ]; then
		say "  still running after ${limit}s: stopped"; am force-stop "$PKG"
	fi
	sleep 1; kill $LOGPID 2>/dev/null; LOGPID=
	grep -m1 "debug.host_environment" "$dir/log.txt" > /dev/null || say "  WARNING: no debug.host_environment line: is this the Dev build with it?"
	grep -m1 "discards on" "$dir/log.txt" | sed 's/.*vulkan: /  /' | tee -a "$OUT/run.txt"
	grep -m1 "swapchain" "$dir/log.txt" | sed 's/.*vulkan: /  /' | tee -a "$OUT/run.txt"
	echo "  discarded then read (warnings): $(grep -c "frame's end and then" "$dir/log.txt")" | tee -a "$OUT/run.txt"
	# GPU ms a frame for each 600-frame window, the first two (loading) left out
	grep "ms a frame" "$dir/log.txt" | sed 's/.*GPU \([0-9.]*\) ms a frame, \([0-9.]*\) ms between.*/\1 \2/' > "$dir/gpu.txt"
	awk 'NR > 2 { g += $1; f += $2; n++ } END { if (n) printf "%.3f %.3f %d\n", g / n, f / n, n; else print "- - 0" }' \
		"$dir/gpu.txt" > "$dir/gpu_mean.txt"
	read g f n < "$dir/gpu_mean.txt"
	say "  GPU $g ms a frame, $f ms between frames ($n windows of 600 frames)"
	if [ "$4" = pictures ]; then
		( cd "$SHOTS" && md5sum *.bmp 2>/dev/null ) > "$dir/shots.md5"
		say "  $(wc -l < "$dir/shots.md5") screenshots"
		# a few kept to look at (every KEEPth one, about 17 MB each)
		i=0
		for f in $(ls "$SHOTS" | sort); do
			[ $((i % KEEP)) = 0 ] && cp "$SHOTS/$f" "$dir/"
			i=$((i + 1))
		done
	fi
}

compare_shots() {
	a=$OUT/$1/shots.md5; b=$OUT/$2/shots.md5
	total=$(wc -l < "$a")
	same=$(awk 'NR == FNR { h[$2] = $1; next } ($2 in h) && h[$2] == $1 { n++ } END { print n + 0 }' "$a" "$b")
	echo "  $1 vs $2: $same of $total screenshots identical"
}

differ_shots() {
	a=$OUT/$1/shots.md5; b=$OUT/$2/shots.md5
	awk 'NR == FNR { h[$2] = $1; next } ($2 in h) { t++; if (h[$2] != $1) n++ } END { printf "%d of %d", n, t }' "$a" "$b"
}

plan_smoke() {
	run smoke smaa 1 timing 40
	{
		echo "smoke test"
		cat "$OUT/device.txt"
		echo
		grep -m1 "debug.host_environment" "$OUT/smoke/log.txt" | sed 's/.*: debug/debug/'
		grep -m1 "discards on" "$OUT/smoke/log.txt" | sed 's/.*vulkan: //'
		echo "GPU ms a frame, each window: $(cut -d' ' -f1 "$OUT/smoke/gpu.txt" | tr '\n' ' ')"
	} > "$OUT/summary.txt"
}

plan_discards() {
	r=1
	while [ $r -le "$ROUNDS" ]; do
		# off first in odd rounds, on first in even ones, so the heat evens out
		if [ $((r % 2)) = 1 ]; then order="0 1"; else order="1 0"; fi
		for aa in smaa msaa4x; do
			for d in $order; do run "t$r-$aa-d$d" $aa $d timing "$TIMING_SECONDS"; done
		done
		r=$((r + 1))
	done
	for aa in smaa msaa4x; do
		for d in 0 1; do run "p-$aa-d$d" $aa $d pictures "$PICTURE_SECONDS"; done
	done
	{
		echo "OpenH1 #302 / #133: HALO_VULKAN_DISCARDS off vs on, a10, bot:$SEED"
		cat "$OUT/device.txt"
		echo
		echo "GPU time per frame (mean of the 600-frame windows after the first two):"
		echo "AA      discards  round  GPU ms/frame  ms between frames  windows"
		for f in "$OUT"/t*/gpu_mean.txt; do
			n=$(basename "$(dirname "$f")")
			read g b w < "$f"
			rr=$(echo "$n" | sed 's/^t\([0-9]*\)-.*/\1/')
			aa=$(echo "$n" | sed 's/^t[0-9]*-\([a-z0-9]*\)-d.*/\1/')
			case "$n" in *-d1) d=on ;; *) d=off ;; esac
			printf "%-7s %-9s %-6s %-13s %-18s %s\n" "$aa" "$d" "$rr" "$g" "$b" "$w"
		done
		echo
		echo "Pictures (frame_time fixed, a shot every 60 frames from 3 s to 20 s):"
		compare_shots p-smaa-d0 p-smaa-d1
		compare_shots p-msaa4x-d0 p-msaa4x-d1
		echo
		echo "From the logs:"
		grep -h -m1 "discards on" "$OUT"/t1-msaa4x-d1/log.txt | sed 's/.*vulkan: /  /'
		echo "  discarded-then-read warnings: $(cat "$OUT"/*/log.txt | grep -c "frame's end and then")"
		echo "  error lines: $(cat "$OUT"/*/log.txt | grep -c " E/halo")"
	} > "$OUT/summary.txt"
}

plan_ao() {
	for MAP in $AO_MAPS; do
		[ -f "$DATA/maps/$(short "$MAP").map" ] || die "no $DATA/maps/$(short "$MAP").map: set AO_MAPS to maps you have"
	done
	timing_map=${AO_MAPS%% *}
	MAP=$timing_map; m=$(short "$MAP")
	r=1
	while [ $r -le "$ROUNDS" ]; do
		# off first in odd rounds, strong first in even ones, so the heat evens out
		if [ $((r % 2)) = 1 ]; then order="off strong"; else order="strong off"; fi
		for aa in smaa msaa4x; do
			for ao in $order; do run "t$r-$m-$aa-$ao" $aa "" timing "$TIMING_SECONDS" $ao 0; done
		done
		r=$((r + 1))
	done
	for MAP in $AO_MAPS; do
		m=$(short "$MAP")
		for ao in off subtle strong; do run "p-$m-$ao" smaa "" pictures "$PICTURE_SECONDS" $ao 0; done
		run "p-$m-view" smaa "" pictures "$PICTURE_SECONDS" strong 1
	done
	{
		echo "OpenH1 ambient occlusion (display.ambient_occlusion), bot:$SEED"
		cat "$OUT/device.txt"
		echo
		echo "GPU time per frame on $(short "$timing_map") (mean of the 600-frame windows after the first two):"
		echo "AA      AO      round  GPU ms/frame  ms between frames  windows"
		for f in "$OUT"/t*/gpu_mean.txt; do
			n=$(basename "$(dirname "$f")")
			read g b w < "$f"
			set -- $(printf '%s\n' "$n" | tr '-' ' ')
			printf "%-7s %-7s %-6s %-13s %-18s %s\n" "$3" "$4" "${1#t}" "$g" "$b" "$w"
		done
		echo
		echo "Pictures (SMAA, frame_time fixed, a shot every 60 frames from 3 s to 20 s);"
		echo "off and the others should differ, as that is the occlusion laid:"
		for MAP in $AO_MAPS; do
			m=$(short "$MAP")
			echo "  $m: off vs subtle $(differ_shots p-$m-off p-$m-subtle) differ," \
				"off vs strong $(differ_shots p-$m-off p-$m-strong)," \
				"strong vs the occlusion alone $(differ_shots p-$m-strong p-$m-view)"
		done
		echo
		echo "From the logs:"
		echo "  'passes cannot be made' warnings: $(cat "$OUT"/*/log.txt | grep -c "ambient_occlusion's passes cannot be made")"
		echo "  error lines: $(cat "$OUT"/*/log.txt | grep -c " E/halo")"
	} > "$OUT/summary.txt"
}

case "$PLAN" in
	smoke) plan_smoke ;;
	discards) plan_discards ;;
	ao) plan_ao ;;
	*) die "no plan $PLAN (smoke, discards, ao)" ;;
esac
say "done"
echo
cat "$OUT/summary.txt"
echo
echo "All of it: $OUT"
# one file to send back (toybox tar on the phone)
if tar -czf "$OUT.tar.gz" -C "$(dirname "$OUT")" "$(basename "$OUT")" 2>/dev/null; then
	echo "Packed: $OUT.tar.gz ($(du -m "$OUT.tar.gz" | cut -f1) MB)"
fi
