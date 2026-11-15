# goat-rig: the test machines' shared kit

Build and run the game on **omarchy.fabric** (32 cores, has the MCC maps) and **5900x.fabric** (24 cores) without
cloning, building from zero or writing run scripts. Everything lives in `~/goat-rig` on each machine.

From a Mac checkout, `tools/rig/goat-rig @omarchy <command>` (or `@5900x`) runs the command on that machine over
ssh. The 5900X's fish shell never sees the arguments, so quote them as you would for bash. On the machine itself,
run `~/goat-rig/bin/goat-rig <command>`. The ssh target is `<name>.fabric` unless `~/.config/goat-rig/hosts` on the
Mac gives a `<name> <user@host>` line for it.

```sh
tools/rig/goat-rig @omarchy build my-branch          # prints the binary's path
tools/rig/goat-rig @omarchy run look --build my-branch --shots at:12 -- HALO_NETWORK_TEST=local:bloodgulch:slayer
tools/rig/goat-rig fetch omarchy:look ~/Desktop/look  # pictures, video, logs back to the Mac
```

## Recipes

**1. One game, one picture** (12 s into the game; `at:12+4` keeps 5 frames):
```sh
goat-rig run shot --build my-branch --seconds 40 --shots at:12 -- \
  HALO_NETWORK_TEST=local:bloodgulch:slayer HALO_NETWORK_TEST_START=2
```

**2. Host and join, two instances** (Slayer; `bot:N` plays each one):
```sh
goat-rig run join --pair host:my-branch client:my-branch --seconds 70 --shots every:1200 -- \
  HALO_NETWORK_TEST=host:bloodgulch:slayer HALO_NETWORK_TEST_START=15 HALO_NETWORK_TEST_NAME=Hosty HALO_TEST_INPUT=bot:1 -- \
  HALO_NETWORK_TEST=join HALO_NETWORK_TEST_NAME=Joiny HALO_TEST_INPUT=bot:2
```
The settings after the first `--` are the host's, after the second the client's (`--env VAR=…` sets one for all).

**3. Split screen with player 2's input** (`p2 ` in `script:`, `p2:` in `menu:`): player 2 walks, then pauses.
```sh
goat-rig run split-pause --seconds 30 --shots at:10 -- HALO_NETWORK_TEST=local:bloodgulch:slayer \
  HALO_NETWORK_TEST_PLAYERS=2 HALO_NETWORK_TEST_START=2 "HALO_TEST_INPUT=script:5-7 p2 move=0,1; 8 p2 start"
```

**4. Co-op campaign, intro skipped** (a10). Both players vote to skip the intro with A, at 35 s and 39 s
on the menus' clock. The host's debug.txt then logs `co-op: skipping the cutscene (2 of 2 voted)`. The cue runs
a console line at a game second, counted from when the host's player has a unit.
```sh
V="menu:$(printf 'wait %.0s' $(seq 35))a wait wait wait a $(printf 'wait %.0s' $(seq 70))"
goat-rig run coop --pair host:my-branch client:my-branch --seconds 110 --shots every:900 -- \
  'HALO_NETWORK_TEST=host:levels\a10\a10:coop' HALO_NETWORK_TEST_START=12 \
  'HALO_NETWORK_TEST_CUES=40 player_enable_input 1' "HALO_TEST_INPUT=$V" -- HALO_NETWORK_TEST=join "HALO_TEST_INPUT=$V"
```
After the skip, the host stays in the cryo pod and the client spectates it ("You join them when they're on
foot"), even with the cue. The cross-play check let the intro play out and gave input back with
`'HALO_NETWORK_TEST_CUES=128 player_enable_input 1'` and `HALO_NETWORK_TEST_CUE_PLAYERS=1`.
Friends, invites and party follow need `--online` (a local MQTT broker and STUN inside the run's network). Use
`{run:<instance>}` for another instance's folder: `"HALO_FRIENDS=3 add @{run:client}/friends.txt"`.

**5. Fast headless determinism**:
```sh
goat-rig exec my-branch -- python3 tools/scorecard/identity.py --repeat                       # must pass
goat-rig exec my-branch -- python3 tools/scorecard/identity.py --repeat --mutation unseeded   # must fail
goat-rig run soak --fast --map bloodgulch --seconds 20 -- HALO_TEST_INPUT=bot:1   # about 200x real time
```
`--fast` draws nothing and runs at a fixed step, so it holds no GPU lock and several runs can go at once. Only a
game of one machine (`--map`) runs faster than real time. A `local:` or networked game runs at real time.

**6. Video at 60 fps** (each frame is 1/60 s of game time; `HALO_SCREENSHOT_FROM/UNTIL` pick the seconds):
```sh
goat-rig run clip --seconds 25 --video 60 -- HALO_NETWORK_TEST=local:bloodgulch:slayer HALO_NETWORK_TEST_START=2 \
  HALO_SCREENSHOT_FROM=8 HALO_SCREENSHOT_UNTIL=12 "HALO_TEST_INPUT=script:6-14 move=0,1; 8-12 look=0.3,0"
```

**7. OpenCE 359 as one side** (omarchy only; the build must have cross-play):
```sh
goat-rig run xplay --pair host:opence client:my-branch --seconds 75 --shots every:1500 -- \
  HALO_NETWORK_TEST=host:bloodgulch:slayer HALO_NETWORK_TEST_START=15 -- HALO_NETWORK_TEST=join
```
OpenCE always uses port 5150, so a pair with it runs only inside the run's private network, which never reaches
the real one.

**8. MCC maps** (omarchy only; never copy `~/goat-rig/mcc-maps` off omarchy). `--mcc host` gives the first
instance the maps and the others only `_shared` (to test the download). `--mcc all` gives every instance the maps.
```sh
goat-rig run mcc --mcc host --seconds 70 --shots at:25 -- \
  'HALO_NETWORK_TEST=host:mcc_maps\3021510694\mp_plains:slayer' HALO_NETWORK_TEST_START=10
```

## What it does for you

- **build** `<branch|sha|pr/N|local branch>` fetches `~/goat-rig/repo.git` (a bare clone of GitHub's
  OpenH1/goath1) and checks the commit out in `~/goat-rig/builds/<branch>`. It configures with the shared ccache
  (`--compiler-launcher ~/goat-rig/bin/rig-ccache`, 30 GB) and builds `halo` and `openh1.assets` under nice, with
  all cores but 2 and at most 2 builds per machine. A new worktree copies `build/*/third_party` from another, so
  configure downloads nothing again. Never edit inside `builds/`: each build resets the worktree to its commit.
  For a branch that must stay off GitHub, push it to the mirror:
  `git push <user@omarchy>:goat-rig/repo.git HEAD:refs/heads/local/<name>`, then `goat-rig build local/<name>`.
  `--clean` runs `ninja -t clean` first, `--release` adds configure.py's `--release`, and extra targets go after the
  ref.
- **run** starts 1 to 4 instances (`--players N`, or `--pair`), each in `~/goat-rig/runs/<label>/<instance>` with
  its own home, data, saves and config. Each is hidden and muted, with no clipboard, updates, Discord, UPnP,
  masters, public brokers or STUN. The instances share a private network (`unshare -rn`: only its loopback). Each
  has its own address, 127.0.0.200, .201 and so on (never 127.0.0.1, which the game takes as this computer only),
  and all share port 25150. The broadcast is 127.255.255.255 plus the peers. Map share and seed caps are lifted
  (20480 KB/s), and the random seed is fixed. A run that draws holds the machine's GPU lock
  (`~/goat-rig/locks/gpu.lock`; older scripts link their lock to it). It never starts while the machine owner's
  own game (`~/Applications/OpenH1/bin/openh1`) runs, and stops if that game starts. Afterwards it keeps `halo.log`, `debug.txt`, the pictures (as PNG),
  `video.mp4` and `summary.txt` (exit codes, failed asserts, peak RSS) and deletes the rest unless you pass
  `--keep`.
- **Settings after `--`:** `VAR=value`; `-VAR` unsets; `@data/<file>=<text>` writes a file into the data folder
  (`@data/init.txt=…`); `@link/<path>=<target>` and `@copy/<path>=<source>` link or copy into it.
- **exec** `<branch> -- <command>` runs a tree's own tool in its worktree, niced, inside a private network.
- **fetch**, **clean** `[--older-than DAYS] [--builds]`, **status** (locks, disk, cache).

Install or update the kit from a checkout: `tools/rig/install.sh omarchy` (or `5900x`). It needs no root. It
pins ccache 4.14.2's static release, checked against its SHA-256.
