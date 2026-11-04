"""The iOS app (tools/ios_app.py, port/ios/README.md): its Info.plist and
entitlements anywhere; on a Mac with Xcode's iOS SDK, the app that ninja ios
built (build/ios/OpenH1.app): its contents, and the guest image in a page
aligned section of the executable's __TEXT, which the loader maps the
guest's code from (port/android/host/host_loader.c)."""
import platform
import plistlib
import re
import subprocess
import sys
from pathlib import Path

import pytest

from tools import ios_app
from tools.ios_build import IOS_BUNDLE_ID, IOS_MINIMUM

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "build" / "ios" / "OpenH1.app"

built = pytest.mark.skipif(
    sys.platform != "darwin" or platform.machine().lower() != "arm64" or not APP.is_dir(),
    reason="needs a Mac with Apple silicon and build/ios/OpenH1.app (ninja ios)")


def test_info_plist():
    plist = ios_app.info_plist(IOS_BUNDLE_ID, 1234, IOS_MINIMUM)
    assert plist["CFBundleIdentifier"] == "com.openh1.openh1"
    assert plist["CFBundleExecutable"] == "halo" and plist["CFBundleName"] == "OpenH1"
    assert plist["CFBundleShortVersionString"] == plist["CFBundleVersion"] == "1234"
    assert plist["MinimumOSVersion"] == IOS_MINIMUM
    assert plist["CFBundleSupportedPlatforms"] == ["iPhoneOS"]
    # landscape only, full screen, the game's files in Documents for the Files app
    assert plist["UISupportedInterfaceOrientations"] == ["UIInterfaceOrientationLandscapeLeft",
                                                         "UIInterfaceOrientationLandscapeRight"]
    assert plist["UISupportedInterfaceOrientations~ipad"] == plist["UISupportedInterfaceOrientations"]
    assert plist["UIRequiresFullScreen"] is True
    assert plist["CADisableMinimumFrameDurationOnPhone"] is True
    assert plist["UIFileSharingEnabled"] is True and plist["LSSupportsOpeningDocumentsInPlace"] is True
    assert plist["GCSupportsControllerUserInteraction"] is True
    assert plist["LSApplicationCategoryType"] == "public.app-category.games"
    assert plist["LSSupportsGameMode"] is True
    assert plist["GCSupportsGameMode"] is True
    assert plist["NSMicrophoneUsageDescription"] and plist["NSLocalNetworkUsageDescription"]
    assert "not endorsed" in plist["NSHumanReadableCopyright"]
    simulator = ios_app.info_plist(IOS_BUNDLE_ID, 0, IOS_MINIMUM, simulator=True)
    assert simulator["CFBundleSupportedPlatforms"] == ["iPhoneSimulator"]


def test_info_plist_takes_the_games_links():
    """the app is the handler of openh1:// links (invites, parties' links), the scheme p2p.h's links have, and
    not of halo:// ones, OpenCE's"""
    plist = ios_app.info_plist(IOS_BUNDLE_ID, 0, IOS_MINIMUM)
    (url_type,) = plist["CFBundleURLTypes"]
    assert url_type["CFBundleURLSchemes"] == ["openh1"] and url_type["CFBundleURLName"] == IOS_BUNDLE_ID
    header = (ROOT / "port/linux/src/p2p.h").read_text()
    assert '#define P2P_LINK_SCHEME "openh1"' in header
    assert '#define P2P_PARTY_LINK_PREFIX "openh1://party/"' in header


def test_entitlements_ask_for_no_jit():
    """the address space and the memory; no JIT or unsigned executable
    memory, which iOS gives no app"""
    with open(ios_app.ENTITLEMENTS, "rb") as file:
        entitlements = plistlib.load(file)
    assert entitlements == {"com.apple.developer.kernel.extended-virtual-addressing": True,
                            "com.apple.developer.kernel.increased-memory-limit": True}


@built
def test_app_contents():
    for path in ("halo", "Info.plist", "brokers.txt", "shaders.metallib", "Assets.car", "Licenses/LICENSE.txt",
                 "touch/touch_button_classic_a.png", "touch/touch_caption.ttf"):
        assert (APP / path).is_file(), path


@built
def test_guest_image_section_is_page_aligned():
    """the guest image's section: in __TEXT, its file offset a multiple of
    16 KB (the loader maps its code from there)"""
    commands = subprocess.run(["otool", "-l", str(APP / "halo")], capture_output=True, text=True, check=True).stdout
    section = re.search(r"sectname __halo_guest\s+segname __TEXT\s+addr \S+\s+size (\S+)\s+offset (\d+)", commands)
    assert section, "no __TEXT,__halo_guest section"
    assert int(section.group(2)) % 16384 == 0
    assert int(section.group(1), 16) > 1 << 20
