#!/usr/bin/env python3
"""The macOS app, GOAT H1 (build/macos/OpenH1.app; GOAT H1.app in the disk
image), its signature, its disk image and notarization (port/macos/README.md,
"The app").

    python3 tools/macos_app.py bundle ...   # ninja macos does it: build/macos/OpenH1.app, signed ad hoc
    python3 tools/macos_app.py dmg ...      # ninja macos_dmg: build/macos/OpenH1.dmg
    python3 tools/macos_app.py sign --identity "Developer ID Application: <name> (<team>)"
    python3 tools/macos_app.py notarize --identity "Developer ID Application: <name> (<team>)" \\
        --keychain-profile <profile>
    python3 tools/macos_app.py notarize --identity ... --key AuthKey_<id>.p8 --key-id <id> --issuer <uuid>

- bundle: the app from the build: Contents/MacOS/halo (the executable, with
  an rpath of @executable_path/../Frameworks), Contents/Frameworks/
  libSDL3.0.dylib and Sparkle.framework (the updater, which the host loads
  from there: port/macos/host_app.m), Contents/Resources (the icon, brokers.txt, the About
  panel's credits, and in Licenses the licences of the app and of what it
  carries, which each copy must have) and Info.plist, signed with the hardened runtime and
  port/macos/halo.entitlements: ad hoc ("-") unless an identity is given.
- dmg: a disk image of the app, named GOAT H1.app in it, with a link to
  Applications to drag it to, in a window of its own (the game's icon on the disk, a background, the icons
  placed: port/macos/dmg) when dmgbuild is installed (pip install
  dmgbuild==1.6.7), else a plain one.
- sign: signs the app again with a Developer ID (and a secure timestamp,
  which notarization needs), and checks the signature.
- notarize: sign, then the app sent to Apple's notary service (xcrun
  notarytool) and its ticket stapled to it, then the disk image made of it,
  signed, notarized and stapled too. The credentials are notarytool's: a
  keychain profile (xcrun notarytool store-credentials <profile>), an App
  Store Connect API key (its .p8 file, its ID and its issuer's: CI's), or an
  Apple ID, its team and an app-specific password.
"""

import argparse
import os
import plistlib
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENTITLEMENTS = ROOT / "port/macos/halo.entitlements"
# the app's name (CFBundleName, CFBundleDisplayName: the menu bar, the About
# panel, Activity Monitor) and, with .app, its file's in the disk image
# (Finder, the Dock). The build's file stays OpenH1.app (no space in ninja's
# and CI's paths), and so does an install that Sparkle updates: Sparkle
# replaces the app where it is, under its old name (port/macos/README.md,
# "The name"). The bundle ID, the appcast and its key are unchanged.
APP_NAME = "GOAT H1"
APP_FILE = APP_NAME + ".app"
# the build's app (ninja macos), and its disk image's file (the release's
# OpenH1.dmg, which the appcast and the README's links name)
BUILD_APP = "OpenH1.app"
BUILD_DMG = "OpenH1.dmg"
EXECUTABLE = "halo"
SDL_LIBRARY = "libSDL3.0.dylib"
SPARKLE = "Sparkle.framework"
ICON = "openh1-icon"
# the scheme of the game's links (port/linux/src/p2p.h's P2P_LINK_SCHEME); not OpenCE's, halo, which older builds'
# links have
URL_SCHEME = "openh1"
# what the README says of the project
COPYRIGHT = ("GOAT H1 is an unofficial fan project. It is not endorsed by or affiliated with Microsoft, Xbox "
             "Game Studios, Halo Studios or Bungie. Halo and Halo: Combat Evolved are trademarks of Microsoft "
             "Corporation. No game data is included: you need your own copy of the game.")


def run(*command: str) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, check=True)


def run_retried(*command: str, tries: int = 5, wait: int = 20) -> None:
    """run, tried again after a failure that may pass: stapler's lookups of
    the ticket (a dropped connection, or a ticket not yet out for a minute
    after the acceptance) and hdiutil's "Resource busy" on CI's runners"""
    for attempt in range(1, tries + 1):
        print("+", " ".join(command), flush=True)
        if subprocess.run(command).returncode == 0:
            return
        if attempt == tries:
            raise subprocess.CalledProcessError(1, command)
        print(f"failed; again in {wait * attempt} s ({attempt} of {tries})", flush=True)
        time.sleep(wait * attempt)


def info_plist(bundle_id: str, build: int, minimum: str, feed: str = None, key: str = None) -> dict:
    """the app's Info.plist. The version is the build's number (that of the
    release it is, HALO_BUILD_NUMBER; 0 for a local build), as the game
    names itself ("OpenH1 macOS | build <n>"), and as Sparkle compares it
    with the appcast's sparkle:version. The games category and Game
    Mode's key make macOS turn Game Mode on while the game is fullscreen.
    The app is the handler of openh1:// links (invites, parties' links:
    port/macos/host_links.c).
    A release's app has Sparkle's appcast and public key (feed, key): it
    looks for a new version at each start and every four hours after
    (port/macos/README.md, "Updates"), without asking first whether it may
    (update.auto, on by default, says whether it does)."""
    version = str(build)
    sparkle = {}
    if feed and key:
        sparkle = {
            "SUFeedURL": feed,
            "SUPublicEDKey": key,
            "SUEnableAutomaticChecks": True,
            "SUScheduledCheckInterval": 4 * 3600,
            "SUVerifyUpdateBeforeExtraction": True,
        }
    return {
        **sparkle,
        "CFBundleDevelopmentRegion": "en",
        "CFBundleDisplayName": APP_NAME,
        "CFBundleExecutable": EXECUTABLE,
        "CFBundleIconFile": ICON,
        "CFBundleIdentifier": bundle_id,
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": APP_NAME,
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": version,
        "CFBundleVersion": version,
        "LSApplicationCategoryType": "public.app-category.games",
        "LSSupportsGameMode": True,
        "LSMinimumSystemVersion": minimum,
        "GCSupportsControllerUserInteraction": True,
        "NSHighResolutionCapable": True,
        "NSHumanReadableCopyright": COPYRIGHT,
        "NSMicrophoneUsageDescription": "Voice chat in network games uses the microphone while you talk.",
        "NSLocalNetworkUsageDescription": "System link finds and plays games on your local network.",
        "CFBundleURLTypes": url_types(bundle_id),
    }


def url_types(bundle_id: str) -> list:
    """CFBundleURLTypes: the scheme of the game's links (openh1://join/...,
    openh1://party/...), which the system then opens the app with; the iOS
    app's too (tools/ios_app.py)"""
    return [{"CFBundleURLName": bundle_id, "CFBundleTypeRole": "Viewer", "CFBundleURLSchemes": [URL_SCHEME]}]


def credits_html(build: int, flavor: str) -> str:
    """the About panel's text: the build, as the game names it
    (source/main/main.c)"""
    label = f"build {build} ({flavor})" if build > 0 else f"local build ({flavor})"
    return ("<html><body style=\"font: 11px -apple-system; text-align: center\">"
            f"<p>GOAT H1 macOS | {label}</p>"
            "<p>The Halo: Combat Evolved decompilation (Xbox build 01.01.14.2342), ported.<br>"
            "<a href=\"https://github.com/OpenH1/goath1\">github.com/OpenH1/goath1</a></p>"
            "</body></html>\n")


def codesign(path: Path, identity: str, entitlements: Path = None) -> None:
    command = ["codesign", "--force", "--sign", identity, "--options", "runtime",
               "--timestamp" if identity != "-" else "--timestamp=none"]
    if entitlements:
        command += ["--entitlements", str(entitlements)]
    run(*command, str(path))


def sign_sparkle(framework: Path, identity: str) -> None:
    """Sparkle.framework's helpers, then the framework, as Sparkle's
    documentation signs them for a Developer ID (with the hardened
    runtime). The app is not sandboxed: the framework's XPC services, a
    sandboxed app's, are not in it (bundle)."""
    versions = framework / "Versions" / "B"
    codesign(versions / "Autoupdate", identity)
    codesign(versions / "Updater.app", identity)
    codesign(framework, identity)


def sign_app(app: Path, identity: str) -> None:
    """the libraries, then the app (whose signature covers them), and checks.
    Signed ad hoc, the app also has disable-library-validation: the hardened
    runtime loads only libraries of the app's team, and ad hoc signatures
    have none ("mapping process and mapped file (non-platform) have
    different Team IDs"); a Developer ID's are the same team."""
    codesign(app / "Contents/Frameworks" / SDL_LIBRARY, identity)
    if (app / "Contents/Frameworks" / SPARKLE).is_dir():
        sign_sparkle(app / "Contents/Frameworks" / SPARKLE, identity)
    if identity != "-":
        codesign(app, identity, ENTITLEMENTS)
    else:
        with open(ENTITLEMENTS, "rb") as file:
            entitlements = plistlib.load(file)
        entitlements["com.apple.security.cs.disable-library-validation"] = True
        with tempfile.TemporaryDirectory() as temporary:
            ad_hoc = Path(temporary) / "ad_hoc.entitlements"
            with open(ad_hoc, "wb") as file:
                plistlib.dump(entitlements, file)
            codesign(app, identity, ad_hoc)
    run("codesign", "--verify", "--strict", "--deep", str(app))


def bundle(args: argparse.Namespace) -> None:
    app = Path(args.app)
    staging = app.with_name(app.name + ".partial")
    shutil.rmtree(staging, ignore_errors=True)
    contents = staging / "Contents"
    for folder in ("MacOS", "Frameworks", "Resources"):
        (contents / folder).mkdir(parents=True)
    shutil.copy2(args.executable, contents / "MacOS" / EXECUTABLE)
    shutil.copy2(args.sdl, contents / "Frameworks" / SDL_LIBRARY)
    if args.sparkle:
        # (its links kept, as a framework's must be for its signature; not
        # its XPC services, which only a sandboxed app uses)
        shutil.copytree(args.sparkle, contents / "Frameworks" / SPARKLE, symlinks=True,
                        ignore=shutil.ignore_patterns("XPCServices"))
    shutil.copy2(args.icon, contents / "Resources" / (ICON + ".icns"))
    for resource in args.resource or []:
        shutil.copy2(resource, contents / "Resources" / Path(resource).name)
    for license in args.license or []:
        source, name = license.split("=", 1)
        (contents / "Resources" / "Licenses").mkdir(exist_ok=True)
        shutil.copy2(source, contents / "Resources" / "Licenses" / name)
    plist = info_plist(args.bundle_id, args.build, args.minimum, args.sparkle_feed, args.sparkle_key)
    with open(contents / "Info.plist", "wb") as file:
        plistlib.dump(plist, file)
    (contents / "PkgInfo").write_text("APPL????")
    (contents / "Resources" / "Credits.html").write_text(credits_html(args.build, args.flavor), encoding="utf-8")
    sign_app(staging, args.identity)
    shutil.rmtree(app, ignore_errors=True)
    staging.rename(app)


# the disk image's window (port/macos/dmg/background.html lays out its
# background for these): its size, the icons' size and their centres
DMG_ART = ROOT / "port/macos/dmg"
DMG_WINDOW = (640, 400)
DMG_ICON_SIZE = 128
DMG_APP_AT = (160, 190)
DMG_APPLICATIONS_AT = (480, 190)


def dmg_laid_out(app: Path, partial: Path) -> None:
    """the disk image with dmgbuild's window: the volume's icon, the
    background (with its @2x), the icons placed, no toolbar or sidebar"""
    import dmgbuild

    settings = {
        "files": [(str(app), APP_FILE)],
        "symlinks": {"Applications": "/Applications"},
        "icon": str(ROOT / "port/macos" / (ICON + ".icns")),
        "background": str(DMG_ART / "background.png"),
        "window_rect": ((200, 140), DMG_WINDOW),
        "icon_size": DMG_ICON_SIZE,
        "text_size": 13,
        "icon_locations": {APP_FILE: DMG_APP_AT, "Applications": DMG_APPLICATIONS_AT},
        "show_status_bar": False, "show_tab_view": False, "show_toolbar": False,
        "show_pathbar": False, "show_sidebar": False,
        "format": "ULFO",
        "filesystem": "APFS",
    }
    for attempt in range(1, 6):
        print(f"+ dmgbuild {partial} ({attempt})", flush=True)
        try:
            dmgbuild.build_dmg(str(partial), APP_NAME, settings=settings)
            return
        except Exception as error:  # (hdiutil's "Resource busy" on CI's runners, as run_retried)
            if attempt == 5:
                raise
            print(f"failed ({error}); again in {20 * attempt} s", flush=True)
            time.sleep(20 * attempt)


def dmg(args: argparse.Namespace) -> None:
    app = Path(args.app)
    output = Path(args.output)
    partial = output.with_name(output.name + ".partial.dmg")
    partial.unlink(missing_ok=True)
    try:
        import dmgbuild  # noqa: F401
    except ImportError:
        print("dmgbuild is not installed (pip install dmgbuild==1.6.7): a plain disk image", flush=True)
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / APP_NAME
            folder.mkdir()
            # (ditto keeps the signature's extended attributes and links)
            run("ditto", str(app), str(folder / APP_FILE))
            (folder / "Applications").symlink_to("/Applications")
            run_retried("hdiutil", "create", "-quiet", "-volname", APP_NAME, "-srcfolder", str(folder), "-fs", "APFS",
                        "-format", "ULFO", "-ov", str(partial))
    else:
        dmg_laid_out(app, partial)
    partial.replace(output)
    if args.identity and args.identity != "-":
        run("codesign", "--force", "--sign", args.identity, "--timestamp", str(output))


def notary_credentials(args: argparse.Namespace) -> list:
    if args.keychain_profile:
        return ["--keychain-profile", args.keychain_profile]
    if args.key and args.key_id and args.issuer:
        return ["--key", args.key, "--key-id", args.key_id, "--issuer", args.issuer]
    if args.apple_id and args.team_id and args.password:
        return ["--apple-id", args.apple_id, "--team-id", args.team_id, "--password", args.password]
    sys.exit("notarize: give --keychain-profile, --key, --key-id and --issuer, or --apple-id, --team-id and "
             "--password")


def notarize_file(path: Path, credentials: list) -> None:
    run("xcrun", "notarytool", "submit", str(path), *credentials, "--wait")


def notarize(args: argparse.Namespace) -> None:
    app = Path(args.app)
    credentials = notary_credentials(args)
    sign_app(app, args.identity)
    with tempfile.TemporaryDirectory() as temporary:
        archive = Path(temporary) / (APP_NAME + ".zip")
        run("ditto", "-c", "-k", "--keepParent", str(app), str(archive))
        notarize_file(archive, credentials)
    run_retried("xcrun", "stapler", "staple", str(app))
    dmg(argparse.Namespace(app=str(app), output=args.output, identity=args.identity))
    notarize_file(Path(args.output), credentials)
    run_retried("xcrun", "stapler", "staple", args.output)
    run("spctl", "--assess", "--type", "execute", "--verbose=2", str(app))
    run("spctl", "--assess", "--type", "open", "--context", "context:primary-signature", "--verbose=2", args.output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    default_app = str(ROOT / "build/macos" / BUILD_APP)
    default_dmg = str(ROOT / "build/macos" / BUILD_DMG)

    command = commands.add_parser("bundle", help="make the app from the build")
    command.add_argument("--app", required=True)
    command.add_argument("--executable", required=True)
    command.add_argument("--sdl", required=True)
    command.add_argument("--sparkle", help="Sparkle.framework, the updater")
    command.add_argument("--sparkle-feed", help="the appcast's URL (SUFeedURL): a release's")
    command.add_argument("--sparkle-key", help="the public EdDSA key its updates are signed with (SUPublicEDKey)")
    command.add_argument("--icon", required=True)
    command.add_argument("--resource", action="append")
    command.add_argument("--license", action="append", help="<file>=<name>: a licence, in Resources/Licenses")
    command.add_argument("--bundle-id", required=True)
    command.add_argument("--build", type=int, default=0)
    command.add_argument("--flavor", default="debug")
    command.add_argument("--minimum", required=True)
    command.add_argument("--identity", default="-")
    command.set_defaults(function=bundle)

    command = commands.add_parser("dmg", help="a disk image of the app")
    command.add_argument("--app", default=default_app)
    command.add_argument("--output", default=default_dmg)
    command.add_argument("--identity", default="-")
    command.set_defaults(function=dmg)

    command = commands.add_parser("sign", help="sign the app with a Developer ID")
    command.add_argument("--app", default=default_app)
    command.add_argument("--identity", required=True, help='"Developer ID Application: <name> (<team>)"')
    command.set_defaults(function=lambda args: sign_app(Path(args.app), args.identity))

    command = commands.add_parser("notarize", help="sign, notarize and staple the app and its disk image")
    command.add_argument("--app", default=default_app)
    command.add_argument("--output", default=default_dmg)
    command.add_argument("--identity", required=True, help='"Developer ID Application: <name> (<team>)"')
    command.add_argument("--keychain-profile", help="notarytool's stored credentials")
    command.add_argument("--key", help="an App Store Connect API key's .p8 file")
    command.add_argument("--key-id", help="the API key's ID")
    command.add_argument("--issuer", help="the API key's issuer ID")
    command.add_argument("--apple-id")
    command.add_argument("--team-id")
    command.add_argument("--password", help="an app-specific password")
    command.set_defaults(function=notarize)

    args = parser.parse_args()
    os.chdir(ROOT)
    args.function(args)


if __name__ == "__main__":
    main()
