"""Ninja rules for the Windows build (``ninja windows``).

The game runs as the 64-bit x86 Linux build's does (tools/linux_guest_build.py,
port/linux/README.md "64-bit x86"): an x32 guest image (tools/guest_arch.py,
X32; tools/android_build.py, generate_guest_image) in a 64-bit host, here a
Windows executable (port/windows/host; port/windows/README.md):

- build/windows/halo_guest.elf, ``ninja windows_guest``: the guest image,
  the Linux builds' desktop guest (drawn with Vulkan, in the host) with the
  Windows code paths (HALO_WINDOWS: the saves in %APPDATA%\\halo, config.toml
  beside the executable, the Windows self-updater's release), no red zone
  (Windows writes an exception's records right below the stack pointer) and
  frame pointers everywhere (the host's function table walks the guest's
  frames with them, port/windows/host/host_memory_watch_win.c). It builds
  on any computer the guests build on (Linux, macOS, Windows), with the pinned
  LLVM (tools/guest_toolchain.py), so that the guest can be checked anywhere:
  on another computer than Windows when configure.py is given
  --windows-guest (it downloads the pinned LLVM, musl, SDL3's source and the
  Vulkan headers), or once those are there;
- build/windows/halo.exe, ``ninja windows``, on Windows, or on Linux with
  configure.py --windows-cross (tools/windows_cross_build.py, against the
  MSVC toolchain of --windows-vctools-dir and --windows-sdk-dir): the host,
  compiled by clang for x86_64-pc-windows-msvc and linked by lld with the
  Windows SDK, the guest image inside it; its import table's entries are
  thunks from the guest's calling convention (System V's) to the host's
  (Microsoft's), tools/android_imports.py --host-abi sysv;
- build/windows/SDL3.dll beside it, from SDL3's Visual C++ development
  package, which the host loads only once it has reserved the guest's memory
  (a delay-loaded DLL);
- build/windows/brokers.txt, internet play's brokers.
- build/windows/prepared.records, the stock maps' preparations' records.
- build/windows/openh1.assets, the textures', fonts' and menus' bytes.
- build/windows/halo_replay.exe, ``ninja windows_replay``: the command
  stream's replay (port/linux/vulkan/tools/halo_replay.c, with the replays'
  shared part, port/tools/replay, and its Windows functions,
  host_render_replay_win.c): the host's renderer and render stream, without
  the guest or the window.
"""

import hashlib
import json
import os
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path
from typing import Any, Dict, List

from .android_build import TOML_DIR, fetch_third_party, generate_guest_image
from .build_common import (MINIUPNPC_DIR, OPTIMISATION, OWN_CODE_ERRORS, REPLAY_DIR, compile_launcher,
                           configuration_defines, miniupnpc_sources, replay_build_label, updater_defines)
from .guest_arch import X32
from .guest_toolchain import LLVM_VERSION, GuestToolchain, guest_toolchain
from .ninja_syntax import Writer
from .vulkan_build import VULKAN_DIR, fetch_vulkan_headers, host_vulkan_renderer
from .windows_cross_build import cross_toolchain_prefix

BUILD = Path("build/windows")
THIRD_PARTY = BUILD / "third_party"
# SDL3's Visual C++ development package (headers, import library and DLL),
# as the guest's SDL3_TAG (tools/android_build.py)
SDL_VERSION = "3.4.16"
SDL_URL = (
    f"https://github.com/libsdl-org/SDL/releases/download/release-{SDL_VERSION}/"
    f"SDL3-devel-{SDL_VERSION}-VC.zip"
)
# (its SHA-256 when it was pinned: the DLL ships in the builds)
SDL_SHA256 = "1a784cb2a5c64d56fe7a62090fe9d242d9865f235e4ea9678f1a6ba4e693e7de"
SDL_DIR = THIRD_PARTY / f"SDL3-{SDL_VERSION}"
VULKAN_HEADERS_DIR = THIRD_PARTY / "vulkan-headers"
PORT_DIR = Path("port/windows/host")
WINDOWS_DIR = Path("port/windows")
ANDROID_DIR = Path("port/android")
LINUX_DIR = Path("port/linux")
LINUX_HOST_DIR = Path("port/linux/host")

# the guest's code paths beside the architecture's (generate_guest_image adds
# HALO_GUEST): the Linux Vulkan build's (tools/linux_guest_build.py: the
# renderer's command stream, Vulkan's window) and Windows's; and its code generation: no red zone, as Windows's exception
# dispatch writes its records right below the interrupted stack pointer (the
# write tracking's page faults happen in guest code), and frame pointers in
# musl too, for the host's walk of guest frames
WINDOWS_GUEST_DEFINES = ["-DHALO_WINDOWS=1", "-DHALO_VULKAN=1", "-mno-red-zone",
                         "-fno-omit-frame-pointer"]

# the host's files of port/android/host (the rest is port/windows/host's: the
# memory below 4 GB, threads on low stacks, Windows's system calls, the
# exception handlers, the entry point)
SHARED_HOST_SOURCES = ["host_audio_route.c", "host_common.c", "host_https.c", "host_loader.c", "host_memory_watch.c",
                       "host_render_record.c", "host_render_stream.c", "host_screenshot.c", "host_sdl.c", "host_stall.c",
                       "host_watch_hash.c"]
# the posix.h and update.h functions the guest's platform layer calls, on
# Windows (port/windows/src)
WIN32_HOST_SOURCES = ["win32_files.c", "win32_net.c", "win32_p2p.c", "win32_update.c", "win32_upnp.c"]
# the host's libraries (with SDL3.lib and delayimp.lib, for the delay-loaded
# SDL3.dll): Winsock and its helpers, the random number generator, timers,
# WaitOnAddress (synchronization), the window's and registry's functions,
# WinHTTP (the self-updater), the debugging helpers (crash reports)
HOST_LIBRARIES = ["ws2_32", "iphlpapi", "bcrypt", "winmm", "synchronization", "user32", "advapi32", "shell32",
                  "ole32", "winhttp", "dbghelp", "delayimp"]

HOST_FLAGS = [
    "--target=x86_64-pc-windows-msvc",
    OPTIMISATION,
    "-g",
    "-gcodeview",
    "-fno-omit-frame-pointer",
    # (the import thunks are System V functions, which may otherwise keep
    # data below the stack pointer, where Windows writes an exception's
    # records)
    "-mno-red-zone",
    # every x86-64 processor, as the guest
    "-march=x86-64",
    "-ffp-contract=off",
    "-std=gnu11",
    "-Wall",
    "-Wno-unused-function",
    *OWN_CODE_ERRORS,
    "-D_CRT_SECURE_NO_WARNINGS",
    "-D_CRT_NONSTDC_NO_WARNINGS",
    "-D_WINSOCK_DEPRECATED_NO_WARNINGS",
    "-DWIN32_LEAN_AND_MEAN",
    "-DNOMINMAX",
    "-D_WIN32_WINNT=0x0A00",
]


def windows_configure_inputs() -> List[Path]:
    return [Path(__file__), Path(__file__).with_name("windows_cross_build.py"), PORT_DIR, WINDOWS_DIR / "src"]


def windows_requested(sln: Any) -> bool:
    """whether to generate the build: on Windows, or with configure.py
    --windows-cross, always; elsewhere (the guest alone) with configure.py
    --windows-guest, or once its sources are there"""
    return (sys.platform == "win32" or windows_cross(sln) or bool(getattr(sln, "windows_guest", False))
            or (THIRD_PARTY / "SDL3").is_dir())


def windows_cross(sln: Any) -> bool:
    """whether the host is cross-compiled from Linux (configure.py --windows-cross)"""
    return sys.platform != "win32" and bool(getattr(sln, "windows_cross", False))


def fetch_sdl() -> None:
    """Downloads SDL3's Visual C++ development package (headers, import
    library and DLL) once."""
    if (SDL_DIR / "lib" / "x64" / "SDL3.lib").is_file():
        return
    THIRD_PARTY.mkdir(parents=True, exist_ok=True)
    archive = THIRD_PARTY / f"SDL3-devel-{SDL_VERSION}-VC.zip"
    print(f"Downloading {SDL_URL}")
    with urllib.request.urlopen(SDL_URL) as response, open(archive, "wb") as f:
        shutil.copyfileobj(response, f)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != SDL_SHA256:
        archive.unlink()
        raise SystemExit(f"{SDL_URL}: SHA-256 {digest}, not the pinned {SDL_SHA256}")
    with zipfile.ZipFile(archive) as z:
        z.extractall(THIRD_PARTY)
    archive.unlink()


def windows_rc(cc: str) -> str:
    """LLVM's resource compiler of the clang named cc: beside it, where cc is
    a path, else the one on the PATH"""
    path = Path(cc)
    if path.parent == Path("."):
        return "llvm-rc"
    return str(path.with_name("llvm-rc" + path.suffix))


def _quote(path: Any) -> str:
    text = str(path).replace(os.sep, "/")
    return f'"{text}"' if " " in text else text


def generate_windows_build(n: Writer, sln: Any) -> None:
    config_path = LINUX_DIR / "port.json"
    if not config_path.is_file() or not PORT_DIR.is_dir() or not windows_requested(sln):
        return
    try:
        toolchain = guest_toolchain(sln, None)
    except (RuntimeError, OSError) as error:
        print(f"Windows build disabled: no LLVM {LLVM_VERSION} for the guest ({error})", file=sys.stderr)
        return
    if not toolchain:
        # (--guest-llvm=system: the PATH's)
        tools = {name: shutil.which(name) for name in ("clang", "ld.lld", "llvm-ar", "llvm-objdump")}
        if not all(tools.values()):
            print("Windows build disabled: --guest-llvm=system needs clang, ld.lld, llvm-ar and "
                  "llvm-objdump on the PATH", file=sys.stderr)
            return
        toolchain = GuestToolchain(cc=tools["clang"], ld=tools["ld.lld"], ar=tools["llvm-ar"],
                                   objdump=tools["llvm-objdump"])
    try:
        fetch_third_party(THIRD_PARTY)
        fetch_vulkan_headers(VULKAN_HEADERS_DIR)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Windows build disabled: cannot fetch musl, SDL3, FFmpeg or the Vulkan headers ({error})",
              file=sys.stderr)
        return
    config: Dict[str, Any] = json.loads(config_path.read_text(encoding="utf-8"))

    n.comment("Windows build (ninja windows, ninja windows_guest); see port/windows/README.md")
    n.variable("windows_guest_cc", toolchain.cc)
    guest = generate_guest_image(
        n, sln, config, arch=X32, prefix="windows", label="WINDOWS", build=BUILD, third_party=THIRD_PARTY,
        guest_cc=toolchain.cc, ar=toolchain.ar, ld=toolchain.ld,
        objdump=toolchain.objdump, builtins_cc="", environment="gnu", platform_defines=WINDOWS_GUEST_DEFINES,
        extra_runtime=[LINUX_HOST_DIR / "guest_desktop.c", VULKAN_DIR / "guest_vulkan.c", PORT_DIR / "guest_windows.c"],
        extra_imports=[LINUX_HOST_DIR / "host_imports.list", VULKAN_DIR / "host_imports.list",
                       PORT_DIR / "host_imports.list"],
        extra_prototypes=[LINUX_HOST_DIR / "guest_desktop.c", VULKAN_DIR / "guest_vulkan.c",
                          PORT_DIR / "guest_windows.c"],
        updater_cflags=updater_defines(getattr(sln, "port_release", False)),
        host_abi="sysv")
    n.build(outputs="windows_guest", rule="phony", inputs=[guest["image"], guest["checked"]])
    n.newline()
    if sys.platform != "win32" and not windows_cross(sln):
        return
    try:
        fetch_sdl()
    except OSError as error:
        print(f"Windows build: the host disabled, cannot fetch SDL3 ({error})", file=sys.stderr)
        return
    _generate_host(n, sln, guest)


def _generate_host(n: Writer, sln: Any, guest: Dict[str, Path]) -> None:
    """the host executable, SDL3.dll and brokers.txt (on Windows, or cross-compiled)"""
    cc = getattr(sln, "windows_cc", None) or "clang"
    # (a cross build's MSVC and Windows SDK, as vcvars64.bat sets them)
    cross_prefix = cross_toolchain_prefix(sln) if windows_cross(sln) else ""
    # ninja quotes the response file's paths for the shell that runs the
    # rule: Windows quoting on Windows, POSIX quoting elsewhere
    rsp_quoting = "windows" if sys.platform == "win32" else "posix"
    host_obj_dir = BUILD / "host" / "obj"
    release = getattr(sln, "port_release", False)
    n.variable("windows_cc", cc)
    n.rule(
        name="windows_host_cc",
        command=f"{cross_prefix}{compile_launcher(sln)}$windows_cc -MMD -MF $out.d $cflags -c $in -o $out",
        description="WINDOWS HOST CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    # (the guest image's object, assembled without the compile cache: ccache
    # hashes guest_image_win.S, not the image its .incbin reads)
    n.rule(
        name="windows_host_cc_uncached",
        command=f"{cross_prefix}$windows_cc -MMD -MF $out.d $cflags -c $in -o $out",
        description="WINDOWS HOST CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    n.rule(
        name="windows_link",
        command=f"{cross_prefix}$windows_cc $ldflags -o $out --rsp-quoting={rsp_quoting} @$out.rsp $libs",
        description="WINDOWS LINK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    n.rule(
        name="windows_copy",
        command="$python tools/guest_build_steps.py copy $in $out",
        description="WINDOWS COPY $out",
    )
    n.variable("windows_rc_tool", windows_rc(cc))
    n.rule(
        name="windows_rc_tool",
        command="$windows_rc_tool /no-preprocess /FO $out $in",
        description="WINDOWS RC $out",
    )

    sdl_include = SDL_DIR / "include"
    base_cflags = [*HOST_FLAGS, *configuration_defines(sln),
                   *([] if release else ["-fstack-protector-strong"])]
    host_cflags = " ".join([
        *base_cflags,
        # (the POSIX names the shared host files use, over Windows's)
        f"-I{PORT_DIR / 'posix'}",
        f"-I{PORT_DIR}", f"-I{ANDROID_DIR / 'include'}", f"-I{ANDROID_DIR / 'host'}", f"-I{LINUX_HOST_DIR}",
        f"-I{LINUX_DIR / 'src'}", f"-I{TOML_DIR}", f"-I{_quote(sdl_include)}", f"-I{VULKAN_HEADERS_DIR}",
        "-DHALO_VULKAN=1",
    ])
    # (the files of port/windows/src talk to Windows alone; and win32_upnp.c's
    # miniupnpc)
    win32_cflags = " ".join([*base_cflags, f"-I{LINUX_DIR / 'src'}", f"-I{_quote(sdl_include)}"])
    miniupnpc_include = f"-I{MINIUPNPC_DIR / 'include'} -DMINIUPNP_STATICLIB"

    objects: List[Path] = []

    def add(source: Path, cflags: str, name: str = "") -> None:
        obj = host_obj_dir / (name or source.name + ".o")
        n.build(outputs=obj, rule="windows_host_cc", inputs=source, variables={"cflags": cflags})
        objects.append(obj)

    for source in sorted(PORT_DIR.glob("*.c")) + sorted(PORT_DIR.glob("*.S")):
        if source.name in ("guest_windows.c", "guest_image_win.S"):
            continue
        if source.name in ("host_crash_win.c", "host_memory_watch_win.c"):
            # (the build's number and configuration name its crash reports)
            add(source, f"{host_cflags} {updater_defines(release)}")
        else:
            add(source, host_cflags)
    for name in SHARED_HOST_SOURCES:
        add(ANDROID_DIR / "host" / name, host_cflags)
    add(LINUX_HOST_DIR / "host_desktop.c", host_cflags)
    add(TOML_DIR / "tomlc17.c", " ".join([*base_cflags, "-w"]))
    for name in WIN32_HOST_SOURCES:
        cflags = win32_cflags
        if name == "win32_upnp.c":
            cflags = f"{win32_cflags} {miniupnpc_include}"
        add(WINDOWS_DIR / "src" / name, cflags)
    # the MCC packages' previews and sharing (posix.h's posix_mcc_*), whose
    # files have Windows's paths of their own
    for name in ("posix_mcc_preview.c", "posix_mcc_share.c"):
        add(LINUX_DIR / "src" / name, win32_cflags)
    for source in miniupnpc_sources():
        add(source, " ".join([*base_cflags, miniupnpc_include, f"-I{MINIUPNPC_DIR / 'src'}", "-w"]),
            name=f"miniupnpc_{source.name}.o")
    # the renderer: Vulkan's (port/linux/vulkan), with its own texture
    # decoding, as Android's host
    objects.extend(host_vulkan_renderer(n, "windows", "WINDOWS", BUILD, "windows_host_cc", host_cflags,
                                        VULKAN_HEADERS_DIR, vr=False, texture_decoding=True))
    add(guest["host_table_c"], host_cflags)
    # the guest image, inside the executable (host_main_win.c)
    image_obj = host_obj_dir / "guest_image_win.S.o"
    n.build(outputs=image_obj, rule="windows_host_cc_uncached", inputs=PORT_DIR / "guest_image_win.S",
            implicit=[guest["image"], guest["checked"]],
            variables={"cflags": f'--target=x86_64-pc-windows-msvc -DGUEST_IMAGE=\\"{guest["image"].as_posix()}\\"'})
    objects.append(image_obj)
    resources = BUILD / "halo.res"
    # (the icon, and the manifest: UTF-8 as the process's code page)
    n.build(outputs=resources, rule="windows_rc_tool", inputs=PORT_DIR / "halo.rc",
            implicit=[WINDOWS_DIR / "openh1-icon.ico", PORT_DIR / "halo.manifest"])
    objects.append(resources)

    ldflags = [
        "--target=x86_64-pc-windows-msvc",
        "-fuse-ld=lld",
        "-g",
        # the guest's memory is reserved below 4 GB at start-up
        # (host_memory_win.c): the executable, its DLLs and their heaps go
        # above it (high-entropy ASLR), and SDL3.dll loads only once the
        # reservation is made
        "-Wl,/DYNAMICBASE",
        "-Wl,/HIGHENTROPYVA",
        "-Wl,/LARGEADDRESSAWARE",
        "-Wl,/DELAYLOAD:SDL3.dll",
        # (never CET's shadow stacks: the guest's threads move to stacks of
        # their own, port/windows/host/host_enter_win.S)
        "-Wl,/CETCOMPAT:NO",
        # (the manifest is the resources', halo.manifest)
        "-Wl,/MANIFEST:NO",
    ]
    if release:
        # (no console window: under Wine (Proton, gamescope) it can hide the
        # game's window; the log goes to halo.log, host_main_win.c)
        ldflags += ["-Wl,/SUBSYSTEM:WINDOWS", "-Wl,/ENTRY:mainCRTStartup"]
    else:
        ldflags += ["-Wl,/SUBSYSTEM:CONSOLE"]
    libs = " ".join([_quote(SDL_DIR / "lib" / "x64" / "SDL3.lib"), *(f"-l{lib}" for lib in HOST_LIBRARIES)])
    output = BUILD / "halo.exe"
    n.build(outputs=output, rule="windows_link", inputs=objects,
            variables={"ldflags": " ".join(ldflags), "libs": libs})
    sdl_dll = BUILD / "SDL3.dll"
    n.build(outputs=sdl_dll, rule="windows_copy", inputs=SDL_DIR / "lib" / "x64" / "SDL3.dll")
    brokers = BUILD / "brokers.txt"
    n.build(outputs=brokers, rule="windows_copy", inputs=Path("port/assets/network/brokers.txt"))
    # the master servers of dedicated servers, a file beside the game too
    # (network.masters_file, server.masters_file)
    masters = BUILD / "masters.txt"
    n.build(outputs=masters, rule="windows_copy", inputs=Path("port/assets/network/masters.txt"))
    # ... and the relays offered when a direct connection fails (network.relays_file)
    relays = BUILD / "relays.txt"
    n.build(outputs=relays, rule="windows_copy", inputs=Path("port/assets/network/relays.txt"))
    # ... and the stock maps' preparations' records (port/linux/src/prepared_shipped.h)
    records = BUILD / "prepared.records"
    n.build(outputs=records, rule="windows_copy", inputs=Path("port/assets/prepared/prepared.records"))
    # ... and the textures', fonts' and menus' bytes (port/linux/src/embedded_assets.h)
    assets = BUILD / "openh1.assets"
    n.build(outputs=assets, rule="windows_copy", inputs=guest["assets"])
    n.build(outputs="windows", rule="phony", inputs=[output, sdl_dll, brokers, masters, relays, records, assets])

    # the command stream's replay: the renderer and the render stream of the
    # host, with the POSIX functions they use, offscreen
    shared_dir = BUILD / "host" / "shared"
    replay_cflags = (f"-I{shared_dir} -I{VULKAN_DIR} -I{VULKAN_HEADERS_DIR} -I{REPLAY_DIR} {host_cflags} "
                     f"-DHALO_REPLAY_BUILD=\\\"{replay_build_label()}\\\"")
    replay_objects: List[Path] = []
    for source in (VULKAN_DIR / "tools" / "halo_replay.c", REPLAY_DIR / "host_render_replay.c",
                   REPLAY_DIR / "host_render_replay_win.c"):
        obj = host_obj_dir / "replay" / (source.name + ".o")
        n.build(outputs=obj, rule="windows_host_cc", inputs=source, order_only=[shared_dir / "platform.h"],
                variables={"cflags": replay_cflags})
        replay_objects.append(obj)
    replay_host = {"host_render_stream.c.o", "host_render_record.c.o", "host_screenshot.c.o", "host_posix_win.c.o"}
    replay_objects += [obj for obj in objects if obj.name in replay_host]
    replay_objects += [obj for obj in objects if obj.parent.name == "vulkan" and obj.name != "host_vulkan.c.o"]
    replay_ldflags = ["--target=x86_64-pc-windows-msvc", "-fuse-ld=lld", "-g", "-Wl,/DYNAMICBASE",
                      "-Wl,/HIGHENTROPYVA", "-Wl,/LARGEADDRESSAWARE", "-Wl,/SUBSYSTEM:CONSOLE"]
    replay = BUILD / "halo_replay.exe"
    n.build(outputs=replay, rule="windows_link", inputs=replay_objects,
            variables={"ldflags": " ".join(replay_ldflags), "libs": libs})
    n.build(outputs="windows_replay", rule="phony", inputs=[replay, sdl_dll])
    n.newline()
