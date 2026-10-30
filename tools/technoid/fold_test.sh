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

PKG=${PKG:-com.openh1.openh1.fixtest}
DATA=/sdcard/Android/data/$PKG/files
PLAN=${1:-discards}
ROUNDS=${2:-2}
MAP=${MAP:-'levels\a10\a10'}
SEED=${SEED:-3}
TIMING_SECONDS=${TIMING_SECONDS:-90}
PICTURE_SECONDS=${PICTURE_SECONDS:-35}
STAMP=$(date +%Y%m%d-%H%M%S)
OUT=/sdcard/Download/openh1-tests/$PLAN-$STAMP
BACKUP=$OUT/backup
SHOTS=$DATA/test_shots
LOGPID=

say() { echo "$(date +%H:%M:%S) $*" | tee -a "$OUT/run.txt"; }
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

# ---- write_config <aa> <discards 0|1> <timing|pictures> <seconds>
write_config() {
	{
		echo "[display]"
		echo "anti_aliasing = \"$1\""
		echo ""
		echo "[debug]"
		echo "gpu_stats = true"
		echo "skip_cinematics = true"
		echo "test_input = \"bot:$SEED\""
		echo "exit_after = $4"
		echo "host_environment = \"HALO_VULKAN_STATS=1 HALO_VULKAN_DISCARDS=$2\""
		if [ "$3" = pictures ]; then
			echo "frame_time = 0.0166667"
			echo "screenshot_directory = \"$SHOTS\""
			echo "screenshot_every = 30"
			echo "screenshot_from = 3.0"
			echo "screenshot_until = 20.0"
		fi
	} > "$DATA/config.toml"
	printf 'map_name %s\n' "$MAP" > "$DATA/init.txt"
}

# ---- run <name> <aa> <discards> <timing|pictures> <seconds>
run() {
	name=$1; dir=$OUT/$name
	mkdir -p "$dir"
	am force-stop "$PKG"; sleep 2
	rm -rf "$SHOTS"; mkdir -p "$SHOTS"
	write_config "$2" "$3" "$4" "$5"
	cp "$DATA/config.toml" "$dir/config.toml"
	logcat -c
	logcat -v time -s 'halo:*' > "$dir/log.txt" 2>&1 &
	LOGPID=$!
	say "run $name: $2, discards $3, $4, ${5}s"
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
		# a few kept to look at (every fourth)
		i=0
		for f in $(ls "$SHOTS" | sort); do
			[ $((i % 4)) = 0 ] && cp "$SHOTS/$f" "$dir/"
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
		echo "Pictures (frame_time fixed, a shot every 30 frames from 3 s to 20 s):"
		compare_shots p-smaa-d0 p-smaa-d1
		compare_shots p-msaa4x-d0 p-msaa4x-d1
		echo
		echo "From the logs:"
		grep -h -m1 "discards on" "$OUT"/t1-msaa4x-d1/log.txt | sed 's/.*vulkan: /  /'
		echo "  discarded-then-read warnings: $(cat "$OUT"/*/log.txt | grep -c "frame's end and then")"
		echo "  error lines: $(cat "$OUT"/*/log.txt | grep -c " E/halo")"
	} > "$OUT/summary.txt"
}

case "$PLAN" in
	smoke) plan_smoke ;;
	discards) plan_discards ;;
	*) die "no plan $PLAN (smoke, discards)" ;;
esac
say "done"
echo
cat "$OUT/summary.txt"
echo
echo "All of it: $OUT"
