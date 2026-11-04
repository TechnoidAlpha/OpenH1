# Capturing the site's clips

The scripts that capture, survey and encode the comparison clips for
openh1.com (`README.md`, "Pictures and clips", lists them). They run on a Linux machine
with a built `linux_x86_64` tree (`python configure.py --release`, `ninja
linux_x86_64`) and the retail maps, drawn offscreen (SDL's offscreen driver),
a BMP a frame.

| Script | What it does |
| --- | --- |
| `capture.sh` | One side of a pair: the Original (the Classic preset, one frame a tick, the Xbox's 640 × 480) or OpenH1 (Enhanced, frames between ticks, at `SIZE`); the HUD hidden through the telnet console once the map draws |
| `survey.py` | Candidate spots for a clip, both sides each, ten games at a time: scored from the sim trace and the frames, the frames then deleted, a few kept as PNGs |
| `rank.py` | The survey's usable candidates, best first, and a contact sheet of them |
| `watch.sh` | Prints each survey result as it lands |
| `encode.sh` | A pair's MP4, WebM and poster, frame for frame, the Original scaled by nearest neighbour |
| `final.py` | A final pair: both sides captured, the loop's end found (the Original's frame most like the first), the masters encoded (`encode.sh`, CRF 16) into `final/out` |
| `encode-web.sh` | The page's clips from a pair's masters: cropped, scaled (the Original by nearest neighbour), MP4, WebM and poster |
| `audio_scout.py` | Windows of local games with bots where the most is heard around the player, from the sim trace: driven vehicles passing close, one in the air above, one behind, enemy rounds passing the eye |
| `audio_clip.py` | The 3D audio clip: a window played twice side by side, `audio.spatial` "headphones" and "speakers", each mix recorded in step with its frames, the first's frames the master; the two sim traces compared |
| `encode-audio-web.sh` | Its web files: the two mixes lined up to the sample (cross-correlated), the video without sound (MP4, WebM, poster), each mix as Opus and AAC |

Settings are environment variables: `TREE` (the built tree), `MAPS`, `RUNS`
(where runs go), `SIZE` (OpenH1's window), `PORT` (the console's; distinct
for runs side by side), `FT` (OpenH1's frame time: 1/60 for the clips, 1/30
in a survey), `SEED`, `TRACE_SECONDS`; for `encode.sh`, `WIDTH`, `HEIGHT`,
`MP4_CRF`, `WEBM_CRF`, `FADE`; `CUSTOM_MAPS` (a `custom_maps` folder linked into
the run's data), `AUDIO` (the sound recorded, no device) and `SPATIAL`
(its `audio.spatial`). No run opens a sound device (`SDL_AUDIO_DRIVER=dummy`).

## How a pair lines up

Both sides play the same script (`debug.test_input`, timed on the
simulation's clock) with fixed frame times, so the simulation is the same
tick for tick. The Original draws a frame a tick; OpenH1 at 1/60 draws two.
A frame's tick is found from the sim trace: the run's frames are its
loading frames and then one a tick, so the frame of tick `k` is the frame
count less the trace's ticks plus `k`. (An `aim=` in the script snaps the
view at once, which also makes a clear mark in both.)

For 4K: OpenH1 at `SIZE=3840x2880` (4:3, the Xbox's field of view), the
Original's 640 × 480 scaled six times (`WIDTH=3840 HEIGHT=2880`); a 16:9
crop of both (3840 × 2160) keeps the six.

## Loops without a fade

A clip loops by cutting from its last frame to its first, so the two must
match. For the grenade: one grenade thrown before the clip starts, so the
scorch mark is already there, and a second at the same spot in the clip,
which ends when its smoke has gone and the frame most like the first comes.
A spot is no good if the blast moves or hurts the player (the view jumps at
the cut): the survey measures both from the trace.

## Running them

On the capture machine, the scripts in a folder of their own (here
`~/halo-capture-runs/tools`), the runs beside it:

```sh
# a survey: candidates scored, the best drawn on a sheet
python tools/survey.py ratrace,wizard --out survey-lights            # grenades (the default script)
python tools/rank.py survey-lights 8                                 # survey-lights/sheet.png
# a final pair at 4K, its loop chosen, masters encoded into final/out
python tools/final.py lights "levels\test\ratrace\ratrace" "0 aim=90,-5;1 throw;5 throw" 4.5 7.6 9.8 --seed 7
```

`survey.py` takes `--yaws`, `--pitch` (lists), `--script` (with `{yaw}` and
`{pitch}`), `--event boom|fire`, `--console` (commands sent once the map
draws), `--seconds` (of simulation, then it quits), `--seed`. Its games run
with `nohup` so that a dropped connection does not stop them; `follow.sh
<log> <pattern>` prints a log as it grows.

Things learnt doing it:

- Do not copy a script over one that is running: bash reads a script as it
  runs it. Copy it beside, then `mv`.
- `debug.trace` must be a relative path (it lands in the data root); the
  game cannot open an absolute one.
- `debug.trace_seconds` quits the game at a time of simulation: much
  sooner than `debug.exit_after`'s wall clock.
- The trace has more steps than ticks (a tick can have several): count
  frames by the steps' ticks.
- A multiplayer spawn did not change with the seed on the maps tried.
- `show_hud 0` in `init.txt` comes too early (and prints its result): send
  it through the console once the map draws, as `capture.sh` does.
- A grenade thrown at a wall close by comes back and kills the player
  (35 of the 80 lights candidates): the survey drops those.

### A weapon the player does not start with

`cheat_all_weapons` (sent with `--console`) drops the globals' weapon list
in a ring 1.5 world units around the player, 22.5° apart, in the list's
order (`game_engine.c`: assault rifle, flamethrower, gravity rifle, needler,
pistol, plasma pistol, plasma rifle, rocket launcher, shotgun, sniper rifle,
ball, flag, frag and plasma grenades; `cheats.c`, `cheat_objects`). Walk to
one and hold action to swap it in; the trace's `e new` lines name what the
weapon fires, which says what was picked up. On Rat Race (seed 7): aim
yaw 26 to 41 and pitch -25, walk forward 0.5 s at 3 s, action at 4.5 s:
the plasma rifle, every time (yaw 45 to 65 gives the rocket launcher, 75
to 105 the sniper rifle).

## Surveys

`surveys/<clip>-<date>/`: `results.jsonl` (a line a candidate), `sheet.webp`
(the best eight: the Original and OpenH1 at the blast, then the loop's first
and last frames) and `picks.webp` (the ones chosen to look at).

### lights, 2026-10-20

Ten multiplayer maps (Derelict, Rat Race, Prisoner, Damnation, Chill Out,
Boarding Action, Hang 'Em High, Longest, Wizard, Chiron TL-34), the seed 7
spawn, eight aims (yaw 0 to 315, pitch -5), a grenade at 1 s and another at
5 s. 80 candidates: 35 killed the player (the first grenade bounced back),
31 usable (no knockback or damage, the same spot twice, a loop of at least
33 dB). The best by eye:

1. Rat Race, yaw 90: a side room; the Original lights the whole room and its
   ceiling yellow, OpenH1 a pool around the blast. Loop 42 dB.
2. Rat Race, yaw 0: a dark pillared corridor, lit far down by the Original.
   Loop 39 dB.
3. Wizard, yaw 45: the courtyard's back wall, lit evenly by the Original.
   Loop 43 dB.
4. Hang 'Em High, yaw 180: milder, mostly the left wall.

The two that scored highest (Hang 'Em High yaw 135, Damnation yaw 45) have
the blast at the frame's edge or in a bright room. The `light_diff` score
is the mean difference between the light each side adds at the blast (the
blast frame less one before it), so it counts the sprite's edges too: look
at the sheet.

## Machines

- The 5900x (RTX 4090): no hangs; about 3 s a candidate in a survey, ten
  games at a time.
- omarchy (Radeon 8060S, RADV): the game stops on the loading screen when
  drawn offscreen there (RADV has headless surfaces, so it makes a
  swapchain; with vsync on or off). Not looked into.
- macOS: see "Capturing on macOS: two problems", below.

### hero, 2026-10-20

The hero trailer, multiplayer only (the campaign and co-op a later pass):
local matches with fifteen legendary bots, the first player played by one
(`debug.bot_player`, with a rocket launcher or a sniper rifle given at each
spawn, or a Warthog to board), scouted (`scout.py`, 42 matches over the
famous maps), previewed (30 at 960 × 540) and eight chosen and filmed again
at 3840 × 2160 (`shoot.py --only`), then cut (`cut.py`): a sniper double,
the magnum's triple, rockets into groups, a sniper double on Longest, a
rocket's body thrown at the camera. `surveys/hero-2026-10-20/hero.json`
has all it takes to render it again: the build's commit (a match replays
exactly only on the build that scouted it), each step's command, the picks
in order, the cut's beats and each shot's window and kills;
`moments.json` is the scout's, `previews/` the previews' notes and sheets.

### glow, edges and strafe, 2026-10-21

4K masters with `final.py` (seed 7), then the page's clips with
`encode-web.sh`:

```sh
# glow: the plasma rifle picked up (cheat_all_weapons), a burst at 6 s to leave the scorch marks, the clip's at 10 s
CONSOLE="(cheat_all_weapons) " python tools/final.py glow 'levels\test\ratrace\ratrace' \
  "0 aim=35,-25;3-3.5 move=0,1;4.5-5 action;5.5 aim=0,0;6-7.2 fire;10-11.2 fire" 9.5 12.6 14.5 --seed 7
tools/encode-web.sh glow glow 3840:2880:0:0 1280x960
# edges: Blood Gulch's spawn faces the cliffs; a slow strafe out and back, a quarter of the frame at 4x
python tools/final.py edges 'levels\test\bloodgulch\bloodgulch' "3-5.5 move=0.4,0;5.5-8 move=-0.4,0" 3.0 7.7 8.8 --seed 7
tools/encode-web.sh edges edges 1920:1080:1200:360 1280x720
# strafe: Rat Race facing yaw 90, past pillars and back; the return's length (1.67 s) found from the trace's positions
python tools/final.py strafe 'levels\test\ratrace\ratrace' "0 aim=90,0;3-4.5 move=1,0;4.5-6.1667 move=-1,0" 3.0 6.4 7.4 --seed 7
tools/encode-web.sh strafe strafe 3840:2160:0:360 1280x720
```

- Glow: the survey's best yaws (247.5 to 292.5) face a wall a few units
  off, where both sides light alike; down the corridor (yaw 0) the bolts
  light the floor in OpenH1 only. Its first takes showed a frame nearly
  white at each shot in OpenH1: a new bolt was drawn a tick back, at the
  eye, where its sprite (and its glow) filled the view. Fixed in the game
  (`render_interpolation.c`: half a tick back; `d3d8_device.c`: glows
  faded near the camera); the clip is from that build.
- `final.py` lines OpenH1 up by the brightest jump in both sides; check its
  log's "guessed" against the one used (the flash above moved it 8
  frames).
- A strafe out and back closes its loop only when the return is timed to
  bring the player back: a strafe of 1.5 s needed 1.67 s back (the
  acceleration), found from ten runs at 640 x 480 a few seconds each.
- At 4K, OpenH1's frames are written at two or three a second, so
  `final.py` gives each game 1500 s of wall time (the trace's end quits it
  first).

### latency, 2026-10-24

The race's two screens (07): one pistol shot on macOS, hidden and silent,
in a local SWAT game on Blood Gulch with no bots (SWAT starts with the
pistol), seed 7, Enhanced, a 1280 × 720 window, frames of a fixed 1/60 s,
the shot scripted and the frames saved only around it:

```sh
HALO_NETWORK_TEST=local:bloodgulch:swat HALO_NETWORK_TEST_START=3 HALO_BOTS=0 \
HALO_GRAPHICS=enhanced HALO_HDR=off HALO_WINDOW_SIZE=1280x720 HALO_FRAME_TIME=0.0166667 HALO_RANDOM_SEED=7 \
HALO_TEST_INPUT='script:5 fire;8 fire' HALO_SCREENSHOT_DIR=frames HALO_SCREENSHOT_EVERY=1 \
HALO_SCREENSHOT_FROM=4.8 HALO_SCREENSHOT_UNTIL=5.4 build/macos/halo
magick frame01591.bmp -crop 1408x792+704+648 +repage -resize 640x360 -quality 74 -define webp:method=6 latency-pistol-idle.webp
magick frame01593.bmp -crop 1408x792+704+648 +repage -resize 640x360 -quality 74 -define webp:method=6 latency-pistol-fire.webp
```

The flash is two frames (the 13th and 14th saved); the second is the
brighter. The frames are 2560 × 1440 (the Retina window), so the crop is in
those pixels.

### 3D audio, 2026-11-01

The clip under the head (08): Blood Gulch with every vehicle (a Custom
Edition map, `bloodgulch_all_vehicles`, with Custom Edition's shared maps
in `CUSTOM_MAPS`), Team Slayer, 15 legendary bots, the player not a bot but
standing still where it spawned, made deathless, and every vehicle dropped
around it at 15 s, all through the console, timed on the game's clock so
that both runs play the same:

```sh
C='(set cheat_deathless_player true)(sleep_until (>= (game_time) 450) 1)(cheat_all_vehicles)'
M='custom_maps\bloodgulch_all_vehicles:team_slayer'
python tools/audio_scout.py "$M" --seeds 7,8,9,10,11,12 --seconds 120 --out scout6 --parallel 3 \
  --console "$C" --env HALO_BOT_PLAYER= --deathless
python tools/audio_clip.py "$M" 10 57.5 --size 1920x1080 --out final --console "$C" \
  --env HALO_BOT_PLAYER= --env HIDE_HUD=0
MP4_CRF=34 WEBM_CRF=50 tools/encode-audio-web.sh final
```

Seed 10 at 57.5 s: a Banshee flies a full circle round the player from
61.8 s (right, behind, left, overhead, landing beside it, round again),
rounds and fuel rod bolts cross the view, the base's fight in front. The
two runs' sim traces were the same, and their mixes lined up within 2
samples.

- The console's line is cut at about 150 characters (the end is read as
  another command): keep `CONSOLE` short.
- A console command sent before the map has loaded is lost; `HUD_AFTER`
  waits for the bots' line, and the wait is five minutes, for runs side by
  side. One that arrives after its `sleep_until`'s time runs at once, and
  the two runs part (two of four candidates did, with eight games
  running at once): film two at a time, the drop at 15 s.
- With the HUD hidden (`show_hud 0`), "Hold BACK for score" stays on the
  screen all game; with the HUD kept it was not shown in the windows
  filmed. The clip keeps the HUD (it has no logos).
- A bot playing the player (the hero's way) fires its own gun throughout,
  and the player's own sounds are not placed: the clip's player holds
  still. Bots all against the player (`bots.teams = "against"`) pin it
  down, the view shaking and darkened; even teams leave it in the fight's
  edge.

### ambient occlusion and Panini, 2026-11-03

Stills, each the same frame twice, from the RTX 4090, Enhanced, offscreen
Vulkan, `HALO_FIXED_STEP=1` and a fixed seed, AI erased, the HUD hidden.
Neither option changes the game, so one still each side shows it.

- Ambient occlusion (05): `tools/preview_rerender`'s `Game`, objects placed
  with `debug_object_place`, the world stopped (`game_speed 0`), the camera
  set with `debug_camera_set`, a frame with `display.ambient_occlusion` off
  and one with `"strong"`; 2560 × 1440, scaled to 1920 × 1080, WebP at
  quality 78.
- Panini (06): the script camera stops the game past 90 degrees
  (`camera_pose.c`), so this is the player's own view from where it starts,
  `HALO_FOV=150`, `HALO_VIEWMODEL_VISIBLE=false`, two runs (`HALO_PANINI` 0
  and 1, `HALO_PANINI_VERTICAL` 0), the 300th frame streamed of each. With
  Panini on, the screen is supersampled (2× at 150), and the screenshot
  stream sends the supersampled frames: a 1280 × 720 window streams
  2560 × 1440, read at that size (its frames began half a frame along, so
  the two halves were swapped back; the scene is still). Scaled to
  1920 × 1080, WebP at quality 78.

### Capturing on macOS: two problems, and the local workarounds used

Neither is fixed in the game. The workarounds below were local edits to
`port/linux/src/d3d8_device.c`, for capturing only, and are not committed.

1. **Interpolation off hangs on the loading screen with a hidden window.**
   With `display.interpolation = false`, `D3DDevice_Present` waits for the
   display's vertical blank (`pending_flips`, at most two frames queued),
   which a hidden window never signals, so the game stops on its first
   frames. The workaround skipped that wait when an environment variable
   was set:

   ```c
   while (pending_flips >= 2 && !getenv("HALO_CAPTURE_NO_VBLANK"))
       pthread_cond_wait(&vertical_blank_condition, &vertical_blank_lock);
   if (!getenv("HALO_CAPTURE_NO_VBLANK"))
       pending_flips++;
   ```

   With it and `HALO_FRAME_TIME=0.0333333`, the Original draws one frame a
   tick, deterministically. (The main loop's throttle still waits up to a
   second for the vertical blank once, then turns itself off: "stuck
   waiting for VBLANK callback".)

2. **A screenshot of every frame or two hangs some loads.** Writing BMPs
   from the first frame, through the loading screen, stopped some runs on
   the loading screen. The workaround started the screenshots at a frame
   number, so loading went untouched:

   ```c
   if (screenshot_every > 0 && device.frame % (unsigned long)screenshot_every == 0
       && device.frame >= (unsigned long)(getenv("HALO_SCREENSHOT_FROM") ? atol(getenv("HALO_SCREENSHOT_FROM")) : 0))
       write_screenshot(back_buffer);
   ```

A machine with more headroom (or a capture path that records the window,
such as OBS at a fixed frame rate) avoids both.
