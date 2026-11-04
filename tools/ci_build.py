#!/usr/bin/env python3
"""Builds one port in one configuration, as the GitHub workflow does
(.github/workflows/build.yml), and collects what it built into dist/:

    python tools/ci_build.py linux_x86_64 debug    (on a 64-bit x86 Linux computer)
    python tools/ci_build.py linux_x86_64 release --vr
    python tools/ci_build.py linux_x86_64 profile
    python tools/ci_build.py linux_arm64 release   (on a 64-bit ARM Linux computer)
    python tools/ci_build.py linux_arm64 release --vr
    python tools/ci_build.py android release
    python tools/ci_build.py windows release       (on Windows)
    python tools/ci_build.py macos release         (on a Mac with Apple silicon)
    python tools/ci_build.py ios debug             (on a Mac with Apple silicon and Xcode's iOS SDK)
    python tools/ci_build.py notes                 (the release notes' network versions, below)

Builds are portable (any x86-64 processor; any ARMv8.0 one for
linux_arm64 and android), so they run on other computers. Debug builds skip link-time and profile-guided optimisation,
which only make the build slower; release builds use both, as a local
release build does (profile-guided optimisation needs clang 22 or later,
and is skipped with an older one; the guests' compiler is the pinned LLVM
22 that configure.py downloads, tools/guest_toolchain.py). A profile build is a debug build with
configure.py --profile, so that the profiling build is built too. CI_COMPILER_LAUNCHER (ccache, say) is
passed on as --compiler-launcher, CI_GUEST_LLVM_CACHE as --guest-llvm-cache, and CI_GUEST_ASM_CACHE as
--guest-asm-cache. A build of the master branch gets the run's
number (HALO_BUILD_NUMBER), which its release is named after and the
self-updater compares.

--vr builds the VR mode in (configure.py --vr; "VR" in port/linux/README.md).
For linux_x86_64, into dist/halo-linux-x86_64-vr-<config>/, a build of its
own, which updates to the release's VR build. For linux_arm64, the Steam
Frame's package (tools/frame_package.py, which its install.sh,
tools/linux_install.sh, installs, and which updates itself to the
release's: updater.c's UPDATER_PACKAGE), zipped as
dist/halo-steam-frame-vr-<config>/halo-steam-frame-vr-<config>.zip, which
the release has as it is.

A Linux build is the release's tree (tools/linux_package.py: bin/openh1,
lib/openh1, share/...), dist/halo-linux-x86_64[-vr]-<config>/ or
dist/halo-linux-arm64-<config>/, named as its self-updater asks
(port/linux/src/updater.c, UPDATE_PLATFORM).

The macOS build is the app's disk image (ninja macos_dmg), signed ad hoc
(tools/macos_app.py), alone in dist/halo-macos-<config>/ (the app carries
the licences): the disk image keeps the app's modes and links, which the
workflow's artifacts would lose, and the release has it as it is,
OpenH1.dmg (OpenH1-debug.dmg).

notes prints the lines the release's notes carry for a dedicated server's
self-updater (port/linux/src/updater.c, updater_server_*), which compares
them with its own to know whether its players can join the new build:

    Network version: <HALO_PORT_NETWORK_VERSION>
    Standard network version: <HALO_PORT_STANDARD_NETWORK_VERSION>

The iOS build is the app (ninja ios), signed ad hoc (tools/ios_app.py: a
device refuses it, a sideloading tool such as AltStore signs it again), as
an .ipa (Payload/OpenH1.app, zipped), alone in dist/halo-ios-<config>/. The
release does not publish it yet (port/ios/README.md).
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import linux_package

ROOT = Path(__file__).resolve().parent.parent

# what each port's build leaves, and what goes into dist/
OUTPUTS = {
    # (and the SDL3 the Linux builds bring, tools/linux_guest_build.py, with
    # its zlib license, as the release carries the other libraries'; laid
    # out by tools/linux_package.py)
    "linux_arm64": ["build/linux_arm64/halo", "build/linux_arm64/libSDL3.so.0",
                    "build/linux_arm64/SDL3-LICENSE.txt"],
    "linux_x86_64": ["build/linux_x86_64/halo", "build/linux_x86_64/libSDL3.so.0",
                     "build/linux_x86_64/SDL3-LICENSE.txt"],
    "windows": ["build/windows/halo.exe", "build/windows/SDL3.dll"],
    "macos": ["build/macos/OpenH1.dmg"],
    "ios": [],  # the .ipa, below
    "android": [],  # the APK, below
}
APKS = {
    "debug": "port/android/app/build/outputs/apk/debug/app-debug.apk",
    "release": "port/android/app/build/outputs/apk/release/app-release.apk",
}


def dist_name(platform: str, config: str, vr: bool = False) -> str:
    """the folder in dist/ (and the release's zip) of a build: halo-<platform>-
    <config>, where a Linux build's platform is the target's with its first
    underscore a hyphen, and -vr after it for the VR build (linux-x86_64,
    linux-x86_64-vr, linux-arm64: updater.c's UPDATE_PLATFORM); but the 64-bit
    ARM VR build is the Steam Frame's package, halo-steam-frame-vr-<config>
    (UPDATE_PLATFORM steam-frame-vr: it updates to that package)"""
    if platform == "linux_arm64" and vr:
        return f"halo-steam-frame-vr-{config}"
    if platform.startswith("linux"):
        platform = platform.replace("_", "-", 1) + ("-vr" if vr else "")
    return f"halo-{platform}-{config}"


def network_versions() -> dict:
    """the network versions of halo_port_limits.h: HALO_PORT_NETWORK_VERSION
    (OpenCE's and Legacy games') and HALO_PORT_STANDARD_NETWORK_VERSION
    (OpenH1's Standard games')"""
    text = (ROOT / "port/linux/include/halo_port_limits.h").read_text()
    versions = {}
    for name in ("HALO_PORT_NETWORK_VERSION", "HALO_PORT_STANDARD_NETWORK_VERSION"):
        match = re.search(rf"^#define {name} (\d+)\b", text, re.MULTILINE)
        if not match:
            sys.exit(f"halo_port_limits.h has no {name}")
        versions[name] = int(match.group(1))
    return versions


def release_notes_versions() -> str:
    """the release's notes' lines that updater.c's updater_latest_release
    reads (the last of each key)"""
    versions = network_versions()
    return (f"Network version: {versions['HALO_PORT_NETWORK_VERSION']}\n"
            f"Standard network version: {versions['HALO_PORT_STANDARD_NETWORK_VERSION']}\n")


def run(command, cwd=ROOT):
    print("+", " ".join(str(part) for part in command), flush=True)
    subprocess.run([str(part) for part in command], cwd=cwd, check=True)


def check_guest_image(platform: str) -> None:
    """A guest build's executables carry the guest image just linked
    (guest_image.S's .incbin): an executable with another guest, as a
    cached object once gave macOS's releases (a black window: the guest's
    render stream of another version than the host's), stops the build."""
    image = ROOT / "build" / platform / "halo_guest.elf"
    executables = [ROOT / "build" / platform / "halo"]
    if platform == "macos":
        executables.append(ROOT / "build/macos/OpenH1.app/Contents/MacOS/halo")
    if platform == "ios":
        executables.append(ROOT / "build/ios/OpenH1.app/halo")
    if platform == "windows":
        executables.append(ROOT / "build/windows/halo.exe")
    if not image.is_file():
        return
    data = image.read_bytes()
    for executable in executables:
        if executable.is_file() and data not in executable.read_bytes():
            sys.exit(f"{executable.relative_to(ROOT)} does not carry {image.relative_to(ROOT)}, the guest just linked")
        print(f"{executable.relative_to(ROOT)} carries {image.relative_to(ROOT)}", flush=True)


def main() -> int:
    if sys.argv[1:] == ["notes"]:
        sys.stdout.write(release_notes_versions())
        return 0
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("platform", choices=sorted(OUTPUTS))
    parser.add_argument("config", choices=["debug", "release", "profile"])
    parser.add_argument("--vr", action="store_true",
                        help="the VR build (linux_x86_64; linux_arm64: the Steam Frame's package)")
    args = parser.parse_args()
    if args.vr and args.platform not in ("linux_x86_64", "linux_arm64"):
        parser.error("--vr is for linux_x86_64 and linux_arm64 only")

    configure = [sys.executable, "configure.py"]
    if args.config == "release":
        configure.append("--release")
    else:
        configure.append("--pgo=off")
    if args.config == "profile":
        configure.append("--profile")
    if args.vr:
        configure.append("--vr")
    launcher = os.environ.get("CI_COMPILER_LAUNCHER")
    if launcher:
        configure += ["--compiler-launcher", launcher]
    # the folder the guests' pinned LLVM is kept in (tools/guest_toolchain.py),
    # which the workflow caches
    llvm_cache = os.environ.get("CI_GUEST_LLVM_CACHE")
    if llvm_cache:
        configure += ["--guest-llvm-cache", llvm_cache]
    # the folder the arm64_32 guests' converted and assembled objects are
    # kept in (tools/guest_asm_cache.py), which the workflow caches too
    asm_cache = os.environ.get("CI_GUEST_ASM_CACHE")
    if asm_cache:
        configure += ["--guest-asm-cache", asm_cache]
    # a build of master knows its number, which names its release (build-<n>),
    # for the self-updater (port/linux/src/updater.c, and the Android app);
    # other builds have none, and never look for updates
    if os.environ.get("GITHUB_REF") == "refs/heads/master" and os.environ.get("GITHUB_RUN_NUMBER", "").isdigit():
        os.environ["HALO_BUILD_NUMBER"] = os.environ["GITHUB_RUN_NUMBER"]
        print(f"build number {os.environ['HALO_BUILD_NUMBER']}", flush=True)
    run(configure)

    if args.platform == "ios":
        # the app as an .ipa: Payload/OpenH1.app, zipped with its modes
        # (ditto), which the workflow's artifacts would lose
        run(["ninja", "ios"])
        check_guest_image(args.platform)
        dist = ROOT / "dist" / f"halo-ios-{args.config}"
        if dist.exists():
            shutil.rmtree(dist)
        payload = dist / "Payload"
        payload.mkdir(parents=True)
        run(["ditto", ROOT / "build/ios/OpenH1.app", payload / "OpenH1.app"])
        run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", payload, dist / "OpenH1.ipa"])
        shutil.rmtree(payload)
        return 0
    if args.platform == "android":
        # the native part, then the app around it (Gradle's variant of the
        # same name: release is not debuggable; both are signed with the
        # workflow's key when it has one, port/android/app/build.gradle)
        run(["ninja", "android"])
        gradlew = "gradlew.bat" if os.name == "nt" else "./gradlew"
        variant = "release" if args.config == "release" else "debug"
        run([gradlew, "--console=plain", "-q", f"assemble{variant.capitalize()}"], cwd=ROOT / "port/android")
        outputs = [APKS[variant]]
    else:
        target = "macos_dmg" if args.platform == "macos" else args.platform
        run(["ninja", target])
        outputs = OUTPUTS[args.platform]
        check_guest_image(args.platform)

    if args.platform == "linux_arm64" and args.vr:
        # the Steam Frame's package (tools/frame_package.py: the game, its
        # SDL3, the launcher, the licences, VERSION and SHA256SUMS), zipped
        # with the files' modes, which the workflow's artifacts would lose:
        # the release has the zip as it is
        dist = ROOT / "dist" / dist_name(args.platform, args.config, args.vr)
        if dist.exists():
            shutil.rmtree(dist)
        run([sys.executable, "tools/frame_package.py", "--zip", dist / f"{dist.name}.zip"])
        return 0

    # the licences of what the build carries (tools/linux_package.py)
    notices = linux_package.notices(args.platform)

    if args.platform.startswith("linux"):
        # the release's tree (tools/linux_package.py; SDL's license with the
        # others), named as the self-updater asks (updater.c's
        # UPDATE_PLATFORM): halo-linux-x86_64-release,
        # halo-linux-x86_64-vr-release and halo-linux-arm64-release (the
        # target's first underscore only)
        executable, library, sdl_license = (ROOT / output for output in outputs)
        notices.append((sdl_license, sdl_license.name))
        dist = ROOT / "dist" / dist_name(args.platform, args.config, args.vr)
        if dist.exists():
            shutil.rmtree(dist)
        linux_package.stage_tree(dist, executable, library, notices, vr=args.vr)
        print(f"{', '.join(outputs)} -> {dist.relative_to(ROOT)}", flush=True)
        return 0

    dist = ROOT / "dist" / f"halo-{args.platform}-{args.config}"
    if dist.exists():
        shutil.rmtree(dist)
    dist.mkdir(parents=True)
    for output in outputs:
        shutil.copy2(ROOT / output, dist)
        print(f"{output} -> {dist.relative_to(ROOT)}", flush=True)
    if args.platform == "windows":
        # the symbols of halo.exe and SDL3.dll, and the guest image, apart
        # (players do not need them): the workflow uploads them to Sentry,
        # which turns the crash reports' minidumps into function names and
        # lines (port/windows/host/host_crash_win.c), and
        # tools/symbolize_crash.py reads halo.log's crash lines with them
        symbols = ROOT / "dist" / f"halo-windows-{args.config}-symbols"
        if symbols.exists():
            shutil.rmtree(symbols)
        symbols.mkdir(parents=True)
        for pdb in [ROOT / "build/windows/halo.pdb", ROOT / "build/windows/halo_guest.elf",
                    *sorted((ROOT / "build/windows/third_party").glob("SDL3-*/lib/x64/SDL3.pdb"))]:
            shutil.copy2(pdb, symbols)
            print(f"{pdb.relative_to(ROOT)} -> {symbols.relative_to(ROOT)}", flush=True)
    if args.platform in ("macos", "android"):
        # (the app carries the licences: the macOS app's
        # Contents/Resources/Licenses, tools/macos_build.py; the APK's
        # assets/licenses, tools/android_build.py), and the release publishes
        # it alone
        return 0
    # OpenH1's own licence, the AGPL, which asks each copy to carry its text
    # (the Linux tree's is in share/licenses: linux_package.py)
    shutil.copy2(ROOT / "LICENSE", dist / "LICENSE.txt")
    for source, name in notices:
        shutil.copy2(source, dist / name)
    # internet play's MQTT brokers, a file beside the game (network.brokers_file;
    # Android's APK and the macOS app have their own copies)
    shutil.copy2(ROOT / "port/assets/network/brokers.txt", dist / "brokers.txt")
    # the master servers that list dedicated servers, for the server browser
    # and the dedicated server (network.masters_file, server.masters_file:
    # docs/dedicated_server.md)
    shutil.copy2(ROOT / "port/assets/network/masters.txt", dist / "masters.txt")
    # ... and the relays a machine offers when a direct connection fails
    # (network.relays_file, server.relays_file: none in it)
    shutil.copy2(ROOT / "port/assets/network/relays.txt", dist / "relays.txt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
