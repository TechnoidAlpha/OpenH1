#!/bin/bash
# vr_capture.sh <out folder> <level> <console commands> [VAR=value ...]
#
# Runs the VR build in the folder it is started from (a Steam Frame's
# ~/apps/halo-vr, over ssh) on <level>, sends <console commands> over the
# telnet console once the level has loaded, and saves what the eyes, the HUD
# and the panels drew to <out folder> (debug.screenshot_directory: eyes*.bmp
# is both eyes side by side). The headset need not be worn
# (debug.vr_force_render), and the head is held still and level
# (debug.vr_test_head).
#
# A fixed camera: debug_camera_load reads camera.txt beside config.toml (the
# position, forward and up vectors and the field of view a devmode
# debug_camera_save wrote), and camera_control 1 inside a cinematic keeps the
# game's camera in VR (vr_render.c), so the eyes look from there:
#
#   tools/vr_capture.sh /tmp/shots 'levels\test\bloodgulch\bloodgulch' \
#     '(begin (cinematic_start) (camera_control 1) (debug_camera_load) (show_hud 0))' \
#     HALO_SCREENSHOT_EVERY=97 HALO_VR_FOVEATION=off HALO_VR_TEST_JITTER=2 \
#     'HALO_VR_TEST_FOV=-12.9 21.9 3.7 5.3'
#
# camera.txt for Blood Gulch's red base seen from the blue base's roof
# (#211's and #214's view, about 100 m):
#
#   42.0 -82.0 4.0
#   0.805 -0.593 -0.02
#   0.0 0.0 1.0
#   0.157
#
# The eyes keep the headset's own yaw as it lies, so a narrow view is aimed
# with an off-centre debug.vr_test_fov ("<outward> <inward> <up> <down>",
# degrees): take a wide one first ("45 45 45 45"), find the target, and give
# a few degrees about it. debug.vr_test_jitter moves the head as a worn
# headset does, for what flickers. SECONDS_RUN (36) is how long the game runs,
# LOAD (22) how long the level is given to load.

out=$1; level=$2; commands=$3
shift 3
rm -rf "$out"
mkdir -p "$out"
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$(id -u)} DISPLAY=${DISPLAY:-:1} HALO_UPDATE_ANSWER=no \
	HALO_NET_ONLINE=0 HALO_NO_CLIPBOARD=1 HALO_VR_FORCE_RENDER=1 HALO_VR_TEST_HEAD=0 \
	HALO_EXIT_AFTER=${SECONDS_RUN:-36} HALO_TELNET_CONSOLE=true HALO_TELNET_CONSOLE_PORT=${PORT:-2399} \
	HALO_SCREENSHOT_DIR=$out HALO_SCREENSHOT_EVERY=${HALO_SCREENSHOT_EVERY:-288} \
	SteamAppId=2598787112 SteamGameId=2598787112
for setting in "$@"; do
	export "$setting"
done
printf 'map_name %s\n' "$level" > init.txt
./halo > "$out/log.txt" 2>&1 &
game=$!
sleep "${LOAD:-22}"
exec 3<>/dev/tcp/127.0.0.1/"$HALO_TELNET_CONSOLE_PORT" && printf '%s\r\n' "$commands" >&3 && sleep 1 && exec 3>&-
wait "$game"
rm -f init.txt
ls "$out"/eyes*.bmp 2>/dev/null | tail -1
