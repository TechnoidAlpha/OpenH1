# iOS and iPadOS

`ninja ios` builds the game for iPhones and iPads (arm64, iOS 17.4 or
later) on a Mac with Apple silicon and Xcode. `ninja ios_sim` builds it for
the iOS Simulator on that Mac.

| Result | Item |
| --- | --- |
| `build/ios/OpenH1.app` | The app, signed for development with `configure.py --ios-team`, else ad hoc |
| `build/ios/halo` | Its executable, with SDL 3.4.16 linked in (without OpenGL) |
| `build/ios_sim/OpenH1.app` | The Simulator's app (arm64), signed ad hoc |

It is the macOS build (refer to [port/macos/README.md](../macos/README.md))
built for iOS: the same guest (the game as ILP32 AArch64 code, with its
memory at a base register) with the iOS code paths (`HALO_IOS`) beside the
Mac's (`HALO_MACOS`), and the same host (`port/android/host`,
`port/linux/host`, `port/macos`) with UIKit's app around it (`port/ios`).
The renderer is the Metal backend of the macOS build. It was tested on an
iPhone 15 Pro Max (A17 Pro) with iOS 27, and in the Simulator of iOS 26.5.

The App Store does not take the game, a decompilation of a commercial game.
The app is for sideloading: installed from a Mac with Xcode's tools (below),
or signed again by a sideloading tool (AltStore, SideStore) from the ad hoc
build.

## Requirements

- A Mac with Apple silicon, and Xcode 15 or later with its iOS SDK (and its
  Simulator's, for `ninja ios_sim`). The iOS build is set up whenever
  `configure.py` finds the SDK.
- What the macOS build needs (refer to its README): the pinned LLVM for the
  guest, CMake for SDL3, a network connection for the first build
  (`configure.py` downloads musl and SDL3 to `build/ios/third_party`).
- For a device: an Apple developer team (a paid membership: the app's
  entitlements, below, are not given to free teams), signed in to Xcode
  (Xcode, Settings, Accounts), and the device in Developer Mode (Settings,
  Privacy & Security, Developer Mode).

## Build and install

```sh
python3 configure.py --ios-team <team ID>      # e.g. --ios-team 5NH5B2VG6Y
ninja ios                                       # build/ios/OpenH1.app
ninja ios_install                               # onto the connected iPhone or iPad
python3 tools/ios_app.py launch                 # start it
python3 tools/ios_app.py log                    # its log (Documents/logs/halo.log)
```

### Signing

With `--ios-team`, the first `ninja ios` makes a development provisioning
profile (`build/ios/embedded.mobileprovision`) with
`tools/ios_provision.py`: the app is built by ninja, not by Xcode, but only
Xcode makes profiles without an App Store Connect key, so the script writes
a stand-in Xcode project (one empty app with the app's bundle identifier and
entitlements) and builds it with `xcodebuild -allowProvisioningUpdates`.
That registers the bundle identifier (`com.openh1.openh1`, the macOS app's;
`--ios-bundle-id` chooses another), turns on the capabilities the
entitlements name, and makes a profile that holds the team's registered
devices (Xcode registers a connected device). Run it again after adding a
device: `python3 tools/ios_provision.py --team <team ID>`.

`tools/ios_app.py` signs the app with the team's Apple Development
certificate (`--ios-identity` names another), the profile and
`port/ios/halo.entitlements`:

- `com.apple.developer.kernel.extended-virtual-addressing`: the guest's
  4 GB are reserved at once, at a multiple of 4 GB, with guards
  (`host_memory_base.c`): more address space than an app has without it.
- `com.apple.developer.kernel.increased-memory-limit`: the game's memory
  is more than the default limit of some devices.

Without `--ios-team` the app is signed ad hoc, without a profile: a device
refuses it, but a sideloading tool signs it again with its own.

## The guest's code

iOS gives apps no memory that was writable and is executable: not with
`mprotect`, and not `MAP_JIT`'s (allowed only under a debugger). The other
builds' loader (`port/android/host/host_loader.c`) writes the guest image's
code into memory and makes it executable, so on iOS the code is part of the
signed executable instead, at the guest's addresses:

1. The guest image (the ELF file the other builds load) is in a section of
   the executable's `__TEXT`, `__TEXT,__halo_guest`, on a 16 KB boundary
   (`port/ios/guest_image.S`). The image's segments lie at file offsets
   that equal their addresses modulo 64 KB (ld.lld's alignment), and its
   header, code and constants (`r--`, `r-x`, `r--`) are one run of the
   file, below the data (`rw-`), which starts on a page of its own.
2. The loader reserves the guest's 4 GB as on macOS, at a base that is a
   multiple of 4 GB (`host_memory_base.c`), copies the image there and fills
   its import table (in the image's `.data`).
3. It then maps the read-only part (`0xA0000000` up to the data) again,
   from the executable's file (`dladdr` gives its path and the image's
   offset in it), with `mmap(MAP_FIXED, PROT_READ | PROT_EXEC)`: the pages
   of the signed executable, which the kernel checks against the app's
   signature as it does any of the executable's. Nothing is written into
   code or made executable by the host.

The base register makes this possible: every guest memory access and
indirect branch is the base plus a 32-bit offset
(`tools/guest_base_register.py`), and the base's low 32 bits are zero, so
code mapped at base plus its link address works anywhere in the process,
and its PC-relative references to its data reach the data at base plus the
data's address. No relocation is needed: the image is static, linked at
its guest addresses, and its only fix-ups, the host functions it calls,
are the import table's entries, in the data.
`host_image_read_only_range` (in `host_loader.c`, tested by
`port/android/tests/host_loader_test.c`) checks that the image is laid out
so.

The Simulator is a Mac process, without iOS's code signing rules: the same
mapping runs there, but proves nothing of the device's.

## The app

`tools/ios_app.py` makes `OpenH1.app`:

| Item | What |
| --- | --- |
| `halo` | The executable |
| `Assets.car`, `AppIcon*.png` | The icon (actool's, from `port/ios/openh1-icon-1024.png`, which `tools/app_icon.py` draws) |
| `brokers.txt` | Internet play's brokers, copied beside `config.toml` at each start |
| `shaders.metallib` | The Metal shaders, made ahead of time (`tools/macos_shaders.py --platform ios`) |
| `Licenses/` | The licences of OpenH1 and of what the app carries |
| `embedded.mobileprovision` | The development profile (with `--ios-team`) |

`Info.plist`: the macOS app's identifier and version (the build's number),
landscape only, full screen without the status bar, `UILaunchScreen`
(black), UIKit's scenes (SDL's scene delegate), `UIFileSharingEnabled` and
`LSSupportsOpeningDocumentsInPlace` (the Files app shows the app's
Documents), `GCSupportsControllerUserInteraction`, Game Mode's keys (the
games category, `LSSupportsGameMode` from iOS 18.6 and `GCSupportsGameMode`
before it: the system turns Game Mode on while the game is fullscreen, with
Bluetooth's lower latency for AirPods and controllers), the microphone's
(voice chat) and the local network's (system link) explanations, and
`CFBundleURLTypes`: the app is the handler of `openh1://` links (invites and
parties' links), as the Mac app is. UIKit gives SDL's scene delegate the
link (`scene:openURLContexts:`, or the scene's options when the link starts
the app), which SDL sends on as a drop event, and `port/macos/host_links.c`
keeps it for internet play (`port/macos/README.md`).

When it starts (`port/ios/host_app.m`), the host puts the game's files in
the app's Documents folder: `config.toml`, the data root (`maps`,
`custom_maps`, `debug.txt`), the saves (`saves`), and the host's log
(`logs/halo.log`, the previous start's `logs/halo.previous.log`). The Metal
pipelines' archive is in `Library/Caches/OpenH1`.

`main` hands the process to UIKit (`SDL_RunApp`), which calls the host back
once the app has launched; the game then runs on the main thread as on
macOS, and its event polls turn UIKit's run loop.

### The game data

The players supply the Xbox game's maps. While the app's Documents have no
`maps` folder of the Xbox game, the first start (`port/ios/data_import.m`,
before the game starts) asks for one of:

- **the disc image** (`.iso` or `.xiso`), chosen with the system's document
  picker (On My iPhone, iCloud Drive, a USB drive, another app's storage)
  and read where it lies: its `maps` folder (and its movies' folder, `bink`,
  when it has one) is extracted into Documents by
  the desktop builds' extractor (`port/linux/src/xiso.c`, built into the
  host beside the renderer's shared parts), with a progress bar;
- **a maps folder**, chosen the same way, whose files are copied;
- **the maps folder copied by hand** into OpenH1's folder with the Files app
  (On My iPhone, OpenH1) or Finder's window of the device, then "Look
  again".

What is found is checked as the macOS app checks it: `ui.map` must be the
Xbox game's (the PC version's and Custom Edition's are refused, saying
why). A copy goes to `maps.partial` and is named `maps` once whole.
`HALO_DATA_ROOT`, or `paths.data` in `config.toml`, skips the screen.
`HALO_IMPORT_ANSWER=<disc image or folder>` answers it, for tests.

From a Mac, the maps can also be copied into the app's Documents directly:

```sh
xcrun devicectl device copy to --device <device> --domain-type appDataContainer \
    --domain-identifier com.openh1.openh1 --source <folder>/maps --destination Documents/maps
```

### The background

As the app goes to the background, the presentation stops (iOS refuses GPU
work from the background) and the game waits in its next event poll until
the app is back; iOS suspends the process meanwhile. In a network game, the
other machines drop it after 15 seconds, as on Android.

## Controls

Game controllers (MFi, Xbox, DualSense, through the GameController
framework) are SDL's, as on macOS: a pad paired while the game runs is
taken at once.

### Touch controls

The on-screen controls are the Android app's (refer to "Touch controls" in
[port/android/README.md](../android/README.md)), drawn by a UIKit view over
the game's (`port/ios/touch_controls.m`, a port of `TouchControls.java`,
`TouchLayout.java`, `TouchContext.java` and `GyroscopeAim.java`): the same
layout on the same 960 x 540 grid, laid out inside the screen's safe area
(clear of the sensor housing and the rounded corners), with the default
places moved clear of the game's HUD, which keeps the corners of a wide
screen (the move stick off the motion tracker; pause, back and the options
by the hide toggle at the top's middle; the D-pad, light and grenade type
at the left between the messages and the tracker; crouch by zoom), which
`tools/test_ios_touch_layout.py` checks on the iPhones' and the iPads'
shapes; the same pictures
(the HUD's buttons in `display.hud_button_style`, Kenney's line icons) and
captions (`input.touch_captions`), copied from the Android app's resources
into the app's `touch/`; the buttons that follow the player's state; Crouch
tapped on and off or held (`input.touch_crouch`); the
move stick (fixed or floating), swipe aiming (with the stick's aim assist,
`input.touch_aim_assist`) and gyroscope aiming (CoreMotion); the toolbar's
hide and options, and the layout editor. The options are a panel of
UIKit's: vibration (the phone's haptics, for a press and for port 0's
rumble), gyroscope aiming, the floating stick, opacity, look sensitivity,
editing the layout, and hiding, adding, copying and sizing buttons. The
layout is kept in the app's preferences in the Android app's layout file
format, and Export and Import (in the editor) write and read
`touch-layout.txt` in the app's Documents, so a layout moves between the
two.

The overlay hands its state to the game through
`port/android/host/host_touch.c`, which Android's shares (without JNI:
`host_touch.h`), and the game reads it through the same host imports
(`port/ios/host_imports.list`; `port/linux/src/touch_input.c`,
`HALO_TOUCH_CONTROLS`). It shows only in a game (not in the menus or a
cinematic), and with `input.touch_controls = "auto"` only while no
controller, keyboard or mouse is connected (the system's list, and the
game's controllers). While it does not show, every finger passes through
to SDL's view: in the menus SDL makes them the mouse, whose pointer the
menus take (the overlay turns `SDL_HINT_TOUCH_MOUSE_EVENTS` on there, and
off in a game, where a tap would fire); in a game a tap skips a cinematic
or types in the text chat (`touch_input.c`).

## Settings

The settings are the desktop builds' (`config.toml` in the app's Documents;
refer to [port/linux/README.md](../linux/README.md)), with the touch
controls' (`input.touch_controls`, `input.touch_captions`,
`input.touch_aim_assist`, `input.touch_crouch`:
[port/android/README.md](../android/README.md)),
and these of iOS's own:

| Setting | Meaning |
| --- | --- |
| `display.render_scale` | The fraction of the screen's pixels the game draws, each way, from 0.25 to 1.0: the game's frames and the presentation's drawables are that size, and the display scales them up. 0.67 draws less than half the pixels. The default, 0, is the device's: 0.67 on an iPhone whose screen has 3 pixels to the point (it keeps the 120 Hz frame there, where all of them took 8 to 10 ms of GPU a frame on an iPhone 15 Pro Max and ran at 60 Hz), 1.0 on an iPad or a 2x iPhone. Read at start (`HALO_RENDER_SCALE`). |

The host logs the device's thermal state at the start and as it changes
(`thermal state: nominal`, `fair`, `serious`, `critical`): a hot phone
slows its processor and its GPU.

## Metal on iOS

The Metal backend is the macOS one. What differs:

- iPhones' GPUs (and the Simulator's) have no BC texture formats: their
  DXT textures are transcoded to ASTC 4x4 when uploaded
  (`display.astc_textures`, `xgpu_metal_textures.c`,
  `port/linux/astc/xgpu_astc.h`), a quarter of the memory and bandwidth of
  the textures decoded to 32 bits, which they were before (and are with
  `display.astc_textures = "off"`). Nearly every block is transcoded
  exactly; the rest (DXT3 and DXT5 alpha that varies inside a block, mostly)
  are encoded by astc-encoder on two worker threads (utility QoS) the first
  time a texture is drawn, the texture drawn decoded meanwhile, and kept in
  `Library/Caches/OpenH1/astc` (256 MB at most; the stock maps' are some
  60 MB). iPads with an M chip sample DXT as it is. The Simulator decodes
  ASTC on its CPU when a texture is uploaded (Apple's `libate`), so its
  loading times say nothing of a device's; `HALO_METAL_STATS` logs the
  textures' bytes every 10 seconds.
- The screen is a `UIScreen`: its refresh rate (ProMotion's 120 Hz), and
  its EDR headroom for `display.hdr`. The display link runs at 120 while
  the frames keep up and at a steady 60 while they do not ("The window and
  presentation" in [port/macos/README.md](../macos/README.md)).
- The back buffer stays RGBA16F with `display.hdr` (not a 32-bit
  extended-range format): it holds the game's values in sRGB's curve past
  1 (the tone map's input, and after it up to the display's headroom),
  which `BGR10_XR`'s 1.25 (1.66 times paper white in linear light) does
  not reach, and the destination alpha the game blends with, which
  `BGR10_XR` has none of (`BGRA10_XR` has it, in 64 bits a pixel, as
  RGBA16F).
- The Simulator's GPU has no binary archives (the pipelines' archive is not
  used there) and its drawables say nothing of their presentation.
- The 3D view and the HUD are drawn in one pass (the aside passes' break,
  which the Mac keeps for its recordings, is not made: "Aside passes" in
  [port/macos/README.md](../macos/README.md)), and the frame's last pass
  discards what the next frame clears ("Frame-end discards" there): a
  phone's tile-based GPU stores none of the screen's depth and samples.
- `display.graphics = "enhanced"` gives MSAA 2x on iOS (not SMAA), resolved
  in tile memory: at the iPhone 15 Pro Max's 2796x1290, measured on an M1
  (the same family of GPU), about SMAA's cost (a10: 7.1 ms of the GPU a
  frame against 6.8; Blood Gulch alone: 5.2 against 7.5), with the
  geometry's edges smoother; on the iPhone itself, in Blood Gulch with
  seven bots, 6 to 7 ms against SMAA's 9 to 10 before the fight builds
  (both near the frame's 15 ms at 60 Hz after it). `display.anti_aliasing
  = "smaa"` still chooses SMAA.

## The Simulator

```sh
xcrun simctl list devices                       # a device, e.g. an iPhone's
xcrun simctl boot <device>                      # headless; Simulator.app shows it if open
ninja ios_sim
python3 tools/ios_app.py install --simulator
python3 tools/ios_app.py launch --simulator
python3 tools/ios_app.py log --simulator
xcrun simctl io <device> screenshot shot.png
```

Its data folder is `xcrun simctl get_app_container <device> com.openh1.openh1 data`;
put `maps` in its `Documents`.

## Not done yet

- The safe areas: the game draws over the whole screen, the sensor housing's
  and the home indicator's edges too.
- System link on a device: iOS lets apps send broadcasts only with the
  multicast networking entitlement, which Apple grants on request.
