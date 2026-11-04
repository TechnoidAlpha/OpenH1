#!/usr/bin/env python3

# root build script: writes build.ninja for the ports (Linux, Windows,
# Android, macOS, iOS)

import argparse
import io
import os
import sys
from pathlib import Path
from types import SimpleNamespace

from tools import ninja_syntax
from tools.android_build import android_configure_inputs, generate_android_build
from tools.build_common import check_profile_options, common_configure_inputs, generate_msvc_semantics
from tools.guest_toolchain import LLVM_VERSION, default_cache
from tools.ios_build import generate_ios_build, ios_configure_inputs
from tools.linux_guest_build import generate_linux_guest_builds, linux_guest_configure_inputs, native_linux_guest
from tools.macos_build import generate_macos_build, is_macos_arm64, macos_configure_inputs
from tools.windows_build import generate_windows_build, windows_configure_inputs


def is_windows() -> bool:
    return os.name == "nt"


# arguments
parser = argparse.ArgumentParser()
parser.add_argument(
    "--compiler-launcher",
    metavar="BINARY",
    help="a program that runs each compile, such as ccache "
    "(links and the Android host's few units run the compiler directly)",
)
parser.add_argument(
    "--release",
    action="store_true",
    help="release builds (Linux, Windows, Android): assertions are not checked",
)
parser.add_argument(
    "--profile",
    action="store_true",
    help="profiling builds (Linux, Windows, Android): CPU scopes recorded on a "
    "console command or launch setting (README, \"Profiling builds\")",
)
parser.add_argument(
    "--pgo",
    default="use",
    choices=["use", "off", "generate"],
    help="profile-guided optimisation: use (the default) optimises every build (they are all guest builds) with "
    "the committed profile, pgo/halo_linux.profdata; off does without; generate instruments the guests to record "
    "a profile (README, \"Optimization profiles\")",
)
parser.add_argument(
    "--pgo-profile",
    metavar="PROFDATA",
    type=Path,
    help="profile-guided optimisation from this profile instead",
)
parser.add_argument(
    "--guest-llvm",
    metavar="pinned|system|FOLDER",
    default="pinned",
    help="the guests' compiler, linker and LLVM tools (the Android, Linux arm64 and x86-64, and macOS builds' "
    f"guest images): pinned (the default) downloads LLVM {LLVM_VERSION}'s release for this computer once (about "
    "1.5 GB, checked against its SHA-256) into --guest-llvm-cache; system takes clang, ld.lld, llvm-ar and "
    "llvm-objdump from the PATH (and the NDK's, or Homebrew's on macOS); a FOLDER is an LLVM installation (its "
    "bin/). A build's own compiler (--linux-arm64-cc, --linux-x86-64-cc, --android-guest-cc, --macos-cc, "
    "--macos-guest-cc) is used instead. The hosts are built with the system's compiler",
)
parser.add_argument(
    "--guest-llvm-cache",
    metavar="FOLDER",
    help=f"where --guest-llvm=pinned keeps LLVM (default: {default_cache()})",
)
parser.add_argument(
    "--guest-asm-cache",
    metavar="FOLDER",
    help="keep the arm64_32 guests' objects (Android, Linux arm64, macOS) in FOLDER, each found again by its "
    "Darwin assembly, so that a build whose compile cache gives that back skips its conversion and assembly "
    "(tools/guest_asm_cache.py; for CI, beside ccache)",
)
parser.add_argument(
    "--android-ndk",
    type=str,
    help="Android NDK for `ninja android` (default: ANDROID_NDK_HOME, or the newest under the Android SDK)",
)
parser.add_argument(
    "--android-guest-cc",
    type=str,
    help="clang with the arm64_32 target for the Android guest (default: --guest-llvm's)",
)
parser.add_argument(
    "--linux-arm64-cc",
    type=str,
    help="clang (with the arm64_32 target) for `ninja linux_arm64`, the 64-bit ARM Linux build, its guest and its "
    "host (default: --guest-llvm's for the guest, clang for the host)",
)
parser.add_argument(
    "--linux-x86-64-cc",
    type=str,
    help="clang (with the x32 target) for `ninja linux_x86_64`, the 64-bit x86 Linux build, its guest and its host "
    "(default: --guest-llvm's for the guest, clang for the host)",
)
parser.add_argument(
    "--vr",
    action="store_true",
    help="the VR mode (OpenXR) in the Linux build of this computer (`ninja linux_x86_64`, or `ninja linux_arm64`: "
    "the Steam Frame's); configure.py also downloads the OpenXR headers; refer to \"VR\" in port/linux/README.md",
)
parser.add_argument(
    "--windows-guest",
    action="store_true",
    help="generate `ninja windows_guest`, the Windows build's guest image, on another computer than Windows "
    "(which always generates it, with `ninja windows`): configure.py downloads its musl, SDL3 and headers (once, to "
    "build/windows/third_party); refer to port/windows/README.md",
)
parser.add_argument(
    "--guest-base",
    action="store_true",
    help="the Linux arm64 and Android builds' guest memory at a base the host chooses (a base register, "
    "tools/guest_base_register.py) instead of the fixed low 4 GB; the macOS build always has it",
)
parser.add_argument(
    "--macos-cc",
    type=str,
    help="clang for `ninja macos`, the macOS build: its host, and its guest unless --macos-guest-cc (default: clang "
    "for the host, --guest-llvm's for the guest)",
)
parser.add_argument(
    "--macos-guest-cc",
    type=str,
    help="clang (with the arm64_32 target) for the macOS build's guest alone (default: --macos-cc's, else "
    "--guest-llvm's)",
)
parser.add_argument(
    "--macos-bundle-id",
    type=str,
    help="the macOS app's bundle identifier (default: com.openh1.openh1, after the "
    "project's domain); macOS keeps the app's permissions (the microphone, the local network) by it",
)
parser.add_argument(
    "--ios-team",
    metavar="TEAM",
    help="the Apple developer team that signs `ninja ios`'s app for development (its ID, such as 5NH5B2VG6Y): "
    "its provisioning profile is made with Xcode's automatic signing (tools/ios_provision.py; Xcode signed in to "
    "the team's account). Without it the app is signed ad hoc, for sideloading tools to sign again",
)
parser.add_argument(
    "--ios-identity",
    metavar="CERTIFICATE",
    help="the certificate that signs the iOS app with --ios-team (default: \"Apple Development\", the keychain's)",
)
parser.add_argument(
    "--ios-bundle-id",
    type=str,
    help="the iOS app's bundle identifier (default: the macOS app's, com.openh1.openh1)",
)

# no need to spam those extra flags when running on Windows
if not is_windows():
    parser.add_argument(
        "--windows-cross",
        action="store_true",
        help="generate the Windows build (`ninja windows`) on Linux, cross-compiling "
        "with clang against an MSVC toolchain (--windows-vctools-dir and --windows-sdk-dir), "
        "and make it the default target",
    )
    parser.add_argument(
        "--windows-cc",
        metavar="BINARY",
        help="compiler for the Windows cross build, `ninja windows` (default: clang). "
             "Only clang is supported for now.",
    )
    parser.add_argument(
        "--windows-vctools-dir",
        metavar="DIR",
        type=Path,
        help="the Visual C++ tools of the MSVC toolchain a --windows-cross build compiles "
        "and links against: the versioned folder under VC/Tools/MSVC, the one with `include` "
        "and `lib` (default: VCToolsInstallDir)",
    )
    parser.add_argument(
        "--windows-sdk-dir",
        metavar="DIR",
        type=Path,
        help="the Windows SDK a --windows-cross build compiles and links against: the "
        "folder with `Include` and `Lib`, each with a versioned folder (default: WindowsSdkDir)",
    )

args = parser.parse_args()
try:
    check_profile_options(args.profile, args.pgo)
except ValueError as error:
    parser.error(str(error))

# the settings the builds read
sln = SimpleNamespace(
    build_dir=Path("build"),
    compiler_launcher=args.compiler_launcher,
    port_release=args.release,
    port_profile=args.profile,
    port_pgo=args.pgo,
    port_pgo_profile=args.pgo_profile,
    android_ndk=args.android_ndk,
    guest_llvm=args.guest_llvm,
    guest_llvm_cache=args.guest_llvm_cache,
    guest_asm_cache=args.guest_asm_cache,
    android_guest_cc=args.android_guest_cc,
    linux_arm64_cc=args.linux_arm64_cc,
    linux_x86_64_cc=args.linux_x86_64_cc,
    windows_guest=args.windows_guest,
    port_vr=args.vr,
    macos_cc=args.macos_cc,
    macos_guest_cc=args.macos_guest_cc,
    macos_bundle_id=args.macos_bundle_id,
    ios_team=args.ios_team,
    ios_identity=args.ios_identity,
    ios_bundle_id=args.ios_bundle_id,
    guest_base=args.guest_base,
)
if not is_windows():
    sln.windows_cc = args.windows_cc
    sln.windows_cross = args.windows_cross
    sln.windows_vctools_dir = args.windows_vctools_dir
    sln.windows_sdk_dir = args.windows_sdk_dir


# build.ninja
out = io.StringIO()
n = ninja_syntax.Writer(out)
n.variable("ninja_required_version", "1.3")
n.newline()

configure_script = Path(os.path.relpath(os.path.abspath(sys.argv[0])))
n.comment("The arguments passed to configure.py, for rerunning it.")
n.variable(
    "configure_args",
    [f'"{arg}"' if any(ch.isspace() for ch in arg) else arg for arg in sys.argv[1:]],
)
n.variable("python", f'"{sys.executable}"')
n.newline()

generate_msvc_semantics(n)
generate_android_build(n, sln)
generate_linux_guest_builds(n, sln)
generate_windows_build(n, sln)
generate_macos_build(n, sln)
generate_ios_build(n, sln)

n.comment("Reconfigure on change")
n.rule(
    name="configure",
    command=f"$python {configure_script} $configure_args",
    generator=True,
    description=f"RUN {configure_script}",
)
n.build(
    outputs="build.ninja",
    rule="configure",
    implicit=[
        configure_script,
        Path("tools/ninja_syntax.py"),
        *common_configure_inputs(),
        *android_configure_inputs(),
        *linux_guest_configure_inputs(),
        *windows_configure_inputs(),
        *macos_configure_inputs(),
        *ios_configure_inputs(),
    ],
)
n.newline()

# the build for this computer, where it could be generated (the Windows
# build is left out when SDL cannot be fetched, for instance): on 64-bit x86
# or ARM Linux its Linux build (linux_x86_64, linux_arm64); on a Mac with
# Apple silicon the macOS build
native_guest = native_linux_guest()
default = ("windows" if is_windows() or getattr(sln, "windows_cross", False) else native_guest.name if native_guest
           else "macos" if is_macos_arm64() else None)
if default and f"\nbuild {default}: " in out.getvalue():
    n.comment("Default rule: the build for this computer")
    n.default(default)

with open("build.ninja", "w", encoding="utf-8") as f:
    f.write(out.getvalue())
out.close()
