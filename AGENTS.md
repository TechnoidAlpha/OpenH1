# Notes for agents

This repository ports the Halo: Combat Evolved decompilation (Xbox build
01.01.14.2342, `cachebeta.exe`) to Linux, Windows, Android, macOS, iOS and the web. The game's C
sources are the decompilation; the port adds a platform layer that
implements the Xbox APIs the game calls, and features the Xbox game did not
have (a new netcode, internet play, the PC version's menus, Custom Edition
maps, touch controls). The project is published as OpenH1, a fork of OpenCE
(OpenCommunityEdition/OpenCE), whose netcode it keeps compatible.

Start with [README.md](README.md). The other documents are listed at the
end.

## Layout

| Path | Contents |
| --- | --- |
| `source/` | The game: 466 C files of the decompilation, compiled as they are with few changes (each marked, below) |
| `port/linux/src/` | The platform layer the ports share: Direct3D 8's front end (`d3d8_device.c`), behind `xgpu_backend.h` the renderer's command stream to the host's backend (`xgpu_stream_encode.c`), DirectSound and XInput on SDL3, files, threads, memory, sockets, settings (`port_config.c`), the mods folder (`port_mods.c`), internet play (`p2p*.c`), the Linux release's layout (`install_layout.c`) and self-updater (`updater.c`), the dedicated server's platform side (`server_*.c`: the mode, the log, the rotation's grammar, HTTP, RCON, the master servers' publisher; `posix_server.c`, in the guest) and the master servers' list (`master_list.c`) |
| `port/linux/game/` | The port's game-side code, compiled as the game's sources are: the distributed netcode (`network_*.c`), co-op, OpenCE's gametypes (`game_modes.c`, `infection.c`, `gun_game.c`), map sharing (`map_share*.c`), the menus (`menu_tags.c`, `menu_functions.c`), Custom Edition maps (`custom_edition_*.c`, `cache_file_formats.c`), the tag validator (`tag_validate.c`, `tag_schema_*.c`), mods (`mods.c`, `mod_*.c`), streamer mode's word filter (`text_filter*.c`), the dedicated server's director, commands, gametypes and status (`server_*.c`), a player host's Host Controls and next game and playlist (`network_host_controls.c`, `network_host_game.c`), touch, FOV and more |
| `port/linux/include/` | The port's headers: the limits (`halo_port_limits.h`, with `HALO_PORT_NETWORK_VERSION`), capacities (`halo_port_capacity.h`), the prefix header, `halo_linux_source_fixups.h` (port functions the game's sources call) |
| `port/include/xdk/` | Stand-ins for the Xbox SDK's headers (declarations only) |
| `port/windows/` | The Windows build's host (`host/`: the x32 guest in a 64-bit Windows process, drawn with Vulkan, `tools/windows_build.py`) and the `posix.h` and `update.h` functions it shares (`src/win32_*.c`) |
| `port/linux/host/` | The Linux host of the Linux builds (`ninja linux_x86_64`, `ninja linux_arm64`), which the macOS build shares: its entry point (`host_main.c`), the desktop's and VR's host functions (`host_desktop.c`, `host_vr.c`: OpenXR) and their import lists, the guest's half of the desktop (`guest_desktop.c`), the command stream's VR records (`host_vr_stream.c`); the rest of the host is Android's (`port/android/host`), its renderer Vulkan's (`port/linux/vulkan`) |
| `port/linux/vulkan/` | The Vulkan renderer of the Linux builds and the Android app, in the host, behind the renderer's command stream: the device, pipelines and passes (`xgpu_vulkan*.c`), the window (`host_vulkan.c`, `guest_vulkan.c`), VR's eye passes and composition (`host_vulkan_vr.c`, its shaders' GLSL `xgpu_vulkan_vr_glsl.inc`); [README](port/linux/vulkan/README.md) |
| `port/linux/astc/` | The Xbox's DXT textures as ASTC 4x4 in the hosts whose GPUs have no BC (iOS's Metal, the Android app's Vulkan): the exact transcoding (`xgpu_astc.c`), astc-encoder for the rest (`xgpu_astc_encode.cpp`, the hosts' only C++), its workers and disk cache (`xgpu_astc_cache.c`); `display.astc_textures` |
| `port/android/` | The Android app: the guest runtime, the host library (`host/`, which the Linux and macOS hosts share; its renderer is Vulkan's, `port/linux/vulkan`), the Java app (`app/`), host imports (`host_imports.list`, every guest's; `android_imports.list`, the app's own) |
| `port/macos/` | The macOS host's own files: Darwin's system calls for the guest (`host_syscall.c`), the Metal renderer's backend (`xgpu_metal.m`, its bindings `xgpu_metal_bindings.h`, the command stream's table `xgpu_metal_stream.m`, the MSL emitter's shaders `xgpu_metal_shaders.c`, the textures `xgpu_metal_textures.c`, the NV2A combiner as one function of function constants `xgpu_metal_combiner.c`, the pipelines' description `xgpu_metal_pipeline.m`, the shader library's build tool `tools/xgpu_metal_prebuild.c`), the window and its presentation (`host_metal.m`), their tests (`tests/`), the app (`host_app.m`: its folders, the maps panel, the window kept; `halo.entitlements`, `openh1-icon.icns`); the rest is `port/android/host` and `port/linux/host` |
| `port/ios/` | The iOS and iPadOS app's own files: its start (`host_app.m`: the Documents folder, the log, the background), the touch controls (`touch_controls.m`, Android's overlay in UIKit, through `port/android/host/host_touch.c`; `host_imports.list`), the guest image in the executable's `__TEXT` (`guest_image.S`), the entitlements, the icon; the rest is the macOS build's (`port/macos`, `port/android/host`, `port/linux/host`) |
| `port/assets/` | What the builds embed or ship: high-res HUD (`hud/`), fonts, titles, menus (`menus/`), the Android touch controls' art (`touch/`), icons, network brokers, the bots' map profiles (`bots/`, made from play logs by `tools/play_log/profile.py`), the Linux desktop entry (`linux/`), Steam's library artwork for the shortcut the Linux and Steam Frame installs add (`steam/`) |
| `port/third_party/` | Vendored libraries, each with a README naming its upstream, version and checksum |
| `port/tools/` | Stand-alone tools (`cache_file_report.c`) |
| `tools/` | Build scripts (`build_common.py`, what the others share; `linux_guest_build.py`, `vulkan_build.py`, `windows_build.py`, `android_build.py` (also the guest image of every guest build), `macos_build.py`, `macos_app.py` (the macOS app, its disk image, signing and notarization), `macos_shaders.py` (the macOS and iOS apps' shader libraries), `ios_build.py`, `ios_app.py` (the iOS app, its signature, devicectl's and simctl's install, launch and log), `ios_provision.py` (its development profile, by Xcode's automatic signing), `render_manifest.py` (the renderer's manifest, the game's vertex shaders from its code), `ci_build.py`, `release_names.py` (the names releases publish the builds under), `release_sign.py` (a release's SHA256SUMS and its signature by the release key, `port/assets/release-key.pub`, which the self-updaters check: `port/linux/src/release_signature.c`), `linux_package.py`, `linux_install.sh` (the one installer: the Linux release's `install.sh`, the Steam Deck's install, and the Steam Frame package's), `frame_install.sh` (it, run on a Frame over ssh), `frame_package.py` (the Steam Frame's)), the game's 32-bit x86 ABI for test tools (`x86_game_abi.py`: `map_validate`), the guest's base register (`guest_base_register.py`, measured by `base_register_bench/`), generators (`ce_menus.py`, `port_settings.py`, `hud_assets.py`, `title_assets.py`, `embed_assets.py`, `xdk_headers.py`, `touch_assets.py`: the Android touch controls' pictures, whose layout `touch_preview.py` draws; `steam_art.py`: Steam's library artwork, `port/assets/steam`), tests, OpenCE's `stagekit/` (scenarios staged and instrumented on the real engine, no retail assets; its map writer runs as a guest image of the 64-bit builds' host), and `scorecard/` (the 60/120 Hz simulation's check: scenarios at each rate compared, the 30 Hz gate against master) |
| `docs/` | Longer design notes (`custom_edition_caches.md`, `mods.md`, `dedicated_server.md`, `master_protocol.md`) |
| `pgo/` | Profile-guided optimisation profiles |
| `deploy/` | Running a dedicated server: systemd, Docker, Pterodactyl, Helm, PufferPanel, MCSManager, example gametypes |
| `assets/` | Game data for local runs (gitignored: `maps/`, `custom_maps/`) |
| `.github/workflows/` | CI: builds every platform on each push, publishes releases from `main` |

## Building

```sh
python configure.py            # debug build; add --release, --profile, --vr, --pgo=off
ninja linux_x86_64             # build/linux_x86_64/halo: x32 guest in a 64-bit host (x86-64 Linux only)
ninja linux_arm64              # build/linux_arm64/halo: arm64_32 guest (64-bit ARM Linux only)
ninja android                  # the Android guest image and native libraries
ninja android_apk              # the APK (Gradle)
ninja macos                    # build/macos/OpenH1.app (and halo), on Apple silicon (port/macos/README.md)
ninja macos_dmg                # build/macos/OpenH1.dmg
ninja ios                      # build/ios/OpenH1.app, on Apple silicon with Xcode's iOS SDK (port/ios/README.md);
                               # configure.py --ios-team <team> signs it for development; ninja ios_install
ninja ios_sim                  # build/ios_sim/OpenH1.app, the iOS Simulator's
ninja windows                  # build/windows/halo.exe: x32 guest in a 64-bit Windows host (on Windows, or on
                               # Linux after configure.py --windows-cross; ninja windows_guest, its guest
                               # image, anywhere after configure.py --windows-guest)
python tools/ci_build.py linux_x86_64 release   # what CI builds
```

- `configure.py` writes `build.ninja`; `configure_args` at its top records
  the options it was run with. Restore them after trying other options.
- The game's sources are C89 (`gnu89`): declarations come before
  statements. Every build compiles them with clang in MSVC's semantics
  (`-fms-extensions -fshort-wchar -fcommon`), as ILP32 guests.
- Every native build compiles without fused multiply-add and with the
  vendored musl maths (`halo_math.h`), so that every machine of a network
  game computes the same results. Do not call the system's maths functions.
- Android: the game is an ILP32 AArch64 guest (`arm64_32`) in a 64-bit app.
  Guest code can call only the host imports declared in
  `port/android/guest/runtime/guest_host.h` and listed in
  `port/android/host_imports.list` (the app's own in `android_imports.list`),
  and only the SDL functions `guest_sdl.c` bridges.
  `tools/check_host_imports.py` (run by the build) checks that the lists
  and the declarations agree, and that every argument crosses to the host
  as the guest left it. Run `ninja android`
  after changing anything it compiles: `port/linux/src`, `port/linux/game`,
  `port/android`, `source/`, the build scripts. The release publishes the
  APK alone, `OpenH1-Android[-debug].apk` (`tools/release_names.py`; the
  app's `Updater.java` asks for it), its licences in its
  `assets/licenses` (`tools/android_build.py`).
- The guest builds (Android, Linux arm64 and x86-64, macOS) compile, link
  and check the guest with one pinned LLVM release (`tools/guest_toolchain.py`:
  `LLVM_VERSION` and each computer's archive's SHA-256), which
  `configure.py` downloads once into the user's cache (`--guest-llvm-cache`),
  not the build folder; the hosts use the system's compiler. A build's own
  compiler option, or `--guest-llvm=system`, uses the PATH's (and the
  NDK's, Homebrew's) instead. `python tools/guest_toolchain.py` prints its
  `bin/`, for the guests' tests.
- Linux x86-64 and arm64: the only Linux builds, 64-bit, drawn with
  Vulkan 1.3. The same guest and host as Android, with the desktop's code
  paths (`tools/linux_guest_build.py`); the guest is `x32` on x86-64,
  `arm64_32` on AArch64. What is particular to a processor is its
  `GuestArch` in `tools/guest_arch.py`. The x32 image is linked below 2 GB
  (`image_base`, which must equal `HALO_GUEST_IMAGE_BASE` in
  `halo_android_abi.h`), links no runtime library, and may not touch
  `%fs`/`%gs` (`tools/guest_image_check.py`, run by the build). The host
  calls guest code only through `host_call_guest`, which passes arguments
  zero-extended. Each target configures only on its own processor, whose
  default build it is; `configure.py` downloads musl, SDL3, FFmpeg (the
  Bink movies' decoders, `tools/ffmpeg_fetch.py`) and the Vulkan headers
  (OpenXR's with `--vr`) into `build/<name>/third_party`. The
  renderer is Vulkan's, in the host (`port/linux/vulkan`, `HALO_VULKAN`,
  `tools/vulkan_build.py`), which draws the guest's frames from the
  renderer's command stream (`port/linux/src/xgpu_stream_encode.c`).
  `--vr` gives the build its VR mode (`HALO_VR`, OpenXR's Vulkan
  binding).
- Windows: the Linux builds' desktop guest (`X32`, with `HALO_WINDOWS`,
  no red zone and frame pointers) in a 64-bit Windows host
  (`port/windows/host`, `tools/windows_build.py`), drawn with Vulkan as
  the Linux builds are: its memory below 4 GB reserved at start-up, each
  guest thread on a stack there, Linux's system calls performed with
  Windows's (`host_syscall_win.c`), the guest's imports called through
  `sysv_abi` thunks (`tools/android_imports.py --host-abi sysv`: the
  host's calling convention is Microsoft's). The host shares
  `port/android/host`'s portable files through a POSIX shim
  (`port/windows/host/posix`): a change there must build for Windows too
  (`configure.py --windows-guest` and `ninja windows_guest` check the
  guest anywhere; the host builds on Windows only).
- macOS: the guest of the Linux arm64 build (`ARM64_32`, with the macOS
  defines: `HALO_MACOS`, `HALO_METAL`), with its
  memory at a base the host chooses (`tools/guest_base_register.py`: every
  guest memory access adds `x21`, and `x21` and `x22` are reserved,
  `GuestArch.base_register_flags`). Guest code built for it must not write
  those registers. Its host is the Linux one (`port/linux/host`) with
  `port/macos`; its system calls are translated to Darwin's
  (`port/macos/host_syscall.c`). Run `ninja macos` after changing what it
  compiles (as for Android). The renderer is Metal's, in the host
  (`port/macos/xgpu_metal.m`): the guest writes its frames as the command
  stream, and the host
  builds the MSL emitter and the Xbox's texture decoding from
  `port/linux/src` (`tools/macos_build.py`, `SHARED_SOURCES`) beside a
  stand-in for `platform.h`: they may use only what it declares.
  `configure.py --guest-base` gives the Linux arm64 and Android builds the
  base register too (not x86-64's, which has none).
- iOS: the macOS build's guest (with `HALO_IOS` beside `HALO_MACOS`) and
  host built for iOS (`tools/ios_build.py`, which `tools/macos_build.py`'s
  `generate_apple_build` serves), with `port/ios` in place of
  `port/macos/host_app.m`. iOS gives apps no memory that was writable and is
  executable, so the guest image is in the executable's `__TEXT`
  (`port/ios/guest_image.S`) and the loader maps its code from the signed
  executable at the guest's addresses (`host_loader.c`,
  `HOST_IMAGE_FROM_FILE`): the image's read-only segments must stay one run
  of the file below its data (`host_image_read_only_range`). Host code
  tests `TARGET_OS_IPHONE` (`TargetConditionals.h`), and
  `TARGET_OS_SIMULATOR` for the Simulator's. Run `ninja ios` after changing
  what it compiles (as for macOS).
- The Linux release is a tree laid out as a Unix prefix is (`bin/openh1`,
  `lib/openh1`, `share/`), which `tools/linux_package.py` lays out and zips
  for every Linux download (`OpenH1-Linux[-debug].zip`,
  `OpenH1-Linux-VR.zip`, `OpenH1-Linux-ARM64[-debug].zip`, as
  `tools/release_names.py` names them and `UPDATE_NAME` in
  `port/linux/src/updater.c` asks for them; but the
  Steam Frame's `OpenH1-Steam-Frame.zip` is `tools/frame_package.py`'s
  package, not a tree); the game
  finds its files from its executable's place
  (`port/linux/src/install_layout.c`), and updates itself only in a
  release's tree (or, the Frame's, in its package's folder: the game
  downloads the package, and its launcher `halo-vr.sh` installs it with
  the package's `install.sh` once the game quits). The tree's `install.sh` (`tools/linux_install.sh`)
  copies it to `~/Applications/OpenH1` (the VR build's, whose tree has
  `share/openh1/vr`, to `~/Applications/OpenH1-VR`) and gives it a menu
  entry, a Steam shortcut and the shortcut's library artwork (the Steam
  Deck's install); a package's tree (`linux_package.py tree`) has none.
  The Frame's package has the same script as its `install.sh` (to
  `~/apps/halo-vr`), one installer with a small part for each package;
  `tools/frame_install.sh` runs it on a Frame from a computer, over ssh.

### Defines

| Define | Set by | Meaning |
| --- | --- | --- |
| `HALO_ANDROID` | Android | The app: its display, input, files and lifecycle |
| `HALO_GUEST` | Android, Linux x86-64, Linux arm64, macOS, iOS | The game is an ILP32 guest image in a 64-bit host process: its memory, its host imports, the SDL it reaches. The instruction set is the compiler's: test `__aarch64__` or `__x86_64__` |
| `HALO_GUEST_MACHO` | Android, Linux arm64, macOS, iOS | The guest is compiled for clang's Darwin target (`arm64_32`), whose Mach-O objects are converted afterwards |
| `HALO_ARM64_GUEST` | Android, Linux arm64, macOS, iOS | `HALO_GUEST` on AArch64 (its name before); new code tests `HALO_GUEST` or `__aarch64__` |
| `HALO_GUEST_BASE` | macOS, iOS, `--guest-base` | The guest's memory at a base register (`tools/guest_base_register.py`): host addresses are base plus offset (`port/android/host/host.h`) |
| `HALO_MACOS` | macOS, iOS (the guests) | The macOS app: its folders (config.toml in the app's settings folder, not the Linux release's layout) and no self-updater; the host tests `__APPLE__` |
| `HALO_IOS` | iOS (the guest) | The iOS app, with `HALO_MACOS`; the host tests `TARGET_OS_IPHONE` |
| `HALO_WINDOWS` | Windows (the guest, `tools/windows_build.py`) | The Windows build: with `HALO_GUEST`, the platform layer's `_WIN32` choices that are behaviour, not Windows's API (the folders, config.toml's place, the self-updater's release); the game's sources' Windows choices (socket buffers, the build's label) |
| `HALO_RELEASE` | `--release` | No assertions checked |
| `HALO_PROFILE` | `--profile` | The profiling build; a normal build must be unchanged by it |
| `HALO_VULKAN` | Android, Linux x86-64 and arm64, Windows | The window is Vulkan's, drawn by the host (`port/linux/vulkan`) |
| `HALO_VR` | `--vr` (Linux x86-64 and arm64) | The VR mode: OpenXR (`port/linux/host/host_vr.c`), with Vulkan's eye passes (`port/linux/vulkan/host_vulkan_vr.c`) |
| `HALO_METAL` | macOS, iOS | The window is Metal's, drawn by the host (`port/macos/host_metal.m`) |
| `HALO_ASTC` | Android (the host's Vulkan renderer) | The DXT textures as ASTC on a device without BC (`port/linux/astc`, `xgpu_vulkan_textures.c`); the Apple hosts build it always, the Linux hosts not |

`HALO_LINUX` and `HALO_CUSTOM_EDITION` are **not** defined by any build:
code guarded by them compiles to nothing. Custom Edition support is a
setting (`game.custom_edition`), checked at run time with
`custom_edition_cache_tags_loaded()`.

## Testing

```sh
python -m pytest -q tools/harness tools/test_touch_menu.py   # the engine's functions in a fake world (clang -m32: gcc-multilib)
python -m pytest -q tools/test_linux_port.py                # the build, the maps' checks (needs assets/maps; map_validate,
                                                            # tools/x86_game_abi.py, needs gcc-multilib on x86-64 Linux)
python -m pytest -q tools/test_mods.py                      # mods played over a map (needs assets/maps)
python -m pytest -q tools/test_bots.py                      # the bots in local games (needs assets/maps)
python -m pytest -q tools/test_screenshot_key.py            # the Screenshot key's PNG in a hidden run (a build, assets/maps)
python -m pytest -q tools/test_cache_file_formats.py tools/test_bmp_files.py tools/test_profile.py
python -m pytest -q tools/test_xiso.py tools/test_port_settings.py  # the disc image's maps and movies extracted; the settings pages' rows fit
python -m pytest -q tools/test_cabinet.py                   # Custom Edition's installer and Halo PC's CD read (made-up cabinets; cabextract checks them)
python -m pytest -q tools/test_render_manifest.py tools/test_render_stream.py  # the renderer's manifest, the command stream
python -m pytest -q tools/test_vulkan_passes.py tools/test_host_vr_vulkan.py  # the Vulkan backend, the VR host (a fake GPU)
python -m pytest -q tools/test_vr_effects.py                # the VR effects: the thickness bake, the eyes' transforms, their SPIR-V
python tools/render_enumerate_sweep.py --data <game data>  # every map's enumeration checked in play (offscreen)
python -m pytest -q tools/test_host_watch_hash.py tools/test_host_units.py tools/test_check_host_imports.py tools/test_host_syscall.py  # the guests' host
python -m pytest -q tools/test_guest_image_check.py         # the guest images' checks (x86-64)
python -m pytest -q tools/test_guest_layout.py tools/test_linux_guest_build.py  # the guests' memory layout, the guest builds' configure
python -m pytest -q tools/test_windows_build.py         # the Windows host's import thunks, the guest build's steps without a shell
python -m pytest -q tools/test_guest_toolchain.py         # the guests' pinned LLVM: its archives, what is kept of them, libxml2's stand-in
python -m pytest -q tools/test_linux_package.py tools/test_install_layout.py   # the Linux release's layout, its install.sh
python -m pytest -q tools/test_guest_base_register.py tools/test_host_memory_base.py  # the base register
python -m pytest -q tools/test_guest_branch_relax.py       # the guests' conditional branches relaxed once the base register puts them out of reach
python -m pytest -q tools/test_macos_metal.py               # the Metal backend (ninja macos_metal_test first)
python -m pytest -q tools/test_metal_post_shaders.py        # the Metal backend's anti-aliasing passes (tools/metal_post_shaders.py)
python -m pytest -q tools/test_hdr.py                       # display.hdr's contract: the draws' classification, the reference curves (docs/hdr.md)
python -m pytest -q tools/test_hdr_backends.py              # display.hdr across the backends: Vulkan's shaders (via SPIRV-Cross) on a Mac's GPU against the reference and Metal's
python -m pytest -q tools/test_macos_app.py                 # the macOS app's Info.plist, entitlements, signature
python -m pytest -q tools/test_astc.py                      # the DXT textures as ASTC: the transcoding, the encoder's bounds, the disk cache
python -m pytest -q tools/test_ios_app.py                   # the iOS app's Info.plist, entitlements, the guest's code section
build/macos/halo_replay <recording> --shots <dir>           # a recorded render stream replayed (ninja macos_replay;
                                                            # HALO_RENDER_RECORD: port/macos/README.md)
python -m pytest -q tools/test_gpu_isa.py                  # tools/gpu_isa.py (a shader's GPU code, on an M1 Mac)
python tools/base_register_bench/run.py                     # the base register's cost (Linux arm64, macOS)
python tools/test_touch_layout.py                           # the Android touch layout (JDK 17+)
python -m pytest -q tools/test_ios_touch_layout.py          # the iOS touch layout clear of the HUD and the safe areas
python tools/touch_assets.py --check                        # the touch controls' pictures as their SVGs draw them
python tools/test_light_storage.py
python tools/test_death_timing.py
python -m pytest -q tools/test_join_page.py                 # the website's join page: the links it reads (Node), nothing loaded from elsewhere
python -m pytest -q tools/test_net_compare.py               # net_compare.py's judging of logs, its copies and their processes
python -m pytest -q tools/test_determinism_compare.py       # determinism_compare.py's reading of the determinism logs
python tools/net_compare.py --a build/linux_x86_64/halo --b <other build> --data <game data> --swap
                                                            # two builds in one network game, in step (retail maps: local only)
tools/wine_bench.sh run                                     # a Windows build under Wine, headless, on a Linux GPU
                                                            # (fetch, shot, logs: port/windows/README.md)
```

`tools/harness/README.md` explains how to add an asset-free test: the code
under test is taken from the sources, not copied. `tools/stagekit/README.md`
explains how to add a scenario: the real engine, with no retail assets.

Running the game without a person at it:

- `debug.network_test` (`HALO_NETWORK_TEST=host:<map>` or `join`) hosts or
  joins a game without the menus and logs each player's state every second;
  several copies play on one machine on different loopback addresses
  (`HALO_NET_ADDRESS`, `HALO_NET_BROADCAST`). `NETCODE.md` describes the
  test settings.
- `halo --server` is a dedicated server (`docs/dedicated_server.md`), and
  `halo --probe [<invite>]` prints a game's advertisement as JSON and exits;
  `tools/test_dedicated_server.py` runs both with clients.
- `debug.exit_after`, `debug.hidden_window`, `debug.null_renderer`,
  `debug.menu_open` (start on one menu screen), `debug.telnet_console`.
- `debug.no_clipboard` (`HALO_NO_CLIPBOARD=1`) keeps a run off the
  clipboard of the person you work for (no invite link put on it, none
  joined from it); runs with `debug.null_renderer`, `debug.test_input`,
  `debug.fixed_step`, `debug.network_test` or `debug.exit_after` keep off
  it too. Give a run its own `HALO_DATA_ROOT` (its maps folder linked in)
  so that its `debug.txt` is its own, not the shared `assets/debug.txt`.
- `debug.determinism_log` (with `debug.frame_time` and a scripted player,
  `debug.test_input`) logs a hash of the game state each tick and every
  taker of the random seeds; two runs of the same input, compared by
  `tools/determinism_compare.py`, part where something besides the input
  reached the simulation ("Determinism" in `port/linux/README.md`). Keep
  the simulation's randomness in the ticks and on the game's seeds.
- Use a virtual display (Xvfb, with `SDL_VIDEO_DRIVER=x11`) for windowed
  runs, never the desktop of the person you work for, and stop only the
  processes you started.

## Rules of the project

### The game's sources

- Change `source/` as little as possible, and mark each change from the
  original with a comment that starts `port:`. Write like the surrounding
  code: its naming, its comment density, and the project's plain,
  descriptive comment style.
- A port function the game's sources call is declared in
  `halo_linux_source_fixups.h` or included with a note naming its file
  (`#include "view_fov.h" /* port: port/linux/game/view_fov.c */`).
- Notes such as `BUG (original, preserved for exact matching)` mark the
  original's bugs. Fixing one is allowed; say so in a `port:` comment.

### The Xbox SDK

`port/include/xdk` was written without the SDK's headers, from the January
build's debug information and public documentation. Never read the SDK's
headers or anything derived from them, never copy from them, and do not
read `xbox/include`. To add a name, follow `port/include/xdk/README.md`.

### Networking

- Raise `HALO_PORT_NETWORK_VERSION` (`port/linux/include/halo_port_limits.h`)
  with any change to what the machines send each other, or to anything they
  must agree on, and say why in the commit. Machines of different versions
  do not play together.
- Only the host decides; clients predict their own players. Read
  `port/linux/NETCODE.md` before changing the netcode.
- Internet play is the same code in every build.
- Anything another machine sends is untrusted: check sizes, indices and
  rates, as the existing handlers do. `debug.network_corrupt` tests this.

### Menus

- The menus start from Halo Custom Edition's: keep its look and layout
  where they work, but do not stay boxed in by a design made for 2001. The
  aim is the menus Halo would have if it came out today (live information
  over decoration, plain names, controls sized for touch and controllers as
  well as the mouse), and still unmistakably Halo: its palette, type,
  framed panels and sounds. Modernise the structure, information and
  wording, not the look. A change that departs from the PC version's menus
  needs no setting. Their XML (`port/assets/menus/ce/`)
  is generated by `tools/ce_menus.py` from the PC version's tags; the
  settings screens come from `tools/port_settings.py`. Do not edit the XML
  by hand: change the generator and run it (the command is in
  `port/assets/menus/README.md`, "Writing them again"), then check that
  `git status` shows only the screens meant to change.
- Functions the menus call but the port has not wired are listed in
  `port/assets/menus/UNWIRED.md`.

### Art and credit

- Replacement HUD, title and menu art is made of hand-drawn redraws.
- An explicit, credited exception to these rules (and to "never the game's
  art" below), decided by the maintainers: the HUD message icons' pictures
  of the weapons and the skull are Aerocatia's and Jessie's art from Halo
  PC Restored (`port/assets/hud/art`, `port/assets/hud/NOTICE.md`), used as
  that NOTICE says, with credit in `AUTHORS.md`. Keep it and its credit; do
  not redraw or remove it, and take nothing else from that art without the
  maintainers' decision.
- Prefer an existing redraw (the ui-svg-handmade set), then a new redraw. A
  Custom Edition picture that cannot reasonably be redrawn (a 3D render, a
  screenshot, a logo) may be used only if it is listed in
  `port/assets/menus/NON_HANDDRAWN.md`.
- The Android touch controls draw the controller's buttons with the HUD's
  redraws (`port/assets/buttons`, in `display.hud_button_style`). What has
  no redraw (the small controls' icons) may come from CC0 packs (Kenney's
  Mobile Controls, `port/assets/touch`), credited in `AUTHORS.md`. Never
  the game's art, icons or logos.
- `AUTHORS.md` credits art only.
- Fonts: Overpass, OpenCE (Newtown, respaced) and, for the console, Hack
  only; see `port/assets/fonts/README.md`. Never ship the maps' commercial typefaces.

### Custom Edition maps

Custom Edition maps live in the data root's `custom_maps` folder with
`bitmaps.map`, `sounds.map` and `loc.map`; their level names are
`custom_maps\<name>`. OpenSauce features are not supported and must not be
added: a map that needs them is refused, and one that only carries
OpenSauce's header runs as stock Custom Edition runs it. Map files are
untrusted input: read them through bounds-checked readers. See
`docs/custom_edition_caches.md`.

### Settings

A setting is an entry in `port/linux/src/port_config.c`, with a default, an
environment variable and a comment that `config.toml` gets. Document it in
the settings table of `port/linux/README.md` (and the Android README for an
Android-only one). A departure from the stock game's look or behaviour
should be a setting, off or stock by default, unless the project decides
otherwise.

### Third-party code and licences

The repository is `AGPL-3.0-or-later` (`LICENSE`); the commits before the
change, and the releases built from them, stay CC0 (`LICENSES/CC0-1.0.txt`).
The README's "Licence" section says what each part is under. Vendored code
goes in `port/third_party/<name>`, copied unchanged, with a README naming
the upstream, the version or commit, the licence, the checksum and what uses
it. Its licence must let it be combined with the AGPL 3.0: MIT, BSD (without
an advertising clause), ISC, zlib, Apache 2.0, MPL 2.0, LGPL or GPL 3.0 or
later, public domain. GPL-2.0-only code and the 4-clause BSD licence cannot
be. Code taken from another project, GPL 3.0 code included, keeps its
notices and its licence, and says where it came from.
The decompilation's upstream, punpckhdq/halo, is AGPL 3.0 from 17 October
2026; bnunu/halo-1 is CC0.

### Commits and pull requests

- Commit subjects are plain sentences saying what changed; the body says
  why and what was checked.
- A pull request is merged with a merge commit (`Merge pull request #N from
  <owner>/<branch>`), and follow-up changes go in separate commits whose
  subject ends with `(#N)`.
- A commit taken from a fork keeps its author and is made with
  `git cherry-pick -x`.

## Documents

| Document | About |
| --- | --- |
| [README.md](README.md) | Downloads, game data, building, build options, profiling builds |
| [port/linux/README.md](port/linux/README.md) | The Linux builds (64-bit, Vulkan), controls, menus, every setting, updates, frame rate, field of view, system link, internet play, voice chat, the server browser, map sharing, security, map checks, how the port operates, the game source changes |
| [port/linux/vulkan/README.md](port/linux/vulkan/README.md) | The Vulkan renderer: its files, building and running, VR, the passes |
| [port/linux/NETCODE.md](port/linux/NETCODE.md) | The distributed netcode, network versions, joining a game in progress, map sharing, transport, testing |
| [port/windows/README.md](port/windows/README.md) | The Windows build, its host, crash reports |
| [port/android/README.md](port/android/README.md) | The Android build, the guest and host, touch controls, Android settings, finding problems |
| [port/macos/README.md](port/macos/README.md) | The macOS build, the guest's memory at a base register, 16 KB pages, Darwin's system calls, the Metal renderer and its window |
| [port/ios/README.md](port/ios/README.md) | The iOS and iPadOS build, its signing and provisioning, the guest's code in the signed executable, the app, the Simulator |
| [port/include/xdk/README.md](port/include/xdk/README.md) | How the SDK declarations were written and are checked |
| [docs/custom_edition_caches.md](docs/custom_edition_caches.md) | Loading and converting Custom Edition maps, and the evidence for each layout |
| [docs/hdr.md](docs/hdr.md) | HDR (`display.hdr`): the shared contract the renderers implement, which draws go past paper white, the tone map and the encodings, the tests |
| [docs/dedicated_server.md](docs/dedicated_server.md) | The dedicated server: its command line, exit codes, commands, HTTP API, RCON, events and files |
| [docs/master_protocol.md](docs/master_protocol.md) | The master servers' protocol: publishing, the probe, the signed list |
| [docs/controller_remapping.md](docs/controller_remapping.md) | Controller button remapping (each player's layout, the overlays' buttons, VR), stick dead zones, the look's inversion |
| [docs/mods.md](docs/mods.md) | Mods: folders of Halo PC tag files whose sounds, bitmaps, text and models play over a map's on one machine |
| [port/assets/text_filter/README.md](port/assets/text_filter/README.md) | Streamer mode's words: Surge AI's list, the review, the allowed words |
| [port/assets/menus/README.md](port/assets/menus/README.md) | The menu files' format and how to write them again |
| [port/assets/menus/NON_HANDDRAWN.md](port/assets/menus/NON_HANDDRAWN.md) | Menu pictures that are not redraws |
| [port/assets/menus/UNWIRED.md](port/assets/menus/UNWIRED.md) | Menu functions that do nothing yet |
| [port/assets/fonts/README.md](port/assets/fonts/README.md) | The fonts and their licences |
| [tools/harness/README.md](tools/harness/README.md) | Asset-free regression tests |
| [tools/stagekit/README.md](tools/stagekit/README.md) | Scenarios staged and instrumented on the real engine, no retail assets |
| `port/third_party/*/README.md` | Each vendored library |
| [AUTHORS.md](AUTHORS.md) | Art credits |
