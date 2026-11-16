# Windows

`ninja windows` makes the Windows build, `build/windows/halo.exe`, with
`SDL3.dll` next to it, on Windows or cross-compiled from Linux. The build is 64-bit: it
starts on 64-bit Windows 10 (version 1903 or later) and Windows 11, and
needs a graphics driver with Vulkan 1.3.

The game is the 64-bit x86 Linux build
([port/linux/README.md](../linux/README.md), "64-bit x86") with a Windows
host: an x32 guest image, inside the executable, in a 64-bit process. The
graphics are the Vulkan renderer of the Linux build
([port/linux/vulkan/README.md](../linux/vulkan/README.md)). The sound, the
input, the settings and the multiplayer functions are the same as on Linux.

The build replaces the 32-bit build of the releases before it. Its download
has the same name (`OpenH1-Windows.zip`), so their self-updaters update to
it.

## Install the release

The release is `OpenH1-Windows.zip`. Unzip it into a folder and start
`halo.exe`. The executable is not code-signed, so when it was downloaded
with a browser, Windows SmartScreen can say "Windows protected your PC". It
says that of any app that is not signed. Choose **More info**, then **Run
anyway**.

### Install with Scoop

[Scoop](https://scoop.sh) installs the same zip with no warning: its
downloads do not carry the browser's mark (the Mark of the Web) that
SmartScreen checks. Scoop checks each download against the hash in its
manifest.

```powershell
scoop bucket add openh1 https://github.com/OpenH1/scoop-bucket
scoop install openh1
```

The bucket is [OpenH1/scoop-bucket](https://github.com/OpenH1/scoop-bucket).
Its manifest follows the releases by itself (its Excavator workflow, every
6 hours).

- The game is in `~\scoop\apps\openh1\current`, with an **OpenH1** Start
  menu shortcut and an `openh1` command.
- The files of the player next to `halo.exe` are kept in
  `~\scoop\persist\openh1` and linked into each version: `config.toml`,
  `maps\`, `custom_maps\`, `mods\`, `screenshots\`, `crashes\`, `bans.txt`,
  `cheaters.txt` and `text_filter.txt`. At the first start the game copies
  `maps\` out of the disc image as usual; the manifest moves that folder
  into `persist` before an update or an uninstall. Saved games stay in
  `%APPDATA%\halo`.
- A first install writes `update.auto = false` in `config.toml`: Scoop
  updates the game (`scoop update openh1`), not the game's updater, which
  would replace the files of Scoop's version folder.

## Requirements

You do not need the Xbox SDK.

- Visual Studio or the Visual Studio Build Tools, with the C++ workload.
  The build uses the 64-bit (x64) MSVC libraries and a Windows 10 or 11
  SDK. It does not use the MSVC compiler.
- LLVM (clang, lld and `llvm-rc`), Python 3, Git and ninja in the `PATH`.
  For example, enter `scoop install llvm python git ninja`.
- A network connection for the first build. `configure.py` downloads, to
  `build/windows/third_party`, the Visual C++ development package of SDL
  3.4.16, the sources of musl and SDL3 for the guest, and the Khronos and
  Vulkan headers. It also downloads LLVM 22 for the guest
  (`tools/guest_toolchain.py`, about 900 MB, once) to the cache folder of
  the user. With `--guest-llvm=system`, the build uses the clang, `ld.lld`,
  `llvm-ar` and `llvm-objdump` in the `PATH` for the guest instead.

## Build the game

1. Go to the root folder of the repository.
2. Enter `python configure.py`.
3. Enter `ninja windows`.

`configure.py` generates the Windows build on Windows, and with
`--windows-cross`, which cross-compiles it from Linux: the same clang and
lld build the host against an MSVC toolchain (its x64 libraries), whose
directories `--windows-vctools-dir` (the versioned folder under
`VC/Tools/MSVC`, the one with `include` and `lib`) and `--windows-sdk-dir`
(the SDK folder with `Include` and `Lib`) name, or `VCToolsInstallDir` and
`WindowsSdkDir`. The build passes them to clang in `INCLUDE` and `LIB`.
`--windows-cc` names the cross compiler (a run-of-the-mill clang by
default); the guest is built with the pinned LLVM, as on Windows.

    python configure.py --windows-cross \
        --windows-vctools-dir "$VCToolsInstallDir" \
        --windows-sdk-dir "$WindowsSdkDir"
    ninja windows

The toolchain can be an [msvc-wine](https://github.com/mstorsjo/msvc-wine)
install or an unmodified MSVC from a Windows install (from an NTFS volume,
for instance). The SDK must have a versioned folder in each of `Include`
and `Lib` (`Include/<version>/um`): a plain `xwin splat` does not, a splat
made with `xwin splat --use-winsysroot-style` does.

On another computer, `python configure.py --windows-guest` and
`ninja windows_guest` make the guest image alone,
`build/windows/halo_guest.elf`, to check that it builds.

## Start the game

Enter `build\windows\halo.exe`.

The game finds the game data as on Linux. Refer to "Start the game" in
[port/linux/README.md](../linux/README.md#start-the-game).

| Item | Location |
| --- | --- |
| Settings | `config.toml` next to `halo.exe` |
| Saved games | `%APPDATA%\halo`, or `paths.saves` in `config.toml` |
| Log | `debug.txt` in the data root (the folder that contains `maps\`) |
| Log of the port | The console. The release build has no console: `halo.log` next to `halo.exe` |
| Vulkan pipeline cache | `%LOCALAPPDATA%\OpenH1\vulkan_pipelines.bin` |
| Crash reports | `crashes\` next to `halo.exe`, until the game sends them |

Without a graphics driver with Vulkan 1.3, the game says so and stops.

## How the port operates

The guest image is the guest of the 64-bit x86 Linux build (`ninja
linux_x86_64`) with the code paths of Windows (`HALO_WINDOWS`): the saved
games in `%APPDATA%\halo`, `config.toml` next to `halo.exe`, the
self-updater of Windows. It is compiled without the red zone, because
Windows writes the records of an exception directly below the stack
pointer, and with frame pointers.

The host is `host/`, with the files of `port/android/host` that do not
depend on the operating system (the loader, the write tracking, the
command stream of the renderer, the SDL functions), and
`port/linux/vulkan`:

| File | Contents |
| --- | --- |
| `host/host_main_win.c` | The entry point. Before it loads `SDL3.dll` (a delay-loaded DLL), the host reserves the memory of the guest below 4 GB. If another module already uses it, the game starts a copy of itself, suspended, and reserves the memory in the copy before it runs. Set `HALO_MEMORY_MAP=1` to log the map of the low 4 GB at the start. |
| `host/host_memory_win.c` | The memory of the guest, as `host_memory.c` on Linux: the Xbox memory window at 0x80000000, the range of the image at 0x70000000, and pools of 256 MB, from the top of the low 4 GB downward. |
| `host/host_thread_win.c`, `host/host_enter_win.S` | Each thread that runs guest code moves to a stack in the memory of the guest, and the limits of the stack in its TEB move with it. |
| `host/host_syscall_win.c` | The Linux system calls of the guest, with the functions of Windows. The file descriptors are those of the C runtime. |
| `host/host_memory_watch_win.c` | A vectored exception handler for the write tracking, the crash report in the log, and a function table that lets the unwinder of Windows walk the frames of the guest. |
| `host/host_crash_win.c` | The crash reports. Refer to "Crash reports". |
| `host/posix/`, `host/host_posix_win.c` | The POSIX functions that the shared host files use, with the functions of Windows. |
| `src/win32_files.c`, `src/win32_net.c`, `src/win32_p2p.c`, `src/win32_upnp.c` | The functions of `port/linux/src/posix.h`: files, sockets, the command line, the `openh1://` links, Discord, UPnP. |
| `src/win32_update.c` | The download and the file replacement of the self-updater (`port/linux/src/update.h`), with WinHTTP. |

The guest calls the host through thunks (`tools/android_imports.py
--host-abi sysv`): the guest uses the System V calling convention, and the
host uses the Microsoft x64 calling convention. The manifest
(`host/halo.manifest`) sets UTF-8 as the code page of the process, so that
paths with any characters operate.

## Crash reports

A crash writes the faulting address, the registers and the calls that led
to it to the log, with the frames of the guest, and to `reports\.crash.txt`
with the log the game keeps in memory, so that the next start offers to
report it as the other computers' builds do (Report a Problem, in
`port/linux/README.md`; the next start makes `halo.log` afresh). The builds of the workflow
(the releases) also send a crash report to the Sentry project of the
developers:

1. The game starts a second copy of `halo.exe` (`halo.exe --crash-report`).
   This copy writes a minidump of the game to `crashes\` next to
   `halo.exe`, and the game stops. A minidump contains the stacks and the
   registers of the threads, and the list of the modules. It does not
   contain the other memory of the game. The stack of a thread that runs
   guest code is its stack in the memory of the guest.
2. At the first crash, the copy asks the player whether to send crash
   reports. `crash_reports.upload` in `config.toml` keeps the answer:
   `"yes"` sends this report and all the reports after it, and `"no"` sends
   no reports and does not ask again. The answer `"no"` deletes the
   minidump.
3. The copy sends the minidump and a copy of `halo.log` to Sentry, then
   deletes them. If it cannot send them (for example, with no network
   connection), the game sends them when it starts the next time
   (`halo.exe --crash-upload`, in the background). `crashes\` keeps at
   most 8 reports.

Sentry finds the function names and the source lines of the minidump in
the PDBs (`halo.pdb`, `SDL3.pdb`). The `release` job of the workflow
uploads the PDBs and the executables of each build of main to Sentry. This
needs the `SENTRY_AUTH_TOKEN` secret of the repository: an organization
auth token from the settings of the Sentry organization (the token gives
the organization and its region). The `SENTRY_PROJECT` variable of the
repository can give the slug of the project; without it, the job uses the
id of the project in the DSN. For a personal token instead, also set the
`SENTRY_ORG` variable (the slug of the organization) and the `SENTRY_URL`
variable (`https://de.sentry.io`).

Each release also has the PDBs and the guest image as
`OpenH1-Windows-symbols.zip` (and `OpenH1-Windows-debug-symbols.zip`).
`tools/symbolize_crash.py` uses them to add the function names and the
source lines to the crash lines of a `halo.log`:

```
python tools/symbolize_crash.py halo.log <folder>
python tools/symbolize_crash.py halo.log OpenH1-Windows.zip OpenH1-Windows-symbols.zip
```

Give the folder of the build that crashed: `halo.exe`, `halo.pdb` and
`halo_guest.debug.elf` (the guest image with its source lines; a release's
before it was published has `halo_guest.elf`, which names the functions
only), or the release's two zips as they are downloaded, of the release
the report's Build line names (the releases keep the last five builds).
The tool needs `llvm-symbolizer` (LLVM).

OpenH1 has no Sentry project yet: no build sends crash reports. To test the
crash reports, set the `HALO_CRASH_REPORTS_ANY_BUILD` environment
variable.

The crash reports also include the crashes of `abort()` and of an invalid
argument to a function of the C runtime.

## Testing under Wine on Linux

`tools/wine_bench.sh` runs a Windows build under Wine on a Linux computer
with a GPU. It can start the game and play a level, draw it with the GPU's
driver, collect the logs and take shots, all without a Windows machine.
Any user can run it: everything it makes is in `~/openh1-wine`
(`OPENH1_BENCH`) of whoever runs it.

It needs these:

- Wine (with winevulkan, which current Wine has), and the GPU's Vulkan
  driver.
- gamescope for the hidden runs (or Xvfb, below).
- `unzip`, `curl` and `python3`.
- Game data: a `maps` folder. The bench uses `OPENH1_BENCH_MAPS`, or else
  `/srv/openh1/maps` where the computer has one, or else
  `~/halo-x86-maps/maps`, or else `~/openh1-wine/data/maps`.
- For CI artifacts (`fetch <PR>`, `fetch run:<id>`): `gh`, logged in once
  with `gh auth login`. Releases and zip URLs need no login.

```sh
tools/wine_bench.sh setup               # the Wine prefix and folders, in ~/openh1-wine (OPENH1_BENCH)
tools/wine_bench.sh fetch               # the latest release's OpenH1-Windows.zip
tools/wine_bench.sh fetch build-510     # a release by its tag (--debug: OpenH1-Windows-debug.zip)
tools/wine_bench.sh fetch 199           # a pull request's halo-windows-debug artifact (gh)
tools/wine_bench.sh fetch run:<run id>  # a workflow run's; or a zip's URL, a zip or a folder
tools/wine_bench.sh run                 # 30 s of bots on Blood Gulch, headless; then the frame rate
tools/wine_bench.sh run --map ratrace --bots 3 --seconds 60 HALO_INTERPOLATION=false
tools/wine_bench.sh run --visible       # in a window on the computer's own display, at the menus
tools/wine_bench.sh shot --every 600 --max 6   # a frame every 600, to the run's shots/
tools/wine_bench.sh logs                # the last run's debug.txt, halo.log, frame rate and Wine's errors
tools/wine_bench.sh doctor              # Wine, the GPU, OpenGL in the headless display, the builds
```

- The last build fetched is the current one, and `run --build <name>`
  runs another. The builds are in `~/openh1-wine/builds`.
- Each run gets its own folder, `~/openh1-wine/runs/<date>-<build>`.
  The folder is the run's data root: `maps/` is linked to the game data,
  and the folder also holds the run's `debug.txt`, its `wine.log` (the
  game's output and Wine's, timestamped), `environment.txt`, `halo.log`
  (from a release build) and `shots/`. `runs/last` is the newest run.
- A run without `--visible` uses a display of its own that nobody sees:
  gamescope's headless backend, which draws OpenGL and Vulkan with the
  GPU. It paces the frames at `--refresh` Hz (1000 by default), even with
  vsync off, and it crashes as it quits, after the game, which does no
  harm. With `OPENH1_BENCH_DISPLAY=xvfb`, the run uses an Xvfb of its own
  instead, where one is installed. Xvfb's OpenGL is Mesa's llvmpipe, drawn
  by the processor, so the 32-bit build's frame rate there says nothing of
  the GPU. Vulkan goes to the GPU's driver where the driver can present to
  Xvfb, and the run's summary names the device. The display gets its own
  settings folder, so it does not touch the desktop session.
  The run plays a local game with bots (`debug.network_test`
  `local:<map>:slayer`, `bots.count`) with vsync off, silent sound,
  system link on the loopback, and no internet play, updates, crash
  reports or clipboard. When `debug.exit_after` ends it, the bench prints
  the renderer, the frame rate while playing (from `debug.gpu_stats`'
  line every 60 frames) and Wine's errors.
- `VAR=value` gives a run a setting's variable (`HALO_*`, as in the
  settings table of [port/linux/README.md](../linux/README.md)) or one of
  Wine's (`WINEDEBUG=+wgl`), over the bench's own. The words after `--`
  go to `halo.exe` as its arguments.
- `shot` saves frames (`debug.screenshot_every`) and keeps at most
  `--max` of them, spread over the run. They show the game's content:
  keep them on the computer, out of the repository and out of public
  places, as with the maps.
- The prefix uses Wine's GLX (`UseEGL` = `N`). With Wine 11's EGL, its
  default on X11, SDL finds no pixel format for the window on NVIDIA's
  driver, and the game runs without a window.
- Only one run at a time: runs share the prefix, and each run stops the
  prefix's Wine processes when it ends.

Wine is not Windows. A run under Wine shows that the game starts, plays
and draws with a real GPU driver, and it gives the logs and shots to
compare with the Linux builds. It does not show these:

- The drivers on Windows. Wine passes OpenGL and Vulkan to the Linux
  driver, which differs from the same GPU's Windows driver in its
  behaviour, its bugs and its frame pacing. Measure frame rate and pacing
  on Windows.
- Windows itself: its memory layout (the DLLs, overlays and security
  programs in the low 4 GB), its exception dispatch and stack limits, and
  minidumps.
- Microsoft Defender, SmartScreen and the Mark of the Web, the unsigned
  executable, Scoop, and the updater replacing a real install.

Check these on a Windows computer before a release.

## Limits

- The startup movies play only with `display.movies` on (off by default),
  from the Xbox's own movie files in the data root's `bink` folder, as on
  the other builds.
- Windows 10 before version 1903 does not take UTF-8 as the code page of
  the process: there, a path to the data or the saved games with
  characters that are not in the code page of the system does not always
  operate.
