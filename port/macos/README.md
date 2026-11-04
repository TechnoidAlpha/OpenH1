# macOS

`ninja macos` builds the game for macOS on Apple silicon (arm64; not
Intel). On such a Mac, `ninja` without a target builds it.

| Result | Item |
| --- | --- |
| `build/macos/OpenH1.app` | The app (refer to "The app"), signed ad hoc |
| `build/macos/halo` | The game, an arm64 executable, for development |
| `build/macos/libSDL3.0.dylib` | SDL 3.4.16, without OpenGL. The game finds it next to the executable. |
| `build/macos/OpenH1.dmg` | A disk image of the app (`ninja macos_dmg`) |
| `build/macos/xgpu_metal_test` | The Metal backend's tests (`ninja macos_metal_test`) |

The game draws with Metal (refer to "Metal"): the menus, the multiplayer
maps and the campaign. It was tested on an Apple A18 Pro with macOS 27.

## Requirements

- macOS 14.4 or later, on Apple silicon.
- Xcode's clang (for the host), Python and ninja. The option `--macos-cc`
  of `configure.py` selects a different clang.
- The guest is compiled with LLVM 22.1.8, which `configure.py` downloads
  the first time (approximately 1.5 GB) to `~/Library/Caches/openh1`. Refer
  to "The guests' compiler" in [README.md](../../README.md#the-guests-compiler).
  With `--macos-guest-cc` (or `--macos-cc`, or `--guest-llvm=system`), the
  guest is compiled with that clang instead, and `configure.py` needs
  `ld.lld`, `llvm-ar` and `llvm-objdump` on the `PATH` or in Homebrew's
  folders (`brew install llvm lld`).
- CMake, for SDL3.
- A network connection for the first build. `configure.py` downloads musl
  1.2.5 and SDL 3.4.16 to `build/macos/third_party`.

## Start the game

Open `build/macos/OpenH1.app` (from Finder, or `open build/macos/OpenH1.app`).
The first time, it asks for the disc image or the maps folder of the Xbox
game (refer to "The app").

For development, the executable runs without the app as on Linux:

```sh
HALO_DATA_ROOT=/path/to/folder/with/maps build/macos/halo
```

The game data, the settings and system link are then as on Linux (refer to
[port/linux/README.md](../linux/README.md)): `config.toml` is next to the
executable, `HALO_DATA_ROOT` (`paths.data`) is the folder that holds
`maps/`, and the saved games go to `~/.local/share/halo-linux`
(`HALO_SAVE_ROOT`, `paths.saves`). To play a game without a person at it,
use the network tests (`HALO_NETWORK_TEST=host:bloodgulch`;
[port/linux/NETCODE.md](../linux/NETCODE.md)).

## The app

`ninja macos` makes `build/macos/OpenH1.app` (`tools/macos_app.py`), named
OpenH1 as the site is; its executable is `halo`, as the other desktop
builds' are (`halo.exe`). Its settings and saves are in Application
Support's OpenH1 folder whatever the app is called, so an earlier
`Halo.app` can simply be deleted:

| Item | What |
| --- | --- |
| `Contents/MacOS/halo` | The executable (its rpaths: `@executable_path`, and `@executable_path/../Frameworks` in the app) |
| `Contents/Frameworks/libSDL3.0.dylib` | SDL |
| `Contents/Frameworks/Sparkle.framework` | The updater ([Sparkle](https://sparkle-project.org) 2, MIT; without its XPC services, which only a sandboxed app uses), loaded by `host_app.m` at run time |
| `Contents/Resources/openh1-icon.icns` | The icon, from `port/assets/icon` (`tools/app_icon.py` writes `port/macos/openh1-icon.icns`) |
| `Contents/Resources/brokers.txt` | Internet play's brokers, copied beside `config.toml` at each start |
| `Contents/Resources/Credits.html` | The About panel's text: the build |
| `Contents/Info.plist` | Below |

`Info.plist`:

- `CFBundleIdentifier`: `com.openh1.openh1`
  (`configure.py --macos-bundle-id`), the reverse-DNS name of the
  project's GitHub organisation's pages, a domain it has. macOS keeps the
  app's permissions (the microphone, the local network) by it.
- `CFBundleShortVersionString` and `CFBundleVersion`: the build's number
  (`HALO_BUILD_NUMBER`, a release's; 0 for a local build), as the game
  names itself ("OpenH1 macOS | build N").
- `LSApplicationCategoryType` `public.app-category.games` and
  `LSSupportsGameMode`: macOS turns Game Mode on while the game is
  fullscreen (the processor and the GPU for the game, Bluetooth's lower
  latency for controllers and headphones; the menu bar shows its icon).
- `GCSupportsControllerUserInteraction`, `NSHighResolutionCapable`,
  `LSMinimumSystemVersion` 14.4 (`os_sync_wait_on_address`;
  `CAMetalDisplayLink` is 14.0), `NSMicrophoneUsageDescription` (voice
  chat), `NSLocalNetworkUsageDescription` (system link), and the project's
  disclaimer as its copyright.
- `CFBundleURLTypes`: the app is the handler of `openh1://` links (invites,
  `openh1://join/...`, and parties' links, `openh1://party/...`), which the
  join page opens (`website/public/join`); not of `halo://` links, OpenCE's,
  which older versions' links have (the game still takes one that reaches
  it). macOS gives SDL's application
  delegate the link (the GetURL Apple event), whether the app is running
  or the link starts it, and SDL sends it on as a drop event;
  `host_links.c` keeps it for the guest's internet play, which joins it as
  it joins a link that a second copy of the game hands over on Linux
  (`p2p_invite_received`). `tools/test_macos_app.py` runs
  `tests/host_links_test.c`.

When its executable runs inside the app, the host (`host_app.m`) does what
a Mac app does, before the game starts:

- **Folders.** `~/Library/Application Support/OpenH1` is the folder of
  `config.toml` (`SDL_GetBasePath` for the game), the data root (`maps`,
  `custom_maps`, `debug.txt`) and the saves (`saves`). The host's log (and
  the game's lines) goes to `~/Library/Logs/OpenH1/halo.log`, the previous
  start's to `halo.previous.log` (Console shows them), unless the error
  output is a terminal. The Metal archive is in `~/Library/Caches/OpenH1`.
  `~` is `HOME`: a test or a script that runs the app with `HOME` set to a
  folder of its own keeps the settings, the log and the caches there,
  away from the player's. `HALO_DATA_ROOT` and `HALO_SAVE_ROOT`, and
  `paths.data` and `paths.saves` in `config.toml`, still choose other
  folders.
- **The maps.** The players supply them, and the game keeps its own copy
  in the app's folder (`maps`), as the Linux builds do: it does not depend
  on where the app is, nor on a folder elsewhere staying where it is. The
  game's data root is this folder (`config.toml`'s, `SDL_GetBasePath`), or
  `paths.data` if it is set; the app never looks in the current folder or
  beside itself (`port/linux/src/xbox_files.c`). Without maps, the game's
  first start (`port/linux/src/first_start.c`, as on Linux: "Start the game"
  in `port/linux/README.md`) shows in the game's own window, from the
  start, what it finds: disc images (`.iso` or `.xiso`) and maps folders in
  Downloads, on the Desktop, in the home folder (not Documents, Pictures,
  Music, Movies or the Library, which would each ask for access), on the
  drives in `/Volumes`, and the maps folders the app's earlier versions used
  (`HALO_MAPS_USED_BEFORE`, which `host_app.m` fills: their `maps.bookmark`
  and `maps` link in Application Support's `OpenH1` and `OpenCE` folders).
  Choosing one copies its maps (an image's are extracted by `xiso.c`)
  through `maps.partial`, with the progress, and the game starts. "Browse
  for the disc image or folder..." opens the open panel as a sheet of the
  game's window (fullscreen or not), which takes an image, the maps folder,
  or the folder holding it. It works with a controller, the keyboard and the
  mouse. A `paths.data` whose folder has no maps (moved, or on a drive that
  is not connected) is said on that screen. The tests:
  `HALO_FIRST_START_INPUT` presses the screen's buttons, and
  `HALO_MAPS_PANEL_ANSWER=<image or folder>` (or `cancel`) answers the panel
  a second after it shows. (The app before this kept a folder where it was,
  through a bookmark and a link; a link that still leads to the maps keeps
  working.)
- **The window.** Its place and size, and whether it was fullscreen, are
  kept in `window.plist`; the next start opens it fullscreen or not
  (`HALO_FULLSCREEN`, unless the environment has it) and puts the window
  where it was (once it is a window). It is kept as a window only when
  the player made it one (F11, Toggle Full Screen), not when the game left
  the Space for `display.mode`'s fullscreen or closed at quit.
- **Menus.** SDL's: About, Hide, Quit (Cmd-Q quits as closing the window
  does; before the game starts, at once), and the Window menu with Toggle
  Full Screen (Cmd-Ctrl-F). The window has the native fullscreen button.

### Updates

A release's app updates itself with Sparkle 2, as Mac apps do. The build
downloads Sparkle's release once, pinned by its SHA-256
(`SPARKLE_VERSION` in `tools/macos_build.py`), into
`build/macos/third_party/Sparkle`.

- A release's `Info.plist` has `SUFeedURL`
  (`https://github.com/OpenH1/OpenH1/releases/latest/download/appcast.xml`),
  `SUPublicEDKey` (the `SPARKLE_PUBLIC_KEY` repository variable),
  `SUEnableAutomaticChecks`, `SUScheduledCheckInterval` (four hours) and
  `SUVerifyUpdateBeforeExtraction`. Debug and local builds have none of
  them, and never look for updates.
- The app looks when the game first reaches its main menu, then every four
  hours, unless `update.auto` is false. The app menu's Automatically Check
  for Updates and Settings > Network Setup > Check for Updates are both
  `update.auto`. Check for Updates... in the app menu looks at once.
- Sparkle's window shows the new version's notes, with Install Update,
  Remind Me Later and Skip This Version. The window opens only at the main
  menu. When a check finds a version during a game, the window waits until
  the game is over: each frame, the guest tells the host whether a game is
  being played (`updater.c`'s `updater_poll`, `host_app_playing`).
- Install Update downloads the release's `OpenH1.dmg`, checks its EdDSA
  signature and the app's Developer ID signature, replaces the app and
  starts it again. Settings and saves are in Application Support, so the
  update does not change them.
- CI: the macOS job signs the notarized disk image with Sparkle's
  `sign_update` and the `SPARKLE_PRIVATE_KEY` secret (`OpenH1.dmg.sparkle`).
  The release job writes `appcast.xml` from it (`tools/macos_appcast.py`:
  the build number, the disk image's URL, the signature, and the merged
  pull requests since the last release as notes), and attaches it to the
  release. Without the secret, or without a notarized app, the release has
  no appcast and the apps already installed stay as they are.
- The key: `bash tools/setup_sparkle_key.sh` (on the Mac whose Keychain
  will keep the private key) makes it with Sparkle's `generate_keys`, sets
  the secret and the variable, and prints the public key.
- Test against an appcast of your own: `HALO_UPDATE_FEED=<url>` replaces the
  appcast URL of an app whose `Info.plist` has a public key.
  `HALO_UPDATE_TEST=window` shows the update window at once (a visible
  window), and `HALO_UPDATE_TEST=install` downloads, checks and installs
  the update at once without a window. With either, the app is never
  started again after an install, because LaunchServices would start it
  without the test's environment, in the player's own folders. Give a test
  its own home (`CFFIXED_USER_HOME`), data and save roots, Metal archive
  (`HALO_METAL_ARCHIVE`) and bundle ID.
- The updater starts on the first `host_sdl_poll_event` call
  (`host_app_update_tick`), and ticks every two seconds after that.
  Neither a run-loop timer nor `NSApplicationDidFinishLaunching` works
  here: the game's event pump does not fire timers, and the notification
  never arrives.

### Signing

The app is signed with the hardened runtime and
`port/macos/halo.entitlements`:

- `com.apple.security.cs.allow-jit`: the host writes the game's code (the
  guest image) into memory and makes it executable. Under the hardened
  runtime, `mprotect` to read and execute of written memory is allowed but
  the process is killed (`SIGKILL`, code signing) when it runs it. The
  loader (`port/android/host/host_loader.c`) maps the code's pages with
  `MAP_JIT` instead, which needs only `allow-jit`.
  `allow-unsigned-executable-memory` (any memory made executable) works
  too, but is wider. `MAP_JIT` cannot be mapped at a fixed address
  (`EINVAL`), so the code's pages are unmapped from the guest's reservation
  and mapped again at the same address. The code is written with
  `pthread_jit_write_protect_np(0)`; every thread runs it.
- `com.apple.security.device.audio-input`: voice chat's microphone.

`ninja macos` signs the app ad hoc (`codesign -s -`). Ad hoc signatures
have no team, and the hardened runtime's library validation refuses a
library of another team (or none: "mapping process and mapped file
(non-platform) have different Team IDs"), so the ad hoc app also has
`com.apple.security.cs.disable-library-validation`; one signed with a
Developer ID does not.

To sign the app with a Developer ID, notarize it and make its disk image
(with the Apple account's Developer ID Application certificate in the
keychain):

```sh
xcrun notarytool store-credentials openh1 --apple-id <apple-id> --team-id <team>   # once
python3 tools/macos_app.py notarize --identity "Developer ID Application: <name> (<team>)" \
    --keychain-profile openh1
```

It signs the app (with a secure timestamp), sends it to the notary
service, staples its ticket to it, makes `build/macos/OpenH1.dmg`, signs,
notarizes and staples that too, and checks both with `spctl`.
`python3 tools/macos_app.py sign --identity ...` only signs the app. An App
Store Connect API key can stand in for the keychain profile: `--key
AuthKey_<id>.p8 --key-id <id> --issuer <issuer ID>`.

CI signs and notarizes the release on `master` this way ("Sign and
notarize" in `.github/workflows/build.yml`), when the repository has the
secrets: `MACOS_CERTIFICATE` (the Developer ID Application certificate and
its private key, a `.p12` exported from Keychain Access, base64) and
`MACOS_CERTIFICATE_PASSWORD`, and the API key's `ASC_KEY` (its `.p8`,
base64), `ASC_KEY_ID` and `ASC_ISSUER_ID`. Without them the release is
signed ad hoc.

### The release build and the disk image

`python3 configure.py --release`, then `ninja macos_dmg`:
`build/macos/OpenH1.dmg` holds the app and a link to Applications, laid
out in a window of its own: OpenH1's icon on the disk, the app on the left
and Applications on the right over a background (`port/macos/dmg`:
`background.html`, rendered by `render.sh` to `background.png` and
`background@2x.png`). That is dmgbuild's
([dmgbuild](https://github.com/dmgbuild/dmgbuild), MIT, `pip install
dmgbuild==1.6.7`), which writes the window's layout without Finder; without
it, the disk image is a plain one. The
guest, where the profile of profile-guided optimisation (`pgo/`) is used,
is compiled with the pinned LLVM 22, which reads the profile. The host
does not use the profile, and Xcode's clang builds it.

## How it operates

The game runs as on Linux arm64 (refer to "64-bit ARM" in
[port/linux/README.md](../linux/README.md#64-bit-arm)): it is ILP32
AArch64 code, a guest image in a 64-bit process, with the desktop code
paths of the platform layer. The host is the Linux arm64 one
(`port/android/host`, `port/linux/host/host_main.c` and
`host_desktop.c`), with these differences.

### The guest's memory: a base register

The guest's 32-bit pointers are addresses in a 4 GB space. On Linux and
Android that space is the low 4 GB of the process. macOS on Apple silicon
keeps the low 4 GB from every process (`__PAGEZERO`): the kernel maps
nothing there, and an executable linked with a smaller `__PAGEZERO` is
killed before `main`.

So the guest's 4 GB lie at a base that the host chooses, a multiple of
4 GB, and each guest address is that base plus a 32-bit offset. The guest
image is built with the base register (`tools/guest_base_register.py`,
after `tools/android_asm_convert.py`), as LFI (Lightweight Fault Isolation)
rewrites AArch64 code:

- `x21` holds the base, and `x22` is a scratch register. The guest is
  compiled with both reserved (`-ffixed-x21 -ffixed-x22`).
- Each load and store through a register adds the base to the register's
  low 32 bits: `ldr w0, [x1]` becomes `ldr w0, [x21, w1, uxtw]`, and
  `ldr w0, [x1, #8]` becomes `add w22, w1, #8` then
  `ldr w0, [x21, w22, uxtw]`. The instructions without that addressing
  mode (`ldp`, exclusives, SIMD structures) add the base into `x22` first.
- The stack pointer and the frame pointer hold full addresses (the stack is
  in the guest's memory), and accesses through them are left as they are.
  So are accesses to globals through `adrp`, whose result is a full address.
- An address taken from the stack, the frame or a page (`add x0, sp, #16`)
  is computed in 32 bits (`add w0, wsp, #16`): every other register holds
  a pointer as its 32-bit offset, as the arm64_32 code expects.
- Indirect branches go to the base plus the target (`blr x8` becomes
  `add x22, x21, w8, uxtw` then `blr x22`).
- An instruction that the pass does not know is an error of the build,
  never left as it is.

The host enters guest code through `port/android/host/host_enter.S`, which
sets `x21`. The guest's calls to the host go through import stubs that
change the pointer arguments into host addresses (`tools/android_imports.py`,
from the prototypes in `guest/runtime/guest_host.h`; the posix wrappers do
so in C). The host changes the pointers inside the structures
that the guest gives it (`iovec`, `timespec`, ...).

The guest's memory is `port/android/host/host_memory_base.c`: the 4 GB
reserved at one time, with guard regions below and above. The Xbox window
and the image are at their usual guest addresses, `0x80000000` and
`0xA0000000`.

The Linux arm64 and Android builds can use the base register too
(`configure.py --guest-base`): then they do not need the fixed low ranges
either. By default they do not use it.

The cost of the base register: refer to `tools/base_register_bench`
(`python3 tools/base_register_bench/run.py`). On an Apple A18 Pro, the
rewrite adds about 10% to the code; the instructions it adds cost from 0
to 6% of the time of CPU-bound kernels (about 3%), and a chain of dependent
loads (pointer chasing) takes one cycle more for each load (4 instead of
3). LFI measured 6.4% on SPEC 2017 on an Apple M1 with the same scheme.
On Linux arm64 the script measures the same guest with and without the
base register.

### Pages of 16 KB

macOS's pages are 16 KB; the guest's are 4 KB (its musl, and the Xbox
memory of `src/xbox_memory.c`). `host_memory_base.c` keeps the protection
of each 4 KB page of the guest. A host page gets the union of the
protections of its four guest pages, stays mapped while any of them is,
and a guest page mapped into a host page in use is zeroed in place. The
write tracking (`host_memory_watch.c`) protects host pages, so a write
marks the whole 16 KB as written.

### System calls

The guest's musl makes Linux system calls. `port/macos/host_syscall.c`
does each one with the Darwin call that does the same, and changes the
numbers, the flags (`open`, `fcntl`, `mmap`, the `AT_` flags), the
structures (`stat`, `dirent64`, `utsname`, `timespec`), the clocks and the
error numbers. The futexes of musl's locks use `os_sync_wait_on_address`.
`HALO_TRACE_SYSCALLS=1` writes each call and its result to the log.

`src/posix_*.c` (files, sockets, UPnP, the updater's TLS) run in the host
as on Linux, with macOS's differences: no `SOCK_CLOEXEC` or `accept4`,
`getentropy` for random bytes, and socket addresses that start with a
length byte.

### The main thread

Cocoa's windows and events belong to the process's main thread, and the
game's main thread makes the window and pumps the events. So the game runs
on the main thread, after `host_main.c` moves it to a stack in the guest's
memory (`host_run_on_stack`). The other threads are made as on Linux.

### Renderer

The guest has the renderer's front end (`src/d3d8_device.c`) and, in place
of a backend, the command stream's encoder (`src/xgpu_stream_encode.c`).
SDL is built without OpenGL. The renderer of this build is Metal's, in the
host (below).

## Metal

The renderer's backend for macOS is Metal, in the host: Metal is a 64-bit
API, and the game is a 32-bit guest. The game's renderer (its front end,
in the guest) writes each frame as a command stream into its memory
("The renderer's command stream" in
[port/linux/README.md](../linux/README.md)), and the host's render thread
plays it into the backend (`port/android/host/host_render_stream.c`, the
backend's table in `xgpu_metal_stream.m`) while the game simulates the next
frame. The shaders are the MSL emitter's (`port/linux/src/xgpu_msl.c`) and
the Xbox's textures are decoded by `port/linux/src/xbox_texture_decode.c`,
both built into the host.

When the stream has no records ready, the render thread waits for either
new records or a GPU completion notification. It can prepare the next
frame while an earlier frame is on the GPU; completed fences release the
stream's bytes and retire frames. Shutdown still drains outstanding GPU
work before closing the stream.

### The window and presentation (`host_metal.m`)

The guest makes its window with `SDL_WINDOW_METAL` (`sdl_platform.c`, with
`HALO_METAL`) and hands it to the host (`host_metal_window_open`, an import
of `host_imports.list`), on the main thread, which Cocoa's views need. The
host takes SDL's view and its `CAMetalLayer` (BGRA8, the window's pixels:
Retina's, resized with the window and in fullscreen; opaque, so that the
window server neither draws what is behind it nor blends it), and brings the
window to the front (a window behind others shows nothing, and the
drawables presented to it never reach the screen).

Presentation has a thread of its own (at the user-interactive QoS, as the
render thread is), run by a `CAMetalDisplayLink`: at
each refresh of the display, the link hands over a drawable, and the
thread draws the oldest frame queued (with vsync off, the newest) into it,
letterboxed to the frame's shape, and presents it. A ProMotion display's rate follows the frames, up
to its highest; a fixed display's is its own. An iPhone's link asks for
its highest rate alone, and halves it while the frames do not keep up
(`rate_adapt`): a second of play in which more than 10% of the refreshes
showed no new frame (a fight the GPU or the phone's heat cannot hold at
120) runs the link at 60, every frame then shown for two refreshes evenly,
not some for one and some for two; the full rate is tried again after
5 s, a hold that doubles (to 80 s) at each try that fails and starts again
once 120 has held for 10 s. `HALO_METAL_ADAPTIVE_RATE=0`: the full rate
always. With battery saver on
(`display.battery_saver`, `host_metal_battery_saver`), the link runs at a
whole fraction of the display's rate, 30 to 60 frames a second (60 at
60 or 120 Hz: a ProMotion display slows to it). When a frame ends, its
back buffer's texture becomes one of two presentation images, and the
back buffer takes the texture of the other, which the display has done
with, for the next frame (a copy would be a pass over the whole of both).
A frame that draws into the back buffer or reads it before it clears the
whole of it finds the last frame's picture there: it is copied in first
(rare: the game clears it at each frame's start). When the display takes
no frames for 100 ms (the window minimized, or between the desktop and
fullscreen), the oldest queued frame is dropped instead, so that the game
goes on.

The display paces the game, by the policy every renderer shares
(`port/linux/src/frame_pacing.h`, in the command stream's host for this
one): the game starts a frame only while at most one of its frames waits
for the display, and then not before its budget ahead of the refresh it
is for. The budget is the most a frame took from its input read to its GPU
work done, of the last 128 (`host_metal_present_signal`'s
`host_render_stream_frame_finished`), and a margin that starts at 1.5 ms
and that the stream learns: a frame the refresh it was started for did not
find widens it, once for each margin (`host_render_stream_frame_displayed`,
as Vulkan's pacing, `display.low_latency`). Each refresh's latch is the
link's deadline for it (its update's `targetTimestamp`), and its frame is
taken 3 ms before it, not as the link calls, a refresh earlier. On an M2 at
60 Hz in a window (vsync on, 120 tick, 240 s each): input to screen went
from 73 ms (median; 61 at 60 tick, 70 with battery saver) to 63 to 65 ms
(63 with battery saver), and the refreshes that showed a frame again from
15 to 52 in 12,000 to 4 to 11. `HALO_METAL_STATS` logs the pacing as
Vulkan's line does (refreshes that showed a frame again, frames taken at
their refresh or after it, the margin and the budget), and
`HALO_FRAME_PACING_MARGIN_MS` holds the margin, for tests. So the game reads its
input as late as it can and still has the frame on screen at the refresh
it is for: on an M1 Max in Blood Gulch, input to screen went from 55 ms
(median; 69 ms 95th percentile) to 18 ms (19 to 25 ms) at 120 Hz, and
from 80 ms (96 ms) to 18 to 31 ms (19 to 38 ms) with the link at 60 Hz,
with no more frames lost; and each displayed frame's game time steps by
about a refresh (the spread of the steps 4 ms at 60 Hz, 1.2 ms at
120 Hz), where three frames in a row had shown nearly the same instant and
the fourth jumped 50 ms.
The waits are precise: macOS let ordinary timed waits end tens of
milliseconds late (5 ms slept 39 with the person away), and the game's
thread is a time constraint thread while it waits for a frame's start.
The process is marked latency critical while the window is open.
`HALO_METAL_RATE=<Hz>` runs the link at that rate (another display's
pacing tested on this one).

**Fullscreen and the window server.** In a window, the window server
composites the frame first: from the link's deadline to the screen is two
refreshes. A fullscreen layer that covers its display can go to the display
without compositing (direct to display): one refresh less. The layer is
eligible as it is (opaque, `framebufferOnly`, BGRA8, its drawables at the
window's pixel size, no other layers over it), at the display's native
mode and at scaled ones (the display scales it). But in a fullscreen Space
(the default fullscreen and `borderless`) a hidden cursor makes the window
server composite again: `[NSCursor hide]`, as SDL's relative mode hides it,
or an invisible cursor image, as `SDL_HideCursor` does. So while the mouse
is captured in fullscreen, the cursor stays shown, as one black point
parked in the window's bottom right corner, 4 points in (not in it, where a
hot corner would act), where the relative mode holds it
(`host_raw_mouse_cursor`, `host_raw_mouse.m`); let go, it is put back in
the window's middle.

Measured on an M2 MacBook Air at 60 Hz (its notch, a desktop that looks
like 1710x1107: 3420x2214 pixels scaled to the panel's 2880x1864), vsync
off, 120 tick: the frames' input to screen in the game (`HALO_METAL_STATS`,
5 s medians, ms), and Game Mode (gamepolicyd's log, the app only: it comes
with the games category of the app's `Info.plist`):

| Mode | Drawable | Input to screen | Game Mode |
|---|---|---|---|
| Window | 1280x960 | 39 to 41 | (not run in the app) |
| `borderless` (the default fullscreen: a Space, below the notch), the cursor hidden (before) | 3420x2146 | 39 to 43 | on |
| the same, the cursor parked | 3420x2146 | 22 to 25 | on |
| `fullscreen` (exclusive) at the desktop's mode | 3420x2214 | 23 to 25 | on |
| `fullscreen`, `display.resolution = "2880x1864"` (the panel's own mode) | 2880x1864 | 24 to 25 | (not run in the app) |
| `borderless`, `SDL_VIDEO_MAC_FULLSCREEN_SPACES=0` (no Space) | 3420x2214 | 24 to 25 | (not run in the app) |

In a Space, the capture's parts tried alone: the cursor hidden alone
(`SDL_HideCursor`), or `[NSCursor hide]` with the cursor held, composited;
the cursor held alone (`CGAssociateMouseAndMouseCursorPosition(NO)`), or
held with an opaque point for a cursor, kept the direct path; an image of
one pixel almost transparent (alpha 1/255) composited too.

That pacing is `display.vsync = true`'s (iOS's default). With vsync off,
the Mac's default (taken as the window opens, and as Settings change it),
the display does not pace the game: it draws as many frames as
`display.max_fps` allows (twice the window's display's refresh rate by
default, following the window to another display), as on Linux and
Windows (`platform_video_swap`, whose wait no input ends: #543),
and each refresh shows the newest frame queued, the older one dropped. The
link still presents only at its refreshes, so nothing tears, in a window
or fullscreen. The newest frame is taken 3 ms before the link's deadline
for the refresh (`HALO_METAL_TAKE_MARGIN_MS` sets another), not as the link
calls, a refresh earlier: on an M2 at 60 Hz in a window, the link calls
16.6 ms before its deadline and 49.9 ms before the frame is on screen. The
game starts its frames at fixed phases of those takes
(`host_metal_frame_phase`, `platform_frame_slot`): one its budget (as the
paced game's, `frame_pacing.h`) before each take, the others whole
fractions of a refresh before it, so that the frame each refresh shows is
always as old and the game's time on screen steps evenly. Frames on a clock
of their own beat against the display's: the frame shown was then one just
drawn or one an interval older, and up to 20% of the refreshes stepped by
more than 4 ms. The budget's changes move the phase by 0.5 ms a refresh at
most; the last 2 ms of each wait are a time constraint thread's sleep (an
ordinary timed wait ended up to 8 ms late); no press ends the wait (#543:
a frame begun at a press was one more than the limit). With
battery saver on, the display link paces the game at its slower rate, vsync
or not (frames spaced by the clock alone came at the link's rate but not in
step with it: on a 60 Hz M2, 15% of the refreshes showed no new frame).
When the window moves to a display of another rate, the link runs at that
display's highest.

Until frames come, each refresh clears the drawable to black and presents
it (the window opens before the game draws).

**HDR** (`display.hdr`, [docs/hdr.md](../../docs/hdr.md)). While the frame is
drawn in HDR on a display with EDR headroom, the layer is RGBA16Float in
extended linear sRGB with `wantsExtendedDynamicRangeContent` (1.0 the
system's SDR white), and the presentation encodes the frame for it (the
shared encoding, `xgpu_hdr_encode` in `xgpu_metal_post_shaders.c`: linear,
at most the peak, times paper white); otherwise it is BGRA8 and untagged, as
it was. The window's screen's headroom is read on the main thread (NSScreen)
four times a second and whenever the window moves to another screen: its
potential headroom says whether the display shows HDR (`"auto"` turns HDR
on past 2: an SDR panel such as a MacBook Air's reports 2.0, its white dimmed
below the backlight), and its headroom now is the peak the tone map and the
encoding roll off to, which follows the display's brightness.
`HALO_METAL_EDR_HEADROOM=<headroom>` stands in for the display's (the
tests, the replay, and evidence made on a Mac without an HDR display). The
backend tone maps each window's 3D view with a draw in the pass that drew
it, reading the pixels where they are (programmable blending), and the
draws that the front end tags blend in their pixel shaders, reading the
pixel they draw over (`[[color(0)]]`); they are the MSL emitter's shaders,
not the combiner function's. A shot of an HDR back buffer
(`HALO_RENDER_SHOTS`) writes, beside its PNG (clamped, as an SDR display
shows it), the frame's half floats (`.rgba16f`) and the frame encoded for
the display it had (`.encoded.rgba16f`, `.encoded.txt`).

`HALO_METAL_STATS=<seconds>` logs the pacing that often: the link's
refreshes, the frames and clears presented, how many drawables reached the
screen and the intervals between them; the refreshes without a new frame,
by what the render thread was doing then (waiting for the GPU to finish a
frame, for an image, playing the stream, or waiting for the game's); the
link's callbacks more than a refresh and a half after the last (with every
drawable still to reach the screen or some free, after another such, after
a slow callback, and whether the link's target time skipped a refresh too
or the callback came late for the next), and the slowest callback; the
frames that waited for an image or were dropped, those that reached the
screen after the link's target, and those whose presentation ended on the
GPU within a millisecond of it (and the least time to spare); and the
presentation's GPU time and its wait from commit to the GPU's start. A
line before it gives the input to screen times: from each frame's input
read (D3DDevice_Present's event pump, which the main loop's input_update
reads at once) to the frame on screen (the drawable's presented time),
their median and 95th percentile, in milliseconds and refreshes; with vsync
off, another gives how late the waits for the newest frame ended. A second line
gives a frame's mean costs: the time the GPU was busy (with the game's
command buffers or the presentation's: the union of their times, which
overlap) and its share of the time, the GPU's time on the frame's command
buffers, the render thread's CPU time, its draws, passes, uploads in the
frame's order, copies of a target drawn into, and pictures copied back
into the back buffer;
the last frame's time to play (from its first record to its end, the wait
for a presentation image included) and its bytes in the stream (and the
most of any frame); the game's waits for the host (the frames in flight,
or room in the ring); the textures and images made, and made again, and
the bytes read for them; the shaders compiled and the time it took; the
pipelines made, and those from the archive. Each hitch is logged as it
comes, with the shaders and pipelines the render thread made since the
frame before it (what the frame may have waited for).

### The GPU's time

Apple's GPUs run as slowly as the frames allow: with the game paced by
the display, the GPU's clock is lowered until it is busy most of each
frame (on an A18 Pro, about 87% of it, whatever the frame draws), so the
GPU's time a frame says little about the work. To measure the work:

- `HALO_METAL_UNPACED=1`: a frame does not wait for a presentation image
  (the oldest queued is dropped), so the game draws as fast as the GPU and
  the render thread can, and the GPU runs at its full clock: the GPU's
  busy time a frame is then the work's.
- `HALO_METAL_PASSES=<seconds>`: a frame that often is timed pass by pass,
  with the GPU's timestamps at each encoder's stages, and logged with each
  pass's targets (their handles, size, formats and samples), load and store
  actions (`L`oad, `C`lear, `D`on't care; `S`tore, `R`esolve, `B`oth),
  draws and clears. The stages of passes overlap on the GPU, so a pass's
  span includes others'. `HALO_METAL_PASSES_SPLIT=1` makes each draw of
  those frames a pass of its own (up to 1024 a frame), logged with its shaders, blend, depth,
  viewport and textures (size, format, levels, filters and anisotropy).
- `HALO_METAL_CAPTURE=<frame>:<file>.gputrace` (with `MTL_CAPTURE_ENABLED=1`)
  writes that frame and the next, the first's presentation included, as a
  GPU trace document for Xcode (its shader profiler and counters).
- Instruments' Metal System Trace
  (`xcrun xctrace record --template 'Metal System Trace' --launch -- build/macos/halo`)
  shows the GPU's performance states and the window server's work.
- `python3 tools/gpu_isa.py <file.metal | file.metallib> --entry <function>`
  shows the machine code a shader compiles to on this Mac, with its counts
  (instructions by kind, registers, stack): with function constants
  (`--constants`), or with them written into the source (`--literal`), in
  a maths mode (`--math safe`, as the backend compiles). It compiles the
  pipeline here (`port/macos/tools/gpu_isa_harvest.m`), takes the code
  from a binary archive and disassembles it with applegpu
  (github.com/dougallj/applegpu, fetched into `~/.cache/gpu_isa`), which
  decodes the M1 family's GPUs only.

### Recording the stream, and replaying it (`halo_replay`)

The renderer's work can be recorded once and replayed in seconds, without
the game: the same frames every time, no maps, no lobby, no guest.

- `HALO_RENDER_RECORD=<file>` records every record the render thread plays
  (`port/android/host/host_render_record.c`, the host's, in every build),
  with the guest memory each one reads through guest addresses at the moment
  it is played: inline data too large for the ring, and what the backend
  reads in place, which the textures' cache names as it reads it (the Xbox
  textures' headers, palettes and texels, the images' pixels). Memory is
  stored by its content's hash, each content once; the file is written in
  blocks that a thread of its own compresses (LZ4, Apple's libcompression;
  on other hosts the blocks are stored as they are). Its format is the
  recorder's and the tool's alone (`HOST_RENDER_RECORD_VERSION`), not the
  stream's. Record with `HALO_METAL_UNPACED=1`: a paced window the display
  does not show (behind another app, on another space) waits 100 ms a frame.
- `HALO_RENDER_SHOTS=<folder>` and `HALO_RENDER_SHOT_FRAMES=<frames>`
  (`600,1500`, `100-900/100`) write those frames' back buffers, as each is
  presented, to `<folder>/frame_<n>.png` (frames counted from the stream's
  start). The replay writes its shots the same way.
- `ninja macos_replay` makes `build/macos/halo_replay`:

```sh
build/macos/halo_replay <file> [--frames a-b] [--window] [--unpaced] [--shots dir]
    [--shot-frames list] [--loop N] [--cold] [--archive path] [--hitch ms] [--quiet]
build/macos/halo_replay --compare <shots> <shots>
```

  It reserves the guest's 4 GB as the host does, writes the records into
  the ring where they were and submits them as the encoder did, and the same
  render thread, decoder and Metal backend play them; before each record,
  the memory it reads is put back, and pages that changed count as written
  for the textures' cache. Offscreen by default (`--window`: paced by the
  display unless `--unpaced`). `--frames a-b` measures those frames (the
  ones before are played unmeasured, as the state is theirs); `--loop N`
  plays it all N times in one process (shots in `dir/loop<n>`); `--cold`
  runs in a throwaway app of a new bundle id, which Metal gives a shader
  cache of its own, with an empty pipeline archive (both removed after);
  the archive is otherwise `<file>.pipelines`. Each measured frame prints
  its interval (render thread, frame end to frame end), the render thread's
  CPU, the GPU time completed in it (a frame or two late), its draws, the
  pipelines made and the MSL functions compiled, with their times; then
  the percentiles and the hitches (over `--hitch`, 12.5 ms by default, and
  twice the median; shot frames, which wait for their PNG, are not
  counted). `--compare` compares two shot folders pixel by pixel.

### The backend (`xgpu_metal.m`)

`xgpu_metal.h` declares its functions; `xgpu_metal_stream.m` makes them the
command stream's backend table. They run on one thread, the render thread.

- **Pipelines.** A render pipeline state is made for what Metal bakes into
  one (the shaders, the vertex layout, blend, the write mask, the targets'
  formats and samples) and kept by that; depth-stencil states by the depth
  and stencil part. Culling, winding, the fill mode, depth bias and depth
  clamping are the encoder's state. Pipelines are made from a binary
  archive (`~/Library/Caches/OpenH1/metal_pipelines.bin`, or
  `HALO_METAL_ARCHIVE`) where it has them. None is made on the render
  thread: each is made on a queue (user-initiated QoS) once its shaders
  are, during a map's loading screen for the draws it is shown first
  (`display.prepare_shaders`: the stream's PREPARE and PREPARED records).
  A draw whose pipeline was not made so waits for it, logged and counted
  ("made while drawing"): a draw is never skipped. The archive is written,
  on a background queue, after a map's preparation and as the process
  ends (host_exit), never in play: a new archive each time, as Metal's
  archiver cannot write one that was read or written and has had pipelines
  added since (it reports a stage missing), of every pipeline made or found
  this run that writes a colour.
- **Shaders.** The app's library (`Contents/Resources/shaders.metallib`, or
  `shaders.metallib` beside a bare executable; `HALO_METAL_LIBRARY`) is
  made when the app is built (`tools/macos_shaders.py`, run by `ninja
  macos`, no maps needed): every vertex shader the game's code creates, in
  each form a draw can need (from its layout or in immediate mode, unlit or
  lit as the settings light it: `tools/render_manifest.py vertex`), the
  clears' and the presentation's functions, the NV2A combiner as one
  function whose configuration is function constants
  (`xgpu_metal_combiner.c`), and the pixel programs of the renderer's
  manifest (`port/assets/render`) that the combiner cannot draw. A shader is
  looked up there by its name (its MSL source's hash). A pixel program is
  the combiner's function specialised for its configuration
  (`newFunctionWithName:constantValues:`, on the queue: about 0.2 ms, 10 ms
  the first time on a Mac): Metal's specialisation gives the same machine
  code as the program's own MSL compiled in the same maths mode. Only a
  shader the library lacks and the combiner cannot draw (a debugging
  setting's) is compiled from MSL (`newLibraryWithSource`), on the queue.
  The library's and the run's shaders use safe maths. Where a draw's shaders read
  what is set in `xgpu_metal_bindings.h`, the one place the MSL emitter
  (`port/linux/src/xgpu_msl.c`) and the backend agree on: the vertex
  constants (`setVertexBytes`), the draw's uniforms (both stages), the
  inputs no stream feeds (a buffer read at a constant step: every input is
  an `[[attribute(n)]]`), the streams, a texture and a sampler for each
  stage.
- **Samplers** are kept by their description. Metal's samplers have no LOD
  bias (the shader applies it) and three border colours: for another (the
  motion tracker's `0x46000000`), the sampler clamps to the edge and the
  pixel shader mixes the colour in where the coordinates leave the
  texture, a variant of the pipeline's shaders.
- **Textures** in the Xbox formats Metal has: BGRA8 (and X8R8G8B8, its
  alpha read as 1 by a swizzle), R5G6B5, A1R5G5B5, X1R5G5B5, A4R4G4B4 (read
  through ABGR4 by a swizzle), L8, A8, A8L8, DXT1 to DXT5 (BC1 to BC3), and
  the depth formats (D24S8 is 32-bit float depth with 8-bit stencil: Apple's
  GPUs have no 24-bit depth), depth kept reversed, 1 - the game's
  (`xgpu_backend.h`, "Reversed depth"). An upload writes a texture at once where no
  work the GPU still has reads it, else it is a blit in the frame's order.
  A buffer (the mirrored vertices' segments) written while the GPU's work
  still reads it, and no blit of the GPU's still writes, is renamed
  instead: another buffer of its size (one the GPU is done with, kept for
  it) takes its place, with its contents and the bytes written, and the
  work already encoded reads the old one. A blit there would end the 3D
  view's pass, as it did about once every four frames of a fight in Blood
  Gulch.
  A multisampled target draws into a multisampled attachment resolved into
  it at each pass's end. A new target is cleared when it is made (black;
  depth the far plane, stencil 0): Apple's GPUs give a new private texture no contents
  of their own (an M1 Max's read as magenta), and the game presents its
  back buffer once, right after it makes its device, before it draws.
- **Guest data in place.** Guest memory is the host's (unified memory), so
  a range the GPU reads is wrapped in a buffer
  (`newBufferWithBytesNoCopy`) over the readable host pages about it, up to
  a granule of 4 MB, without a copy. A buffer keeps the pages it was made
  over: when the guest maps pages again, `host_memory_base.c` says so
  (`host_memory_remapped`), and the buffers over them go. The command
  stream's ring is one buffer too, so its inline data is drawn in place.
- **Passes.** Each change of targets ends the render command encoder (the
  same targets again keep it, and keep the clears waiting for its start);
  the next draw or clear begins one, with load actions "load", or "clear"
  for an attachment that a clear of the whole of it came first for. Other
  clears are quads drawn through a clear pipeline (of the channels
  cleared).
- **Aside passes.** A pass of another target (the open pass's at least
  512x512: an object's shadow and its blur, a mip chain, and the game's
  2048x2048 targets that it draws in four passes in the middle of the 3D
  view, larger than the view at a phone's resolution) begun while the
  large pass is open would end it: its
  colour, depth and stencil stored and loaded again when the game comes back,
  tens of megabytes at the screen's size each time. Where the small pass
  draws into nothing the frame's command buffer has written and its draws
  read nothing it has written (textures, buffers its blits wrote), it is
  encoded instead into a command buffer of its own enqueued before that one,
  and the large pass, parked, goes on when the game comes back to its
  targets. A target that the command buffer's work has read is given another
  texture first (its contents copied where the pass loads them, or has other
  levels or layers). On the Mac the first return in a frame still ends the
  large pass (a frame drawn in one pass from its start differed from the
  stock order at a few pixels of some frames; with that break, every shot
  of the recordings is the same). On iOS it goes on (the backend's
  `aside_continue`), the 3D view and the HUD then one pass, which a
  phone's GPU pays least for and MSAA needs to resolve its samples in tile
  memory: at 2796x1290 on an M1, MSAA 4x went from 9.1 to 5.2 ms of the
  GPU a frame, and one or two pixels of a shot differ. A recording of a
  fight in Blood Gulch (16 players, 3728x1720, MSAA 4x, HDR), replayed as
  iOS plays it: with the 2048x2048 targets' passes put aside too, the 3D
  view's pass was resumed (its samples, depth and stencil loaded again)
  0.02 times a frame, from 1.3 (up to 8), and the GPU's time went from 12.8
  to 6.8 ms a frame; up to 24 pixels of a shot differ, by at most 12 of
  255 (with the Mac's break, none).
  `HALO_METAL_ASIDE_BREAK=1` or `0` chooses on either. `HALO_METAL_ASIDE=0`:
  no aside passes. HALO_METAL_STATS counts them.
- **Frame-end discards.** A tile-based GPU (Apple's) stores each pass's
  attachments from tile memory into their textures as the pass ends. The
  frame's last pass decides its depth, stencil and multisampled colour
  attachments' stores as it ends (deferred store actions): a target whose
  first use in each of the last 30 frames was a clear of the whole of it
  (no load, draw's read, copy or resolve before) has its contents at the
  frame's end discarded, a multisampled colour resolved and its samples
  dropped. The game clears its depth and its multisampled targets at each
  frame's start, so those stores go. A first use that is not a clear starts
  the count again (logged once, if the frame read what was discarded). The
  shots of recorded frames are the same with and without them.
  `HALO_METAL_DISCARDS=0`: none. `HALO_METAL_MEMORYLESS=1` (with
  `aside_continue`) makes multisampled attachments memoryless, saving their
  memory too (about 130 MB at an iPhone Pro Max's resolution); off by
  default, as the game ends and resumes its 3D view's pass now and then
  (every few hundred frames of a10), and a memoryless target's samples are
  gone then, which shows for a frame (the target is then kept in memory,
  and memoryless again after 120 frames without such a load).
- **Visibility tests** count samples in a visibility result buffer, one for
  each pass (Apple's GPUs zero the first results an earlier pass counted
  into a buffer when a later pass counts into the same one), and the counts
  are known when the frame's command buffer completes; they are written for
  the game divided by the samples one of its pixels is.
- **Frames.** Each frame is a command buffer; at most two frames are in
  flight, and each has a fence (the serial of its last command buffer).

### The command stream's table (`xgpu_metal_stream.m`)

- **Shaders.** A pipeline's shaders are made by `xgpu_metal_shaders.c`: the
  stream's vertex program and pixel program (the shader intermediate
  form's bytes) become the host's structures, and the MSL emitter writes
  them, its functions named after their source's hash (the archive tells
  functions apart by their names). They are made for the pipeline's key
  and for what only a draw knows: the inputs its layout feeds as
  unnormalized shorts (read as integers) and the stages whose border colour
  the shader draws. The emitter's uniforms are made from the front end's,
  the blend colour, the per-pixel lighting's lights (vertex constants) and
  the stages' border colours. The emitter's sources are built into the host
  beside a stand-in for the platform header (`xgpu_metal_platform.h`;
  `tools/macos_build.py` copies them to `build/macos/host/shared`).
  `HALO_METAL_SHADERS=<folder>` writes each shader's MSL there; the pixel
  shaders' debugging settings are read from their environment variables
  (`HALO_GPU_DEBUG_EXPR`, `HALO_GPU_DEBUG_T0`, `HALO_GPU_DEBUG_FLAT`).
- **Textures** (`xgpu_metal_textures.c`). An Xbox texture is found by its
  header's Data, Format and Size (and a palettized one's palette, and a
  Custom Edition map's channel order), read from the guest's memory and
  decoded by `xbox_texture_decode.c`: DXT as BC1
  to BC3, the rest to BGRA. On a GPU without BC (an iPhone's), DXT is
  transcoded to ASTC 4x4 (`display.astc_textures`; `port/linux/astc`,
  `port/ios/README.md`); `"always"` does so on a Mac too, and
  `HALO_METAL_NO_BC=1` makes the backend take the Mac's GPU for one without
  BC, to measure an iPhone's textures on a Mac (`"off"` then decodes them as
  the iPhone did before). `HALO_ASTC_CACHE=<folder>` keeps the encoded
  blocks there instead of `~/Library/Caches/OpenH1/astc` (empty: nowhere),
  and `HALO_METAL_STATS` logs the textures' bytes (all, DXT as ASTC,
  decoded, as BC) every 10 seconds. Its pages are write-protected (the write
  tracking, `host_memory_watch.c`, safe from the render thread), and it is
  read again once any of them has been written; a texture whose memory the
  game has let go of is not read. The high-res replacements (the HUD's,
  the menus' art, the high-res text) are images: the guest keeps their
  pixels (`src/xgpu_images.c`) and the stream names them; the host makes a
  texture of each, and its mip levels, again when its serial changes.
- **A draw that samples its own target** (Metal cannot sample an attachment
  of the pass) samples a copy, made again once the target has been drawn
  into since.
- **Fans and line loops** (the game draws fans in immediate mode) are made
  into lists.
- **The enhanced dynamic lights** (`display.dynamic_lights = "enhanced"`, the
  Enhanced preset's): the MSL emitter's lit structure and models
  (`port/linux/src/xgpu_msl.c`, the SPIR-V emitter's lighting operation
  for operation) read the frame's lights' block and their grid's cells from two
  buffers (`XGPU_METAL_BUFFER_DYNAMIC_LIGHTS`, `_DYNAMIC_LIGHT_CELLS`), which
  the stream's upload record copies into the frame's upload ring, so that
  the draws after it read them in the stream's order without a pass broken;
  where a frame has sent none, zeroed ones. The glow about each plasma bolt
  is a pass of its own, as Vulkan's: a camera-facing quad about each light
  (the emitter's `xgpu_msl_halo_shader`), added to the 3D view's colour
  after its transparent geometry, faded against the depth the scene was
  drawn with (read at each pixel; a multisampled depth target's resolved
  first, `xgpu_metal_texture_resolve`).
- **Anti-aliasing** (`display.anti_aliasing = "smaa"`, the Enhanced preset's,
  or `"fxaa"`): the Vulkan backend's passes (`tools/vulkan_post_shaders.py`)
  over the window's 3D view, after the glows and before the HUD and the
  menus, which stay sharp, drawn through the backend's own functions
  (`xgpu_metal_post.c`): the target's pixels copied, then FXAA, or SMAA's
  edges, blending weights (with its area and search textures) and blend,
  colour alone written back. Their MSL is the Vulkan backend's SPIR-V
  written by SPIRV-Cross (`xgpu_metal_post_shaders.c`, generated by
  `tools/metal_post_shaders.py`); the functions are in the app's library,
  their pipelines made as the setting is chosen and as a map is
  prepared.

### Tests

`ninja macos_metal_test`, then `python3 -m pytest -q tools/test_macos_metal.py`,
or `build/macos/xgpu_metal_test` itself: clears (whole, rectangles, a
channel alone), a textured quad with the vertex constants and the draw's
uniforms, every format, the pipeline cache, the winding, visibility tests,
multisampling, the order of uploads, cubes, arrays and volumes, guest
memory read in place and mapped again, frames in flight, and the command
stream's table, with anti-aliasing's passes (SMAA and FXAA blend the 3D
view's staircase and leave the HUD drawn after them, and the target's alpha,
as they were) and the enhanced dynamic lights (the emitter's lit structure
compiles and draws unlit with no lights sent, lit near a light sent, and a
bolt's glow is added about its middle). Each draws with shaders written in the test, reads the
pixels back and checks them; and through the table, with the MSL emitter's
shaders made from a vertex program and a pixel program written as the
stream's bytes: an Xbox texture in the guest's memory (swizzled), read
again once written, an image, and a border colour Metal has and one it has
not. With `--archive file`, a
second run checks that every pipeline comes from the archive. A windowless
GPU test also uses the production presentation queue and event helpers: an
uncommitted later game buffer must not block an earlier picture or the loading
draw, and an unsignaled picture event must block its consumer. Exact pixel
readback checks the producer dependency. This tests GPU ownership and ordering,
not display cadence. The separate `--window` path does not run this fixture.
`build/macos/xgpu_metal_test --window 15 --resize` opens a window, draws
frames as fast as the presentation takes them, resizes it, makes it
fullscreen, and logs the rate (with `HALO_METAL_STATS=3`, the
presentation's own log); then it checks the back buffer given to the
presentation: the next frame's picture drawn on the last's, read before a
draw, and cleared.

## Find problems

- The log goes to the terminal (the app's, to
  `~/Library/Logs/OpenH1/halo.log`); the game's own log is `debug.txt` in
  the data folder.
- A crash prints the registers, the guest's base and the frame chain. The
  guest's addresses are the low 32 bits:
  `llvm-symbolizer --obj=build/macos/halo_guest.elf 0x<address>`.
- `HALO_TRACE_SYSCALLS=1` logs the guest's system calls.

## Limits

- Not drawn yet: a render target's mip levels drawn one by one (the
  water's ripple map: its first level is sampled), points drawn as
  polygons' fill (lines instead).
- With `display.prepare_shaders` off (it is on by default), a pipeline is
  made the first time a draw needs it, which the draw waits for (the shaders
  are the library's or specialised: no MSL is compiled); later runs find it
  in the archive.
- The Discord invites are not set up for macOS.
