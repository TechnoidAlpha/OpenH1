# The Dev build and its phone tests

This folder lives only on the fork's `dev` branch (TechnoidAlpha/OpenH1). It
is how we test the maintainers' changes on a real phone, a Galaxy Z Fold7
(Snapdragon 8 Elite, Adreno 830), and send them numbers and pictures.

## What `dev` is

`dev` is upstream `master` plus a few commits that never go upstream:

- its own app id and name, `com.openh1.openh1.fixtest` and "OpenH1 Dev", so it
  installs beside the release build, with an amber icon to tell them apart;
- `debug.host_environment` in config.toml: `NAME=value` pairs, separated by
  spaces, set in the Android app's environment before the game starts. An
  Android app gets no environment from outside, so this is how the renderer's
  environment-only switches (`HALO_VULKAN_STATS`, `HALO_VULKAN_DISCARDS`) are
  reached on a phone. Each one is logged as `debug.host_environment: NAME=value`;
- this folder.

Pull requests are still cut from `master`, never from `dev`.

### Bringing it up to date

```sh
git fetch origin
git checkout dev
git merge origin/master        # a merge, never a rebase: dev is public
git push fork dev              # never --force
```

### Building the APK

From Windows, in the worktree that has `dev` checked out:

```sh
wsl -d Ubuntu -- bash -c 'rm -rf $HOME/halo-build/src/build/android'   # when the tree or branch changed
wsl -d Ubuntu -- bash /mnt/c/path/to/the/worktree/tools/wsl_build_android.sh
```

The APK lands in `dist/android/app-debug.apk`. Every WSL build is signed with
the same debug key, so it installs over the Dev app already on the phone and
keeps its data. Over 30 MB it has to be zipped to send through the Claude app.

## Running a test on the phone

`fold_test.sh` runs on the phone itself with adb's rights. It writes a test
config.toml (and init.txt for the map) into the app's folder, starts the app,
waits for `debug.exit_after` to close it, and keeps the log, the GPU time per
frame and the screenshots. Your own config.toml and init.txt are put back at
the end, even if you stop it part way.

### Before a run

- The Dev app is installed and has been opened once (so its folder and maps
  exist).
- The phone is unfolded, unlocked, on charge and not already hot. Leave it alone
  while it runs: touching the screen moves the player.
- Battery saver is off.

### Getting adb's rights without a computer: rish (Shizuku)

Done once, in Termux:

1. In Shizuku: "Use Shizuku in terminal apps", then "Export files", into a
   folder Termux can read (for example Download).
2. In Termux, copy `rish` and `rish_shizuku.dex` into `~/bin` (make it, and add
   it to `PATH` in `~/.bashrc` if it is not there).
3. In `rish`, change `RISH_APPLICATION_ID="PKG"` to
   `RISH_APPLICATION_ID="com.termux"`.
4. `chmod +x ~/bin/rish` and `chmod 400 ~/bin/rish_shizuku.dex` (Android 14
   and later refuse a dex file that can be written).
5. `rish -c id` should print `uid=2000(shell)`.

Shizuku has to be running (after a reboot, start it again from its app).

### The commands

Get the script (or take it from Discord), then run a plan:

```sh
curl -fsSL -o /sdcard/Download/fold_test.sh https://raw.githubusercontent.com/TechnoidAlpha/OpenH1/dev/tools/technoid/fold_test.sh
rish -c 'sh /sdcard/Download/fold_test.sh smoke'
rish -c 'sh /sdcard/Download/fold_test.sh ao'
```

With a computer and adb instead:

```sh
adb push tools/technoid/fold_test.sh /data/local/tmp/
adb shell sh /data/local/tmp/fold_test.sh ao
```

Always run `smoke` first after a new APK. It takes about a minute and shows
the setup works (the app starts, the settings reach it, the stats come out).

### The plans

| Plan | What it answers | Runs | About |
| --- | --- | --- | --- |
| `smoke` | Does the setup work? | 1 | 1 min |
| `discards [rounds]` | #302 / #133: `HALO_VULKAN_DISCARDS` off vs on at SMAA and MSAA 4x, GPU time and pictures | 8 + 4 | 30 min |
| `ao [rounds]` | Ambient occlusion: `display.ambient_occlusion` off vs strong timed at SMAA and MSAA 4x, then pictures off, subtle, strong and the occlusion alone, on each map | 8 + 8 | 30 min |

Settings go in front of the plan, inside the quotes:

```sh
rish -c 'SEED=5 AO_MAPS="levels\a30\a30" sh /sdcard/Download/fold_test.sh ao 1'
```

| Setting | Default | Meaning |
| --- | --- | --- |
| `MAP` | `levels\a10\a10` | the map for `smoke` and `discards` |
| `AO_MAPS` | `levels\b30\b30 levels\a10\a10` | the maps for `ao`; the first is the one timed |
| `SEED` | 3 | the scripted player (`debug.test_input = "bot:<seed>"`) |
| `TIMING_SECONDS` | 90 | each timing run |
| `PICTURE_SECONDS` | 35 | each picture run (shots from 3 s to 20 s) |
| `KEEP` | 2 (`ao`: 4) | every KEEPth screenshot copied out (about 17 MB each) |

Timing runs are unpaced (`display.vsync = false`), so the GPU runs flat out
instead of clocking down, and the order swaps each round (off first, then on
first) so the phone warming up evens out. The first two 600-frame windows of
each run (loading) are left out of the means.

Picture runs fix the frame time (`debug.frame_time`), so the same frame
numbers show the same moment in every run, and take a shot every 60 frames.

### Results

Everything is in `/sdcard/Download/openh1-tests/<plan>-<date>-<time>/`, and
packed into one file beside it, `<plan>-<date>-<time>.tar.gz`, to send back.

- `summary.txt`: the answer. The device, the GPU time table, how many
  screenshots matched or differed, and the warning and error counts.
- `run.txt`: what happened, run by run.
- `device.txt`: model, Android version, screen, battery, heat.
- one folder per run: its `config.toml`, `log.txt` (logcat, tag `halo`),
  `gpu.txt` (each window's GPU ms and ms between frames), `gpu_mean.txt`,
  `shots.md5`, and the kept screenshots (`frame<number>.bmp`).

Reading `ao`'s summary: off vs subtle and off vs strong should differ in
almost every shot (that is the occlusion being laid); if they match, it did
not run. "passes cannot be made" warnings and error lines should both be 0.

## On the computer: comparison sheets

```sh
python tools/technoid/compare_shots.py <results folder> <sheets folder> p-b30-off p-b30-subtle p-b30-strong
```

One PNG for each screenshot, 1600 pixels wide: the first and last runs' whole
frames, with a box where they differ most, then that box at full size from
each run, and the difference eight times brighter. A contact shadow under a
wheel is lost when a whole frame is shrunk; the close-up keeps it.

## Asking agy to do it

In Termux, paste this into agy (it can run the commands and wait):

> Run the OpenH1 phone test. Download
> https://raw.githubusercontent.com/TechnoidAlpha/OpenH1/dev/tools/technoid/fold_test.sh
> to /sdcard/Download/fold_test.sh (overwrite it). Run
> `rish -c 'sh /sdcard/Download/fold_test.sh smoke'` and show me its summary.
> If it ends in "done" with GPU numbers, run
> `rish -c 'sh /sdcard/Download/fold_test.sh ao'`. It takes about 30 minutes;
> wait for it, do not touch other files, and do not change rish or Shizuku.
> When it finishes, show me summary.txt and tell me the path of the .tar.gz
> it packed.

## What the script cannot see

It compares still frames. Flicker, shimmer, shadows that crawl as the camera
moves, and things showing through walls only show in motion, so each test
ends with a few minutes of playing by hand: the setting on, an outdoor level,
moving and turning, against the same spot with it off.
