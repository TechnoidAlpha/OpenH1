"""Ninja rules for the Android build (``ninja android``).

The Android port (port/android/README.md) runs the game as ILP32 AArch64
code - 32-bit pointers, as the game's data formats require - inside an
ordinary 64-bit Android app. This graph builds

- the guest image, build/android/halo_guest.elf: the game sources, the
  platform layer shared with the Linux port (port/linux/src) and the guest
  runtime (port/android/guest) with a subset of musl as its C library, all
  compiled by clang for arm64_32-apple-watchos (ARM64_32 in
  tools/guest_arch.py), converted to ELF assembly
  (tools/android_asm_convert.py), assembled for AArch64 and linked at a fixed
  address below 4 GB;
- the host library, build/android/jniLibs/arm64-v8a/libmain.so, with the
  Android NDK: the loader and the services the guest calls, over SDL3;
- SDL3 itself, with the NDK's CMake toolchain file;

and stages both, with the SDL3 Java sources, for the Gradle project in
port/android/app, which ``ninja android_apk`` then assembles.
"""

import hashlib
import os
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path
from typing import Any, Dict, List, Optional

from .guest_arch import ARM64_32, GuestArch
from .guest_toolchain import LLVM_VERSION, guest_toolchain
from .build_common import (LINUX_PROFILE, MBEDTLS_DIR, MINIUPNPC_DEFINES, MINIUPNPC_DIR, MUSL_MATH_DIR, OWN_CODE_ERRORS,
                           PLATFORM_SEMANTICS_HEADER, QUIET_OWN_CODE_FLAGS, SEMANTICS_HEADER, STB_VORBIS_SOURCE,
                           XDK_INCLUDE, compile_launcher, configuration_defines, download, game_defines_and_includes,
                           FFMPEG_CALLER, ffmpeg_cflags, game_sources, miniupnpc_sources,
                           musl_math_sources, opus_cflags, opus_sources,
                           PROFILE_GENERATE_FLAGS, pgo_generate_defines, pgo_mode, pgo_profile, profile_use_flags, updater_defines,
                           xdk_headers)
from . import check_host_imports
from .ffmpeg_fetch import fetch_ffmpeg, ffmpeg_sources, ffmpeg_tree
from .embed_assets import assets_file, hud_assets_build, hud_configure_inputs, server_ui_build
from .linux_package import ROOT as REPO_ROOT, notices as release_notices
from .ninja_syntax import Writer
from .vulkan_build import VULKAN_DIR, fetch_vulkan_headers, host_vulkan_renderer

PORT_DIR = Path("port/android")
GUEST_ARCH_SCRIPT = Path("tools/guest_arch.py")
GUEST_TOOLCHAIN_SCRIPT = Path("tools/guest_toolchain.py")
LINUX_DIR = Path("port/linux")
BUILD = Path("build/android")
THIRD_PARTY = BUILD / "third_party"
# the TOML parser config.toml is read with (port/linux/src/port_config.c)
TOML_DIR = Path("port/third_party/tomlc17")
EXPAT_DIR = Path("port/third_party/expat")
EXPAT_SOURCES = ("xmlparse.c", "xmlrole.c", "xmltok.c")
KCP_DIR = Path("port/third_party/kcp")
MONOCYPHER_DIR = Path("port/third_party/monocypher")
# the port's zlib (port/third_party/zlib/zlib_prefixed.h): what inflates the
# maps, the menus' and the HUD's PNGs and the updates, data from anywhere,
# instead of the game's own 1.1.3 (its inflate only, its names prefixed z_)
ZLIB_DIR = Path("port/third_party/zlib")
ZLIB_SOURCES = ("adler32.c", "crc32.c", "inffast.c", "inflate.c", "inftrees.c", "uncompr.c", "zutil.c")
# (its names prefixed, and the one Z_PREFIX leaves, its error messages, which
# the game's zlib names the same)
ZLIB_DEFINES = ("-DZ_PREFIX", "-Dz_errmsg=z_port_errmsg")
MUSL_VERSION = "1.2.5"
MUSL_DIR = THIRD_PARTY / f"musl-{MUSL_VERSION}"
MUSL_URL = f"https://musl.libc.org/releases/musl-{MUSL_VERSION}.tar.gz"
# (the download's SHA-256, and the tag's commit, when they were pinned)
MUSL_SHA256 = "a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4"
# (as tools/windows_build.py's SDL_VERSION)
SDL_TAG = "release-3.4.16"
SDL_COMMIT = "fa2c02bb6e21974a89ea9824bc53c9932abe5f9c"
SDL_DIR = THIRD_PARTY / "SDL3"
SDL_URL = "https://github.com/libsdl-org/SDL.git"
SDL_ANDROID_MOUSE_PATCH = Path("port/android/patches/sdl-relative-mouse.patch")
# the Wayland compositor's reference white in cd/m² (its SDR brightness), as
# the window's property openh1.wayland.reference_luminance: SDL 3.4 keeps
# only the headroom over it, which gives the display's peak in no nits
# (port/linux/vulkan/host_vulkan.c)
SDL_WAYLAND_REFERENCE_PATCH = Path("port/android/patches/sdl-wayland-reference-luminance.patch")
SDL_WAYLAND_REFERENCE_FILE = "src/video/wayland/SDL_waylandcolor.c"
SDL_ANDROID_MOUSE_LISTENER = "android-project/app/src/main/java/org/libsdl/app/SDLControllerManager.java"
ANDROID_API = 28
# what Gradle builds the APK from beside what ninja stages: the app's Java,
# manifest and resources, and the Gradle files
APP_SOURCES = PORT_DIR / "app" / "src"
GRADLE_FILES = [PORT_DIR / "build.gradle", PORT_DIR / "settings.gradle", PORT_DIR / "gradle.properties",
                PORT_DIR / "gradle" / "wrapper" / "gradle-wrapper.properties", PORT_DIR / "app" / "build.gradle"]

# The guest's code generation on every architecture (tools/guest_arch.py has
# each one's target): no C library but the guest's (musl), no unwinding
# tables, and thread-local variables through the guest runtime
GUEST_FLAGS = [
    "-nostdinc",
    "-fshort-wchar",
    "-fno-stack-protector",
    "-fno-unwind-tables",
    "-fno-asynchronous-unwind-tables",
    "-femulated-tls",
    # no fused multiply-add: the game was written for x87/SSE arithmetic,
    # and its debug assertions (colours within 0..1, unit vectors) trip on
    # the different rounding of fused operations
    "-ffp-contract=off",
    "-O2",
    # the source lines of the code, for a crash's frames
    # (tools/symbolize_crash.py; tools/android_asm_convert.py keeps them
    # through the Darwin assembly), relative to the checkout, so that every
    # computer's build writes the same; the image the builds carry is
    # stripped of them (generate_guest_image)
    "-gline-tables-only",
    "-fdebug-compilation-dir=.",
]
# the Android app's code paths, with the anti-aliasing a phone's GPU can
# spare (d3d8_device.c); the Linux guests have the desktop's code paths
# instead (tools/linux_guest_build.py)
ANDROID_DEFINES = ["-DHALO_ANDROID=1"]

# the MSVC semantics the game's sources were written for, as the Windows
# build has them (tools/windows_build.py), minus what only x86 needs
GUEST_CODE_FLAGS = [
    "-fms-extensions",
    "-fcommon",
    "-fno-strict-aliasing",
    "-fwrapv",
    "-fno-delete-null-pointer-checks",
    "-fno-omit-frame-pointer",
    *(f"-fno-builtin-{name}" for name in (
        "wcslen", "wcsnlen", "wcschr", "wcsrchr", "wcscmp", "wcsncmp", "wcscpy",
        "wcsncpy", "wcscat", "wcsncat", "wmemchr", "wmemcmp", "wmemcpy",
        "wmemmove", "wmemset",
    )),
]

MUSL_DIRECTORIES = [
    "conf", "ctype", "dirent", "env", "errno", "exit", "fcntl", "internal",
    "locale", "malloc", "malloc/mallocng", "math", "mman", "multibyte",
    "prng", "sched", "select", "signal", "stat", "stdio", "stdlib", "string",
    "time", "unistd",
]
MUSL_FILES = [
    "thread/__lock.c", "thread/__wait.c", "thread/__timedwait.c",
    "thread/__syscall_cp.c", "thread/vmlock.c", "thread/pthread_self.c",
    "thread/pthread_equal.c", "thread/pthread_once.c",
    "thread/pthread_setcancelstate.c", "thread/pthread_testcancel.c",
    "thread/default_attr.c", "thread/lock_ptc.c", "misc/getauxval.c",
    "linux/sysinfo.c", "misc/basename.c", "misc/dirname.c",
    "misc/realpath.c", "misc/uname.c", "misc/ioctl.c", "misc/getrlimit.c",
    "misc/syscall.c", "network/htonl.c", "network/htons.c", "network/ntohl.c",
    "network/ntohs.c", "network/inet_addr.c", "network/inet_aton.c",
    "network/inet_ntoa.c", "network/inet_pton.c", "network/inet_ntop.c",
]
MUSL_THREAD_PREFIXES = (
    "pthread_attr_", "pthread_cond", "pthread_mutex", "pthread_rwlock",
    "pthread_spin", "sem_",
)
# replaced by the guest runtime (port/android/guest/runtime)
MUSL_EXCLUDE = {
    "env/__stack_chk.c", "env/__init_tls.c", "env/__libc_start_main.c",
    "env/__reset_tls.c", "malloc/oldmalloc", "thread/pthread_create.c",
    # unused, and its compiler barrier is an inline assembly statement
    "string/explicit_bzero.c",
    # eight bytes at a time (guest_string.c)
    "string/memcmp.c",
    # the same sort, faster (guest_qsort.c)
    "stdlib/qsort.c", "stdlib/qsort_nr.c",
}
# game files that call variadic functions without a prototype in scope, which
# only works under x86's calling convention (tools/android_abi_check.py)
VARIADIC_PROTOTYPE_FILES = {
    "source/ai/action_uncover.c", "source/ai/ai.c", "source/ai/ai_debug.c",
    "source/bungie_net/common/public_key_crypt.c", "source/camera/editor_flying_camera.c",
    "source/game/cheats.c", "source/game/game_engine.c", "source/game/players.c",
    "source/hs/hs.c", "source/interface/hud_nav_points.c",
    "source/networking/telnet_console.c", "source/rasterizer/xbox/rasterizer_xbox_errors.c",
    "source/render/render.c",
}

HOST_LIBRARIES = ["SDL3", "log", "android", "m", "dl"]


def _quote(path: Any) -> str:
    text = str(path).replace(os.sep, "/")
    return f'"{text}"' if " " in text else text


def _find_ndk() -> Optional[Path]:
    for variable in ("ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "ANDROID_NDK"):
        if os.environ.get(variable) and Path(os.environ[variable]).is_dir():
            return Path(os.environ[variable])
    for variable in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        sdk = os.environ.get(variable)
        if sdk and (Path(sdk) / "ndk").is_dir():
            versions = sorted((Path(sdk) / "ndk").iterdir())
            if versions:
                return versions[-1]
    for sdk in (Path.home() / "Android/Sdk", Path("/opt/android-sdk")):
        if (sdk / "ndk").is_dir():
            versions = sorted((sdk / "ndk").iterdir())
            if versions:
                return versions[-1]
    return None


def compiler_include(cc: str) -> str:
    """the folder of the compiler's own headers (clang's resource
    directory's), asked of it at configure time"""
    try:
        resource = subprocess.run([cc, "-print-resource-dir"], capture_output=True, text=True,
                                  check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as error:
        raise RuntimeError(f"{cc} -print-resource-dir failed ({error})") from error
    return _quote(Path(resource) / "include")


def guest_abi_flags(arch: GuestArch, platform_defines: List[str]) -> List[str]:
    """the guest's code generation and defines: the architecture's, every
    guest's (HALO_GUEST, and HALO_GUEST_MACHO where clang writes Mach-O,
    port/linux/include/halo_linux_prefix.h), and the platform's
    (ANDROID_DEFINES, or a desktop's)"""
    guest_defines = ["-DHALO_GUEST=1", *(["-DHALO_GUEST_MACHO=1"] if arch.darwin_assembly else [])]
    return [*arch.target_flags, *GUEST_FLAGS, *guest_defines, *arch.defines, *platform_defines]


def check_sdl_commit(directory: Path) -> None:
    """SDL3's clone refused unless its tag is still the pinned commit."""
    commit = subprocess.run(["git", "-C", str(directory), "rev-parse", "HEAD"], capture_output=True, text=True,
                            check=True).stdout.strip()
    if commit != SDL_COMMIT:
        shutil.rmtree(directory)
        raise SystemExit(f"SDL3 {SDL_TAG} is {commit}, not the pinned {SDL_COMMIT}")


def fetch_third_party(third_party: Path = THIRD_PARTY) -> None:
    """Download musl, SDL3 and FFmpeg (configure time, once). musl's archive
    is checked against MUSL_SHA256 before it is unpacked (FFmpeg's against
    FFMPEG_SHA256: tools/ffmpeg_fetch.py); a failed download raises
    RuntimeError, and a failed clone CalledProcessError."""
    musl_dir = third_party / f"musl-{MUSL_VERSION}"
    sdl_dir = third_party / "SDL3"
    third_party.mkdir(parents=True, exist_ok=True)
    if not musl_dir.is_dir():
        print(f"Downloading {MUSL_URL}")
        archive = third_party / f"musl-{MUSL_VERSION}.tar.gz"
        archive.write_bytes(download([MUSL_URL], MUSL_SHA256))
        # (unpacked beside it and then moved, so that an interrupted unpacking
        # leaves no folder that a later configure would take as complete)
        unpacked = third_party / f"musl-{MUSL_VERSION}.unpacked"
        shutil.rmtree(unpacked, ignore_errors=True)
        unpacked.mkdir()
        if os.name == "nt":
            # (Windows: the tar first on a Git Bash PATH takes a drive
            # letter's colon for a remote host; Python's reads it alike)
            with tarfile.open(archive, "r:gz") as tar:
                tar.extractall(unpacked)
        else:
            subprocess.run(["tar", "xzf", str(archive.resolve())], cwd=unpacked, check=True)
        (unpacked / musl_dir.name).rename(musl_dir)
        unpacked.rmdir()
        archive.unlink()
    if not sdl_dir.is_dir():
        print(f"Cloning SDL3 {SDL_TAG}")
        subprocess.run(["git", "clone", "-q", "--depth", "1", "--branch", SDL_TAG, SDL_URL, str(sdl_dir)],
                       check=True)
        check_sdl_commit(sdl_dir)
    # SDL 3.4.16's generic mouse listener drops captured relative motion and
    # button transitions unless they are forwarded from captured pointer events.
    # A tree patched by another version of the patch (an older checkout, or
    # CI's cached one) is put back as SDL has it before this one is applied.
    reverse = subprocess.run(
        ["git", "-C", str(sdl_dir), "apply", "--reverse", "--check", str(SDL_ANDROID_MOUSE_PATCH.resolve())],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    if reverse.returncode != 0:
        subprocess.run(["git", "-C", str(sdl_dir), "checkout", "--", SDL_ANDROID_MOUSE_LISTENER], check=True)
        subprocess.run(["git", "-C", str(sdl_dir), "apply", str(SDL_ANDROID_MOUSE_PATCH.resolve())], check=True)
    # (and the Wayland one, the same way)
    reverse = subprocess.run(
        ["git", "-C", str(sdl_dir), "apply", "--reverse", "--check", str(SDL_WAYLAND_REFERENCE_PATCH.resolve())],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    if reverse.returncode != 0:
        subprocess.run(["git", "-C", str(sdl_dir), "checkout", "--", SDL_WAYLAND_REFERENCE_FILE], check=True)
        subprocess.run(["git", "-C", str(sdl_dir), "apply", str(SDL_WAYLAND_REFERENCE_PATCH.resolve())], check=True)
    # the Bink movies' decoders (display.movies)
    fetch_ffmpeg(third_party)


def _musl_sources(musl_dir: Path = MUSL_DIR) -> List[Path]:
    src = musl_dir / "src"
    result = set()
    for directory in MUSL_DIRECTORIES:
        for path in (src / directory).glob("*.c"):
            result.add(path)
    for name in MUSL_FILES:
        result.add(src / name)
    for path in (src / "thread").glob("*.c"):
        if path.name.startswith(MUSL_THREAD_PREFIXES):
            result.add(path)
    sources = []
    for path in sorted(result):
        relative = path.relative_to(src).as_posix()
        if relative in MUSL_EXCLUDE or any(relative.startswith(e + "/") for e in MUSL_EXCLUDE):
            continue
        sources.append(path)
    return sources


def host_memory_sources(sources: List[Path], base_register: bool) -> List[Path]:
    """The host's sources (port/android/host) for its guest's memory: the
    fixed low ranges (host_memory.c), or with the base register its own
    memory manager and the entry into guest code."""
    if base_register:
        return [source for source in sources if source.name != "host_memory.c"] + [
            PORT_DIR / "host" / "host_enter.S"]
    return [source for source in sources if source.name != "host_memory_base.c"]


def app_sources() -> List[Path]:
    """the files under port/android/app/src, which the APK's rule lists"""
    return sorted(path for path in APP_SOURCES.rglob("*") if path.is_file())


def android_configure_inputs() -> List[Path]:
    # (app/src's folders, so that adding or removing a file there re-runs it)
    app_folders = [APP_SOURCES, *sorted(path for path in APP_SOURCES.rglob("*") if path.is_dir())]
    return [Path(__file__), GUEST_ARCH_SCRIPT, GUEST_TOOLCHAIN_SCRIPT, SDL_ANDROID_MOUSE_PATCH, SDL_WAYLAND_REFERENCE_PATCH, PORT_DIR / "guest" / "runtime", PORT_DIR / "host",
            LINUX_DIR / "src", Path("tools/vulkan_build.py"), VULKAN_DIR, *app_folders, *hud_configure_inputs()]


def generate_guest_image(n: Writer, sln: Any, config: Dict[str, Any], *, arch: GuestArch, prefix: str, label: str,
                         build: Path, third_party: Path, guest_cc: str, ar: str, ld: str,
                         objdump: str, builtins_cc: str, environment: str, platform_defines: List[str],
                         extra_runtime: Optional[List[Path]] = None, extra_imports: Optional[List[Path]] = None,
                         extra_prototypes: Optional[List[Path]] = None, updater_cflags: str = "",
                         base_register: bool = False, link_flags: str = "",
                         host_abi: Optional[str] = None) -> Dict[str, Path]:
    """The guest image (build/halo_guest.elf) and the host's import table, for
    the builds that run the game as ILP32 code of the architecture arch
    (tools/guest_arch.py): Android, and the desktop guest builds
    (tools/linux_guest_build.py). The caller sets ${prefix}_guest_cc to
    guest_cc; ar, ld and objdump are the archiver, the linker and the disassembler (with
    which tools/guest_image_check.py checks the image). builtins_cc is the compiler
    that names the runtime library the image links with (empty for none: the
    macOS guest's, as Apple's clang has no ELF one), and environment the
    toolchain's (android, gnu), which the assembler's target ends with;
    platform_defines are the platform's code paths (ANDROID_DEFINES, or a
    desktop's). extra_runtime are more guest runtime sources, and
    extra_imports more lists of the host functions they import;
    updater_cflags are the desktop self-updater's defines
    (port/linux/src/updater.c), for a guest that has it.
    base_register builds a guest whose memory lies
    at any base the host chooses (tools/guest_base_register.py) rather than
    in the low 4 GB, for an architecture that has one
    (GuestArch.base_register_flags); extra_prototypes are then the files that
    declare extra_imports' functions, whose pointer arguments the import
    stubs translate. link_flags are more of the image's linker options.
    host_abi "sysv"
    is for a host whose calling convention is not the guest's (the 64-bit
    Windows host, tools/windows_build.py): its table's entries are thunks
    (tools/android_imports.py --host-abi), from every import's prototype
    (extra_prototypes too). The steps that are shell commands elsewhere are
    tools/guest_build_steps.py's on Windows, which has no POSIX shell."""
    extra_runtime = extra_runtime or []
    extra_imports = extra_imports or []
    extra_prototypes = extra_prototypes or []
    asm_target = arch.assembler_target(environment)
    if base_register and arch.base_register_flags is None:
        raise RuntimeError(f"the {arch.name} guest has no base register (tools/guest_base_register.py)")
    musl_dir = third_party / f"musl-{MUSL_VERSION}"
    sdl_dir = third_party / "SDL3"
    guest_dir = build / "guest"
    obj_dir = guest_dir / "obj"
    gen_dir = guest_dir / "gen"
    libc_include = guest_dir / "libc_include"
    libc_internal = guest_dir / "libc_internal"
    libc_arch = PORT_DIR / "guest" / "libc" / "arch" / arch.name
    # what every guest architecture shares: the system call interface the
    # host translates (port/android/include/halo_guest_syscall.h)
    arch_generic = PORT_DIR / "guest" / "libc" / "arch" / "generic"
    semantics_header = SEMANTICS_HEADER
    platform_semantics_header = PLATFORM_SEMANTICS_HEADER
    prefix_header = LINUX_DIR / "include" / "halo_linux_prefix.h"
    image = build / "halo_guest.elf"
    python = "$python"
    base_flag = " --base-register" if base_register else ""
    # (with the base register, the architecture's registers for it kept from
    # the compiler, GuestArch.base_register_flags)
    base_register_flags = [*(arch.base_register_flags or ()), "-DHALO_GUEST_BASE=1"] if base_register else []
    # (the steps that are shell commands, on a computer without a POSIX shell)
    steps = f"{python} tools/guest_build_steps.py" if os.name == "nt" else None

    # ---------- generated headers and sources

    alltypes = libc_include / "bits" / "alltypes.h"
    syscall_h = libc_include / "bits" / "syscall.h"
    version_h = libc_internal / "version.h"
    n.rule(
        name=f"{prefix}_alltypes",
        command=f"{steps} alltypes $out $in" if steps else f"sed -f {musl_dir}/tools/mkalltypes.sed $in > $out",
        description=f"{label} MUSL $out",
    )
    n.build(outputs=alltypes, rule=f"{prefix}_alltypes",
            inputs=[libc_arch / "bits" / "alltypes.h.in", musl_dir / "include" / "alltypes.h.in"])
    n.rule(
        name=f"{prefix}_syscall_h",
        command=f"{steps} syscall-h $out $in" if steps else "cp $in $out && sed -n -e s/__NR_/SYS_/p < $in >> $out",
        description=f"{label} MUSL $out",
    )
    n.build(outputs=syscall_h, rule=f"{prefix}_syscall_h", inputs=arch_generic / "bits" / "syscall.h.in")
    n.rule(
        name=f"{prefix}_version_h",
        command=(f"{steps} version-h $out {MUSL_VERSION}" if steps else
                 f"echo '#define VERSION \"{MUSL_VERSION}\"' > $out"),
        description=f"{label} MUSL $out",
    )
    n.build(outputs=version_h, rule=f"{prefix}_version_h")

    guest_posix_c = gen_dir / "guest_posix.c"
    posix_imports = gen_dir / "posix_imports.list"
    n.rule(
        name=f"{prefix}_posix_stubs",
        command=(f"{python} tools/android_posix_stubs.py{base_flag} {LINUX_DIR}/src/posix.h {guest_posix_c} "
                 f"{posix_imports}"),
        description=f"{label} POSIX STUBS",
    )
    n.build(outputs=[guest_posix_c, posix_imports], rule=f"{prefix}_posix_stubs",
            implicit=[Path("tools/android_posix_stubs.py"), LINUX_DIR / "src" / "posix.h"])

    imports_s = gen_dir / "imports.s"
    host_table_c = build / "host" / "host_import_table.c"
    host_imports_list = PORT_DIR / "host_imports.list"
    # (with the base register, the stubs translate the pointer arguments
    # the prototypes name)
    prototypes = [PORT_DIR / "guest" / "runtime" / "guest_host.h", *extra_prototypes]
    if host_abi:
        # (every import's, posix.h's too: the thunks')
        prototypes.append(LINUX_DIR / "src" / "posix.h")
    prototype_flags = "".join(f" --prototypes {path}" for path in prototypes) if base_register or host_abi else ""
    abi_flag = f" --host-abi {host_abi}" if host_abi else ""
    n.rule(
        name=f"{prefix}_imports",
        command=(f"{python} tools/android_imports.py --arch {arch.name}{base_flag}{abi_flag}{prototype_flags} "
                 f"--host-table {host_table_c} {imports_s} $in"),
        description=f"{label} IMPORTS",
    )
    # every import's arguments must cross to the host as the guest left them
    # (tools/check_host_imports.py)
    imports_checked = gen_dir / "host_imports.checked"
    n.rule(
        name=f"{prefix}_import_check",
        command=f"{python} tools/check_host_imports.py --stamp $out",
        description=f"{label} CHECK IMPORTS",
    )
    n.build(outputs=imports_checked, rule=f"{prefix}_import_check",
            implicit=[Path("tools/check_host_imports.py"), *check_host_imports.inputs()])
    n.build(outputs=[imports_s, host_table_c], rule=f"{prefix}_imports",
            inputs=[host_imports_list, *extra_imports, posix_imports],
            implicit=[Path("tools/android_imports.py"), GUEST_ARCH_SCRIPT, imports_checked,
                      *(prototypes if base_register or host_abi else [])])

    generated_headers = [*xdk_headers(), alltypes, syscall_h, version_h,
                         semantics_header, platform_semantics_header]

    # ---------- guest compilation: C -> object, or for a Darwin target
    # C -> Darwin assembly -> ELF assembly -> object

    # (with configure.py --guest-asm-cache, the conversion and assembly
    # through tools/guest_asm_cache.py, which keeps their objects: ccache
    # caches only the compile to Darwin assembly)
    asm_cache = getattr(sln, "guest_asm_cache", None)
    if arch.darwin_assembly and asm_cache:
        guest_cc_command = (f"{compile_launcher(sln)}${prefix}_guest_cc -MMD -MF $out.d $cflags -S $in "
                            f"-o $out.darwin.s && {python} tools/guest_asm_cache.py {asm_cache}{base_flag} "
                            f"$out.darwin.s $out.s $out -- ${prefix}_guest_cc --target={asm_target} -c $out.s -o $out")
    elif arch.darwin_assembly:
        guest_cc_command = (f"{compile_launcher(sln)}${prefix}_guest_cc -MMD -MF $out.d $cflags -S $in "
                            f"-o $out.darwin.s && {python} tools/android_asm_convert.py{base_flag} $out.darwin.s $out.s && "
                            f"${prefix}_guest_cc --target={asm_target} -c $out.s -o $out")
    else:
        guest_cc_command = f"{compile_launcher(sln)}${prefix}_guest_cc -MMD -MF $out.d $cflags -c $in -o $out"
    n.rule(
        name=f"{prefix}_guest_cc",
        command=guest_cc_command,
        description=f"{label} CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    n.rule(
        name=f"{prefix}_guest_as",
        command=f"${prefix}_guest_cc --target={asm_target} -c $in -o $out",
        description=f"{label} AS $out",
    )

    libc_includes = [
        f"-isystem {libc_include}", f"-isystem {libc_arch}", f"-isystem {arch_generic}",
        f"-isystem {musl_dir}/arch/generic",
        f"-isystem {musl_dir}/include",
        *([f"-idirafter {compiler_include(guest_cc)}"] if arch.compiler_headers else []),
    ]
    guest_abi = " ".join(guest_abi_flags(arch, platform_defines) + base_register_flags + configuration_defines(sln))
    guest_code = " ".join(GUEST_CODE_FLAGS)
    tool_implicit = [*([Path("tools/android_asm_convert.py"), Path("tools/guest_branch_relax.py")]
                       if arch.darwin_assembly else []),
                     *([Path("tools/guest_asm_cache.py")] if arch.darwin_assembly and asm_cache else []),
                     *generated_headers]
    if base_register:
        tool_implicit.append(Path("tools/guest_base_register.py"))
    # profile-guided optimisation with the committed profile: the game and
    # platform code are every guest's, and functions that differ simply go
    # without; or, with --pgo=generate, instrumented to record it
    # (guest_profile.c writes it at exit)
    profile_generate = pgo_mode(sln) == "generate"
    profile = pgo_profile(sln, [LINUX_PROFILE], guest_cc)
    profile_flags = " ".join(PROFILE_GENERATE_FLAGS if profile_generate else profile_use_flags(profile))
    if profile:
        tool_implicit.append(profile)

    def guest_object(source: Path, cflags: str, subdir: str = "") -> Path:
        obj = obj_dir / subdir / Path(str(source).lstrip("/")).with_suffix(".o")
        if str(source).startswith(str(build)):
            obj = obj_dir / subdir / source.relative_to(build).with_suffix(".o")
        n.build(outputs=obj, rule=f"{prefix}_guest_cc", inputs=source, implicit=tool_implicit,
                variables={"cflags": cflags})
        return obj

    # musl
    musl_cflags = " ".join([
        guest_abi, "-std=c99", "-ffreestanding", "-fno-common", "-D_XOPEN_SOURCE=700", "-w",
        f"-I{libc_arch}", f"-I{arch_generic}", f"-I{musl_dir}/arch/generic", f"-I{libc_internal}",
        f"-I{PORT_DIR}/guest/libc/src_include", f"-I{musl_dir}/src/include",
        f"-I{musl_dir}/src/internal", f"-I{libc_include}", f"-I{musl_dir}/include",
    ])
    musl_objects = [guest_object(source, musl_cflags, "musl") for source in _musl_sources(musl_dir)]
    libguestc = guest_dir / "libguestc.a"
    n.rule(
        name=f"{prefix}_ar",
        command=f"{steps} ar {ar} $out $out.rsp" if steps else f"rm -f $out && {ar} rcs $out @$out.rsp",
        description=f"{label} AR $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    n.build(outputs=libguestc, rule=f"{prefix}_ar", inputs=musl_objects)

    # the game: without warnings, but for the mistakes it must not have
    # (build_common.QUIET_OWN_CODE_FLAGS); the Ogg Vorbis decoder, built as
    # its units are, without any
    def game_unit_cflags(warnings: List[str]) -> str:
        return " ".join([
            guest_abi, guest_code, "-std=gnu89", "-D__STRICT_ANSI__", *warnings, profile_flags,
            f"-include {prefix_header}", f"-include {semantics_header}",
            f"-I{LINUX_DIR}/include",
            # the headers of the port's own game units (port/linux/game), for
            # the game sources that call them
            f"-iquote {Path(config['game_sources'])}",
            game_defines_and_includes(config), *libc_includes, f"-idirafter {XDK_INCLUDE}",
        ])

    objects: List[Path] = []
    game_cflags = game_unit_cflags(QUIET_OWN_CODE_FLAGS)
    for source in game_sources(config):
        cflags = game_cflags
        if source.as_posix() == "source/main/main.c":
            cflags += " " + updater_defines(getattr(sln, "port_release", False))
        if source.as_posix() in VARIADIC_PROTOTYPE_FILES:
            cflags += f" -include {PORT_DIR}/include/halo_android_variadic_prototypes.h"
        objects.append(guest_object(source, cflags))
    for source in sorted(Path(config["game_sources"]).glob("*.c")):
        objects.append(guest_object(source, game_cflags))
    objects.append(guest_object(STB_VORBIS_SOURCE, game_unit_cflags(["-w"])))

    # the platform layer shared with Linux, and the guest runtime: as the
    # game, without warnings but OWN_CODE_ERRORS; the vendored libraries
    # built with it, without any
    def platform_unit_cflags(warnings: List[str]) -> str:
        return " ".join([
            guest_abi, guest_code, "-std=gnu11", "-D_GNU_SOURCE", "-DHALO_LINUX_PLATFORM_LAYER", *warnings,
            profile_flags, f"-include {prefix_header}", f"-include {platform_semantics_header}",
            f"-I{LINUX_DIR}/src", f"-I{LINUX_DIR}/include", f"-I{PORT_DIR}/guest/runtime",
            f"-I{PORT_DIR}/include", f"-I{TOML_DIR}", f"-I{EXPAT_DIR}", f"-I{KCP_DIR}", f"-I{MONOCYPHER_DIR}",
            f"-I{ZLIB_DIR}", "-Isource -Isource/cseries",
            f"-I{sdl_dir}/include", *libc_includes, f"-idirafter {XDK_INCLUDE}",
        ])

    platform_cflags = platform_unit_cflags(QUIET_OWN_CODE_FLAGS)
    third_party_cflags = platform_unit_cflags(["-w"])
    guest_host_only = {"memory_watch.c"}  # replaced by guest_memory_watch.c
    # (the posix_*.c are the host's, which the guest calls through
    # guest_posix.c; but a dedicated server's signals, standard input and
    # files are the guest's own calls: posix_server.c)
    guest_posix = {"posix_server.c"}
    for source in sorted((LINUX_DIR / "src").glob("*.c")):
        if (source.name.startswith("posix_") and source.name not in guest_posix) or source.name in guest_host_only:
            continue
        # (with FFmpeg's headers, below)
        if source.name == FFMPEG_CALLER:
            continue
        # (the build's number: the updater's, and a dedicated server's
        # --version and status)
        if source.name in ("updater.c", "server_mode.c") and updater_cflags:
            objects.append(guest_object(source, f"{platform_cflags} {updater_cflags}"))
        else:
            objects.append(guest_object(source, platform_cflags))
    # the high-res HUD's textures (port/assets/hud; port/linux/src/hud_hires.c)
    for source in hud_assets_build(n, prefix, gen_dir / "hud_hires_assets.c"):
        objects.append(guest_object(source, platform_cflags))
    # a dedicated server's admin page (port/assets/server_ui;
    # port/linux/src/server_http.c)
    for source in server_ui_build(n, prefix, gen_dir / "server_ui_assets.c"):
        objects.append(guest_object(source, platform_cflags))
    # the settings file's parser (port/third_party/tomlc17)
    objects.append(guest_object(TOML_DIR / "tomlc17.c", third_party_cflags))
    # the menus' XML parser (port/third_party/expat; menu_files.c)
    for name in EXPAT_SOURCES:
        objects.append(guest_object(EXPAT_DIR / name, third_party_cflags))
    # internet play's reliable streams (port/third_party/kcp; p2p.c)
    objects.append(guest_object(KCP_DIR / "ikcp.c", third_party_cflags))
    # voice chat's codec (port/third_party/opus), with the guest's ABI and C
    # library
    for source in opus_sources():
        objects.append(guest_object(source, " ".join([opus_cflags(guest_abi), *libc_includes])))
    # the Bink movies' decoders (FFmpeg's, fetched: tools/ffmpeg_fetch.py),
    # and the platform layer's file that calls them, with the guest's ABI and
    # C library
    ffmpeg_dir = ffmpeg_tree(third_party)
    for source in ffmpeg_sources(ffmpeg_dir):
        objects.append(guest_object(source, " ".join([ffmpeg_cflags(guest_abi, ffmpeg_dir), *libc_includes])))
    objects.append(guest_object(LINUX_DIR / "src" / FFMPEG_CALLER, " ".join([
        ffmpeg_cflags(guest_abi, ffmpeg_dir, QUIET_OWN_CODE_FLAGS), profile_flags, *libc_includes])))
    # internet play's signatures, for public games' listings
    # (port/third_party/monocypher; p2p_crypto.c)
    for name in ("monocypher.c", "monocypher-ed25519.c"):
        objects.append(guest_object(MONOCYPHER_DIR / name, third_party_cflags))
    # the port's zlib
    for name in ZLIB_SOURCES:
        # (not the processor's features that the guest's assembly step is
        # not told it may use: AArch64's CRC32 instructions)
        objects.append(guest_object(ZLIB_DIR / name, " ".join([third_party_cflags, *ZLIB_DEFINES, *arch.zlib_flags])))
    # the game's sin, pow and the rest, the same on every port
    # (port/include/halo_math.h)
    musl_math_cflags = " ".join([
        guest_abi, "-std=gnu11", "-w", profile_flags, *libc_includes, f"-I{MUSL_MATH_DIR}/include",
        f"-include {MUSL_MATH_DIR}/include/libm.h",
    ])
    for source in musl_math_sources():
        objects.append(guest_object(source, musl_math_cflags))
    runtime_internal_cflags = " ".join([
        guest_abi, "-std=c99", "-ffreestanding", "-fno-common", "-D_XOPEN_SOURCE=700", "-D_GNU_SOURCE",
        *OWN_CODE_ERRORS,
        f"-I{PORT_DIR}/guest/runtime", f"-I{PORT_DIR}/include",
        f"-I{libc_arch}", f"-I{arch_generic}", f"-I{musl_dir}/arch/generic", f"-I{libc_internal}",
        f"-I{PORT_DIR}/guest/libc/src_include", f"-I{musl_dir}/src/include",
        f"-I{musl_dir}/src/internal", f"-I{libc_include}", f"-I{musl_dir}/include",
    ])
    runtime_cflags = " ".join([
        guest_abi, guest_code, "-std=gnu11", "-D_GNU_SOURCE", *OWN_CODE_ERRORS,
        f"-I{PORT_DIR}/guest/runtime", f"-I{PORT_DIR}/include", f"-I{LINUX_DIR}/src",
        f"-I{sdl_dir}/include", *libc_includes,
    ])
    runtime_dir = PORT_DIR / "guest" / "runtime"
    for source in sorted(runtime_dir.glob("*.c")):
        if source.name == "guest_profile.c" and not profile_generate:
            continue
        if source.name in ("guest_thread.c", "guest_start.c"):
            objects.append(guest_object(source, runtime_internal_cflags))
        elif source.name == "guest_memory_watch.c":
            objects.append(guest_object(source, platform_cflags))
        else:
            objects.append(guest_object(source, runtime_cflags))
    for source in extra_runtime:
        objects.append(guest_object(source, runtime_cflags))
    objects.append(guest_object(guest_posix_c, runtime_cflags))
    imports_o = obj_dir / "gen" / "imports.o"
    n.build(outputs=imports_o, rule=f"{prefix}_guest_as", inputs=imports_s)
    objects.append(imports_o)

    # ---------- the guest image

    linker_script = PORT_DIR / "guest" / "guest.ld"
    # (the folder of the script's guest_init_array.ld: which constructors the
    # image takes, guest.ld)
    linker_fragments = PORT_DIR / "guest" / "ld" / ("macho" if arch.darwin_assembly else "elf")
    n.rule(
        name=f"{prefix}_guest_link",
        command=(f"{ld} -m {arch.ld_emulation} -static -nostdlib -T {linker_script} -L {linker_fragments} "
                 f"--defsym=__guest_image_base={arch.image_base:#x} {link_flags}{' ' if link_flags else ''}"
                 f"$strip -Map $out.map -o $out @$out.rsp {libguestc}"
                 + (f" {arch.builtins(builtins_cc)}" if builtins_cc and arch.builtins(builtins_cc) else "")),
        description=f"{label} LINK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    # the image the builds carry, and the same image with its source lines
    # (GUEST_FLAGS' line tables), which they need not carry: the crash
    # reports' frames are found in it (tools/symbolize_crash.py)
    debug_image = build / "halo_guest.debug.elf"
    for output, strip in ((image, "--strip-debug"), (debug_image, "")):
        n.build(outputs=output, rule=f"{prefix}_guest_link", inputs=objects,
                implicit=[libguestc, linker_script, linker_fragments / "guest_init_array.ld"],
                variables={"strip": strip})
    # what the host cannot give the image: thread-local storage, the thread
    # pointer, an address outside its range (tools/guest_image_check.py);
    # what uses the image waits for this
    checked = guest_dir / "halo_guest.checked"
    n.rule(
        name=f"{prefix}_guest_check",
        command=(f"{python} tools/guest_image_check.py --arch {arch.name} --objdump {objdump} "
                 f"{'--pgo-generate ' if profile_generate else ''}--map $in.map $in $out"),
        description=f"{label} CHECK $in",
    )
    # (and the image with its lines made with it, wherever the image is)
    n.build(outputs=checked, rule=f"{prefix}_guest_check", inputs=image,
            implicit=[Path("tools/guest_image_check.py"), GUEST_ARCH_SCRIPT, debug_image])

    # (and the assets' file the game reads beside it: embedded_assets.h)
    return {"image": image, "debug_image": debug_image, "checked": checked, "host_table_c": host_table_c,
            "assets": assets_file(gen_dir / "hud_hires_assets.c")}


def generate_android_build(n: Writer, sln: Any) -> None:
    config_path = LINUX_DIR / "port.json"
    if not config_path.is_file() or not (PORT_DIR / "host").is_dir():
        return
    ndk = Path(sln.android_ndk) if getattr(sln, "android_ndk", None) else _find_ndk()
    if not ndk or not ndk.is_dir():
        n.comment("Android build: no NDK found (set ANDROID_NDK_HOME or pass --android-ndk)")
        return
    # (the renderer is Vulkan's, in the host: port/linux/vulkan; a phone needs
    # a Vulkan 1.3 driver, which the app's manifest asks for)
    vulkan_headers = THIRD_PARTY / "vulkan-headers"
    try:
        fetch_third_party()
        fetch_vulkan_headers(vulkan_headers)
    except (subprocess.CalledProcessError, OSError, RuntimeError) as error:
        print(f"Android build disabled: cannot fetch musl, SDL3, FFmpeg or the Vulkan headers ({error})", file=sys.stderr)
        return
    import json
    config: Dict[str, Any] = json.loads(config_path.read_text(encoding="utf-8"))

    prebuilt = ndk / "toolchains" / "llvm" / "prebuilt"
    _host_tag = os.environ.get("ANDROID_NDK_HOST_TAG", "")
    if not _host_tag:
        if sys.platform == "darwin":
            _host_tag = "darwin-x86_64"
        elif os.name == "nt":
            _host_tag = "windows-x86_64"
        else:
            _host_tag = "linux-x86_64"
    if not (prebuilt / _host_tag).is_dir():
        # an NDK that names its host folder differently: take the one there is
        _tags = sorted(entry.name for entry in prebuilt.iterdir() if entry.is_dir()) if prebuilt.is_dir() else []
        if not _tags:
            n.comment("Android build: the NDK has no LLVM toolchain")
            return
        _host_tag = _tags[0]
    toolchain = prebuilt / _host_tag
    host_cc = toolchain / "bin" / f"aarch64-linux-android{ANDROID_API}-clang"
    ndk_bin = toolchain / "bin"
    # the guest's compiler and tools: --android-guest-cc, else the pinned
    # LLVM's (tools/guest_toolchain.py, configure.py --guest-llvm); with
    # --android-guest-cc or --guest-llvm=system, the NDK's tools
    explicit_cc = getattr(sln, "android_guest_cc", None)
    try:
        toolchain = guest_toolchain(sln, explicit_cc)
    except (RuntimeError, OSError) as error:
        print(f"Android build disabled: no LLVM {LLVM_VERSION} for the guest ({error})", file=sys.stderr)
        return
    guest_cc = toolchain.cc if toolchain else explicit_cc or "clang"

    n.comment("Android build (ninja android); see port/android/README.md")
    n.variable("android_guest_cc", guest_cc)
    n.variable("android_host_cc", str(host_cc))
    # (and its C++, astc-encoder's: port/linux/astc)
    n.variable("android_host_cxx", str(host_cc) + "++")
    n.variable("android_ndk_bin", str(ndk_bin))

    # (configure.py --guest-base: the guest's memory at a base the host
    # chooses, tools/guest_base_register.py, instead of the fixed low ranges)
    base_register = getattr(sln, "guest_base", False)
    # (the renderer's backend is the host's, as the Linux guest builds have
    # it, Vulkan's)
    guest = generate_guest_image(
        n, sln, config, arch=ARM64_32, prefix="android", label="ANDROID", build=BUILD, third_party=THIRD_PARTY,
        guest_cc=guest_cc, ar=toolchain.ar if toolchain else "$android_ndk_bin/llvm-ar",
        ld=toolchain.ld if toolchain else "$android_ndk_bin/ld.lld",
        objdump=toolchain.objdump if toolchain else "$android_ndk_bin/llvm-objdump", builtins_cc="$android_host_cc",
        environment="android", platform_defines=ANDROID_DEFINES + ["-DHALO_VULKAN=1"],
        extra_imports=[PORT_DIR / "android_imports.list", VULKAN_DIR / "host_imports.list"],
        extra_runtime=[VULKAN_DIR / "guest_vulkan.c"],
        extra_prototypes=[VULKAN_DIR / "guest_vulkan.c"], base_register=base_register)
    image = guest["image"]
    host_table_c = guest["host_table_c"]
    sdl_build = BUILD / "sdl3-build"
    libsdl = sdl_build / "libSDL3.so"
    jni_dir = BUILD / "jniLibs" / "arm64-v8a"
    libmain = jni_dir / "libmain.so"
    assets_dir = BUILD / "assets"

    # ---------- SDL3

    # (through the --compiler-launcher too, as the Linux builds' SDL:
    # build_common.sdl3_rule)
    launcher = getattr(sln, "compiler_launcher", None)
    n.rule(
        name="android_sdl3",
        command=(f"cmake -S {SDL_DIR} -B {sdl_build} -G Ninja "
                 + (f"-DCMAKE_C_COMPILER_LAUNCHER={launcher} " if launcher else "") +
                 f"-DCMAKE_TOOLCHAIN_FILE={ndk}/build/cmake/android.toolchain.cmake "
                 f"-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-{ANDROID_API} -DCMAKE_BUILD_TYPE=Release "
                 f"-DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF "
                 # 16 KB pages (Android 15 and later), as the host library
                 f"-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384 "
                 f"> {BUILD}/sdl3-configure.log && ninja -C {sdl_build} > {BUILD}/sdl3-build.log"),
        description="ANDROID SDL3",
        pool="console",
    )
    n.build(outputs=libsdl, rule="android_sdl3", implicit=[SDL_DIR / "CMakeLists.txt"])

    # ---------- the host library

    host_objects: List[Path] = []
    host_obj_dir = BUILD / "host" / "obj"
    n.rule(
        name="android_host_cc",
        command="$android_host_cc -MMD -MF $out.d $cflags -c $in -o $out",
        description="ANDROID HOST CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    host_cflags = " ".join([
        "-O2", "-g", "-fPIC", "-Wall", "-Wno-unused-function", *OWN_CODE_ERRORS, "-D_GNU_SOURCE",
        f"-I{PORT_DIR}/include", f"-I{PORT_DIR}/host", f"-I{SDL_DIR}/include", f"-I{LINUX_DIR}/src",
        f"-I{TOML_DIR}", "-DHALO_VULKAN=1", *(["-DHALO_GUEST_BASE=1"] if base_register else []), *pgo_generate_defines(sln),
    ])
    host_sources = host_memory_sources(sorted((PORT_DIR / "host").glob("*.c")), base_register) + [
        LINUX_DIR / "src" / "posix_files.c", LINUX_DIR / "src" / "posix_net.c",
        LINUX_DIR / "src" / "posix_mcc_preview.c",
        LINUX_DIR / "src" / "posix_mcc_share.c",
        # the app reads debug.sample_seconds from config.toml (host_main.c)
        TOML_DIR / "tomlc17.c",
    ]
    for source in host_sources:
        obj = host_obj_dir / (source.name + ".o")
        n.build(outputs=obj, rule="android_host_cc", inputs=source, variables={"cflags": host_cflags})
        host_objects.append(obj)
    # internet play's UPnP (posix_upnp.c, with port/third_party/miniupnpc),
    # as the other posix_*.c in the host
    miniupnpc_cflags = " ".join([host_cflags, f"-I{MINIUPNPC_DIR / 'include'}", f"-I{MINIUPNPC_DIR / 'src'}",
                                 *MINIUPNPC_DEFINES])
    for source in [LINUX_DIR / "src" / "posix_upnp.c", *miniupnpc_sources()]:
        obj = host_obj_dir / ("miniupnpc_" + source.name + ".o" if source.parent.parent == MINIUPNPC_DIR
                              else source.name + ".o")
        n.build(outputs=obj, rule="android_host_cc", inputs=source,
                variables={"cflags": miniupnpc_cflags + (" -w" if source.name != "posix_upnp.c" else "")})
        host_objects.append(obj)
    # the renderer for the command stream, Vulkan's, with the Xbox's texture
    # decoding, and the DXT textures as ASTC on the GPUs without BC (most
    # phones': port/linux/astc), with astc-encoder, the host's C++
    n.rule(
        name="android_host_cxx",
        command="$android_host_cxx -MMD -MF $out.d $cflags -c $in -o $out",
        description="ANDROID HOST CXX $out",
        depfile="$out.d",
        deps="gcc",
    )
    host_objects += host_vulkan_renderer(n, "android", "ANDROID", BUILD, "android_host_cc",
                                         host_cflags, vulkan_headers, texture_decoding=True,
                                         astc_cxx="android_host_cxx")
    # the server browser's requests to the master servers (host_https.c),
    # with posix_update.c's HTTPS client and port/third_party/mbedtls, as the
    # other posix_*.c in the host (with Mbed TLS's own locks: the browser's
    # fetches are threads of their own)
    mbedtls_cflags = " ".join([host_cflags, f"-I{MBEDTLS_DIR / 'include'}", f"-I{MBEDTLS_DIR / 'library'}",
                               "-DMBEDTLS_THREADING_C", "-DMBEDTLS_THREADING_PTHREAD"])
    for source in [LINUX_DIR / "src" / "posix_update.c", *sorted((MBEDTLS_DIR / "library").glob("*.c"))]:
        obj = host_obj_dir / ("mbedtls_" + source.name + ".o" if source.parent.parent == MBEDTLS_DIR
                              else source.name + ".o")
        n.build(outputs=obj, rule="android_host_cc", inputs=source,
                variables={"cflags": mbedtls_cflags + (" -w" if source.parent.parent == MBEDTLS_DIR else "")})
        host_objects.append(obj)
    table_obj = host_obj_dir / "host_import_table.c.o"
    n.build(outputs=table_obj, rule="android_host_cc", inputs=host_table_c, variables={"cflags": host_cflags})
    host_objects.append(table_obj)
    n.rule(
        name="android_host_link",
        # (C++'s library linked in, astc-encoder's: none to ship beside it)
        command=(f"$android_host_cxx -static-libstdc++ -shared -o $out $in -L{sdl_build} "
                 + " ".join(f"-l{lib}" for lib in HOST_LIBRARIES)
                 + " -Wl,-z,max-page-size=16384 -Wl,--no-undefined"),
        description="ANDROID HOST LINK $out",
    )
    n.build(outputs=libmain, rule="android_host_link", inputs=host_objects, implicit=[libsdl])

    # ---------- staging for Gradle

    staged_sdl = jni_dir / "libSDL3.so"
    staged_image = assets_dir / "halo_guest.elf"
    n.rule(name="android_copy", command="cp $in $out", description="ANDROID STAGE $out")
    n.build(outputs=staged_sdl, rule="android_copy", inputs=libsdl)
    n.build(outputs=staged_image, rule="android_copy", inputs=image, implicit=[guest["checked"]])
    # internet play's MQTT brokers, in the APK: the app writes them beside
    # config.toml (port/android/host/host_main.c)
    staged_brokers = assets_dir / "brokers.txt"
    n.build(outputs=staged_brokers, rule="android_copy", inputs=Path("port/assets/network/brokers.txt"))
    # the licences the APK carries (assets/licenses), as the macOS app does
    # (Contents/Resources/Licenses): OpenH1's, the AGPL; those of what every
    # build carries (tools/linux_package.py); and the SDL3 built here. The
    # release publishes the APK alone (.github/workflows/build.yml)
    licenses = [(Path("LICENSE"), "LICENSE.txt"),
                *((source.relative_to(REPO_ROOT), name) for source, name in release_notices("android")),
                (SDL_DIR / "LICENSE.txt", "SDL3-LICENSE.txt")]
    staged_licenses = [assets_dir / "licenses" / name for _, name in licenses]
    for (source, _), staged in zip(licenses, staged_licenses):
        n.build(outputs=staged, rule="android_copy", inputs=source)
    # ... and the stock maps' preparations' records, which it writes there too
    # (port/linux/src/prepared_shipped.h)
    staged_records = assets_dir / "prepared.records"
    n.build(outputs=staged_records, rule="android_copy", inputs=Path("port/assets/prepared/prepared.records"))
    # the textures', fonts' and menus' bytes (port/linux/src/embedded_assets.h),
    # which the app writes there too
    staged_embedded = assets_dir / "openh1.assets"
    n.build(outputs=staged_embedded, rule="android_copy", inputs=guest["assets"])
    staged_assets = [staged_image, staged_brokers, staged_records, staged_embedded, *staged_licenses]
    # the native libraries load on kernels of 16 KB pages (Android 15 and
    # later; Google Play requires it): every loadable segment 16 KB aligned
    # (-z max-page-size=16384, above), checked
    elf_alignment = BUILD / "elf_alignment.stamp"
    n.rule(name="android_elf_alignment", command="$python tools/android_elf_alignment.py $out $in",
           description="ANDROID CHECK 16 KB pages")
    n.build(outputs=elf_alignment, rule="android_elf_alignment", inputs=[libmain, staged_sdl],
            implicit=[Path("tools/android_elf_alignment.py")])
    n.build(outputs="android", rule="phony", inputs=[libmain, staged_sdl, elf_alignment, *staged_assets])

    apk = PORT_DIR / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk"
    sdl_android_mouse_listener = SDL_DIR / SDL_ANDROID_MOUSE_LISTENER
    n.rule(
        name="android_gradle",
        # Gradle leaves the APK alone when its contents would not change
        command=(f"cd {PORT_DIR} && ./gradlew --console=plain -q assembleDebug && "
                 "touch app/build/outputs/apk/debug/app-debug.apk"),
        description="ANDROID GRADLE $out",
        pool="console",
    )
    # (and the app's own files: a change to its Java, manifest or resources
    # alone would otherwise leave the old APK in place)
    n.build(outputs=apk, rule="android_gradle", inputs=[libmain, staged_sdl, elf_alignment, *staged_assets],
            implicit=[sdl_android_mouse_listener, *app_sources(), *GRADLE_FILES])
    n.build(outputs="android_apk", rule="phony", inputs=apk)
    n.newline()
