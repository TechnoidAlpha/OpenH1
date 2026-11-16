"""Ninja rules for the Linux builds: on 64-bit x86 (``ninja linux_x86_64``)
and on 64-bit ARM (``ninja linux_arm64``), each drawn with Vulkan 1.3.

The game's data needs 32-bit pointers, and a 64-bit Linux computer runs
64-bit programs: recent 64-bit ARM processors have no 32-bit mode, and few
distributions still carry 32-bit libraries and drivers. So these builds run
the game as the Android port does (port/android/README.md, "How the port
operates"): the guest image is ILP32 code of the computer's architecture
(tools/guest_arch.py: x32, or arm64_32; tools/android_build.py,
generate_guest_image), with the Linux desktop's code paths instead of the
app's, and the host is a 64-bit Linux executable instead of an app's
library:

- build/<name>/halo: the host (port/android/host, with
  port/linux/host/host_main.c as its entry point), with the guest image
  inside it;
- build/<name>/libSDL3.so.0: SDL3, next to the executable, which finds
  it there (an rpath of $ORIGIN), as few distributions have SDL3 yet, and
  its license, SDL3-LICENSE.txt.

The renderer is Vulkan's, in the host (port/linux/vulkan), as the Android
app's: the guest writes its frames as the renderer's command stream
(port/linux/src/xgpu_stream.h). With configure.py --vr the
build has the VR mode (OpenXR, HALO_VR: port/linux/host/host_vr.c).

Each build is a LinuxGuest in LINUX_GUESTS, generated on a computer of its
architecture, whose default build it is. Its sources (musl, SDL3 and the
Vulkan headers, and the OpenXR headers for VR) are downloaded when
configure.py generates it, as the Android build's are; a build that cannot
be generated is a target that stops with the reason. Refer to "64-bit ARM"
and "64-bit x86" in port/linux/README.md.
"""

import hashlib
import io
import json
import platform
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from .android_build import (GUEST_ARCH_SCRIPT, GUEST_TOOLCHAIN_SCRIPT, SDL_ANDROID_MOUSE_PATCH,
                            SDL_TAG, SDL_WAYLAND_REFERENCE_PATCH, TOML_DIR, fetch_third_party, generate_guest_image, host_memory_sources)
from .build_common import (MBEDTLS_DIR, MINIUPNPC_DEFINES, MINIUPNPC_DIR, OWN_CODE_ERRORS, REPLAY_DIR,
                           SDL_RPATH_LDFLAGS, compile_launcher, download, miniupnpc_sources, replay_build_label,
                           sdl3_desktop_check, sdl3_rule, pgo_generate_defines, updater_defines)
from .guest_arch import ARM64_32, X32, GuestArch
from .guest_toolchain import LLVM_VERSION, guest_toolchain
from .ninja_syntax import Writer, escape
from .vulkan_build import VULKAN_DIR, fetch_vulkan_headers, host_vulkan_renderer

# the desktop host's own files (its entry point, SDL's desktop functions, VR)
PORT_DIR = Path("port/linux/host")
ANDROID_DIR = Path("port/android")
LINUX_DIR = Path("port/linux")
# The Khronos OpenXR headers, for the VR build's host (port/linux/host/host_vr.c),
# downloaded by the VR build when it is configured, as the OpenXR SDK has them
# (Apache-2.0 OR MIT). The host loads the OpenXR runtime itself, so the build
# needs no OpenXR package.
OPENXR_TAG = "release-1.1.63"
OPENXR_URLS = [
    "https://raw.githubusercontent.com/KhronosGroup/OpenXR-SDK/{tag}/include/openxr/{name}",
    "https://cdn.jsdelivr.net/gh/KhronosGroup/OpenXR-SDK@{tag}/include/openxr/{name}",
]
OPENXR_HEADERS = {
    "openxr.h": "17e8e334a7ad0c6933b31143330d4771010e393df5d5d9d08566eb15bb171a15",
    "openxr_platform.h": "f17e6d752e0065ede9f2a4c8932155acff96462306a2468a67a6ad77484641d8",
    "openxr_platform_defines.h": "a458ba5777415f2de518fdcdcc87fcf8651ba0468c9f69a101378672aad80946",
    "openxr_loader_negotiation.h": "ebcca0946b4b738ebee41943b821abe4df518b3a37766e0ce6883bcefce65124",
}

# (no Vulkan loader: the host loads it, through SDL, when the window is made;
# no OpenGL, EGL or OpenGL ES)
HOST_LIBRARIES = ["SDL3", "m", "dl", "pthread"]
# the guest's and the host's code paths: the desktop's (no HALO_ANDROID) and
# Vulkan's window, the renderer's backend in the host (the guest's frames are
# its command stream, port/linux/src/xgpu_stream.h)
GUEST_DEFINES = ["-DHALO_VULKAN=1"]
HOST_DEFINES = ["-DHALO_VULKAN=1"]


@dataclass(frozen=True)
class LinuxGuest:
    # the ninja target, the build's folder (build/<name>), the prefix of its
    # rules and the configure.py setting that is its own (<name>_cc)
    name: str
    # what its rules' descriptions start with
    label: str
    # what it is, for the build file's comments and messages
    title: str
    # the guest's architecture
    arch: GuestArch
    # the processors (platform.machine()) of the computers that build it
    machines: Tuple[str, ...]


LINUX_GUESTS = (
    LinuxGuest(name="linux_x86_64", label="LINUX X86_64", title="64-bit x86 Linux", arch=X32,
               machines=("x86_64", "amd64")),
    LinuxGuest(name="linux_arm64", label="LINUX ARM64", title="64-bit ARM Linux", arch=ARM64_32,
               machines=("aarch64", "arm64")),
)


def fetch_openxr_headers(openxr_dir: Path) -> None:
    """Download the OpenXR headers into openxr_dir/openxr (configure time,
    once; a header whose SHA-256 is wrong is downloaded again)."""
    include = openxr_dir / "openxr"
    include.mkdir(parents=True, exist_ok=True)
    for name, digest in OPENXR_HEADERS.items():
        header = include / name
        if header.is_file() and hashlib.sha256(header.read_bytes()).hexdigest() == digest:
            continue
        print(f"Downloading the OpenXR SDK's {name} ({OPENXR_TAG})")
        data = download([url.format(tag=OPENXR_TAG, name=name) for url in OPENXR_URLS], digest)
        header.with_suffix(".tmp").write_bytes(data)
        header.with_suffix(".tmp").replace(header)


def native_linux_guest() -> Optional[LinuxGuest]:
    """the Linux build of this computer, if there is one: its default build"""
    if not sys.platform.startswith("linux"):
        return None
    machine = platform.machine().lower()
    return next((target for target in LINUX_GUESTS if machine in target.machines), None)


def third_party_folder(target: LinuxGuest) -> Path:
    """where configure.py downloads the build's sources (musl, SDL3, the
    Vulkan and OpenXR headers): build/<name>/third_party"""
    return Path("build") / target.name / "third_party"


def linux_guest_configure_inputs() -> List[Path]:
    return [Path(__file__), GUEST_ARCH_SCRIPT, GUEST_TOOLCHAIN_SCRIPT, PORT_DIR, Path("tools/vulkan_build.py"), VULKAN_DIR]


def generate_linux_guest_builds(n: Writer, sln: Any) -> None:
    for target in LINUX_GUESTS:
        generate_linux_guest_build(n, sln, target)


def _unavailable(n: Writer, target: LinuxGuest, reason: str) -> None:
    """a target of the build's name that stops with the reason it cannot be
    built, rather than none (ninja would then say only that it is unknown);
    configure.py says it too"""
    message = f"{target.title} build ({target.name}) unavailable: {reason}"
    print(message, file=sys.stderr)
    n.comment(message)
    n.rule(
        name=f"{target.name}_unavailable",
        command=escape(f"echo {shlex.quote(message)} >&2 && exit 1"),
        description=f"{target.label} UNAVAILABLE",
    )
    n.build(outputs=target.name, rule=f"{target.name}_unavailable")
    n.newline()


def generate_linux_guest_build(n: Writer, sln: Any, target: LinuxGuest) -> None:
    config_path = LINUX_DIR / "port.json"
    if not config_path.is_file() or not PORT_DIR.is_dir():
        return
    native = native_linux_guest()
    if not native or native.machines != target.machines:
        n.comment(f"{target.title} build ({target.name}): only on a {target.title} computer")
        return
    # (written aside, so that a build that fails to generate halfway leaves
    # nothing of itself but its unavailable target)
    output = io.StringIO()
    try:
        _generate(Writer(output, n.width), sln, target, config_path, third_party_folder(target))
    except (subprocess.CalledProcessError, OSError, RuntimeError) as error:
        # (the reasons of _generate's own, and those of what it calls: a
        # compiler that cannot run, android_build.compiler_include)
        _unavailable(n, target, str(error))
        return
    n.output.write(output.getvalue())


def _generate(n: Writer, sln: Any, target: LinuxGuest, config_path: Path, third_party: Path) -> None:
    """writes the build to n; raises RuntimeError with the reason it cannot
    be built"""
    prefix, label = target.name, target.label
    build = Path("build") / target.name
    sdl_dir = third_party / "SDL3"
    openxr_dir = third_party / "openxr"
    vulkan_headers = third_party / "vulkan-headers"
    # the host's compiler: the build's own (--<name>-cc), or the system's; the
    # guest's: the build's own too, else the pinned LLVM's
    # (tools/guest_toolchain.py, configure.py --guest-llvm)
    explicit_cc = getattr(sln, f"{prefix}_cc", None)
    cc = explicit_cc or "clang"
    vr = bool(getattr(sln, "port_vr", False))
    try:
        fetch_third_party(third_party)
    except (subprocess.CalledProcessError, OSError, RuntimeError) as error:
        raise RuntimeError(f"cannot fetch musl, SDL3 or FFmpeg ({error})") from error
    try:
        fetch_vulkan_headers(vulkan_headers)
    except (RuntimeError, OSError) as error:
        raise RuntimeError(f"cannot fetch the Vulkan headers ({error})") from error
    if vr:
        try:
            fetch_openxr_headers(openxr_dir)
        except (RuntimeError, OSError) as error:
            raise RuntimeError(f"cannot fetch the OpenXR headers ({error})") from error
    try:
        toolchain = guest_toolchain(sln, explicit_cc)
    except (RuntimeError, OSError) as error:
        raise RuntimeError(f"no LLVM {LLVM_VERSION} for the guest ({error})") from error
    guest_cc = toolchain.cc if toolchain else cc
    config: Dict[str, Any] = json.loads(config_path.read_text(encoding="utf-8"))
    guest_defines = list(GUEST_DEFINES)
    host_defines = list(HOST_DEFINES)
    if vr:
        guest_defines.append("-DHALO_VR=1")
        host_defines.append("-DHALO_VR=1")
    # (configure.py --guest-base: the guest's memory at a base the host
    # chooses, tools/guest_base_register.py, instead of the low 4 GB; for an
    # architecture that has one, GuestArch.base_register_flags)
    base_register = bool(getattr(sln, "guest_base", False))
    if base_register:
        if target.arch.base_register_flags is None:
            raise RuntimeError(f"--guest-base: the {target.arch.name} guest has no base register")
        host_defines.append("-DHALO_GUEST_BASE=1")

    n.comment(f"{target.title} build (ninja {target.name}); see port/linux/README.md")
    n.variable(f"{prefix}_guest_cc", guest_cc)
    n.variable(f"{prefix}_cc", cc)

    guest = generate_guest_image(
        n, sln, config, arch=target.arch, prefix=prefix, label=label, build=build, third_party=third_party,
        guest_cc=guest_cc, ar=toolchain.ar if toolchain else "llvm-ar",
        ld=toolchain.ld if toolchain else "ld.lld", objdump=toolchain.objdump if toolchain else "llvm-objdump",
        builtins_cc=f"${prefix}_cc",
        environment="gnu", platform_defines=guest_defines,
        extra_runtime=[PORT_DIR / "guest_desktop.c", VULKAN_DIR / "guest_vulkan.c"],
        extra_imports=[PORT_DIR / "host_imports.list", *([PORT_DIR / "vr_imports.list"] if vr else []),
                       VULKAN_DIR / "host_imports.list"],
        extra_prototypes=[PORT_DIR / "guest_desktop.c", *([LINUX_DIR / "src" / "vr_host.h"] if vr else []),
                          VULKAN_DIR / "guest_vulkan.c"],
        updater_cflags=updater_defines(getattr(sln, "port_release", False)),
        base_register=base_register)

    # ---------- SDL3, built from the same source as the guest's headers

    sdl_build = build / "sdl3-build"
    libsdl = sdl_build / "libSDL3.so.0"
    sdl3_rule(n, sln, name=f"{prefix}_sdl3", description=f"{label} SDL3 {SDL_TAG}", source=sdl_dir,
              sdl_build=sdl_build, log_dir=build, cc=f"${prefix}_cc", check=sdl3_desktop_check(sdl_build))
    # (built again when a patch of it changes: fetch_third_party applies them
    # at configure)
    n.build(outputs=libsdl, rule=f"{prefix}_sdl3",
            implicit=[sdl_dir / "CMakeLists.txt", SDL_ANDROID_MOUSE_PATCH, SDL_WAYLAND_REFERENCE_PATCH])

    # ---------- the host

    host_obj_dir = build / "host" / "obj"
    host_cc = f"{prefix}_host_cc"
    n.rule(
        name=host_cc,
        command=f"{compile_launcher(sln)}${prefix}_cc -MMD -MF $out.d $cflags -c $in -o $out",
        description=f"{label} HOST CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    # (frame pointers: the sampler and the crash report walk the host's
    # frames too, host_arch.c)
    host_cflags = " ".join([
        "-O2", "-g", "-fno-omit-frame-pointer", "-fPIC", "-Wall", "-Wno-unused-function", *OWN_CODE_ERRORS,
        "-D_GNU_SOURCE",
        f"-I{ANDROID_DIR}/include", f"-I{ANDROID_DIR}/host", f"-I{sdl_dir}/include", f"-I{LINUX_DIR}/src",
        f"-I{TOML_DIR}", f"-I{openxr_dir}", f"-I{PORT_DIR}", f"-I{vulkan_headers}", *host_defines,
        *pgo_generate_defines(sln),
    ])
    # (the app's own: its entry point, and the touch controls through Java)
    android_only = {"host_main.c", "host_touch.c"}
    host_sources = [source for source in host_memory_sources(sorted((ANDROID_DIR / "host").glob("*.c")), base_register)
                    if source.name not in android_only]
    host_sources += [
        PORT_DIR / "host_main.c", PORT_DIR / "host_desktop.c",
        LINUX_DIR / "src" / "posix_files.c", LINUX_DIR / "src" / "posix_net.c",
        LINUX_DIR / "src" / "posix_trace_marker.c",
        # the MCC packages' previews and sharing (posix.h's posix_mcc_*)
        LINUX_DIR / "src" / "posix_mcc_preview.c", LINUX_DIR / "src" / "posix_mcc_share.c",
        # (where config.toml is, for the debugging settings host_main.c reads)
        LINUX_DIR / "src" / "install_layout.c",
        TOML_DIR / "tomlc17.c",
    ]
    if vr:
        # OpenXR (port/linux/src/vr_host.h), with the runtime loaded at run time
        host_sources.append(PORT_DIR / "host_vr.c")
    host_objects: List[Path] = []
    for source in host_sources:
        obj = host_obj_dir / (source.name + ".o")
        n.build(outputs=obj, rule=host_cc, inputs=source, variables={"cflags": host_cflags})
        host_objects.append(obj)
    miniupnpc_cflags = " ".join([host_cflags, f"-I{MINIUPNPC_DIR / 'include'}", f"-I{MINIUPNPC_DIR / 'src'}",
                                 *MINIUPNPC_DEFINES])
    for source in [LINUX_DIR / "src" / "posix_upnp.c", *miniupnpc_sources()]:
        obj = host_obj_dir / ("miniupnpc_" + source.name + ".o" if source.parent.parent == MINIUPNPC_DIR
                              else source.name + ".o")
        n.build(outputs=obj, rule=host_cc, inputs=source,
                variables={"cflags": miniupnpc_cflags + (" -w" if source.name != "posix_upnp.c" else "")})
        host_objects.append(obj)
    # the self-updater's download (posix_update.c), with
    # port/third_party/mbedtls: with Mbed TLS's own locks, as built from its
    # unchanged config by these defines, since the updater runs on threads of
    # its own, and two threads setting up its PSA crypto or drawing on its
    # random generator at once crashed it
    mbedtls_cflags = " ".join([host_cflags, f"-I{MBEDTLS_DIR / 'include'}",
                               "-DMBEDTLS_THREADING_C", "-DMBEDTLS_THREADING_PTHREAD"])
    n.build(outputs=host_obj_dir / "posix_update.c.o", rule=host_cc,
            inputs=LINUX_DIR / "src" / "posix_update.c", variables={"cflags": mbedtls_cflags})
    host_objects.append(host_obj_dir / "posix_update.c.o")
    for source in sorted((MBEDTLS_DIR / "library").glob("*.c")):
        obj = host_obj_dir / "mbedtls" / (source.name + ".o")
        n.build(outputs=obj, rule=host_cc, inputs=source,
                variables={"cflags": f"{mbedtls_cflags} -I{MBEDTLS_DIR / 'library'} -w"})
        host_objects.append(obj)
    # the renderer for the command stream, Vulkan's, with the Xbox's texture
    # decoding, as the Android app's (and VR's passes)
    host_objects += host_vulkan_renderer(n, prefix, label, build, host_cc, host_cflags, vulkan_headers, vr,
                                         texture_decoding=True)
    table_obj = host_obj_dir / "host_import_table.c.o"
    n.build(outputs=table_obj, rule=host_cc, inputs=guest["host_table_c"],
            variables={"cflags": host_cflags})
    host_objects.append(table_obj)
    # the guest image, inside the executable (host_main.c), assembled
    # without the compile cache: ccache hashes guest_image.S, not the image
    # its .incbin reads (macOS's releases shipped an old guest so:
    # tools/macos_build.py)
    n.rule(name=f"{host_cc}_uncached", command=f"${prefix}_cc -MMD -MF $out.d $cflags -c $in -o $out",
           description=f"{label} HOST CC $out", depfile="$out.d", deps="gcc")
    image_obj = host_obj_dir / "guest_image.S.o"
    n.build(outputs=image_obj, rule=f"{host_cc}_uncached", inputs=PORT_DIR / "guest_image.S",
            implicit=[guest["image"], guest["checked"]], variables={"cflags": f'-DGUEST_IMAGE=\\"{guest["image"]}\\"'})
    host_objects.append(image_obj)

    output = build / "halo"
    staged_sdl = build / "libSDL3.so.0"
    n.rule(
        name=f"{prefix}_host_link",
        # (a position-independent executable, as the guest needs the low 4 GB
        # of the address space: a fixed one, and the heap after it, would be
        # loaded there)
        command=(f"${prefix}_cc -pie -o $out $in -L{sdl_build} "
                 + " ".join(f"-l{lib}" for lib in HOST_LIBRARIES)
                 # (the SDL beside the executable, or in the release's lib/openh1)
                 + " " + " ".join(SDL_RPATH_LDFLAGS) + " -Wl,--no-undefined"
                 # (posix_trace_marker.c's, which the GPU driver's calls must reach)
                 + "".join(f" -Wl,--export-dynamic-symbol={name}" for name in ("open", "open64", "openat", "openat64"))),
        description=f"{label} LINK $out",
    )
    n.build(outputs=output, rule=f"{prefix}_host_link", inputs=host_objects, implicit=[libsdl])
    n.rule(name=f"{prefix}_copy", command="cp -L $in $out", description=f"{label} STAGE $out")
    n.build(outputs=staged_sdl, rule=f"{prefix}_copy", inputs=libsdl)
    # (SDL's license, which the release carries beside it)
    sdl_license = build / "SDL3-LICENSE.txt"
    n.build(outputs=sdl_license, rule=f"{prefix}_copy", inputs=sdl_dir / "LICENSE.txt")
    # internet play's MQTT brokers, a file beside the game (network.brokers_file)
    brokers = build / "brokers.txt"
    n.build(outputs=brokers, rule=f"{prefix}_copy", inputs=Path("port/assets/network/brokers.txt"))
    # ... and the master servers of dedicated servers (network.masters_file)
    masters = build / "masters.txt"
    n.build(outputs=masters, rule=f"{prefix}_copy", inputs=Path("port/assets/network/masters.txt"))
    # ... and the relays offered when a direct connection fails (network.relays_file)
    relays = build / "relays.txt"
    n.build(outputs=relays, rule=f"{prefix}_copy", inputs=Path("port/assets/network/relays.txt"))
    # ... and the stock maps' preparations' records (port/linux/src/prepared_shipped.h)
    records = build / "prepared.records"
    n.build(outputs=records, rule=f"{prefix}_copy", inputs=Path("port/assets/prepared/prepared.records"))
    # ... and the textures', fonts' and menus' bytes (port/linux/src/embedded_assets.h)
    assets = build / "openh1.assets"
    n.build(outputs=assets, rule=f"{prefix}_copy", inputs=guest["assets"])
    n.build(outputs=target.name, rule="phony", inputs=[output, staged_sdl, sdl_license, brokers, masters, relays,
                                                         records, assets])

    # ---------- the command stream's replay (port/linux/vulkan/tools/halo_replay.c), with the replays' shared
    # part (port/tools/replay): the render stream and its recording, and the Vulkan renderer, without the
    # window (host_vulkan.c) or the guest; not the VR build's (its renderer is OpenXR's host's too)
    if not vr:
        _generate_replay(n, target, build, host_obj_dir, host_cc, host_cflags, host_objects, sdl_build, libsdl,
                         staged_sdl)
    n.newline()


def _generate_replay(n: Writer, target: LinuxGuest, build: Path, host_obj_dir: Path, host_cc: str, host_cflags: str,
                     host_objects: List[Path], sdl_build: Path, libsdl: Path, staged_sdl: Path) -> None:
    """build/<name>/halo_replay (ninja <name>_replay, linux_replay)"""
    prefix, label = target.name, target.label
    replay_cflags = f"{host_cflags} -I{REPLAY_DIR} -DHALO_REPLAY_BUILD=\\\"{replay_build_label()}\\\""
    replay_objects: List[Path] = []
    for source in (VULKAN_DIR / "tools" / "halo_replay.c", REPLAY_DIR / "host_render_replay.c"):
        obj = host_obj_dir / "replay" / (source.name + ".o")
        n.build(outputs=obj, rule=host_cc, inputs=source, order_only=[build / "host" / "shared" / "platform.h"],
                variables={"cflags": f"-I{build / 'host' / 'shared'} -I{VULKAN_DIR} {replay_cflags}"})
        replay_objects.append(obj)
    replay_host = {"host_render_stream.c.o", "host_render_record.c.o", "host_screenshot.c.o"}
    replay_objects += [obj for obj in host_objects if obj.name in replay_host]
    replay_objects += [obj for obj in host_objects if obj.parent.name == "vulkan" and obj.name != "host_vulkan.c.o"]
    replay = build / "halo_replay"
    n.rule(name=f"{prefix}_replay_link",
           command=(f"${prefix}_cc -pie -o $out $in -L{sdl_build} " + " ".join(f"-l{lib}" for lib in HOST_LIBRARIES) + " "
                    + " ".join(SDL_RPATH_LDFLAGS) + " -Wl,--no-undefined"),
           description=f"{label} LINK $out")
    n.build(outputs=replay, rule=f"{prefix}_replay_link", inputs=replay_objects, implicit=[libsdl])
    n.build(outputs=f"{target.name}_replay", rule="phony", inputs=[replay, staged_sdl])
    n.build(outputs="linux_replay", rule="phony", inputs=[f"{target.name}_replay"])
