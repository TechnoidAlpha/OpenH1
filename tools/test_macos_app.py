"""The macOS app (tools/macos_app.py, port/macos/README.md, "The app"): its
Info.plist and entitlements anywhere; on a Mac with Apple silicon, the app
that ninja macos built (build/macos/OpenH1.app): its contents, the
executable's rpath to its Frameworks, and its ad hoc signature with the
hardened runtime."""
import platform
import plistlib
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from tools import macos_app
from tools.macos_build import MACOS_BUNDLE_ID, MACOS_MINIMUM

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "build" / "macos" / "OpenH1.app"
SDL_INCLUDE = ROOT / "build" / "macos" / "third_party" / "SDL3" / "include"

built = pytest.mark.skipif(
    sys.platform != "darwin" or platform.machine().lower() != "arm64" or not APP.is_dir(),
    reason="needs a Mac with Apple silicon and build/macos/OpenH1.app (ninja macos)")


def test_info_plist_asks_for_game_mode_and_names_the_build():
    plist = macos_app.info_plist(MACOS_BUNDLE_ID, 1234, MACOS_MINIMUM)
    assert plist["LSApplicationCategoryType"] == "public.app-category.games"
    assert plist["LSSupportsGameMode"] is True
    assert plist["GCSupportsControllerUserInteraction"] is True
    assert plist["NSHighResolutionCapable"] is True
    assert plist["LSMinimumSystemVersion"] == MACOS_MINIMUM
    assert plist["CFBundleShortVersionString"] == plist["CFBundleVersion"] == "1234"
    assert plist["CFBundleExecutable"] == "halo"
    # (the name GOAT H1; the bundle ID, which Sparkle and the system's permissions know the app by, OpenH1's)
    assert plist["CFBundleName"] == plist["CFBundleDisplayName"] == "GOAT H1"
    assert plist["CFBundleIdentifier"] == MACOS_BUNDLE_ID == "com.openh1.openh1"
    assert plist["NSMicrophoneUsageDescription"] and plist["NSLocalNetworkUsageDescription"]
    assert "not endorsed" in plist["NSHumanReadableCopyright"]


def test_info_plist_takes_the_games_links():
    """the app is the handler of openh1:// links (invites, parties' links), the scheme p2p.h's links have, and
    not of halo:// ones, OpenCE's"""
    plist = macos_app.info_plist(MACOS_BUNDLE_ID, 0, MACOS_MINIMUM)
    (url_type,) = plist["CFBundleURLTypes"]
    assert url_type["CFBundleURLSchemes"] == ["openh1"] and url_type["CFBundleURLName"] == MACOS_BUNDLE_ID
    assert url_type["CFBundleTypeRole"] == "Viewer"
    header = (ROOT / "port/linux/src/p2p.h").read_text()
    assert '#define P2P_LINK_SCHEME "openh1"' in header
    assert '#define P2P_PARTY_LINK_PREFIX "openh1://party/"' in header


@pytest.mark.skipif(not shutil.which("clang") or not (SDL_INCLUDE / "SDL3/SDL.h").is_file(),
                    reason="needs clang and SDL's headers (python configure.py fetches them)")
def test_links_reach_the_guest(tmp_path):
    """port/macos/host_links.c: the links SDL's delegate sends as drop
    events (the system's GetURL event, UIKit's openURLContexts) are kept
    for the guest, oldest first, and anything else dropped is not"""
    binary = tmp_path / "host_links_test"
    subprocess.run(["clang", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                    f"-I{ROOT / 'port/android/include'}", f"-I{ROOT / 'port/android/host'}", f"-I{SDL_INCLUDE}",
                    "-pthread", str(ROOT / "port/macos/tests/host_links_test.c"), str(ROOT / "port/macos/host_links.c"),
                    "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr


def test_entitlements_are_the_narrowest():
    """MAP_JIT's entitlement, not unsigned executable memory, and library
    validation kept (only the ad hoc signature goes without it)"""
    with open(macos_app.ENTITLEMENTS, "rb") as file:
        entitlements = plistlib.load(file)
    assert entitlements == {"com.apple.security.cs.allow-jit": True,
                            "com.apple.security.device.audio-input": True}


@built
def test_app_contents():
    contents = APP / "Contents"
    for path in ("MacOS/halo", "Frameworks/libSDL3.0.dylib", "Resources/openh1-icon.icns",
                 "Resources/brokers.txt", "Resources/prepared.records", "Resources/openh1.assets", "Info.plist"):
        assert (contents / path).is_file(), path
    with open(contents / "Info.plist", "rb") as file:
        plist = plistlib.load(file)
    assert plist["LSSupportsGameMode"] is True
    assert plist["CFBundleURLTypes"][0]["CFBundleURLSchemes"] == ["openh1"]
    commands = subprocess.run(["otool", "-l", str(contents / "MacOS/halo")], capture_output=True, text=True,
                              check=True).stdout
    assert "path @executable_path/../Frameworks " in commands


@built
def test_app_is_signed_with_the_hardened_runtime():
    subprocess.run(["codesign", "--verify", "--strict", "--deep", str(APP)], check=True)
    details = subprocess.run(["codesign", "-d", "--verbose=2", str(APP)], capture_output=True, text=True).stderr
    assert "runtime" in details
    entitlements = subprocess.run(["codesign", "-d", "--entitlements", "-", "--xml", str(APP)],
                                  capture_output=True).stdout
    granted = plistlib.loads(entitlements[entitlements.index(b"<?xml"):])
    assert granted["com.apple.security.cs.allow-jit"] is True
    assert "com.apple.security.cs.allow-unsigned-executable-memory" not in granted
