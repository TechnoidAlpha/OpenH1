"""Tests of each release's symbols, offline: every guest build's
halo_guest.debug.elf is collected beside its build (tools/ci_build.py's
dist/<build>-symbols/) only when it is the image the build carries, the
workflow uploads each one and the release publishes it as
OpenH1-<machine>[-debug]-symbols.zip (tools/release_names.py) without
renaming a player's download, and a release's folder with a build but no
symbols warns rather than fails."""
import re
import subprocess
import sys
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import ci_build  # noqa: E402
import release_names  # noqa: E402
from test_symbolize_crash import FUNCTIONS, elf  # noqa: E402

WORKFLOW = (ROOT / ".github/workflows/build.yml").read_text()

# each guest build the workflow makes: (ci_build.py's platform, config, --vr, the
# symbols artifact, its published name)
GUEST_BUILDS = [
    ("linux_x86_64", "debug", False, "halo-linux-x86_64-debug-symbols", "OpenH1-Linux-debug-symbols"),
    ("linux_x86_64", "release", False, "halo-linux-x86_64-release-symbols", "OpenH1-Linux-symbols"),
    ("linux_x86_64", "release", True, "halo-linux-x86_64-vr-release-symbols", "OpenH1-Linux-VR-symbols"),
    ("linux_arm64", "debug", False, "halo-linux-arm64-debug-symbols", "OpenH1-Linux-ARM64-debug-symbols"),
    ("linux_arm64", "release", False, "halo-linux-arm64-release-symbols", "OpenH1-Linux-ARM64-symbols"),
    ("linux_arm64", "release", True, "halo-steam-frame-vr-release-symbols", "OpenH1-Steam-Frame-symbols"),
    ("android", "debug", False, "halo-android-debug-symbols", "OpenH1-Android-debug-symbols"),
    ("android", "release", False, "halo-android-release-symbols", "OpenH1-Android-symbols"),
    ("macos", "debug", False, "halo-macos-debug-symbols", "OpenH1-macOS-debug-symbols"),
    ("macos", "release", False, "halo-macos-release-symbols", "OpenH1-macOS-symbols"),
    ("ios", "debug", False, "halo-ios-debug-symbols", "OpenH1-iOS-debug-symbols"),
]


@pytest.mark.parametrize("platform, config, vr, artifact, name", GUEST_BUILDS)
def test_each_build_s_symbols_are_uploaded_and_named(platform, config, vr, artifact, name):
    """ci_build.py's folder is the artifact the workflow uploads (on a full run only, as the build itself, for a
    release build), and the release's name for it says the machine"""
    folder = ci_build.dist_name(platform, config, vr) if platform != "ios" else f"halo-ios-{config}"
    assert f"{folder}-symbols" == artifact
    assert f"name: {artifact}\n          path: dist/{artifact}\n" in WORKFLOW
    upload = WORKFLOW[:WORKFLOW.index(f"name: {artifact}\n")].rsplit("- uses: actions/upload-artifact@v4", 1)[1]
    assert ("if: needs.changes.outputs.full == 'true'" in upload) == (config == "release")
    assert release_names.published(artifact) == name


def test_the_players_downloads_keep_their_names():
    """the builds' own names, which the self-updaters ask for, are as they were; Windows's symbols too"""
    assert release_names.published("halo-macos-release") == "OpenH1"
    assert release_names.published("halo-macos-debug") == "OpenH1-debug"
    assert release_names.published("halo-android-release") == "OpenH1-Android"
    assert release_names.published("halo-linux-x86_64-debug") == "OpenH1-Linux-debug"
    assert release_names.published("halo-steam-frame-vr-release") == "OpenH1-Steam-Frame"
    assert release_names.published("halo-windows-release-symbols") == "OpenH1-Windows-symbols"
    assert release_names.published("halo-windows-debug-symbols") == "OpenH1-Windows-debug-symbols"
    # (the iOS app, not published as a build of its own yet, keeps its artifact's name)
    assert release_names.published("halo-ios-debug") == "halo-ios-debug"
    assert "ios" not in release_names.NAMES
    for platform in release_names.NAMES:
        for config in ("release", "debug"):
            assert not release_names.symbols_name(platform, config).startswith(
                release_names.NAMES[platform] + release_names.EXTENSIONS.get(platform, ".zip"))


def write_zip(path, members):
    with zipfile.ZipFile(path, "w") as archive:
        for name, data in members.items():
            archive.writestr(name, data)


def test_a_build_without_its_symbols_warns(tmp_path):
    for name in ("OpenH1.dmg", "OpenH1-Linux.zip", "OpenH1-Android-debug.apk", "OpenH1-Windows.zip", "SHA256SUMS"):
        (tmp_path / name).write_bytes(b"build")
    write_zip(tmp_path / "OpenH1-macOS-symbols.zip", {"halo_guest.debug.elf": b"image"})
    write_zip(tmp_path / "OpenH1-Linux-symbols.zip", {"README": b"no image"})
    write_zip(tmp_path / "OpenH1-Windows-symbols.zip", {"halo.pdb": b"pdb", "halo_guest.debug.elf": b"image"})
    problems = release_names.missing_symbols(tmp_path)
    assert len(problems) == 2
    assert any("OpenH1-Linux-symbols.zip has no halo_guest.debug.elf" in problem for problem in problems)
    assert any("OpenH1-Android-debug.apk has no OpenH1-Android-debug-symbols.zip" in problem for problem in problems)
    result = subprocess.run([sys.executable, str(ROOT / "tools/release_names.py"), "--check-symbols", str(tmp_path)],
                            capture_output=True, text=True)
    assert result.returncode == 0 and result.stdout.count("::warning::") == 2
    write_zip(tmp_path / "OpenH1-Linux-symbols.zip", {"halo_guest.debug.elf": b"image"})
    write_zip(tmp_path / "OpenH1-Android-debug-symbols.zip", {"halo_guest.debug.elf": b"image"})
    assert release_names.missing_symbols(tmp_path) == []


def with_lines(image: bytes) -> bytes:
    """the image linked again with its source lines: the same loaded segments, more after them"""
    return image + b".debug_line" + b"\0" * 64


@pytest.fixture
def build_root(tmp_path, monkeypatch):
    monkeypatch.setattr(ci_build, "ROOT", tmp_path)
    return tmp_path


def test_the_debug_image_is_collected(build_root, capsys):
    image = elf(FUNCTIONS, is64=False)
    build = build_root / "build/macos"
    build.mkdir(parents=True)
    (build / "halo_guest.elf").write_bytes(image)
    (build / "halo_guest.debug.elf").write_bytes(with_lines(image))
    ci_build.collect_symbols("macos", "halo-macos-release")
    collected = build_root / "dist/halo-macos-release-symbols/halo_guest.debug.elf"
    assert collected.read_bytes() == with_lines(image)
    assert "::warning::" not in capsys.readouterr().out


def test_another_build_s_debug_image_is_not_collected(build_root, capsys):
    """an image whose code is not the build's (a later link, a stale file) gives a warning and no symbols"""
    image = elf(FUNCTIONS, is64=False)
    other = bytearray(image)
    other[0x1000:0x1004] = b"\x1f\x20\x03\xd5"  # (an instruction in the code's segment)
    build = build_root / "build/linux_x86_64"
    build.mkdir(parents=True)
    (build / "halo_guest.elf").write_bytes(image)
    (build / "halo_guest.debug.elf").write_bytes(with_lines(bytes(other)))
    stale = build_root / "dist/halo-linux-x86_64-debug-symbols"
    stale.mkdir(parents=True)
    ci_build.collect_symbols("linux_x86_64", "halo-linux-x86_64-debug")
    assert not stale.exists()
    assert "::warning::no symbols for halo-linux-x86_64-debug" in capsys.readouterr().out
    # (and no image at all)
    (build / "halo_guest.debug.elf").unlink()
    ci_build.collect_symbols("linux_x86_64", "halo-linux-x86_64-debug")
    assert "::warning::no symbols" in capsys.readouterr().out


def test_android_s_image_is_the_apk_s(build_root, capsys):
    image = elf(FUNCTIONS, is64=False)
    build = build_root / "build/android"
    build.mkdir(parents=True)
    (build / "halo_guest.elf").write_bytes(image)
    (build / "halo_guest.debug.elf").write_bytes(with_lines(image))
    apk = build_root / ci_build.APKS["release"]
    apk.parent.mkdir(parents=True)
    write_zip(apk, {"assets/halo_guest.elf": image})
    ci_build.collect_symbols("android", "halo-android-release")
    assert (build_root / "dist/halo-android-release-symbols/halo_guest.debug.elf").is_file()
    write_zip(apk, {"assets/halo_guest.elf": image + b"other"})
    ci_build.collect_symbols("android", "halo-android-release")
    assert not (build_root / "dist/halo-android-release-symbols").exists()
    assert "the APK's assets/halo_guest.elf" in capsys.readouterr().out


def test_ci_build_notes_needs_only_the_release_s_checkout():
    """the release job's sparse checkout runs ci_build.py notes: what collect_symbols uses is imported where it
    is used, not at the top"""
    checkout = WORKFLOW[WORKFLOW.index("  release:\n"):]
    checkout = checkout[checkout.index("sparse-checkout: |"):checkout.index("sparse-checkout-cone-mode")]
    files = set(re.findall(r"^\s+(\S+)$", checkout, re.MULTILINE))
    imports = set(re.findall(r"^import (\w+)$", (ROOT / "tools/ci_build.py").read_text(), re.MULTILINE))
    local = {name for name in imports if (ROOT / "tools" / f"{name}.py").is_file()}
    assert {f"tools/{name}.py" for name in local} <= files
    assert "tools/release_names.py" in files
