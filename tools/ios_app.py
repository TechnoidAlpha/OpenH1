#!/usr/bin/env python3
"""The iOS app, OpenH1.app, and its signature (port/ios/README.md).

    python3 tools/ios_app.py bundle ...      # ninja ios does it: build/ios/OpenH1.app
    python3 tools/ios_app.py install         # ninja ios_install: onto the connected device
    python3 tools/ios_app.py launch          # start it there (its log: Documents/logs/halo.log)
    python3 tools/ios_app.py log             # the device's copy of the log, printed

- bundle: OpenH1.app from the build: the executable (halo), the icons
  (actool's, from port/ios/openh1-icon-1024.png), brokers.txt, the shaders'
  library, the licences in Licenses, Info.plist, and with --profile the
  provisioning profile (embedded.mobileprovision), signed with
  port/ios/halo.entitlements and what the profile grants (the application
  identifier, the team's, get-task-allow): with --identity's certificate
  (Apple Development) and --profile, or ad hoc ("-", without a profile),
  which a device refuses but sideloading tools sign again.
  With --simulator, the simulator's app (ninja ios_sim), signed ad hoc.
- install, launch, log: devicectl (Xcode 15 and later) on the connected
  device, or --device's; with --simulator, simctl on the booted simulator
  (headless: xcrun simctl boot <device>, without Simulator.app).
"""

import argparse
import json
import os
import plistlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENTITLEMENTS = ROOT / "port/ios/halo.entitlements"
ICON = ROOT / "port/ios/openh1-icon-1024.png"
APP_NAME = "OpenH1"
EXECUTABLE = "halo"
DEFAULT_APP = ROOT / "build/ios" / (APP_NAME + ".app")
DEFAULT_BUNDLE_ID = "com.openh1.openh1"
# the scheme of the game's links (port/linux/src/p2p.h's P2P_LINK_SCHEME), as the macOS app's
URL_SCHEME = "openh1"
# what the README says of the project (tools/macos_app.py's)
COPYRIGHT = ("OpenH1 is an unofficial fan project. It is not endorsed by or affiliated with Microsoft, Xbox "
             "Game Studios, Halo Studios or Bungie. Halo and Halo: Combat Evolved are trademarks of Microsoft "
             "Corporation. No game data is included: you need your own copy of the game.")


def run(*command: str, **arguments) -> subprocess.CompletedProcess:
    print("+", " ".join(command), flush=True)
    return subprocess.run(command, check=True, **arguments)


def info_plist(bundle_id: str, build: int, minimum: str, simulator: bool = False) -> dict:
    """the app's Info.plist. The version is the build's number (HALO_BUILD_NUMBER;
    0 for a local build), as the macOS app's."""
    version = str(build)
    return {
        "CFBundleDevelopmentRegion": "en",
        "CFBundleDisplayName": APP_NAME,
        "CFBundleExecutable": EXECUTABLE,
        "CFBundleIdentifier": bundle_id,
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": APP_NAME,
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": version,
        "CFBundleVersion": version,
        "CFBundleSupportedPlatforms": ["iPhoneSimulator" if simulator else "iPhoneOS"],
        "DTPlatformName": "iphonesimulator" if simulator else "iphoneos",
        "MinimumOSVersion": minimum,
        "LSRequiresIPhoneOS": True,
        "UIDeviceFamily": [1, 2],
        "UIRequiredDeviceCapabilities": ["arm64", "metal"],
        "LSApplicationCategoryType": "public.app-category.games",
        # (Game Mode while the game is fullscreen, as it always is here:
        # the processor and the GPU for the game, and Bluetooth's doubled
        # rate for AirPods and controllers. LSSupportsGameMode from iOS
        # 18.6, GCSupportsGameMode in 18.0 to 18.5, which it replaces)
        "LSSupportsGameMode": True,
        "GCSupportsGameMode": True,
        "NSHumanReadableCopyright": COPYRIGHT,
        # (a ProMotion iPhone's 120 Hz: without it iOS holds a phone's apps to
        # 60 Hz, though the screen reports 120 as its most; an iPad needs none)
        "CADisableMinimumFrameDurationOnPhone": True,
        # (the game fills the screen, sideways, without the status bar;
        # UIKit's scenes, which SDL's delegate takes)
        "UILaunchScreen": {"UIColorName": "LaunchBackground"},
        "UIRequiresFullScreen": True,
        "UIStatusBarHidden": True,
        "UIViewControllerBasedStatusBarAppearance": False,
        "UISupportedInterfaceOrientations": ["UIInterfaceOrientationLandscapeLeft",
                                             "UIInterfaceOrientationLandscapeRight"],
        "UISupportedInterfaceOrientations~ipad": ["UIInterfaceOrientationLandscapeLeft",
                                                  "UIInterfaceOrientationLandscapeRight"],
        "UIApplicationSceneManifest": {"UIApplicationSupportsMultipleScenes": False},
        "UIApplicationSupportsIndirectInputEvents": True,
        # (the game's files in Documents, which the Files app shows: the
        # maps, config.toml, the saves, the log)
        "UIFileSharingEnabled": True,
        "LSSupportsOpeningDocumentsInPlace": True,
        # (controllers: the game's menus take them, and the system's own
        # gestures leave them to it)
        "GCSupportsControllerUserInteraction": True,
        "GCSupportedGameControllers": [{"ProfileName": "ExtendedGamepad"}],
        "NSMicrophoneUsageDescription": "Voice chat in network games uses the microphone while you talk.",
        "NSLocalNetworkUsageDescription": "System link finds and plays games on your local network.",
        # (the handler of the game's links, openh1://join/... and
        # openh1://party/..., which SDL's scene delegate is given:
        # port/macos/host_links.c)
        "CFBundleURLTypes": [{"CFBundleURLName": bundle_id, "CFBundleTypeRole": "Viewer",
                              "CFBundleURLSchemes": [URL_SCHEME]}],
    }


def icons(app: Path, minimum: str, simulator: bool = False) -> dict:
    """the app's icons (Assets.car and the PNGs iOS reads), from the 1024
    pixel one, and the launch screen's colour, by actool; the Info.plist
    entries it names them with"""
    with tempfile.TemporaryDirectory() as temporary:
        catalog = Path(temporary) / "Assets.xcassets"
        icon_set = catalog / "AppIcon.appiconset"
        icon_set.mkdir(parents=True)
        shutil.copy2(ICON, icon_set / "icon.png")
        (catalog / "Contents.json").write_text(json.dumps({"info": {"author": "xcode", "version": 1}}))
        # (the launch screen's colour: black, as the game starts)
        launch = catalog / "LaunchBackground.colorset"
        launch.mkdir()
        (launch / "Contents.json").write_text(json.dumps({
            "colors": [{"idiom": "universal", "color": {"color-space": "srgb", "components": {
                "red": "0.000", "green": "0.000", "blue": "0.000", "alpha": "1.000"}}}],
            "info": {"author": "xcode", "version": 1}}))
        (icon_set / "Contents.json").write_text(json.dumps({
            "images": [{"filename": "icon.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"}],
            "info": {"author": "xcode", "version": 1}}))
        partial = Path(temporary) / "partial.plist"
        run("xcrun", "actool", "--compile", str(app), "--platform",
            "iphonesimulator" if simulator else "iphoneos", "--minimum-deployment-target",
            minimum, "--target-device", "iphone", "--target-device", "ipad", "--app-icon", "AppIcon",
            "--output-partial-info-plist", str(partial), "--output-format", "human-readable-text", str(catalog),
            stdout=subprocess.DEVNULL)
        with open(partial, "rb") as file:
            return plistlib.load(file)


def entitlements_for(profile: Path, team: str) -> dict:
    """port/ios/halo.entitlements with what the profile grants the app: its
    application identifier, the team's and get-task-allow (a development
    profile's: the debugger, and devicectl's launches)"""
    with open(ENTITLEMENTS, "rb") as file:
        entitlements = plistlib.load(file)
    if profile:
        data = subprocess.run(["security", "cms", "-D", "-i", str(profile)], capture_output=True, check=True).stdout
        granted = plistlib.loads(data)["Entitlements"]
        for key in ("application-identifier", "com.apple.developer.team-identifier", "get-task-allow"):
            if key in granted:
                entitlements[key] = granted[key]
        missing = [key for key in entitlements if key not in granted]
        if missing:
            sys.exit(f"the provisioning profile {profile} does not grant {', '.join(missing)}: make it again "
                     "(python3 tools/ios_provision.py)")
    return entitlements


def sign(app: Path, identity: str, profile: Path, team: str, simulator: bool = False) -> None:
    # (the simulator, a Mac process, refuses an ad hoc signature with the
    # developer entitlements, which only a profile grants: its address space
    # is a Mac's, larger than the guest needs)
    entitlements = {} if simulator else entitlements_for(profile, team)
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary) / "halo.entitlements"
        with open(path, "wb") as file:
            plistlib.dump(entitlements, file)
        run("codesign", "--force", "--sign", identity, "--timestamp=none", "--generate-entitlement-der",
            "--entitlements", str(path), str(app))
    run("codesign", "--verify", "--strict", str(app))


def bundle(args: argparse.Namespace) -> None:
    app = Path(args.app)
    staging = app.with_name(app.name + ".partial")
    shutil.rmtree(staging, ignore_errors=True)
    staging.mkdir(parents=True)
    shutil.copy2(args.executable, staging / EXECUTABLE)
    for resource in args.resource or []:
        shutil.copy2(resource, staging / Path(resource).name)
    # (the touch controls' pictures and captions' font, the Android app's:
    # port/ios/touch_controls.m reads them from touch/)
    for resource in args.touch or []:
        (staging / "touch").mkdir(exist_ok=True)
        shutil.copy2(resource, staging / "touch" / Path(resource).name)
    for license in args.license or []:
        source, name = license.split("=", 1)
        (staging / "Licenses").mkdir(exist_ok=True)
        shutil.copy2(source, staging / "Licenses" / name)
    plist = info_plist(args.bundle_id, args.build, args.minimum, args.simulator)
    plist.update(icons(staging, args.minimum, args.simulator))
    with open(staging / "Info.plist", "wb") as file:
        plistlib.dump(plist, file, fmt=plistlib.FMT_BINARY)
    (staging / "PkgInfo").write_text("APPL????")
    profile = Path(args.profile) if args.profile else None
    if profile:
        shutil.copy2(profile, staging / "embedded.mobileprovision")
    sign(staging, args.identity if profile else "-", profile, args.team, args.simulator)
    shutil.rmtree(app, ignore_errors=True)
    staging.rename(app)


def device_arguments(args: argparse.Namespace) -> list:
    """devicectl's --device: --device's, else the one connected device"""
    if args.device:
        return ["--device", args.device]
    with tempfile.TemporaryDirectory() as temporary:
        output = Path(temporary) / "devices.json"
        subprocess.run(["xcrun", "devicectl", "list", "devices", "--quiet", "--json-output", str(output)],
                       check=True, stdout=subprocess.DEVNULL)
        devices = json.loads(output.read_text())["result"]["devices"]
    connected = [device for device in devices if device.get("hardwareProperties", {}).get("reality") == "physical"
                 and device.get("connectionProperties", {}).get("pairingState") == "paired"]
    if len(connected) != 1:
        sys.exit(f"{len(connected)} paired devices: name one with --device <identifier>")
    return ["--device", connected[0]["hardwareProperties"]["udid"]]


def install(args: argparse.Namespace) -> None:
    if args.simulator:
        run("xcrun", "simctl", "install", args.device or "booted", str(args.app))
        return
    run("xcrun", "devicectl", "device", "install", "app", *device_arguments(args), str(args.app))


def launch(args: argparse.Namespace) -> None:
    if args.simulator:
        run("xcrun", "simctl", "launch", "--terminate-running-process", args.device or "booted", args.bundle_id)
        return
    run("xcrun", "devicectl", "device", "process", "launch", *device_arguments(args), "--terminate-existing",
        args.bundle_id)


def log(args: argparse.Namespace) -> None:
    if args.simulator:
        container = subprocess.run(["xcrun", "simctl", "get_app_container", args.device or "booted", args.bundle_id,
                                    "data"], capture_output=True, text=True, check=True).stdout.strip()
        sys.stdout.write((Path(container) / "Documents/logs" / args.name).read_text(errors="replace"))
        return
    with tempfile.TemporaryDirectory() as temporary:
        destination = Path(temporary) / "halo.log"
        run("xcrun", "devicectl", "device", "copy", "from", *device_arguments(args), "--domain-type",
            "appDataContainer", "--domain-identifier", args.bundle_id, "--source",
            f"Documents/logs/{args.name}", "--destination", str(destination), stdout=subprocess.DEVNULL)
        sys.stdout.write(destination.read_text(errors="replace"))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)

    command = commands.add_parser("bundle", help="make OpenH1.app from the build")
    command.add_argument("--app", required=True)
    command.add_argument("--executable", required=True)
    command.add_argument("--resource", action="append")
    command.add_argument("--license", action="append", help="<file>=<name>: a licence, in Licenses")
    command.add_argument("--touch", action="append", help="a picture or font of the touch controls, in touch/")
    command.add_argument("--bundle-id", required=True)
    command.add_argument("--build", type=int, default=0)
    command.add_argument("--minimum", required=True)
    command.add_argument("--identity", default="-", help='the signing certificate ("Apple Development")')
    command.add_argument("--team", help="the developer team's ID")
    command.add_argument("--profile", help="the development provisioning profile (tools/ios_provision.py)")
    command.add_argument("--simulator", action="store_true", help="the simulator's app")
    command.set_defaults(function=bundle)

    for name, function, text in (("install", install, "install the app on the connected device"),
                                 ("launch", launch, "start the app on the connected device"),
                                 ("log", log, "print the app's log from the connected device")):
        command = commands.add_parser(name, help=text)
        command.add_argument("--app", default=str(DEFAULT_APP))
        command.add_argument("--bundle-id", default=DEFAULT_BUNDLE_ID)
        command.add_argument("--device", help="devicectl's identifier of the device (simctl's with --simulator)")
        command.add_argument("--simulator", action="store_true", help="the booted simulator's (simctl)")
        if name == "log":
            command.add_argument("--name", default="halo.log", help="halo.log, or halo.previous.log")
        command.set_defaults(function=function)

    args = parser.parse_args()
    os.chdir(ROOT)
    args.function(args)


if __name__ == "__main__":
    main()
