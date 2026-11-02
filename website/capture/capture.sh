#!/bin/bash
# capture.sh <run-name> <original|openh1> <scenario> <script> <seconds> [extra init lines]
#
# One side of a comparison, offscreen, a BMP a frame: the Original (the Classic
# preset, one frame a tick, the Xbox's 640x480) or OpenH1 (Enhanced, frames between
# ticks, at SIZE). Settings: TREE (a built linux_x86_64 tree), MAPS, RUNS, SIZE,
# HIDE_HUD (0 keeps it), PORT (the console's, distinct for runs side by side), CONSOLE (commands sent with
# the HUD's, as "(cheat_all_weapons) "), FT (OpenH1's frame time),
# SEED, TRACE_SECONDS (write the sim trace to data/trace.txt and quit then),
# CUSTOM_MAPS (a custom_maps folder), SPATIAL (audio.spatial, with AUDIO).
set -u
# (PRIVATE_NET: the run in a network of its own, only its loopback - a user
# network namespace - so that local games side by side never see each other)
if [ -n "${PRIVATE_NET:-}" ] && [ -z "${IN_PRIVATE_NET:-}" ]; then
  exec unshare -rn env IN_PRIVATE_NET=1 bash -c 'ip link set lo up && exec "$0" "$@"' "$0" "$@"
fi
TREE=${TREE:-$HOME/halo-capture} MAPS=${MAPS:-$HOME/halo-x86-maps/maps}
RUNS=${RUNS:-$HOME/halo-capture-runs} SIZE=${SIZE:-1280x960}
name=$1 side=$2 scenario=$3 script=$4 seconds=$5 extra=${6:-}
root=$RUNS/$name
rm -rf "$root"; mkdir -p "$root/data" "$root/saves" "$root/frames"
ln -s "$MAPS" "$root/data/maps"
# (CUSTOM_MAPS: a custom_maps folder, Custom Edition maps and their shared maps)
[ -n "${CUSTOM_MAPS:-}" ] && ln -s "$CUSTOM_MAPS" "$root/data/custom_maps"
# (NETTEST: a debug.network_test session, "local:<map>:<variant>" for a local
# game with bots, instead of the scenario loaded directly; its HUD is hidden
# once the network test logs the game, HUD_AFTER)
if [ -n "${NETTEST:-}" ]; then
  printf '%s' "$extra" > "$root/data/init.txt"; export HALO_NETWORK_TEST=$NETTEST
  HUD_AFTER=${HUD_AFTER:-network test: tick}
else
  printf 'map_name %s\n%s' "$scenario" "$extra" > "$root/data/init.txt"
fi
export HALO_DATA_ROOT=$root/data HALO_SAVE_ROOT=$root/saves HALO_NET_ONLINE=false \
  HALO_DISPLAY_MODE=windowed HALO_NO_AUDIO=1 HALO_UPDATE_ANSWER=no HALO_NO_CLIPBOARD=1 \
  HALO_EXIT_AFTER=$seconds HALO_SCREENSHOT_DIR=$root/frames HALO_SCREENSHOT_EVERY=1 \
  HALO_TEST_INPUT="script:$script" HALO_HDR=off SDL_VIDEO_DRIVER=offscreen \
  HALO_TELNET_CONSOLE=true HALO_TELNET_CONSOLE_PORT=${PORT:-2399}
# (no sound device, ever: the machine's own speakers are someone else's)
export SDL_AUDIO_DRIVER=dummy
if [ "$side" = original ]; then
  export HALO_GRAPHICS=classic HALO_INTERPOLATION=false HALO_RESOLUTION_SCALING=original \
    HALO_WINDOW_SIZE=$SIZE HALO_FRAME_TIME=0.0333333
else
  export HALO_GRAPHICS=enhanced HALO_INTERPOLATION=true HALO_RESOLUTION_SCALING=native \
    HALO_WINDOW_SIZE=$SIZE HALO_FRAME_TIME=${FT:-0.0166667}
fi
[ -n "${SEED:-}" ] && export HALO_RANDOM_SEED=$SEED
# (AUDIO: the game's sound on, recorded to $root/sound.wav in step with the
# frames - debug.audio_record with the fixed frame time - and no device)
if [ -n "${AUDIO:-}" ]; then
  unset HALO_NO_AUDIO; export HALO_AUDIO_RECORD=$root/sound.wav
  [ -n "${SPATIAL:-}" ] && export HALO_AUDIO_SPATIAL=$SPATIAL
fi
# (a relative path, which lands in the data root: an absolute one cannot be opened)
[ -n "${TRACE_SECONDS:-}" ] && export HALO_TRACE=trace.txt HALO_TRACE_SECONDS=$TRACE_SECONDS
# (STREAM_FPS: the frames piped to the GPU's HEVC encoder at that rate as
# they are drawn, $root/stream.mp4, none on the disk; the Original's are the
# Xbox's 640x480)
if [ -n "${STREAM_FPS:-}" ]; then
  [ "$side" = original ] && frame_size=640x480 || frame_size=$SIZE
  mkfifo "$root/frames.fifo"
  ffmpeg -loglevel error -y -f rawvideo -pix_fmt bgra -s "$frame_size" -r "$STREAM_FPS" -i "$root/frames.fifo" \
    -c:v hevc_nvenc -preset p5 -rc vbr -cq "${STREAM_CQ:-20}" -b:v 0 -tag:v hvc1 -pix_fmt yuv420p "$root/stream.mp4" &
  encoder=$!
  export HALO_SCREENSHOT_STREAM=$root/frames.fifo
fi
cd "$TREE"
start=$SECONDS
timeout $((${seconds%.*} + 60)) ./build/linux_x86_64/halo > "$root/log.txt" 2>&1 &
game=$!
# hide the HUD once the map is drawing (a command sent while it loads can stop the game)
# (and CONSOLE's commands sent then, with the HUD kept: HIDE_HUD=0)
hide='(show_hud 0) '; [ "${HIDE_HUD:-1}" = 1 ] || hide=
{ [ -n "$hide" ] || [ -n "${CONSOLE:-}" ]; } && ( for i in $(seq 3000); do grep -q "${HUD_AFTER:-its shaders and pipelines made}" "$root/log.txt" 2>/dev/null && break; sleep 0.1; done
  sleep 0.5
  exec 3<>/dev/tcp/127.0.0.1/$HALO_TELNET_CONSOLE_PORT && printf '(begin %s%s(sleep 1))\r\n' "$hide" "${CONSOLE:-}" >&3 && sleep 1 && exec 3>&-
  echo "console sent at frame $(ls $root/frames | wc -l)" ) &
wait $game
status=$?
if [ -n "${STREAM_FPS:-}" ]; then
  # (a game that never reached its window never opened the pipe: opened and
  # closed here, so that the encoder ends)
  kill -0 $encoder 2> /dev/null && { exec 4> "$root/frames.fifo"; exec 4>&-; }
  wait $encoder
  rm -f "$root/frames.fifo"
fi
echo "exit=$status took=$((SECONDS - start))s frames=$(ls $root/frames | wc -l)"
wait
