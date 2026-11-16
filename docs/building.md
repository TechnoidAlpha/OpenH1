# Building GOAT H1

You do not need the Xbox SDK. The port supplies the SDK declarations that
the game uses. Refer to [port/include/xdk](../port/include/xdk/README.md).

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
[port/linux/README.md](../port/linux/README.md).

`tools/ci_build.py` makes the same builds as GitHub Actions. For example,
enter `python tools/ci_build.py linux_x86_64 release`.

## Build options

Give these options to `configure.py`:

| Option | Result |
| --- | --- |
| (none) | A debug build. A failed assertion stops the game. |
| `--release` | A release build. The game does not examine assertions, as in the retail game. |
| `--vr` | The VR mode (OpenXR) in the Linux build of the computer. Refer to "VR" in [port/linux/README.md](../port/linux/README.md#vr). |
| `--pgo=off` | No profile-guided optimization. |
| `--pgo=generate` | A build that records a profile for profile-guided optimization. Refer to "Optimization profiles" below. |
| `--profile` | A profiling build. It records CPU times. Refer to "Profiling builds" below. |
| `--guest-llvm=system`, `--guest-llvm=FOLDER` | The guest builds (Linux arm64 and x86-64, Android, macOS, Windows) compile the guest with clang and the LLVM tools on the PATH, or with the LLVM in FOLDER, not with the pinned LLVM. Refer to "The guests' compiler" below. |
| `--guest-llvm-cache FOLDER` | The folder for the pinned LLVM (else `~/.cache/openh1`, on macOS `~/Library/Caches/openh1`). |
| `--guest-base` | The Linux arm64 and Android builds put the guest's memory at a base that the host chooses, not in the low 4 GB (a base register: `tools/guest_base_register.py`). The 64-bit x86 build has none: with it, `ninja linux_x86_64` stops with the reason. |
| `--ios-team TEAM` | `ninja ios` signs the iOS app for development with the team's certificate and a provisioning profile that Xcode makes (`tools/ios_provision.py`; Xcode signed in to the team's account). Without it the app is signed ad hoc. `--ios-identity` and `--ios-bundle-id` choose another certificate and bundle identifier. Refer to [port/ios/README.md](../port/ios/README.md). |


## The guests' compiler

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

## Optimization profiles

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

## Profiling builds

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
[port/linux/README.md](../port/linux/README.md).

On Android, the files are in `/sdcard/Android/data/com.openh1.openh1/files/profiles/`.
Enter `adb pull` to copy the recording parts to a computer.

A profiling build plays with normal builds.

## Testing

The tests are listed in [AGENTS.md](../AGENTS.md) ("Testing"). Most run
without the game data, in GitHub Actions too.

A game started with a new data folder at the main menu first prepares the
menu pictures ("Preparing menu pictures", about 12 seconds for the 23 retail
pictures on a desktop computer; at most 2 minutes). Runs started by tests skip
this: a run with `debug.exit_after`, `fixed_step`, `test_input`,
`network_test`, `menu_open`, `hidden_window`, `telnet_console`,
`screenshot_every` or `load_trace` set goes directly to its map or the main
menu. Other runs that start in a map (`init.txt`) prepare the pictures only
when they first go to the main menu.
`HALO_DEBUG_MENU_PICTURES=1` prepares the pictures in a test run as well.
Refer to "Menu pictures" in [port/linux/README.md](../port/linux/README.md).

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
[port/linux/NETCODE.md](../port/linux/NETCODE.md).
