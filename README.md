<p align="center">
  <a href="https://openh1.com"><img src="website/public/media/og.jpg" alt="OpenH1: welcome back, soldier" width="100%"></a>
</p>

<h3 align="center">Halo: Combat Evolved, recompiled for Mac, Windows, Linux, Android and VR.</h3>

<p align="center">
  The 2001 game, compiled natively from its decompiled Xbox source. It plays
  exactly as it shipped, and new Vulkan and Metal renderers make it look the
  way you remember.
</p>

<p align="center">
  <a href="https://github.com/OpenH1/OpenH1/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/OpenH1/OpenH1?label=release&color=9dff3c&labelColor=0a0d09"></a>
  <a href="https://github.com/OpenH1/OpenH1/actions/workflows/build.yml"><img alt="Build" src="https://github.com/OpenH1/OpenH1/actions/workflows/build.yml/badge.svg?branch=master"></a>
  <a href="https://discord.gg/MJa327PSWt"><img alt="Discord" src="https://img.shields.io/discord/1561955817463222332?logo=discord&logoColor=white&label=Discord&color=5865F2&labelColor=0a0d09"></a>
  <a href="LICENSE"><img alt="Licence: AGPL-3.0-or-later" src="https://img.shields.io/badge/licence-AGPL--3.0--or--later-eef4ea?labelColor=0a0d09"></a>
</p>

<p align="center">
  <a href="#download"><b>Download</b></a> ·
  <a href="https://openh1.com">openh1.com</a> ·
  <a href="https://discord.gg/MJa327PSWt">Discord</a> ·
  <a href="#game-data">Bring your own game</a> ·
  <a href="#contributing">Contributing</a>
</p>

OpenH1 is a fork of [OpenCE](https://github.com/OpenCommunityEdition/OpenCE),
the community's port, and adds a Mac app with its own Metal renderer, a
Vulkan renderer, VR and more. It keeps OpenCE's netcode, so players of the
two can play together. OpenCE starts from the decompilation of
[bnunu/halo-1](https://github.com/bnunu/halo-1), a fork of
[punpckhdq/halo](https://github.com/punpckhdq/halo), of the Xbox build 2342
(`cachebeta.exe`, SHA-256
`4cc87b45f721270392a96f1674ed2b5cd4a7bb4355faeab4531d1cf1884d9520`).

## Features

- **The original game, natively.** Not emulation: the game's own code, with
  the same 30 ticks a second on every machine, so it plays as it shipped.
- **New renderers.** Vulkan on Linux, Windows, Android and the Steam Frame, Metal on
  the Mac, with SMAA, per-pixel dynamic lights and frames drawn between
  ticks at your display's rate (4K, 120 Hz and up).
- **VR** on the Steam Frame and OpenXR headsets: reload by hand, draw from
  holsters, raise the scope to your eye.
- **128 players** online and on the LAN, up to 4 in split screen on each
  machine, and the campaign in co-op over the internet. Plays with OpenCE.
- **Mods and Custom Edition maps**, from the game's own menus.

| | Original | OpenH1 |
| --- | --- | --- |
| **Dynamic lights**<br><sub>Rat Race, a frag grenade</sub> | <img src="website/public/media/lights-original-poster.webp" alt="A grenade going off in a room as the original drew it: the whole room turns yellow at once" width="360"> | <img src="website/public/media/lights-openh1-poster.webp" alt="The same moment in OpenH1: the light pools around the blast" width="360"> |
| **Anti-aliasing**<br><sub>Blood Gulch, enlarged 4×</sub> | <img src="website/public/media/edges-original-poster.webp" alt="Blood Gulch's cliffs with no anti-aliasing" width="360"> | <img src="website/public/media/edges-openh1-poster.webp" alt="The same cliffs with SMAA" width="360"> |
| **Plasma glow**<br><sub>Blood Gulch, a charged plasma pistol</sub> | <img src="website/public/media/glow-original-poster.webp" alt="A plasma bolt as the original drew it" width="360"> | <img src="website/public/media/glow-openh1-poster.webp" alt="The same bolt with OpenH1's glow" width="360"> |

<sub>Real captures, the same moment twice. [openh1.com](https://openh1.com) has them side by side, with clips.</sub>

## Under the hood

| | |
| --- | --- |
| **Code** | The Xbox game's decompiled C, compiled natively, with few and marked changes. Not emulation |
| **Simulation** | 30 ticks a second, as shipped, with the same calculations on every machine |
| **Frames** | One per refresh of your display, each drawn between the last two ticks; nothing the game calculates changes |
| **Architecture** | A 64-bit program, with the game compiled as a 32-bit-pointer guest inside it (`arm64_32` on ARM, `x32` on x86-64) |
| **Renderers** | One command stream into Vulkan 1.3 on Linux, Windows, Android and in VR, and into Metal on the Mac |
| **Picture** | SMAA or FXAA, anisotropic filtering, sharper shadows, per-pixel lighting, full model detail. One switch: Video Setup › Graphics › Preset, Classic or Enhanced |
| **Display** | Any resolution from 640 × 480, a field of view up to 150°, a HUD redrawn at 8× |
| **Network** | Plays online with OpenCE players; internet play needs no servers to set up |
| **Mods** | Texture, sound, text and model packs from the game's Mods menu; Custom Edition maps that share themselves |
| **Releases** | Every commit is built for every platform, and each build of `master` that passes is a release |

Contributions are welcome: refer to "Contributing" below. Players and
contributors meet on the [OpenH1 Discord](https://discord.gg/MJa327PSWt).

## Download

GitHub Actions builds the game for each commit, and each build of `master`
that passes on all the platforms is a new release. These links download the
builds of the latest release:

| Platform | Release | Debug |
| --- | --- | --- |
| macOS (Apple silicon, macOS 14.4 or later) | [OpenH1.dmg](https://github.com/OpenH1/OpenH1/releases/latest/download/OpenH1.dmg) | [OpenH1-debug.dmg](https://github.com/OpenH1/OpenH1/releases/latest/download/OpenH1-debug.dmg) |
| Windows | [OpenH1-Windows.zip](https://github.com/OpenH1/OpenH1/releases/latest/download/OpenH1-Windows.zip) | [OpenH1-Windows-debug.zip](https://github.com/OpenH1/OpenH1/releases/latest/download/OpenH1-Windows-debug.zip) |
| Linux x86-64 (also the Steam Deck) | [OpenH1-Linux.zip](https://github.com/OpenH1/OpenH1/releases/latest/download/OpenH1-Linux.zip) | [OpenH1-Linux-debug.zip](https://github.com/OpenH1/OpenH1/releases/latest/download/OpenH1-Linux-debug.zip) |
| Linux arm64 | [OpenH1-Linux-ARM64.zip](https://github.com/OpenH1/OpenH1/releases/latest/download/OpenH1-Linux-ARM64.zip) | [OpenH1-Linux-ARM64-debug.zip](https://github.com/OpenH1/OpenH1/releases/latest/download/OpenH1-Linux-ARM64-debug.zip) |
| Linux x86-64 VR | [OpenH1-Linux-VR.zip](https://github.com/OpenH1/OpenH1/releases/latest/download/OpenH1-Linux-VR.zip) | |
| Steam Frame VR ([install](port/linux/frame/README.md#install-or-update)) | [OpenH1-Steam-Frame.zip](https://github.com/OpenH1/OpenH1/releases/latest/download/OpenH1-Steam-Frame.zip) | |
| Android (64-bit, Android 9 or later, Vulkan 1.3) | [OpenH1-Android.apk](https://github.com/OpenH1/OpenH1/releases/latest/download/OpenH1-Android.apk) | [OpenH1-Android-debug.apk](https://github.com/OpenH1/OpenH1/releases/latest/download/OpenH1-Android-debug.apk) |

Use the release build to play. The debug build stops at the first failed
assertion and writes it to the log. Use the debug build to find and report
problems. The [Releases](https://github.com/OpenH1/OpenH1/releases) page
keeps the last five releases: if the latest build has a problem, get an
older build there.

Each release also has `SHA256SUMS`, the SHA-256 of each of its files, and
`SHA256SUMS.sig`, that file's Ed25519 signature by OpenH1's release key,
whose public key is [port/assets/release-key.pub](port/assets/release-key.pub)
(the self-updaters check both). `sha256sum -c --ignore-missing SHA256SUMS`
checks the downloads, and `python3 tools/release_sign.py verify SHA256SUMS`
(OpenSSL 3) the signature.

- **macOS:** open the `.dmg` and drag OpenH1 to Applications. The app
  is signed with a Developer ID and notarized by Apple, so it opens like any
  other downloaded app.
- **Windows:** unzip into a folder and start `halo.exe`. Windows may say
  that it protected your PC: it says that of any app that is not signed, and
  this one is not. Choose **More info**, then **Run anyway**. Or install it
  with [Scoop](https://scoop.sh), with no warning:
  `scoop bucket add openh1 https://github.com/OpenH1/scoop-bucket`, then
  `scoop install openh1` (refer to
  [port/windows/README.md](port/windows/README.md#install-with-scoop)).
- **Linux and the Steam Deck:** unzip, and open `install.sh` in the
  folder (on a Steam Deck, in Desktop Mode). It installs the game in
  `~/Applications/OpenH1`, asks for your game's disc image or maps folder,
  puts OpenH1 in the application menu and, on a Steam Deck, in Steam's
  library for Game Mode (the VR build's, as OpenH1 VR in
  `~/Applications/OpenH1-VR`). Or start `bin/openh1` where you unzipped it. The
  game needs a GPU and driver with Vulkan 1.3. The folders are laid out as
  a Unix prefix is (`bin/`, `lib/`, `share/`), for package managers too:
  refer to "Steam Deck" and "Packaging" in
  [port/linux/README.md](port/linux/README.md#steam-deck).
- **Android:** download the APK on the phone and open it to install it
  (Android asks you to allow installs from the browser the first time). An
  install from before the project's own signing key must be uninstalled
  once first: back up its `maps/` and `save/` folders. Refer to
  [port/android/README.md](port/android/README.md).

The Windows, Linux and Android builds update themselves. At start-up they
look for a newer release, and ask if you want to install it (refer to
"Updates" in [port/linux/README.md](port/linux/README.md#updates)). On
macOS, download the new release. A Scoop install is updated by Scoop
(`scoop update openh1`), not by the game.

## Game data

The port does not include the game data. You need your own, legally
obtained copy of Halo: Combat Evolved for the original Xbox: an Xbox disc
image (`.xiso` or `.iso`) made from a disc you own. OpenH1 does not supply
the game or say where to get it. How to make a disc image of your disc,
and how each build takes it: [openh1.com/maps](https://openh1.com/maps). All versions of the game operate. The maps of the European (PAL) version were made for a slower
console. The port changes them to play as the North American (NTSC) maps do,
so players of the two versions can play together.

1. Start the game.
2. At the first start, the game asks for the disc image. On Linux (the
   Steam Deck and the Steam Frame too), the game finds it itself: put the
   image in Downloads, on the Desktop, in your home folder, on an SD card or
   USB drive, or next to the game, and choose it on the game's own screen,
   with a controller, the keyboard or the mouse. This works in the Steam
   Deck's Game Mode and needs no file picker. On Windows, select it in the
   file picker (a disc image next to `halo.exe` is used without one). The
   macOS app's open panel takes the disc image or the maps folder copied
   from the disc.
3. The game extracts the `maps/` folder, and the disc's movies (its `bink/`
   folder, for the startup movies, `display.movies`) when the image has
   them. Then the game starts.

A game whose maps came from elsewhere, or from a version before the movies
were extracted, copies them when Startup Movies (Settings > Video) is
turned on, if the disc image the maps came from is still where it was.
Otherwise put the disc's `bink/` folder (`intro.bik` and the rest) beside
`maps/`.

The `maps/` folder holds the Xbox maps only. If it holds Halo PC's maps
instead (a Halo PC or Custom Edition install's `maps` folder), the game says
so and stops: Custom Edition maps go in `custom_maps/`, beside `maps/`.

The Halo PC multiplayer maps (Gearbox's among them) come from your own copy
of Custom Edition's installer (`halocesetup_en_1.00.exe`): the first start
offers to add them to `custom_maps/` on Linux, macOS and Windows, and a game
that already has its maps adds them with `openh1 --import-maps
<installer>` (`halo.exe` on Windows). They show in the map lists' CUSTOM
MULTIPLAYER. The game reads the installer without running it, with
ChupathingyCE's reader (their pull request
[#313](https://github.com/ChupathingyCE/chupathingyce/pull/313), by
Milenko). Halo PC's own CD has the same maps in a form OpenH1 does not play.
Refer to "Custom Edition's maps from its installer" in
[port/linux/README.md](port/linux/README.md).

On Linux, the game puts `maps/` in the folder of the release (next to
`bin/`). On Windows, the game puts `maps/` next to the executable. On
macOS, a disc image's `maps/` goes in
`~/Library/Application Support/OpenH1`, and a maps folder chosen instead
stays where it is. On
Android, copy the disc image to the phone first. The app puts `maps/` in its
data folder. Refer to [port/android/README.md](port/android/README.md).

## Mods

Mods are folders of Halo PC tag files, as the Halo Editing Kit and Invader
write them, in `mods/` next to `maps/`: texture, sound, text and model packs
that play over the maps' on your machine only, in any game. Each has a
`mod.toml`. Every mod in the folder is on; Settings > Profiles > Mods turns them
off and on and sets the order they load in.
Refer to [docs/mods.md](docs/mods.md).

## Platforms

Each platform has its own instructions:

| Platform | Instructions |
| --- | --- |
| Linux x86-64 (x86-64 executable, Vulkan 1.3, SDL3) | [port/linux/README.md](port/linux/README.md#64-bit-x86) |
| Linux arm64 (arm64 executable, Vulkan 1.3, SDL3) | [port/linux/README.md](port/linux/README.md#64-bit-arm) |
| Linux VR (x86-64 and arm64, Vulkan 1.3; OpenXR headsets such as the Steam Frame) | [port/linux/README.md](port/linux/README.md#vr) |
| Windows (64-bit executable, Vulkan 1.3, SDL3; Windows 10 1903 or later) | [port/windows/README.md](port/windows/README.md) |
| Android (arm64 app, Vulkan 1.3, SDL3) | [port/android/README.md](port/android/README.md) |
| macOS (Apple silicon: OpenH1.app, Metal) | [port/macos/README.md](port/macos/README.md) |
| iOS and iPadOS (arm64, iOS 17.4 or later: OpenH1.app, Metal; built from source and sideloaded, not released yet) | [port/ios/README.md](port/ios/README.md) |

The Linux README also gives the controls, the settings and the multiplayer
functions. These are almost the same on all platforms.

## Multiplayer

The game can play system link games on a local network and on the internet:

- A system link game can have up to 128 players on up to 128 machines.
- Linux (x86-64 and arm64), Windows, macOS and Android machines can play in
  the same game.
- An invite link, or the server browser, lets a machine join a game on the
  internet ([port/linux/README.md](port/linux/README.md)). No server of this
  project is necessary.
- The netcode is new. Each machine moves its own player at once,
  and the host makes the decisions for the game. Refer to
  [port/linux/NETCODE.md](port/linux/NETCODE.md).

To find people to play with, ask in #looking-for-game on the
[Discord](https://discord.gg/MJa327PSWt).

## Build the game

You do not need the Xbox SDK. The port supplies the SDK declarations that
the game uses. Refer to [port/include/xdk](port/include/xdk/README.md).

To build the game:

1. Install Python and [ninja](https://ninja-build.org/).
2. Install the tools for your platform. Refer to the README for the
   platform.
3. In the root folder of the repository, enter `python configure.py`.
4. Enter `ninja` with the target for the platform:

| Target | Result |
| --- | --- |
| `ninja linux_x86_64` (on 64-bit x86 Linux) | `build/linux_x86_64/halo` and `libSDL3.so.0` (with `python configure.py --vr`, its VR mode) |
| `ninja linux_arm64` (on 64-bit ARM Linux) | `build/linux_arm64/halo` and `libSDL3.so.0` (with `python configure.py --vr`, its VR mode) |
| `ninja windows` (on Windows, or on Linux after `configure.py --windows-cross`) | `build/windows/halo.exe` and `SDL3.dll`; `ninja windows_guest`, its guest image alone, anywhere after `configure.py --windows-guest` |
| `ninja android_apk` | `port/android/app/build/outputs/apk/debug/app-debug.apk` |
| `ninja macos` (on a Mac with Apple silicon) | `build/macos/OpenH1.app` (and `halo` with `libSDL3.0.dylib`, for development); `ninja macos_dmg`: `OpenH1.dmg` |
| `ninja ios` (on a Mac with Apple silicon and Xcode's iOS SDK) | `build/ios/OpenH1.app`, signed for development with `configure.py --ios-team`; `ninja ios_install` installs it on the connected device; `ninja ios_sim`: the Simulator's |

If you enter `ninja` without a target, ninja builds the game for the
computer that you use: on 64-bit x86 Linux, `ninja linux_x86_64`.

The Linux builds are 64-bit, and draw with Vulkan 1.3: `configure.py`
downloads the Vulkan headers with musl and SDL3, so the build needs no
Vulkan development packages. Refer to
[port/linux/README.md](port/linux/README.md).

`tools/ci_build.py` makes the same builds as GitHub Actions. For example,
enter `python tools/ci_build.py linux_x86_64 release`.

### Build options

Give these options to `configure.py`:

| Option | Result |
| --- | --- |
| (none) | A debug build. A failed assertion stops the game. |
| `--release` | A release build. The game does not examine assertions, as in the retail game. |
| `--vr` | The VR mode (OpenXR) in the Linux build of the computer. Refer to "VR" in [port/linux/README.md](port/linux/README.md#vr). |
| `--pgo=off` | No profile-guided optimization. |
| `--pgo=generate` | A build that records a profile for profile-guided optimization. Refer to "Optimization profiles". |
| `--profile` | A profiling build. It records CPU times. Refer to "Profiling builds". |
| `--guest-llvm=system`, `--guest-llvm=FOLDER` | The guest builds (Linux arm64 and x86-64, Android, macOS, Windows) compile the guest with clang and the LLVM tools on the PATH, or with the LLVM in FOLDER, not with the pinned LLVM. Refer to "The guests' compiler". |
| `--guest-llvm-cache FOLDER` | The folder for the pinned LLVM (else `~/.cache/openh1`, on macOS `~/Library/Caches/openh1`). |
| `--guest-base` | The Linux arm64 and Android builds put the guest's memory at a base that the host chooses, not in the low 4 GB (a base register: `tools/guest_base_register.py`). The 64-bit x86 build has none: with it, `ninja linux_x86_64` stops with the reason. |
| `--ios-team TEAM` | `ninja ios` signs the iOS app for development with the team's certificate and a provisioning profile that Xcode makes (`tools/ios_provision.py`; Xcode signed in to the team's account). Without it the app is signed ad hoc. `--ios-identity` and `--ios-bundle-id` choose another certificate and bundle identifier. Refer to [port/ios/README.md](port/ios/README.md). |


### The guests' compiler

The Linux arm64 and x86-64, Android, macOS and Windows builds compile the
game as a guest image (`arm64_32` or `x32` code). On all computers, the
guest is compiled and linked by the same LLVM: release 22.1.8 from
[LLVM's releases](https://github.com/llvm/llvm-project/releases), for
64-bit x86 Linux, 64-bit ARM Linux, macOS on Apple silicon and 64-bit
Windows. The first
`configure.py` downloads it (approximately 1.5 GB, checked against its
SHA-256) and keeps clang, `ld.lld` and the LLVM tools that the builds use
(approximately 700 MB) in `~/.cache/openh1` (on macOS
`~/Library/Caches/openh1`, or the folder of `--guest-llvm-cache`). The
version and the checksums are in `tools/guest_toolchain.py`. The hosts
use the compiler of the system. On Linux, the LLVM tools need glibc 2.34
or later, libstdc++ and zlib; LLVM's `ld.lld` is also linked to
libxml2.so.2, for Windows links only, which some distributions (Arch,
CachyOS) no longer have, so a small stand-in for it is written beside the
tools (`tools/library_stand_in.py`).

A compiler that you give for a build (`--linux-arm64-cc`,
`--linux-x86-64-cc`, `--android-guest-cc`, `--macos-cc`,
`--macos-guest-cc`) is used instead, with the LLVM tools on the PATH, as
with `--guest-llvm=system`.

### Optimization profiles

Every build uses one profile of the game to optimize the code:
`pgo/halo_linux.profdata`. All the builds are guest builds (Linux, Android,
macOS, iOS, Windows), and the profile is of the guest: the game and the
platform layer. A function that has changed since the profile was recorded
is compiled without it.

The profile needs clang 22 or later. With an older clang, the builds do not
use it. The guest builds' pinned LLVM (refer to "The guests' compiler")
reads it on all platforms. `--pgo-profile PROFDATA` uses another profile.

To record the profile again (after large changes to the game's hot code,
or to how the compiler lays it out, as a change to inlining), use a guest
build of your computer. On a Mac:

1. Enter `python configure.py --release --pgo=generate`, then
   `ninja macos`. The guest is compiled with clang's instrumentation
   (`-fprofile-generate`), and its own small profile runtime
   (`port/android/guest/runtime/guest_profile.c`) writes the counters when
   the game exits, to the file that `HALO_PGO_FILE` names. The
   instrumented image is larger than the 64 MB that the host keeps for the
   guest image, so this build (only this one: `HALO_PGO_GENERATE`) keeps
   128 MB for it (`HALO_GUEST_IMAGE_RESERVE`).
2. Enter `python tools/pgo_train.py --binary build/macos/halo --data
   <game data>`. The tool plays three short system link games (a host with
   bots and a joined client, both drawing in hidden windows) and the start
   of a campaign level, silently, in approximately five minutes. Then it
   merges the recordings into `pgo/halo_linux.profdata` with the pinned
   LLVM's `llvm-profdata`. `--work FOLDER` keeps the runs' data roots and
   logs out of `build/`, and `--output` writes the profile elsewhere.
3. Configure the build again without `--pgo=generate` (for example
   `python configure.py --release`), and build it.
4. Compare the new profile with the old one before you commit it: build
   `python configure.py --release --profile --pgo-profile <profile>` with
   each, play the same game with each build (for example a local bot game
   with a fixed frame time, `HALO_NETWORK_TEST=local:bloodgulch:slayer
   HALO_BOTS=6 HALO_TEST_INPUT=bot:1 HALO_FRAME_TIME=0.0333333
   HALO_PROFILE_RECORD=true HALO_PROFILE_RECORD_WHEN=game`), and compare
   `game_tick` and `render` in `tools/net_report.py`'s summaries.

On Linux, use `ninja linux_arm64` or `ninja linux_x86_64` and
`build/linux_*/halo` with a display (a virtual one: Xvfb with
`SDL_VIDEO_DRIVER=x11`). The profile was last recorded by the macOS
build (`arm64_32`, as the Android, iOS and Linux arm64 builds are); the
x86-64 builds use it too.

### Profiling builds

A profiling build records where the game spends its time.

1. Enter `python configure.py --profile`, then build as usual.
2. Start a recording. Enter `profile_record` in the console (or the telnet
   console), or set `debug.profile_record = true` in `config.toml`. To
   record each game of a session, also set
   `debug.profile_record_when = "game"`.
3. Stop the recording. Enter `profile_stop`, load another map, or quit.

The game writes numbered `.part<n>.json` files under `profiles/` in its data root
and logs their paths. Parts are retained after the recording stops. Logs go to
standard error; a Windows release build without standard error writes them to
`halo.log` next to `halo.exe`.

`tools/net_report.py` is a helper for developers. Enter
`python tools/net_report.py <recording.part1.json>` to read a recording
as tables. The command also writes `<recording>.summary.txt`, a short text
file.

| Command | Result |
| --- | --- |
| `profile_record [seconds]` | Starts a recording. With a number of seconds, the recording stops after that time. |
| `profile_stop` | Stops the recording. |

`debug.profile_memory` sets the memory of a recording in MB (default 256,
from 4 to 1024). A recording has no length limit:
the game writes a part each time the memory is full. Refer to "Settings" in
[port/linux/README.md](port/linux/README.md).

On Android, the files are in `/sdcard/Android/data/com.openh1.openh1/files/profiles/`.
Enter `adb pull` to copy the recording parts to a computer.

A profiling build plays with normal builds.

## Testing

The tests are listed in [AGENTS.md](AGENTS.md) ("Testing"). Most run
without the game data, in GitHub Actions too.

One check needs the retail maps, so it runs on your own computer (or on a
self-hosted runner), not in GitHub Actions: `tools/net_compare.py` plays
two builds together in a network game on one computer and checks that they
stay in step. Run it when a change can alter how a build calculates the
game, as a new platform, compiler or architecture does, before it is
merged. For example, to check your build against the latest release:

```sh
python tools/net_compare.py --a build/linux_x86_64/halo --b <release folder>/bin/openh1 \
    --data <folder with maps/> --maps bloodgulch,beavercreek,hangemhigh --swap
```

Each game prints PASS or FAIL. A FAIL names the first player, tick and
value that differ. Refer to "Comparing two builds" in
[port/linux/NETCODE.md](port/linux/NETCODE.md).

## Contributing

Pull requests are welcome, from small fixes to whole renderer features. We
review them promptly, and merge what is ready.

1. Fork the repository and branch from `master`.
2. Read [AGENTS.md](AGENTS.md): the layout, the build, the conventions (the
   game's sources are C89, `gnu89`), the tests, and what must stay
   deterministic.
3. Open a pull request. GitHub Actions builds it for Android, Windows,
   macOS and the Linux builds, and runs the tests (debug builds off
   `master`, so a check takes minutes).
4. Say what you tested and how, and what still needs a device to check.

**Fixes from OpenCE are welcome too.** If you have a pull request waiting in
[OpenCE](https://github.com/OpenCommunityEdition/OpenCE), open it here as
well, or tell us about it. We bring good ones in with their authorship kept
and credit given, as for OpenCE's #328, #369 and #392.

**Keep network play compatible.** A change to what the game sends, or to how
it calculates the game, needs `HALO_PORT_NETWORK_VERSION` and a
`tools/net_compare.py` run (refer to "Testing"), so that OpenH1 and OpenCE
players can still play together.

Report problems on the [issue tracker](https://github.com/OpenH1/OpenH1/issues),
with the build number (Settings, Profile, About shows it) and `debug.txt`.
For help, or to talk about a change before you make it, ask on the
[Discord](https://discord.gg/MJa327PSWt): #help, and #dev-general.

## Licence

OpenH1 is free software: you can give it to others and change it under the
terms of the GNU Affero General Public License as published by the Free
Software Foundation, version 3 of the License or (at your option) any later
version (`AGPL-3.0-or-later`). It comes with no warranty. The full text is in
[LICENSE](LICENSE).

- **From the change.** The AGPL applies from the commit that made the change,
  and to each release built from that commit or a later one. Earlier
  releases, and the source at every earlier commit, were released under
  [CC0 1.0](LICENSES/CC0-1.0.txt) and stay that way. (OpenH1 took the
  change with its merge of OpenCE, which had made it.)
- **What OpenH1 builds on.** The decompilation of
  [punpckhdq/halo](https://github.com/punpckhdq/halo) is under the AGPL 3.0
  from 17 October 2026, and under CC0 1.0 before that date.
  [bnunu/halo-1](https://github.com/bnunu/halo-1) is under CC0 1.0.
- **Contributions** are under `AGPL-3.0-or-later`. A change taken from a
  project under GPL 3.0 keeps that licence and its notices; the GPL 3.0 and
  the AGPL 3.0 let the two be combined (section 13 of each).
- **Hosts and servers.** If you change the game and let other persons play
  with your version over a network (for example, as the host of an internet
  game, or on a web site), the AGPL asks you to offer them the source of
  your version (section 13). The game as released has its source here, and
  each release's source is its tag (`build-<n>`).
- **Third-party code, fonts and art** keep their own licences, which are
  next to them (`port/third_party`, `port/assets/fonts`,
  `port/assets/icons`, `port/assets/text_filter`). The release downloads and
  the web site's `licenses.txt` carry their notices.
- **Halo is not covered.** The licence covers the work of OpenH1's and
  OpenCE's authors. It gives no rights in Halo, its maps or anything else that Microsoft owns.

## Disclaimer

OpenH1 is an unofficial fan project, not endorsed by or affiliated with
Microsoft, Xbox Game Studios, Halo Studios or Bungie. Halo is a trademark of
Microsoft Corporation. No game data is included: you need your own copy of
the game.
