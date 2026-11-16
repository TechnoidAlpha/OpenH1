"""What the native builds' ninja rules share (tools/linux_guest_build.py,
android_build.py, macos_build.py, windows_build.py): the game's
sources and their settings (port/linux/port.json), the vendored libraries and
their flags, the configuration's defines, profile-guided optimisation, the
SDL3 that the Linux builds bring, and the headers that give the game MSVC's
semantics (tools/linux_msvc_semantics.py).
"""

import hashlib
import http.client
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence

from .embed_assets import hud_configure_inputs
from .ninja_syntax import Writer

PORT_DIR = Path("port/linux")
PORT_CONFIG = PORT_DIR / "port.json"
# the Xbox SDK declarations the game and the platform layer use, in place of
# the SDK's headers (port/include/xdk/README.md)
XDK_INCLUDE = Path("port/include/xdk")


def xdk_headers() -> List[Path]:
    return sorted(XDK_INCLUDE.glob("*.h"))


def game_sources(config: Dict[str, Any]) -> List[Path]:
    """the game's C sources (port.json "game"): every one under its root but
    those excluded (a file, or a folder: a name ending in "/")"""
    game = config["game"]
    excluded = game.get("exclude", [])
    return sorted(
        source for source in Path(game["root"]).rglob("*.c")
        if not any(source.as_posix() == name or (name.endswith("/") and source.as_posix().startswith(name))
                   for name in excluded)
    )


def game_defines_and_includes(config: Dict[str, Any]) -> str:
    """the game sources' defines and include directories (port.json "game")"""
    game = config["game"]
    return " ".join(
        [f"-D{define}" for define in game.get("defines", [])]
        + [f"-I{_quote(Path(directory))}" for directory in game.get("include_dirs", [])]
    )


def compile_launcher(sln: Any) -> str:
    """what the native ports' compile commands start with: the
    --compiler-launcher (ccache, say) and a space, or nothing"""
    launcher = getattr(sln, "compiler_launcher", None)
    return f"{launcher} " if launcher else ""


# The optimisation level of every unit, and of link-time optimisation.
OPTIMISATION = "-O2"

# What no unit of our own code (port/, source/, tools/) may hold, on every
# platform: a function that can end without its value, a call to a function
# it has no declaration of, a declaration without its type, an integer as a
# pointer or a pointer as an integer, a pointer of the wrong type. The
# compiler warns of each, and each has shipped as a crash or a hang (a
# function that returned whatever its register held; an undeclared
# function's int cutting a pointer short). These make it stop the build;
# the vendored libraries (port/third_party, and what is downloaded: musl,
# SDL) keep their own flags. (tools/test_build_errors.py)
# the replays' shared part (port/tools/replay/host_render_replay.c): Metal's
# replay (tools/macos_build.py) and Vulkan's (tools/linux_guest_build.py)
REPLAY_DIR = Path("port/tools/replay")


def replay_build_label() -> str:
    """the commit configure.py ran at, which the replay's service names (its
    "build"); "unknown" outside a clone"""
    try:
        result = subprocess.run(["git", "rev-parse", "--short=12", "HEAD"], capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return "unknown"
    label = result.stdout.strip()
    return label if result.returncode == 0 and re.fullmatch(r"[0-9a-f]+", label) else "unknown"


OWN_CODE_ERRORS = [
    "-Werror=return-type",
    "-Werror=implicit-function-declaration",
    "-Werror=implicit-int",
    "-Werror=int-conversion",
    "-Werror=incompatible-pointer-types",
]
# Our units that otherwise build without warnings (the game's decompiled
# sources, the guest's platform layer): -w would silence OWN_CODE_ERRORS
# too, as clang's -w drops every diagnostic that is not an error by
# default, -Werror= ones as well; -Wno-everything before them leaves them.
QUIET_OWN_CODE_FLAGS = ["-Wno-everything", *OWN_CODE_ERRORS]

# the TOML parser the platform layer reads config.toml with (port_config.c)
TOML_DIR = Path("port/third_party/tomlc17")
EXPAT_DIR = Path("port/third_party/expat")
EXPAT_SOURCES = ("xmlparse.c", "xmlrole.c", "xmltok.c")
KCP_DIR = Path("port/third_party/kcp")
MONOCYPHER_DIR = Path("port/third_party/monocypher")
MUSL_MATH_DIR = Path("port/third_party/musl-math")
# the port's zlib (port/third_party/zlib/zlib_prefixed.h): what inflates the
# maps, the menus' and the HUD's PNGs and the updates, data from anywhere,
# instead of the game's own 1.1.3 (its inflate only, its names prefixed z_)
ZLIB_DIR = Path("port/third_party/zlib")
ZLIB_SOURCES = ("adler32.c", "crc32.c", "inffast.c", "inflate.c", "inftrees.c", "uncompr.c", "zutil.c")
# (its names prefixed, and the one Z_PREFIX leaves, its error messages, which
# the game's zlib names the same)
ZLIB_DEFINES = ("-DZ_PREFIX", "-Dz_errmsg=z_port_errmsg")
# the self-updater's TLS (port/linux/src/posix_update.c)
MBEDTLS_DIR = Path("port/third_party/mbedtls")
# internet play's UPnP (port/linux/src/posix_upnp.c)
MINIUPNPC_DIR = Path("port/third_party/miniupnpc")
# the Custom Edition maps' Ogg Vorbis decoder (custom_edition_sounds.c),
# compiled as the port's game units are
STB_VORBIS_SOURCE = Path("port/third_party/stb_vorbis/stb_vorbis.c")
# miniupnpc's own build's definitions (its Makefile), and a static library
MINIUPNPC_DEFINES = ["-DMINIUPNP_STATICLIB", "-DMINIUPNPC_SET_SOCKET_TIMEOUT", "-DMINIUPNPC_GET_SRC_ADDR",
                     "-D_BSD_SOURCE", "-D_DEFAULT_SOURCE"]


def miniupnpc_sources() -> List[Path]:
    """miniupnpc's library sources (port/third_party/miniupnpc/src)"""
    return sorted((MINIUPNPC_DIR / "src").glob("*.c"))


# the DXT textures as ASTC, in the hosts of the GPUs without BC (iOS's and
# Android's: port/linux/astc/xgpu_astc.h), with ARM's astc-encoder, the
# hosts' only C++: its library's sources, as its own CMake build compiles
# them for AArch64 (NEON), without exceptions or RTTI, which it does not use
ASTC_ENCODER_DIR = Path("port/third_party/astc-encoder/Source")
ASTC_DIR = Path("port/linux/astc")
ASTC_HOST_SOURCES = [ASTC_DIR / "xgpu_astc.c", ASTC_DIR / "xgpu_astc_cache.c"]
ASTC_HOST_CXX_SOURCES = [ASTC_DIR / "xgpu_astc_encode.cpp"]
ASTC_CXX_FLAGS = ["-std=c++14", "-O2", "-fno-exceptions", "-fno-rtti", "-DASTCENC_NEON=1", "-DASTCENC_SVE=0",
                  "-DASTCENC_SSE=0", "-DASTCENC_AVX=0", "-DASTCENC_POPCNT=0", "-DASTCENC_F16C=0",
                  f"-I{ASTC_ENCODER_DIR}"]


def astc_encoder_sources() -> List[Path]:
    """astc-encoder's library sources (not its command-line program's)"""
    return sorted(source for source in ASTC_ENCODER_DIR.glob("astcenc_*.cpp"))


def build_repository() -> str:
    """the GitHub repository a build is made from, owner/name, which a client
    tells the servers it joins (network_distributed.c): CI's
    GITHUB_REPOSITORY, else the checkout's origin remote's (its credentials
    left out: only the owner and the name are kept), else none"""
    repository = os.environ.get("GITHUB_REPOSITORY", "")
    if not repository:
        try:
            url = subprocess.run(["git", "remote", "get-url", "origin"], capture_output=True, text=True,
                                 timeout=10, check=False).stdout.strip()
        except (OSError, subprocess.SubprocessError):
            url = ""
        found = re.search(r"github\.com[:/]+([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+?)(?:\.git)?/*$", url)
        repository = found.group(1) if found else ""
    return repository if re.fullmatch(r"[A-Za-z0-9_.-]{1,39}/[A-Za-z0-9_.-]{1,100}", repository) else ""


def updater_defines(release: bool) -> str:
    """the self-updater's build (port/linux/src/updater.c): its number, from
    HALO_BUILD_NUMBER (tools/ci_build.py gives it for builds of master; none
    elsewhere, which never look for updates), its configuration, and the
    repository it is made from (build_repository)"""
    number = os.environ.get("HALO_BUILD_NUMBER", "0")
    if not number.isdigit():
        number = "0"
    flavor = "release" if release else "debug"
    # (the commit, in the game's log as it starts: CI's, which names a
    # release's; none for a local build)
    commit = os.environ.get("GITHUB_SHA", "")[:12]
    if not re.fullmatch(r"[0-9a-f]*", commit):
        commit = ""
    return (f'-DHALO_BUILD_NUMBER={number} -DHALO_BUILD_FLAVOR=\\"{flavor}\\" '
            f'-DHALO_BUILD_REPOSITORY=\\"{build_repository()}\\" -DHALO_BUILD_COMMIT=\\"{commit}\\"')


def configuration_defines(sln: Any) -> List[str]:
    """what configure.py's options define for every unit of a native build:
    --release (no assertions), --profile (the profiling build's recording,
    port/linux/src/profile_trace.c)"""
    defines = []
    if getattr(sln, "port_release", False):
        defines.append("-DHALO_RELEASE")
    if getattr(sln, "port_profile", False):
        defines.append("-DHALO_PROFILE")
    defines += pgo_generate_defines(sln)
    return defines


def pgo_generate_defines(sln: Any) -> List[str]:
    """--pgo=generate's define, for the guest and every host: the
    instrumented image's larger range (HALO_GUEST_IMAGE_RESERVE,
    port/android/include/halo_android_abi.h; tools/guest_image_check.py
    --pgo-generate). Other builds have none, so their images and memory
    are laid out as without it."""
    return ["-DHALO_PGO_GENERATE"] if pgo_mode(sln) == "generate" else []


def check_profile_options(profile: bool, pgo: str) -> None:
    """A profiling build is optimised with the committed profiles or none:
    recorded by it, a profile would hold the profiling code's own paths."""
    if profile and pgo == "generate":
        raise ValueError("--profile cannot be used with --pgo=generate: record profiles with a normal build")


# Units compiled with a profile (-fprofile-use) that does not quite match
# them: new or changed functions simply go without. (Without value
# profiling, as the profile was recorded: --pgo=generate's
# PROFILE_GENERATE_FLAGS.)
PROFILE_USE_FLAGS = [
    "-mllvm", "-disable-vp",
    "-Wno-profile-instr-unprofiled",
    "-Wno-profile-instr-out-of-date",
    "-Wno-profile-instr-missing",
    "-Wno-backend-plugin",
]


# voice chat's codec (port/third_party/opus; network_voice.c): the
# floating-point library in plain C, its files as sources.txt lists them
OPUS_DIR = Path("port/third_party/opus")
OPUS_DEFINES = ("-DOPUS_BUILD", "-DVAR_ARRAYS", "-DHAVE_LRINTF", "-DHAVE_LRINT")


def opus_sources() -> List[Path]:
    """Opus's library sources (port/third_party/opus/sources.txt)"""
    return [OPUS_DIR / line.strip() for line in (OPUS_DIR / "sources.txt").read_text().splitlines() if line.strip()]


def opus_cflags(abi: str) -> str:
    """the flags Opus's sources build with, on the platform layer's ABI"""
    return " ".join([abi, "-std=gnu11", "-O2", *OPUS_DEFINES, *(f"-I{OPUS_DIR / name}" for name in
                                                            ("include", "celt", "silk", "silk/float")), "-w"])


# the Xbox's Bink movies' decoders (display.movies, port/linux/src/bink_ffmpeg.c):
# FFmpeg's plain C, fetched at configure time with its configuration
# (tools/ffmpeg_fetch.py), its files as port/third_party/ffmpeg/sources.txt
# lists them
FFMPEG_DIR = Path("port/third_party/ffmpeg")
FFMPEG_DEFINES = ("-DHAVE_AV_CONFIG_H", "-D_GNU_SOURCE")
# (the platform layer's file that calls it, compiled with its headers)
FFMPEG_CALLER = "bink_ffmpeg.c"


def ffmpeg_cflags(abi: str, tree: Path, warnings: Sequence[str] = ("-w",)) -> str:
    """the flags FFmpeg's sources (in tree, tools/ffmpeg_fetch.py) build
    with, on the platform layer's ABI; its caller's with its own code's
    warnings. (Its file names, in its assertions' messages, are the
    release's, wherever the build keeps it.)"""
    return " ".join([abi, "-std=gnu17", "-O2", *FFMPEG_DEFINES, f"-I{tree}/config", f"-I{tree}",
                     f"-I{tree}/compat/atomics/dummy", f"-ffile-prefix-map={tree}={tree.name}",
                     *warnings])


def musl_math_sources() -> List[Path]:
    """musl's maths functions the game uses (port/third_party/musl-math)"""
    return sorted((MUSL_MATH_DIR / "src").glob("*.c"))


def musl_math_cflags(abi: str) -> str:
    """their flags: the game's ABI, and the headers standing in for musl's"""
    return " ".join([abi, "-std=gnu11", "-w", f"-I{MUSL_MATH_DIR}/include",
                     f"-include {MUSL_MATH_DIR}/include/libm.h"])


# ---------- the SDL3 the Linux builds bring

# SDL's options: what it would build on a desktop distribution, its X11,
# Wayland (with libdecor's window decorations, which GNOME's Wayland asks
# for), PipeWire, PulseAudio and ALSA loaded when it starts (dlopen), less
# what the game does not use (KMS/DRM without a desktop, input methods,
# libusb, JACK, sndio, io_uring) and SDL's own tests
SDL_OPTIONS = [
    "-DCMAKE_BUILD_TYPE=Release", "-DSDL_SHARED=ON", "-DSDL_STATIC=OFF", "-DSDL_TEST_LIBRARY=OFF",
    "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF", "-DSDL_KMSDRM=OFF", "-DSDL_WAYLAND_LIBDECOR=ON",
    "-DSDL_WAYLAND_LIBDECOR_SHARED=ON", "-DSDL_IBUS=OFF", "-DSDL_HIDAPI_LIBUSB=OFF", "-DSDL_JACK=OFF",
    "-DSDL_SNDIO=OFF", "-DSDL_LIBURING=OFF", "-DSDL_FRIBIDI=OFF", "-DSDL_LIBTHAI=OFF", "-DSDL_X11_XTEST=OFF",
]
# (the linker's options for the executable that loads the SDL it brings:
# beside it in a build, in ../lib/openh1 in the release's tree
# (tools/linux_package.py); DT_RPATH rather than DT_RUNPATH, as it comes
# before LD_LIBRARY_PATH, which Steam sets for the games it starts, so that
# the SDL the release brings is the one loaded, whatever other SDL that path
# holds; a package without lib/openh1 gets the system's)
SDL_RPATH_LDFLAGS = ["'-Wl,-rpath,$$ORIGIN:$$ORIGIN/../lib/openh1'", "-Wl,--disable-new-dtags"]


def sdl3_desktop_check(sdl_build: Path) -> str:
    """sdl3_rule's check for the Linux builds' SDL: that it has both of its
    video drivers, X11's and Wayland's (with libdecor's decorations). CMake
    leaves Wayland out without a word where a development file it needs is
    missing (wayland-client, wayland-egl, wayland-cursor, egl, xkbcommon:
    the releases before this check had no egl.pc, and so played under
    XWayland on every Wayland desktop)."""
    config = sdl_build / "include-config-release" / "build_config" / "SDL_build_config.h"
    return (f"{{ {{ grep -q '^#define SDL_VIDEO_DRIVER_X11 1' {config} && "
            f"grep -q '^#define SDL_VIDEO_DRIVER_WAYLAND 1' {config} && "
            f"grep -q '^#define HAVE_LIBDECOR_H 1' {config}; }} || "
            f"{{ echo 'SDL3 was configured without X11, Wayland or libdecor: see "
            f"{sdl_build.parent}/sdl3-configure.log for the development files it lacks' >&2; exit 1; }}; }}")


def sdl3_rule(n: Writer, sln: Any, *, name: str, description: str, source: Path, sdl_build: Path, log_dir: Path,
              cc: str, cmake_args: Sequence[str] = (), check: str = "") -> None:
    """The rule that builds SDL3 from source into sdl_build, as the Linux
    builds that bring their own libSDL3.so.0 do (tools/linux_guest_build.py):
    with SDL_OPTIONS and the --compiler-launcher, CMake's cmake_args (a
    toolchain file, say), its logs in log_dir, and then check, a command that
    stops the build if SDL lacks what it needs."""
    launcher = getattr(sln, "compiler_launcher", None)
    options = SDL_OPTIONS + ([f"-DCMAKE_C_COMPILER_LAUNCHER={launcher}"] if launcher else [])
    n.rule(
        name=name,
        # (a fresh configure, as CMake 3.24's --fresh does, also with the
        # older CMake of Ubuntu 22.04, which builds the Linux releases)
        command=(f"rm -rf {sdl_build}/CMakeCache.txt {sdl_build}/CMakeFiles && "
                 f"cmake -S {source} -B {sdl_build} -G Ninja "
                 + "".join(f"{argument} " for argument in cmake_args)
                 + f"-DCMAKE_C_COMPILER={cc} {' '.join(options)} > {log_dir}/sdl3-configure.log && "
                 f"ninja -C {sdl_build} > {log_dir}/sdl3-build.log"
                 + (f" && {check}" if check else "")),
        description=description,
        pool="console",
        restat=True,
    )


# ---------- profile-guided optimisation

# The profile recorded by playing an instrumented guest (configure.py
# --pgo=generate, tools/pgo_train.py), kept in the repository so that every
# build is optimised with it. It is in the format of this LLVM version,
# which older compilers cannot read. It is every build's (they are all
# guests: Android, Linux, macOS, iOS, Windows), whose game and platform code
# it was recorded from.
PGO_DIR = Path("pgo")
LINUX_PROFILE = PGO_DIR / "halo_linux.profdata"
PROFILE_LLVM_MAJOR = 22

# An instrumented guest (--pgo=generate): clang's IR instrumentation, which
# the committed profile is, without value profiling, which the guests'
# profile runtime does not write (port/android/guest/runtime/guest_profile.c)
PROFILE_GENERATE_FLAGS = ["-fprofile-generate", "-mllvm", "-disable-vp"]

_clang_majors: Dict[str, Optional[int]] = {}


def clang_major(cc: str) -> Optional[int]:
    """the major version of the clang named cc, or None if unknown"""
    if cc not in _clang_majors:
        try:
            output = subprocess.run([cc, "--version"], capture_output=True, text=True, check=False).stdout
            match = re.search(r"clang version (\d+)", output)
            _clang_majors[cc] = int(match.group(1)) if match else None
        except OSError:
            _clang_majors[cc] = None
    return _clang_majors[cc]


def pgo_mode(sln: Any) -> str:
    """use, generate or off (configure.py --pgo)"""
    return getattr(sln, "port_pgo", "use")


def pgo_profile(sln: Any, others: Sequence[Path], cc: str) -> Optional[Path]:
    """The profile a guest is optimised with: --pgo-profile's, or else the
    first committed profile there is. None with --pgo=off or --pgo=generate,
    or when cc is too old to read the profiles."""
    mode = pgo_mode(sln)
    if mode == "generate":
        return None
    explicit = getattr(sln, "port_pgo_profile", None)
    if explicit:
        return explicit
    if mode == "off":
        return None
    major = clang_major(cc)
    if major is not None and major < PROFILE_LLVM_MAJOR:
        print(f"{cc} is clang {major}; the profiles in {PGO_DIR} need clang {PROFILE_LLVM_MAJOR}: "
              "building without profile-guided optimisation", file=sys.stderr)
        return None
    for profile in others:
        if profile.is_file():
            return profile
    return None


def profile_use_flags(profile: Any) -> List[str]:
    return [f"-fprofile-use={_quote(profile)}", *PROFILE_USE_FLAGS] if profile else []


# ---------- MSVC's semantics for the game (tools/linux_msvc_semantics.py)

# what every unit of the guests' game and platform layer includes (the
# Android, Linux and macOS builds; the Windows build writes its own):
# the struct tags MSVC gives file scope, and the inline functions it makes
# COMDAT, which these make weak
SEMANTICS_DIR = Path("build/msvc_semantics")
SEMANTICS_HEADER = SEMANTICS_DIR / "halo_msvc_semantics.h"
PLATFORM_SEMANTICS_HEADER = SEMANTICS_DIR / "platform_msvc_semantics.h"
SEMANTICS_SCRIPT = Path("tools/linux_msvc_semantics.py")


def generate_msvc_semantics(n: Writer) -> None:
    """The rules that write SEMANTICS_HEADER (the game's: its own tags and
    inline functions and those of the SDK declarations) and
    PLATFORM_SEMANTICS_HEADER (the platform layer's, which includes only the
    SDK declarations and so needs only their inline functions)."""
    if not PORT_CONFIG.is_file():
        return
    n.comment("MSVC's semantics for the game and the platform layer (tools/linux_msvc_semantics.py)")
    n.rule(
        name="msvc_semantics",
        command=f"$python {SEMANTICS_SCRIPT} --output $out $scan",
        description="MSVC SEMANTICS $out",
        restat=True,
    )
    game_headers = sorted(p for p in Path("source").rglob("*") if p.suffix in (".c", ".h"))
    n.build(
        outputs=SEMANTICS_HEADER,
        rule="msvc_semantics",
        implicit=[SEMANTICS_SCRIPT, *xdk_headers(), *game_headers],
        variables={"scan": f"--all-inlines --tags source --inlines source --inlines {XDK_INCLUDE}"},
    )
    n.build(
        outputs=PLATFORM_SEMANTICS_HEADER,
        rule="msvc_semantics",
        implicit=[SEMANTICS_SCRIPT, *xdk_headers()],
        variables={"scan": f"--inlines {XDK_INCLUDE}"},
    )
    n.newline()


def _load_port_config() -> Dict[str, Any]:
    with open(PORT_CONFIG, "r", encoding="utf-8") as f:
        return json.load(f)


def common_configure_inputs() -> List[Path]:
    """Files whose change must re-run configure.py, for every build: the
    game's sources and settings, the platform layer's folders."""
    if not PORT_CONFIG.is_file():
        return [Path(__file__)]
    # (the folders of the game's sources, so that adding or removing one
    # re-runs it)
    game_folders = sorted({source.parent for source in game_sources(_load_port_config())})
    return [PORT_CONFIG, Path(__file__), SEMANTICS_SCRIPT, PORT_DIR / "src", PORT_DIR / "game", XDK_INCLUDE,
            *game_folders, *hud_configure_inputs()]


def _quote(path: Any) -> str:
    text = str(path).replace(os.sep, "/")
    return f'"{text}"' if " " in text else text


# ---------- downloads at configure time

TRIES = 3
RETRY_SECONDS = 2


def download(urls: List[str], digest: str) -> bytes:
    """the file at the first of urls that answers, checked against digest"""
    errors = []
    for url in urls:
        for attempt in range(1, TRIES + 1):
            if attempt > 1:
                time.sleep(RETRY_SECONDS * (attempt - 1))
            try:
                with urllib.request.urlopen(url, timeout=60) as response:
                    data = response.read()
            # (a connection cut short is http.client.IncompleteRead, not an
            # OSError)
            except (OSError, http.client.HTTPException) as error:
                errors.append(f"{url}: {error!r}")
                # (the mirror does not have it: no use asking again)
                if isinstance(error, urllib.error.HTTPError) and error.code == 404:
                    break
                continue
            actual = hashlib.sha256(data).hexdigest()
            if actual == digest:
                return data
            errors.append(f"{url}: SHA-256 {actual}, expected {digest}")
    raise RuntimeError("cannot download " + "; ".join(errors))
