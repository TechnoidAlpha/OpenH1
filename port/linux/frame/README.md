# Halo VR for the Steam Frame

The VR build of the Linux port (`configure.py --vr`, `ninja linux_arm64`:
64-bit ARM, drawn with Vulkan), packaged
by `tools/frame_package.py` for a Steam Frame. Each release has it as
[OpenH1-Steam-Frame.zip](https://github.com/OpenH1/goath1/releases/latest/download/OpenH1-Steam-Frame.zip)
(the package's `halo-vr-frame` folder). Refer to "VR" and "Steam Frame" in
`port/linux/README.md` of the repository.

| File | Is |
| --- | --- |
| `halo` | The game |
| `libSDL3.so.0` | SDL3, which the game finds beside it |
| `halo-vr.sh` | The launcher: the target of the Steam shortcut, which installs the game's updates when it quits for one. Its comments list the environment it gives the game. |
| `openh1.png` | The icon of the menu entry and the Steam shortcut |
| `openh1.desktop` | The menu entry, which the install makes the installed game's |
| `steam-*.png` | Steam's library artwork for the shortcut: the capsule, the wide capsule, the hero, the logo and the icon |
| `install.sh` | The installer (`tools/linux_install.sh`, the Linux release's too): opened on the Frame, it installs the package it is in |
| `brokers.txt` | Internet play's MQTT brokers (`network.brokers_file`): without it, internet play finds no games |
| `VERSION` | The commit the game was built from |
| `SHA256SUMS` | The files' SHA-256, which the install checks |
| `*-LICENSE.txt`, ... | The licenses of the libraries in the game |

## The game's data

The game plays the maps of an Xbox disc of the game: the `maps` folder,
in `~/apps/halo-vr/maps` (beside `halo`), or the folder that contains
`maps/` named by `paths.data` in `config.toml`. The package does not
contain them, and the install does not touch them.

## Install, or update

On the Frame, in its desktop mode: download the latest release's
[OpenH1-Steam-Frame.zip](https://github.com/OpenH1/goath1/releases/latest/download/OpenH1-Steam-Frame.zip)
(or an older one from the
[Releases](https://github.com/OpenH1/goath1/releases) page), unzip it, and
open `install.sh` in its `halo-vr-frame` folder. It installs the package
into `~/apps/halo-vr/` (as below), in a terminal window that shows what it
did. At the first install it asks for the maps folder of the game's Xbox
disc (or the folder that contains it) and copies the maps, adds the game
to Steam (SteamOS's `steamos-add-to-steam`) as OpenH1 VR, and gives it
its library artwork (in Steam's grid folder; a running Steam may show it
only after it restarts). Start SteamVR, then the game from the library.
The game updates itself: started from Steam (through `halo-vr.sh`), it
looks for a newer release, downloads its package while you play, and
asks at the main menu, in the headset, whether to update. UPDATE quits
the game; `halo-vr.sh` installs the package with its `install.sh` (as
below) and starts the new version. NOT NOW asks again at the next start.
Settings > Network Setup > Check for Updates turns it off. (Refer to
"Updates" in `port/linux/README.md` of the repository.) To update by hand,
do the same as the first install with a newer zip.

The install refuses while the game runs, and never starts it. It checks
the files against `SHA256SUMS`; each goes in as `<name>.new`, then takes
the old one's name at once. The replaced `halo` is kept as `halo.prev`
(only the last one), as are `libSDL3.so.0` and `halo-vr.sh` when they
change. `brokers.txt` is replaced, and files an earlier package had and
this one has not are removed. It prints what changed. It never touches
`config.toml`, `halo-vr.env`, the maps, the logs or the saved games, nor
artwork of your own in Steam's grid folder.

Or from a
computer with the repository's `tools/frame_install.sh` and `ssh` to the
Frame (in its desktop mode, with SSH on):

```sh
tools/frame_install.sh --package OpenH1-Steam-Frame.zip <frame>
tools/frame_install.sh <frame>          # dist/halo-vr-frame, built from the repository
```

It runs the same install on the Frame, without its questions, and
without adding the game to Steam (that needs the Frame's own session).
Add it once, on the Frame: in Steam's desktop mode, Games > Add a
Non-Steam Game to My Library, tick OpenH1 VR (the menu entry the install
writes, `~/.local/share/applications/openh1-vr.desktop`), then Add
Selected Programs. The install prints the same until Steam has such a
shortcut; install again afterwards to give it its artwork.

Or by hand: copy the files of the zip's `halo-vr-frame` folder into
`~/apps/halo-vr/`, and add the shortcut as above.

To go back to the previous build: in `~/apps/halo-vr`, `mv halo.prev halo`
(and `libSDL3.so.0.prev`, `halo-vr.sh.prev` likewise, where they are).

## Uninstall

Remove the shortcut in Steam (its Properties > Manage > Remove
non-Steam game from your library), then the program's files:

```sh
cd ~/apps/halo-vr && cut -c 67- SHA256SUMS | xargs rm -f -- && rm -rf SHA256SUMS .steam-art update.partial update-ready halo.prev libSDL3.so.0.prev halo-vr.sh.prev ~/.local/share/applications/openh1-vr.desktop
```

That keeps the maps, `config.toml`, `halo-vr.env` and the logs; remove
`~/apps/halo-vr` to remove those too. The saved games are in `~/.local/share/halo-linux`.

## Settings and logs

| Where | What |
| --- | --- |
| `~/apps/halo-vr/config.toml` | The settings (written at the first start) |
| `~/apps/halo-vr/halo-vr.env` | Your own environment for the game (`HALO_*=value` lines), read by `halo-vr.sh`. Updates leave it. Steam's launch options for the shortcut (`HALO_VR=0 %command%`) work too. |
| `~/apps/halo-vr/halo-vr.log` | The game's output when started from Steam, the previous run's in `halo-vr.log.prev`. A crash's backtrace is here. |
| `debug.txt` in the data folder | The game's log (`~/apps/halo-vr/debug.txt` with the maps beside `halo`) |
| `~/.local/share/halo-linux` | The saved games and the map cache |

Start SteamVR before the game. Without a headset the game plays flat, in
its window.

## Measuring performance

- Where a frame's time goes: put `HALO_GPU_STATS=1` and `HALO_VR_GPU_TIME=1`
  in `halo-vr.env` (or `gpu_stats = true` and `vr_gpu_time = true` under
  `[debug]` in `config.toml`). Every 300 frames `halo-vr.log` then has
  `vr: a frame's draws: ...` (the render thread's time before the eyes, on
  the eyes, and on the HUD and the present) and
  `vr: elapsed render GPU timeline per frame: ...` (the GPU's, from the
  frame's first commands to its last, gaps included); every 600 frames,
  `vulkan: ... frames: ... passes, ... draws`.
- The GPU's busy time alone, from the kernel, with nothing turned on in the
  game: copy `tools/gpu_busy.py` here and, over ssh while the game plays,
  `python3 gpu_busy.py --hz 120` (the headset's refresh rate). Near the
  frame's budget (8.33 ms at 120 Hz), the GPU holds the frame rate; well
  under it while the frame rate is still short, the CPU does.
