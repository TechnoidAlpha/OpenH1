"""Ninja rules for the macOS build (``ninja macos``), for Apple Silicon.

The game runs as the Linux arm64 build runs it (tools/linux_guest_build.py,
port/android/README.md, "How the port operates"): the guest image is the
game built as ILP32 AArch64 code (tools/guest_arch.py, ARM64_32), with the
Linux desktop's code paths and the Mac's (HALO_MACOS), and
the host is a native executable that loads it and serves its calls. macOS
keeps the low 4 GB of every process from it (__PAGEZERO), so the guest is
built with the base register (tools/guest_base_register.py): its 4 GB lie
wherever the host reserves them (port/android/host/host_memory_base.c).

- build/macos/halo: the host (port/android/host, with
  port/linux/host/host_main.c as its entry point and port/macos for
  Darwin's system calls), with the guest image inside it;
- build/macos/libSDL3.0.dylib: SDL3, built from source without OpenGL, next
  to the executable, which finds it there (an rpath of @executable_path);
- build/macos/OpenH1.app: the app, the executable and SDL in a bundle (where
  the executable finds SDL in Contents/Frameworks, its other rpath), signed
  ad hoc with the hardened runtime (tools/macos_app.py); build/macos/OpenH1.dmg
  (ninja macos_dmg), a disk image of it;
- build/macos/xgpu_metal_test (ninja macos_metal_test): the Metal backend's
  tests (port/macos/tests).

The renderer is Metal's (port/macos/xgpu_metal.m), in the host: the guest
writes its frames as the renderer's command stream.
Refer to port/macos/README.md.
"""

import json
import os
import platform
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Set

from .linux_package import ROOT as REPO_ROOT, notices as release_notices
from .android_build import (GUEST_ARCH_SCRIPT, GUEST_TOOLCHAIN_SCRIPT, SDL_TAG,
                            TOML_DIR, fetch_third_party, generate_guest_image)
from .build_common import (ASTC_CXX_FLAGS, ASTC_DIR, ASTC_HOST_CXX_SOURCES, ASTC_HOST_SOURCES, MBEDTLS_DIR, MINIUPNPC_DEFINES,
                           MINIUPNPC_DIR, OWN_CODE_ERRORS, REPLAY_DIR, astc_encoder_sources, compile_launcher, download,
                           miniupnpc_sources, pgo_generate_defines, replay_build_label)
from .guest_arch import ARM64_32
from .guest_toolchain import LLVM_VERSION, guest_toolchain
from .ninja_syntax import Writer

PORT_DIR = Path("port/macos")
# the desktop guest builds' host (tools/linux_guest_build.py), which this
# one shares
LINUX_HOST_DIR = Path("port/linux/host")
ANDROID_DIR = Path("port/android")
LINUX_DIR = Path("port/linux")
BUILD = Path("build/macos")
THIRD_PARTY = BUILD / "third_party"
SDL_DIR = THIRD_PARTY / "SDL3"
# macOS 14.4: os_sync_wait_on_address (the guest's futexes, host_syscall.c)
MACOS_MINIMUM = "14.4"

# Sparkle, the macOS app's updater (port/macos/host_app.m; port/macos/README.md,
# "Updates"): its release's framework and tools (bin/sign_update, which
# signs a release's disk image for the appcast in CI), downloaded once
# (MIT; its LICENSE goes in the app's Licenses)
SPARKLE_VERSION = "2.10.0"
SPARKLE_SHA256 = "c2bf58aa8387266ac179357b1415d6f2635f044da8be41042af32425dae6da0c"
SPARKLE_URL = (f"https://github.com/sparkle-project/Sparkle/releases/download/{SPARKLE_VERSION}/"
               f"Sparkle-{SPARKLE_VERSION}.tar.xz")
# the appcast a release's app asks for new versions (each release has its
# own, appcast.xml, written by tools/macos_appcast.py in CI's release job),
# and the public half of the key its disk images are signed with (EdDSA:
# tools/setup_sparkle_key.sh), from the SPARKLE_PUBLIC_KEY environment
# variable (CI's repository variable); an app without both never looks
SPARKLE_FEED = "https://github.com/OpenH1/goath1/releases/latest/download/appcast.xml"

# the guest's code paths beside the architecture's: the Mac's (the base
# register is added by generate_guest_image); Apple's clang otherwise notes
# that its macOS sysroot is not the watchOS target's, which the guest does
# not use
MACOS_GUEST_DEFINES = ["-DHALO_MACOS=1", "-DHALO_METAL", "-Wno-incompatible-sysroot"]

# the app's bundle identifier (configure.py --macos-bundle-id): the
# reverse-DNS name of the project's domain, openh1.com
MACOS_BUNDLE_ID = "com.openh1.openh1"

# the host's frameworks: Metal and its layer (port/macos/host_metal.m,
# xgpu_metal.m), the mouse's own counts (host_raw_mouse.m: GameController),
# the output device's kind (port/android/host/host_audio_route.c: CoreAudio),
# the sound's output unit (port/macos/host_audio_output.c: AudioToolbox), and
# the keys' and mouse buttons' state (port/android/host/host_sdl.c: CoreGraphics)
MACOS_FRAMEWORKS = "-framework Metal -framework QuartzCore -framework Foundation -framework AppKit -framework CoreGraphics -framework ImageIO -framework GameController -framework CoreAudio -framework AudioToolbox -lcompression"

# (port/android/host's: those of the Android app alone, the fixed low
# memory and Linux's system calls, which port/macos and host_memory_base.c
# replace)
HOST_EXCLUDED = {"host_main.c", "host_touch.c", "host_memory.c", "host_syscall.c"}

# the renderer's parts the host builds from port/linux/src (the MSL
# emitter, the Xbox's texture decoding), and port/macos's files that use
# them
SHARED_SOURCES = ["xgpu.h", "xgpu_backend.h", "loading_presentation.h", "loading_art.h", "loading_art_tables.h", "loading_ring.h", "loading_scene.h", "xgpu_shader.h", "xgpu_msl.h", "xgpu_msl.c", "xgpu_text.c",
                  "xbox_texture_decode.c", "xgpu_dynamic_lights_world.h", "xgpu_dynamic_lights_world.c"]
SHARED_USERS = {"xgpu_metal_shaders.c", "xgpu_metal_textures.c", "xgpu_metal_combiner.c"}
# the Metal backend's own C files, linked with it into its tests and the
# replay too: anti-aliasing's passes and their shaders (generated by
# tools/metal_post_shaders.py), display.ambient_occlusion "world"'s resources
METAL_C_SOURCES = {"xgpu_metal_post.c", "xgpu_metal_post_shaders.c", "xgpu_metal_world_ao.c"}
# the shaders' tool's own (port/macos/tools/xgpu_metal_prebuild.c): the
# decoders, the stream's descriptions, the HDR tags' classification and the
# manifest's reader with its derivation of a draw under the settings, beside
# the same stand-in
TOOL_SHARED_SOURCES = ["nv2a_vsh.c", "nv2a_psh.c", "xgpu_stream.h", "xgpu_stream_describe.h", "xgpu_stream_describe.c",
                       "xgpu_hdr.h", "xgpu_hdr.c",
                       "xgpu_manifest.h", "xgpu_manifest.c"]
# the renderer's manifest the app's shaders are made from
# (port/assets/render/README.md)
RENDER_MANIFEST = Path("port/assets/render/manifest.txt")
# the app's start (its folders, maps and window), which the Metal tests do
# without
APP_SOURCES = {"host_app.m"}


def is_macos_arm64() -> bool:
    return sys.platform == "darwin" and platform.machine().lower() == "arm64"


def macos_configure_inputs() -> List[Path]:
    return [Path(__file__), GUEST_ARCH_SCRIPT, GUEST_TOOLCHAIN_SCRIPT, PORT_DIR, LINUX_HOST_DIR]


def fetch_sparkle(third_party: Path = THIRD_PARTY) -> Path:
    """Download Sparkle's release (configure time, once) into
    third_party/Sparkle: Sparkle.framework (its links kept), bin/ and
    LICENSE; its folder."""
    import io
    import tarfile

    folder = third_party / "Sparkle"
    stamp = folder / ".sha256"
    if stamp.is_file() and stamp.read_text().strip() == SPARKLE_SHA256:
        return folder
    print(f"Downloading Sparkle {SPARKLE_VERSION}")
    data = download([SPARKLE_URL], SPARKLE_SHA256)
    partial = folder.with_name("Sparkle.partial")
    shutil.rmtree(partial, ignore_errors=True)
    partial.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:xz") as archive:
        members = [member for member in archive.getmembers()
                   if Path(member.name).parts[:1] in (("Sparkle.framework",), ("bin",), ("LICENSE",))]
        if hasattr(tarfile, "data_filter"):
            archive.extractall(partial, members=members, filter="tar")
        else:
            archive.extractall(partial, members=members)
    (partial / ".sha256").write_text(SPARKLE_SHA256 + "\n")
    shutil.rmtree(folder, ignore_errors=True)
    partial.rename(folder)
    return folder


def _tool(name: str) -> str:
    """name on the PATH, else Homebrew's (LLVM's tools, and lld's)."""
    found = shutil.which(name)
    if found:
        return found
    for directory in ("/opt/homebrew/opt/llvm/bin", "/opt/homebrew/opt/lld/bin", "/opt/homebrew/bin"):
        candidate = Path(directory) / name
        if candidate.is_file():
            return str(candidate)
    return ""


# the platforms' names in the build file's comments
TITLES = {"macos": "macOS", "ios": "iOS"}


@dataclass
class ApplePlatform:
    """What the macOS build and the iOS build (tools/ios_build.py), which
    share this file's rules, do differently. name prefixes their rules and
    targets; label is their builds' lines' prefix."""
    name: str
    label: str
    build: Path
    third_party: Path
    # the guest's code paths beside MACOS_GUEST_DEFINES
    guest_defines: List[str]
    # the host's compiler options for its target (the deployment target, the SDK)
    target_flags: str
    frameworks: str
    # port/macos's Objective-C files the platform does not build, and its own
    # host sources
    host_excluded: Set[str]
    host_sources: List[Path]
    # SDL3's CMake options for the platform; the library made and how the
    # host links it
    sdl_options: str
    sdl_library: str
    sdl_link: str
    # where the guest image goes in the executable (an assembly file
    # including GUEST_IMAGE)
    guest_image_source: Path
    # tools/macos_shaders.py's --platform
    shader_platform: str
    link_flags: str = ""
    # another platform's guest image (generate_apple_build's "guest"), used
    # in place of building one: the iOS simulator's, which is the device's
    guest: Optional[Dict[str, Path]] = None
    # more lists of host functions the guest imports (iOS's touch controls')
    guest_imports: Optional[List[Path]] = None
    # more of port/linux/src's files built into the app's host (not the
    # Metal tests') beside the platform.h stand-in: the disc image's
    # extractor, xiso.c, for the iOS app's first start
    # (port/ios/data_import.m; the macOS app's is the guest's, first_start.c)
    shared_extra: Optional[List[str]] = None


def macos_platform() -> ApplePlatform:
    return ApplePlatform(
        name="macos", label="MACOS", build=BUILD, third_party=THIRD_PARTY, guest_defines=[],
        target_flags=f"-mmacosx-version-min={MACOS_MINIMUM}", frameworks=MACOS_FRAMEWORKS, host_excluded=set(),
        host_sources=[],
        sdl_options=(f"-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET={MACOS_MINIMUM} "
                     "-DSDL_SHARED=ON -DSDL_STATIC=OFF"),
        sdl_library="libSDL3.0.dylib", sdl_link="-lSDL3", guest_image_source=PORT_DIR / "guest_image.S",
        shader_platform="macos",
        link_flags="-Wl,-rpath,@executable_path -Wl,-rpath,@executable_path/../Frameworks")


def generate_macos_build(n: Writer, sln: Any) -> None:
    generate_apple_build(n, sln, macos_platform())


def generate_apple_build(n: Writer, sln: Any, target: ApplePlatform) -> Optional[Dict[str, Path]]:
    """The rules of an Apple platform's build (macOS's, or iOS's from
    tools/ios_build.py); the paths of what the caller packages: the
    executable ("executable"), SDL ("sdl"), the shaders' library
    ("library"), brokers.txt ("brokers"); None when it cannot be built."""
    config_path = LINUX_DIR / "port.json"
    if not config_path.is_file() or not PORT_DIR.is_dir():
        return None
    if not is_macos_arm64():
        n.comment(f"{target.name} build: only on a Mac with Apple silicon")
        return None
    name, label, build, third_party = target.name, target.label, target.build, target.third_party
    sdl_dir = third_party / "SDL3"
    cc = getattr(sln, "macos_cc", None) or "clang"
    # the guest's compiler and tools: --macos-guest-cc's, else --macos-cc's,
    # else the pinned LLVM's (tools/guest_toolchain.py, configure.py
    # --guest-llvm), which reads the profiles in pgo/ (Xcode's clang is too
    # old for them); with a compiler of the build's own or
    # --guest-llvm=system, ld.lld, llvm-ar and llvm-objdump from the PATH or
    # Homebrew
    explicit_cc = getattr(sln, "macos_guest_cc", None) or getattr(sln, "macos_cc", None)
    try:
        toolchain = guest_toolchain(sln, explicit_cc)
    except (RuntimeError, OSError) as error:
        print(f"{name} build disabled: no LLVM {LLVM_VERSION} for the guest ({error})", file=sys.stderr)
        return None
    if toolchain:
        guest_cc, ld, ar, objdump = toolchain.cc, toolchain.ld, toolchain.ar, toolchain.objdump
    else:
        guest_cc, ld, ar, objdump = explicit_cc or cc, _tool("ld.lld"), _tool("llvm-ar"), _tool("llvm-objdump")
    if not ld or not ar or not objdump:
        n.comment(f"{name} build: needs ld.lld, llvm-ar and llvm-objdump (brew install llvm lld)")
        return None
    try:
        fetch_third_party(third_party)
        # (the macOS app's updater: not the iOS app's, which the App Store
        # or a developer's install updates)
        sparkle = fetch_sparkle(third_party) if name == "macos" else None
    except (subprocess.CalledProcessError, OSError, RuntimeError) as error:
        print(f"{name} build disabled: cannot fetch musl, SDL3 or FFmpeg ({error})", file=sys.stderr)
        return None
    config: Dict[str, Any] = json.loads(config_path.read_text(encoding="utf-8"))

    n.comment(f"{TITLES.get(name, name)} build (ninja {name}); see port/{name}/README.md")
    n.variable(f"{name}_guest_cc", guest_cc)
    n.variable(f"{name}_cc", cc)

    guest = target.guest or generate_guest_image(
        n, sln, config, arch=ARM64_32, prefix=name, label=label, build=build, third_party=third_party,
        guest_cc=guest_cc, ar=ar, ld=ld, objdump=objdump,
        # (no runtime library: Apple's clang has none for an ELF image, and
        # the guest needs none of it)
        builtins_cc="", environment="gnu", platform_defines=MACOS_GUEST_DEFINES + target.guest_defines,
        extra_runtime=[LINUX_HOST_DIR / "guest_desktop.c", PORT_DIR / "guest_metal.c"],
        extra_imports=[LINUX_HOST_DIR / "host_imports.list", PORT_DIR / "host_imports.list",
                       *(target.guest_imports or [])],
        extra_prototypes=[LINUX_HOST_DIR / "guest_desktop.c", PORT_DIR / "guest_metal.c"],
        base_register=True)

    # ---------- SDL3, built from the same source as the guest's headers

    sdl_build = build / "sdl3-build"
    libsdl = sdl_build / target.sdl_library
    # (its C and Objective-C through the --compiler-launcher too, as the
    # Linux builds' SDL: build_common.sdl3_rule)
    launcher = getattr(sln, "compiler_launcher", None)
    sdl_launcher = (f"-DCMAKE_C_COMPILER_LAUNCHER={launcher} -DCMAKE_OBJC_COMPILER_LAUNCHER={launcher} "
                    if launcher else "")
    n.rule(
        name=f"{name}_sdl3",
        command=(f"cmake -S {sdl_dir} -B {sdl_build} -G Ninja -DCMAKE_C_COMPILER=${name}_cc {sdl_launcher}"
                 f"-DCMAKE_BUILD_TYPE=Release {target.sdl_options} "
                 f"-DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF "
                 # (no OpenGL: the renderer will be Metal's)
                 f"-DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF > {build}/sdl3-configure.log && "
                 f"ninja -C {sdl_build} > {build}/sdl3-build.log"),
        description=f"{label} SDL3 {SDL_TAG}",
        pool="console",
    )
    n.build(outputs=libsdl, rule=f"{name}_sdl3", implicit=[sdl_dir / "CMakeLists.txt"])

    # ---------- the host

    host_obj_dir = build / "host" / "obj"
    n.rule(
        name=f"{name}_host_cc",
        command=f"{compile_launcher(sln)}${name}_cc -MMD -MF $out.d $cflags -c $in -o $out",
        description=f"{label} HOST CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    host_cflags = " ".join([
        "-O2", "-g", "-Wall", "-Wno-unused-function", *OWN_CODE_ERRORS, "-D_DARWIN_C_SOURCE", "-DHALO_GUEST_BASE=1",
        target.target_flags,
        f"-I{ANDROID_DIR}/include", f"-I{ANDROID_DIR}/host", f"-I{sdl_dir}/include", f"-I{LINUX_DIR}/src", f"-I{ASTC_DIR}",
        f"-I{TOML_DIR}", f"-I{ANDROID_DIR}/guest/libc/arch/generic", f"-I{PORT_DIR}", *pgo_generate_defines(sln),
    ])
    objc_cflags = f"{host_cflags} -fobjc-arc"
    host_sources = [source for source in sorted((ANDROID_DIR / "host").glob("*.[cS]"))
                    if source.name not in HOST_EXCLUDED]
    host_sources += [
        LINUX_HOST_DIR / "host_main.c", LINUX_HOST_DIR / "host_desktop.c",
        *sorted(source for source in PORT_DIR.glob("*.c")
                if not source.name.startswith("guest_") and source.name not in SHARED_USERS
                and source.name not in METAL_C_SOURCES),
        LINUX_DIR / "src" / "posix_files.c", LINUX_DIR / "src" / "posix_net.c",
        # the MCC packages' previews and sharing (posix.h's posix_mcc_*)
        LINUX_DIR / "src" / "posix_mcc_preview.c", LINUX_DIR / "src" / "posix_mcc_share.c",
        TOML_DIR / "tomlc17.c",
    ]
    host_objects: List[Path] = []
    for source in host_sources:
        obj = host_obj_dir / (source.name + ".o")
        n.build(outputs=obj, rule=f"{name}_host_cc", inputs=source, variables={"cflags": host_cflags})
        host_objects.append(obj)
    # (the platform's own: iOS's app, port/ios)
    for source in target.host_sources:
        obj = host_obj_dir / (source.name + ".o")
        n.build(outputs=obj, rule=f"{name}_host_cc", inputs=source,
                variables={"cflags": objc_cflags if source.suffix == ".m" else host_cflags})
        host_objects.append(obj)
    # the renderer's shared parts in the host, copied beside a stand-in for
    # the guest's platform.h (xgpu_metal_platform.h), which their quoted
    # includes would otherwise find in port/linux/src, and the files of
    # port/macos that use them; the XDK's enumerations and texture format
    # words for the stand-in (xdk_enums.h)
    shared_dir = build / "host" / "shared"
    n.rule(name=f"{name}_shared_copy", command="cp $in $out", description=f"{label} SHARED $out")
    xdk = Path("port/include/xdk")
    n.rule(name=f"{name}_xdk_enums",
           command=(f"(sed -n '/^\\/\\* ---------- enumerations/,/^\\/\\* ---------- structures and unions/p' "
                    f"{xdk / 'xdk_pdb.h'} | sed '$$d' && "
                    f"grep -E '^#define D3D(FORMAT|SIZE|TEXTURE)_' {xdk / 'xdk_d3d8.h'}) > $out"),
           description=f"{label} SHARED $out")
    shared_copies: List[Path] = []
    shared_sources = SHARED_SOURCES + list(target.shared_extra or [])
    for source_name in shared_sources:
        n.build(outputs=shared_dir / source_name, rule=f"{name}_shared_copy", inputs=LINUX_DIR / "src" / source_name)
        shared_copies.append(shared_dir / source_name)
    n.build(outputs=shared_dir / "platform.h", rule=f"{name}_shared_copy", inputs=PORT_DIR / "xgpu_metal_platform.h")
    n.build(outputs=shared_dir / "xdk_enums.h", rule=f"{name}_xdk_enums",
            inputs=[xdk / "xdk_pdb.h", xdk / "xdk_d3d8.h"])
    shared_copies += [shared_dir / "platform.h", shared_dir / "xdk_enums.h"]
    shared_cflags = f"-I{shared_dir} {host_cflags}"
    shared_objects: List[Path] = []
    for source in [shared_dir / source_name for source_name in shared_sources if source_name.endswith(".c")] + \
            [PORT_DIR / source_name for source_name in sorted(SHARED_USERS)]:
        obj = host_obj_dir / "shared" / (source.name + ".o")
        n.build(outputs=obj, rule=f"{name}_host_cc", inputs=source, order_only=shared_copies,
                variables={"cflags": shared_cflags})
        # (the app's own, shared_extra: in the app, not the Metal tests)
        if source.name in (target.shared_extra or []):
            host_objects.append(obj)
        else:
            shared_objects.append(obj)
    # the DXT textures as ASTC on a GPU without BC (an iPhone's:
    # port/linux/astc/xgpu_astc.h), with astc-encoder, the host's C++; linked
    # with the Metal backend into its tests and the replay too
    n.rule(
        name=f"{name}_host_cxx",
        command=f"{compile_launcher(sln)}${name}_cc -x c++ -MMD -MF $out.d $cflags -c $in -o $out",
        description=f"{label} HOST CXX $out",
        depfile="$out.d",
        deps="gcc",
    )
    astc_cxx_flags = " ".join(["-g", target.target_flags, *ASTC_CXX_FLAGS])
    for source in ASTC_HOST_SOURCES:
        obj = host_obj_dir / "astc" / (source.name + ".o")
        n.build(outputs=obj, rule=f"{name}_host_cc", inputs=source, variables={"cflags": host_cflags})
        shared_objects.append(obj)
    for source in ASTC_HOST_CXX_SOURCES:
        obj = host_obj_dir / "astc" / (source.name + ".o")
        n.build(outputs=obj, rule=f"{name}_host_cxx", inputs=source,
                variables={"cflags": f"{astc_cxx_flags} -Wall -I{ASTC_DIR}"})
        shared_objects.append(obj)
    for source in astc_encoder_sources():
        obj = host_obj_dir / "astc" / (source.name + ".o")
        n.build(outputs=obj, rule=f"{name}_host_cxx", inputs=source, variables={"cflags": f"{astc_cxx_flags} -w"})
        shared_objects.append(obj)
    host_objects += shared_objects
    # the Metal renderer and the window's presentation (Objective-C)
    metal_objects: List[Path] = []
    for source in sorted(PORT_DIR.glob("*.m")):
        if source.name in target.host_excluded:
            continue
        obj = host_obj_dir / (source.name + ".o")
        # (the app's start: Sparkle's headers, its framework loaded at run
        # time from the bundle, so that the bare executable runs without it)
        flags = f"{objc_cflags} -F{sparkle}" if sparkle and source.name in APP_SOURCES else objc_cflags
        n.build(outputs=obj, rule=f"{name}_host_cc", inputs=source, variables={"cflags": flags})
        if source.name in APP_SOURCES:
            host_objects.append(obj)
        else:
            metal_objects.append(obj)
    for source_name in sorted(METAL_C_SOURCES):
        obj = host_obj_dir / (source_name + ".o")
        n.build(outputs=obj, rule=f"{name}_host_cc", inputs=PORT_DIR / source_name, variables={"cflags": host_cflags})
        metal_objects.append(obj)
    host_objects += metal_objects
    # the shaders' tool, a Mac's (the macOS build's: the iOS build uses it
    # too): the emitter's and the pipelines' parts of the host
    tool = BUILD / "xgpu_metal_prebuild"
    if name == "macos":
        tool_objects: List[Path] = []
        for source_name in TOOL_SHARED_SOURCES:
            n.build(outputs=shared_dir / source_name, rule=f"{name}_shared_copy",
                    inputs=LINUX_DIR / "src" / source_name)
        tool_copies = [shared_dir / source_name for source_name in TOOL_SHARED_SOURCES]
        for source in [shared_dir / source_name for source_name in TOOL_SHARED_SOURCES
                       if source_name.endswith(".c")] + [PORT_DIR / "tools" / "xgpu_metal_prebuild.c"]:
            obj = host_obj_dir / "tool" / (source.name + ".o")
            flags = f"-I{PORT_DIR / 'tools'} {shared_cflags}" + (" -DXGPU_MANIFEST_READER_ONLY" if source.name ==
                                                                   "xgpu_manifest.c" else "")
            n.build(outputs=obj, rule=f"{name}_host_cc", inputs=source, order_only=shared_copies + tool_copies,
                    variables={"cflags": flags})
            tool_objects.append(obj)
        obj = host_obj_dir / "tool" / "xgpu_metal_prebuild_script.m.o"
        n.build(outputs=obj, rule=f"{name}_host_cc", inputs=PORT_DIR / "tools" / "xgpu_metal_prebuild_script.m",
                variables={"cflags": f"-I{PORT_DIR / 'tools'} {objc_cflags}"})
        tool_objects.append(obj)
        n.rule(name="macos_tool_link",
               command=f"$macos_cc -mmacosx-version-min={MACOS_MINIMUM} -o $out $in -framework Metal -framework Foundation",
               description="MACOS LINK $out")
        n.build(outputs=tool, rule="macos_tool_link",
                inputs=[*tool_objects, *(obj for obj in shared_objects if obj.name in (
                    "xgpu_msl.c.o", "xgpu_text.c.o", "xgpu_metal_shaders.c.o", "xgpu_metal_combiner.c.o")),
                        host_obj_dir / "xgpu_metal_pipeline.m.o", host_obj_dir / "xgpu_metal_post_shaders.c.o"])
    # the app's shaders, made ahead of time (tools/macos_shaders.py)
    library = build / "shaders.metallib"
    n.rule(name=f"{name}_shaders",
           command=(f"$python tools/macos_shaders.py --platform {target.shader_platform} --tool {tool} "
                    f"--manifest {RENDER_MANIFEST} --work {build / 'shaders'} --library $out > {build}/shaders.log"),
           description=f"{label} SHADERS $out")
    n.build(outputs=library, rule=f"{name}_shaders", inputs=[tool],
            implicit=[Path("tools/macos_shaders.py"), Path("tools/render_manifest.py"),
                      Path("source/rasterizer/xbox/rasterizer_xbox_vertex_shaders.c"),
                      Path("source/rasterizer/xbox/rasterizer_xbox_vertex_shaders_initialize.c"),
                      Path("source/rasterizer/xbox/rasterizer_xbox_vertex_shaders_data.inc")] +
                     ([RENDER_MANIFEST] if RENDER_MANIFEST.exists() else []))
    miniupnpc_cflags = " ".join([host_cflags, f"-I{MINIUPNPC_DIR / 'include'}", f"-I{MINIUPNPC_DIR / 'src'}",
                                 *MINIUPNPC_DEFINES])
    for source in [LINUX_DIR / "src" / "posix_upnp.c", *miniupnpc_sources()]:
        obj = host_obj_dir / ("miniupnpc_" + source.name + ".o" if source.parent.parent == MINIUPNPC_DIR
                              else source.name + ".o")
        n.build(outputs=obj, rule=f"{name}_host_cc", inputs=source,
                variables={"cflags": miniupnpc_cflags + (" -w" if source.name != "posix_upnp.c" else "")})
        host_objects.append(obj)
    # the self-updater's download (posix_update.c), with
    # port/third_party/mbedtls
    mbedtls_cflags = " ".join([host_cflags, f"-I{MBEDTLS_DIR / 'include'}"])
    n.build(outputs=host_obj_dir / "posix_update.c.o", rule=f"{name}_host_cc",
            inputs=LINUX_DIR / "src" / "posix_update.c", variables={"cflags": mbedtls_cflags})
    host_objects.append(host_obj_dir / "posix_update.c.o")
    for source in sorted((MBEDTLS_DIR / "library").glob("*.c")):
        obj = host_obj_dir / "mbedtls" / (source.name + ".o")
        n.build(outputs=obj, rule=f"{name}_host_cc", inputs=source,
                variables={"cflags": f"{mbedtls_cflags} -I{MBEDTLS_DIR / 'library'} -w"})
        host_objects.append(obj)
    table_obj = host_obj_dir / "host_import_table.c.o"
    n.build(outputs=table_obj, rule=f"{name}_host_cc", inputs=guest["host_table_c"],
            variables={"cflags": host_cflags})
    host_objects.append(table_obj)
    # the guest image, inside the executable (host_main.c), assembled
    # without the compile cache: ccache hashes guest_image.S, not the image
    # its .incbin reads, and gave back an object with an old guest (the
    # releases of 20 to 22 October drew nothing: their guest's render
    # stream was of an older version than the host's)
    n.rule(name=f"{name}_host_cc_uncached", command=f"${name}_cc -MMD -MF $out.d $cflags -c $in -o $out",
           description=f"{label} HOST CC $out", depfile="$out.d", deps="gcc")
    image_obj = host_obj_dir / "guest_image.S.o"
    n.build(outputs=image_obj, rule=f"{name}_host_cc_uncached", inputs=target.guest_image_source,
            implicit=[guest["image"], guest["checked"]],
            variables={"cflags": f'{target.target_flags} -DGUEST_IMAGE=\\"{guest["image"]}\\"'})
    host_objects.append(image_obj)

    output = build / "halo"
    n.rule(
        name=f"{name}_host_link",
        command=(f"${name}_cc {target.target_flags} -o $out $in -L{sdl_build} {target.sdl_link} "
                 # (C++'s library: astc-encoder's)
                 f"{target.frameworks} -lc++ {target.link_flags}"),
        description=f"{label} LINK $out",
    )
    n.build(outputs=output, rule=f"{name}_host_link", inputs=host_objects, implicit=[libsdl])
    n.rule(name=f"{name}_copy", command="cp $in $out", description=f"{label} STAGE $out")
    # internet play's MQTT brokers, a file beside the game (network.brokers_file)
    brokers = build / "brokers.txt"
    n.build(outputs=brokers, rule=f"{name}_copy", inputs=Path("port/assets/network/brokers.txt"))
    # ... and the stock maps' preparations' records (port/linux/src/prepared_shipped.h)
    records = build / "prepared.records"
    n.build(outputs=records, rule=f"{name}_copy", inputs=Path("port/assets/prepared/prepared.records"))
    # ... and the textures', fonts' and menus' bytes (port/linux/src/embedded_assets.h)
    assets = build / "openh1.assets"
    n.build(outputs=assets, rule=f"{name}_copy", inputs=guest["assets"])
    result = {"sparkle": sparkle, "guest": guest, "executable": output, "sdl": libsdl, "library": library, "brokers": brokers,
              "records": records, "assets": assets,
              "metal_objects": metal_objects, "shared_objects": shared_objects, "host_obj_dir": host_obj_dir, "objc_cflags": objc_cflags,
              "host_cflags": host_cflags}
    if name == "macos":
        _generate_macos_app(n, sln, result)
    return result


def _generate_macos_app(n: Writer, sln: Any, built: Dict[str, Any]) -> None:
    """the macOS app, its disk image, the Metal backend's tests and the
    command stream's replay"""
    output, libsdl, library, brokers = built["executable"], built["sdl"], built["library"], built["brokers"]
    records, assets = built["records"], built["assets"]
    metal_objects, shared_objects, host_obj_dir = built["metal_objects"], built["shared_objects"], built["host_obj_dir"]
    staged_sdl = BUILD / "libSDL3.0.dylib"
    n.build(outputs=staged_sdl, rule="macos_copy", inputs=libsdl)

    # ---------- the app (tools/macos_app.py), and its disk image

    app = BUILD / "OpenH1.app"
    app_executable = app / "Contents" / "MacOS" / "halo"
    icon = PORT_DIR / "openh1-icon.icns"
    build_number = os.environ.get("HALO_BUILD_NUMBER", "0")
    bundle_id = getattr(sln, "macos_bundle_id", None) or MACOS_BUNDLE_ID
    # the licences the app carries (Contents/Resources/Licenses): OpenH1's,
    # the AGPL; those of what every build carries (tools/linux_package.py);
    # and the SDL3 built here
    sparkle = built["sparkle"]
    licenses = [(Path("LICENSE"), "LICENSE.txt"),
                *((source.relative_to(REPO_ROOT), name) for source, name in release_notices("macos")),
                (BUILD / "third_party/SDL3/LICENSE.txt", "SDL3-LICENSE.txt"),
                (sparkle / "LICENSE", "Sparkle-LICENSE.txt")]
    # Sparkle's appcast and key: a release's alone (a numbered build of
    # CI's, release flavour; the debug builds and local ones never look)
    release = getattr(sln, "port_release", False)
    sparkle_key = os.environ.get("SPARKLE_PUBLIC_KEY", "").strip()
    sparkle_options = f"--sparkle {sparkle / 'Sparkle.framework'}"
    if build_number.isdigit() and int(build_number) > 0 and release and sparkle_key:
        sparkle_options += f" --sparkle-feed {SPARKLE_FEED} --sparkle-key {sparkle_key}"
    n.rule(
        name="macos_app",
        command=(f"$python tools/macos_app.py bundle --app {app} --executable {output} --sdl {libsdl} "
                 f"--icon {icon} --resource {brokers} --resource {records} --resource {assets} --resource {library} --bundle-id {bundle_id} {sparkle_options} "
                 f"{' '.join(f'--license {source}={name}' for source, name in licenses)} "
                 f"--build {build_number if build_number.isdigit() else 0} "
                 f"--flavor {'release' if release else 'debug'} "
                 f"--minimum {MACOS_MINIMUM} > {BUILD}/app.log"),
        description=f"MACOS APP {app}",
    )
    n.build(outputs=app_executable, rule="macos_app", inputs=[output, libsdl, brokers, records, assets, library],
            implicit=[icon, PORT_DIR / "halo.entitlements", Path("tools/macos_app.py"),
                      *(source for source, _ in licenses)])
    n.build(outputs="macos_app", rule="phony", inputs=[app_executable])
    dmg = BUILD / "OpenH1.dmg"
    n.rule(name="macos_dmg", command=f"$python tools/macos_app.py dmg --app {app} --output $out > {BUILD}/dmg.log",
           description=f"MACOS DMG $out")
    n.build(outputs=dmg, rule="macos_dmg", inputs=[app_executable])
    n.build(outputs="macos_dmg", rule="phony", inputs=[dmg])
    n.build(outputs="macos", rule="phony", inputs=[output, staged_sdl, brokers, records, assets, library, app_executable])

    # ---------- the Metal backend's tests (python3 tools/test_macos_metal.py)

    objc_cflags = built["objc_cflags"]
    test_obj = host_obj_dir / "tests" / "xgpu_metal_test.m.o"
    n.build(outputs=test_obj, rule="macos_host_cc", inputs=PORT_DIR / "tests" / "xgpu_metal_test.m",
            variables={"cflags": objc_cflags})
    test = BUILD / "xgpu_metal_test"
    # (the textures' cache names what it reads to the render stream's recording)
    record_obj = host_obj_dir / "host_render_record.c.o"
    n.build(outputs=test, rule="macos_host_link", inputs=[test_obj, record_obj, *metal_objects, *shared_objects],
            implicit=[libsdl])
    n.build(outputs="macos_metal_test", rule="phony", inputs=[test, staged_sdl])

    # ---------- the command stream's replay (port/macos/tools/halo_replay.m)

    # (with the replays' shared part, port/tools/replay)
    replay_cflags = f"-I{REPLAY_DIR} -DHALO_REPLAY_BUILD=\\\"{replay_build_label()}\\\""
    replay_obj = host_obj_dir / "tools" / "halo_replay.m.o"
    n.build(outputs=replay_obj, rule="macos_host_cc", inputs=PORT_DIR / "tools" / "halo_replay.m",
            variables={"cflags": f"{objc_cflags} {replay_cflags}"})
    replay_shared_obj = host_obj_dir / "tools" / "host_render_replay.c.o"
    n.build(outputs=replay_shared_obj, rule="macos_host_cc", inputs=REPLAY_DIR / "host_render_replay.c",
            variables={"cflags": f"{built['host_cflags']} {replay_cflags}"})
    replay_host = [host_obj_dir / "host_render_stream.c.o", host_obj_dir / "host_screenshot.c.o", record_obj,
                   replay_shared_obj]
    replay = BUILD / "halo_replay"
    n.build(outputs=replay, rule="macos_host_link", inputs=[replay_obj, *replay_host, *metal_objects, *shared_objects],
            implicit=[libsdl])
    # (and the shaders' library it plays with, as the app does: halo_replay copies the one beside it into the app
    # it runs, and one left from an older build drew the high-res text's distance fields as colours)
    n.build(outputs="macos_replay", rule="phony", inputs=[replay, staged_sdl, library])
    n.newline()
