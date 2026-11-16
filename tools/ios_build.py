"""Ninja rules for the iOS and iPadOS build (``ninja ios``), on a Mac with
Apple silicon and Xcode's iOS SDK.

The macOS build's pieces (tools/macos_build.py, which this shares) built
for iOS: the guest image is the macOS guest (ARM64_32 with the base
register, the Metal renderer in the host through the command stream) with
the iOS code paths (HALO_IOS) beside the Mac's (HALO_MACOS), and the host
is the macOS host (port/android/host, port/linux/host, port/macos) with
UIKit's app around it (port/ios). Refer to port/ios/README.md.

iOS gives apps no memory that was writable and is executable, so the guest's
code is not loaded as on the other builds: the image is in the executable's
__TEXT (port/ios/guest_image.S), and the loader maps its code from the
signed executable at the guest's addresses (port/android/host/host_loader.c).

- build/ios/halo: the executable, arm64 (iOS 17.4 and later), with SDL3
  linked in (a static library built from source, without OpenGL);
- build/ios/OpenH1.app: the app (tools/ios_app.py), signed for development
  with configure.py --ios-team's provisioning profile (tools/ios_provision.py
  makes it with Xcode's automatic signing), else ad hoc, which a device
  refuses but which sideloading tools sign again;
- ninja ios_install: the app installed on the connected device (devicectl).
"""

import os
import subprocess
from pathlib import Path
from typing import Any, List

from .linux_package import ROOT as REPO_ROOT, notices as release_notices
from .macos_build import MACOS_BUNDLE_ID, ApplePlatform, generate_apple_build, is_macos_arm64
from .ninja_syntax import Writer

PORT_DIR = Path("port/ios")
BUILD = Path("build/ios")
SIMULATOR_BUILD = Path("build/ios_sim")
# the Android app's touch controls' pictures and font, which the iOS app's
# overlay (port/ios/touch_controls.m) draws with too
TOUCH_ART = Path("port/android/app/src/main/res/drawable-nodpi")
TOUCH_FONT = Path("port/android/app/src/main/res/font/touch_caption.ttf")
THIRD_PARTY = BUILD / "third_party"
# iOS 17.4: os_sync_wait_on_address (the guest's futexes, port/macos/host_syscall.c);
# CAMetalDisplayLink is 17.0
IOS_MINIMUM = "17.4"
# the macOS app's (configure.py --ios-bundle-id)
IOS_BUNDLE_ID = MACOS_BUNDLE_ID

# the host's frameworks: Metal and its layer, UIKit, and SDL's (its audio,
# controllers, haptics, motion and Bluetooth controllers)
IOS_FRAMEWORKS = " ".join(f"-framework {name}" for name in (
    "Metal", "QuartzCore", "Foundation", "UIKit", "ImageIO", "CoreGraphics", "AVFoundation", "AudioToolbox",
    "CoreAudio", "CoreHaptics", "CoreMotion", "GameController", "CoreBluetooth", "UniformTypeIdentifiers",
    "CoreFoundation", "Security", "CoreMedia", "CoreVideo", "IOSurface", "CoreText")) + " -lcompression -lz"


def ios_configure_inputs() -> List[Path]:
    return [Path(__file__), PORT_DIR]


def ios_sdk(name: str = "iphoneos") -> str:
    """the iOS SDK's folder (or the simulator's, iphonesimulator), or '' without one"""
    try:
        return subprocess.run(["xcrun", "--sdk", name, "--show-sdk-path"], capture_output=True, text=True,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def generate_ios_build(n: Writer, sln: Any) -> None:
    if not PORT_DIR.is_dir():
        return
    if not is_macos_arm64():
        n.comment("iOS build: only on a Mac with Apple silicon")
        return
    sdk = ios_sdk()
    if not sdk:
        n.comment("iOS build: needs Xcode's iOS SDK")
        return
    built = generate_apple_build(n, sln, ios_platform("ios", sdk))
    if not built:
        return
    generate_ios_app(n, sln, built, "ios", BUILD, signed=True)
    # the simulator's (Apple silicon's, arm64): its own host and SDL, the
    # device's guest image
    simulator_sdk = ios_sdk("iphonesimulator")
    if simulator_sdk:
        simulator = generate_apple_build(n, sln, ios_platform("ios_sim", simulator_sdk, built["guest"]))
        if simulator:
            generate_ios_app(n, sln, simulator, "ios_sim", SIMULATOR_BUILD, signed=False)


def ios_platform(name: str, sdk: str, guest=None) -> ApplePlatform:
    """the device's build (ios), or the simulator's (ios_sim) with the
    device's guest"""
    simulator = name == "ios_sim"
    return ApplePlatform(
        name=name, label="IOS SIM" if simulator else "IOS", build=SIMULATOR_BUILD if simulator else BUILD,
        third_party=THIRD_PARTY, guest_defines=["-DHALO_IOS=1"],
        target_flags=f"-target arm64-apple-ios{IOS_MINIMUM}{'-simulator' if simulator else ''} -isysroot {sdk}",
        frameworks=IOS_FRAMEWORKS,
        # (port/macos/host_app.m is the Mac's: port/ios/host_app.m stands in)
        host_excluded={"host_app.m"},
        host_sources=[*sorted(source for source in [*PORT_DIR.glob("*.m"), *PORT_DIR.glob("*.c")]
                              if not source.name.startswith("guest_")),
                      # (the touch controls' state, which the overlay,
                      # touch_controls.m, shares with Android's)
                      Path("port/android/host/host_touch.c")],
        sdl_options=(f"-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT={'iphonesimulator' if simulator else 'iphoneos'} "
                     f"-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET={IOS_MINIMUM} "
                     f"-DSDL_SHARED=OFF -DSDL_STATIC=ON"),
        sdl_library="libSDL3.a", sdl_link="-lSDL3", guest_image_source=PORT_DIR / "guest_image.S",
        shader_platform="ios_simulator" if simulator else "ios", guest=guest,
        guest_imports=[PORT_DIR / "host_imports.list"],
        # (the first start's disc image extraction and the maps' unpack into the
        # cache: port/ios/data_import.m)
        shared_extra=["xiso.h", "xiso.c", "posix.h", "map_unpack.h", "map_unpack.c"])


def generate_ios_app(n: Writer, sln: Any, built: dict, name: str, build: Path, signed: bool) -> None:

    # ---------- the app (tools/ios_app.py)

    app = build / "OpenH1.app"
    app_executable = app / "halo"
    build_number = os.environ.get("HALO_BUILD_NUMBER", "0")
    bundle_id = getattr(sln, "ios_bundle_id", None) or IOS_BUNDLE_ID
    team = getattr(sln, "ios_team", None) if signed else None
    identity = getattr(sln, "ios_identity", None) or "Apple Development"
    licenses = [(Path("LICENSE"), "LICENSE.txt"),
                *((source.relative_to(REPO_ROOT), name) for source, name in release_notices("macos")),
                (THIRD_PARTY / "SDL3/LICENSE.txt", "SDL3-LICENSE.txt")]
    # the touch controls' pictures and their captions' font: the Android
    # app's (tools/touch_assets.py draws them), and Overpass's licence
    touch = [*sorted(TOUCH_ART.glob("touch_*.png")), TOUCH_FONT]
    licenses.append((Path("port/assets/fonts/Overpass-OFL.txt"), "Overpass-OFL.txt"))
    implicit = [PORT_DIR / "halo.entitlements", Path("tools/ios_app.py"), Path("tools/app_icon.py"),
                Path("port/ios/openh1-icon-1024.png"), *touch, *(source for source, _ in licenses)]
    signing = "--identity -"
    if team:
        # the development profile (tools/ios_provision.py: Xcode's automatic
        # signing), made once and again when the entitlements change
        profile = BUILD / "embedded.mobileprovision"
        n.rule(name="ios_provision",
               command=f"$python tools/ios_provision.py --team {team} --bundle-id {bundle_id} > {BUILD}/provision.log",
               description=f"IOS PROVISION {profile}")
        n.build(outputs=profile, rule="ios_provision",
                implicit=[PORT_DIR / "halo.entitlements", Path("tools/ios_provision.py")])
        signing = f'--identity "{identity}" --team {team} --profile {profile}'
        implicit.append(profile)
    n.rule(
        name=f"{name}_app",
        command=(f"$python tools/ios_app.py bundle --app {app} --executable {built['executable']} "
                 f"--resource {built['brokers']} --resource {built['records']} --resource {built['assets']} "
                 f"--resource {built['library']} "
                 f"--bundle-id {bundle_id} "
                 f"{' '.join(f'--license {source}={name}' for source, name in licenses)} "
                 f"{' '.join(f'--touch {source}' for source in touch)} "
                 f"--build {build_number if build_number.isdigit() else 0} --minimum {IOS_MINIMUM} {signing} "
                 f"{'--simulator ' if not signed else ''}> {build}/app.log"),
        description=f"{name.upper()} APP {app}",
    )
    n.build(outputs=app_executable, rule=f"{name}_app",
            inputs=[built["executable"], built["brokers"], built["records"], built["assets"],
                    built["library"]],
            implicit=implicit)
    n.build(outputs=name, rule="phony", inputs=[app_executable])
    # (the app on the connected device, or the booted simulator:
    # tools/ios_app.py install)
    n.rule(name=f"{name}_install",
           command=f"$python tools/ios_app.py install --app {app}{' --simulator' if not signed else ''}",
           description=f"{name.upper()} INSTALL", pool="console")
    n.build(outputs=f"{name}_install", rule=f"{name}_install", inputs=[app_executable])
    n.newline()
