"""The Linux release's layout (tools/linux_package.py): the tree that packages copy to /usr, the zips' modes, and
that the game (install_layout.c, updater.c), tools/ci_build.py, the workflow and the desktop entry agree with them.
Needs no build: the files are stand-ins.

    python -m pytest -q tools/test_linux_package.py
"""

import hashlib
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
import time
import zipfile
import zlib
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ci_build  # noqa: E402
import linux_package  # noqa: E402
import release_names  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


@pytest.fixture
def build(tmp_path):
    """stand-ins for the build's executable, SDL and SDL's licence"""
    folder = tmp_path / "build"
    folder.mkdir()
    for name in ("halo", "libSDL3.so.0", "SDL3-LICENSE.txt", "openh1.assets"):
        (folder / name).write_bytes(name.encode())
    return folder


def notices(build):
    return linux_package.notices("linux") + [(build / "SDL3-LICENSE.txt", "SDL3-LICENSE.txt")]


def files(folder):
    return sorted(path.relative_to(folder).as_posix() for path in folder.rglob("*") if path.is_file())


def test_tree(build, tmp_path):
    tree = tmp_path / "tree"
    linux_package.stage_tree(tree, build / "halo", build / "libSDL3.so.0", notices(build))
    names = files(tree)
    assert (tree / "bin/openh1").read_bytes() == b"halo"
    assert (tree / "bin/goath1").read_bytes() == linux_package.COMMAND.read_bytes()
    assert "lib/openh1/libSDL3.so.0" in names
    assert "share/openh1/brokers.txt" in names
    assert "share/openh1/prepared.records" in names
    assert "share/openh1/openh1.assets" in names
    assert "share/applications/openh1.desktop" in names
    assert "share/icons/hicolor/48x48/apps/openh1.png" in names
    assert "share/licenses/openh1/LICENSE" in names
    assert "share/licenses/openh1/SDL3-LICENSE.txt" in names
    assert "share/doc/openh1/README.md" in names
    assert (tree / "install.sh").read_bytes() == linux_package.INSTALLER.read_bytes()
    for kind in ("capsule", "wide", "hero", "logo", "icon"):
        assert (tree / f"share/openh1/steam/{kind}.png").read_bytes() == \
            (ROOT / f"port/assets/steam/openh1/{kind}.png").read_bytes()
    # (nothing but the prefix's folders, and the installer, at its top)
    assert {name.split("/")[0] for name in names} == {"bin", "lib", "share", "install.sh"}


def test_tree_without_sdl(build, tmp_path):
    tree = tmp_path / "tree"
    linux_package.stage_tree(tree, build / "halo", None, linux_package.notices("linux"))
    assert not (tree / "lib").exists()


def test_package_tree_has_no_installer(build, tmp_path):
    """the tree for a package's /usr (linux_package.py tree) has no install.sh, which would be /usr/install.sh"""
    tree = tmp_path / "tree"
    linux_package.stage_tree(tree, build / "halo", None, linux_package.notices("linux"), installer=False)
    assert {name.split("/")[0] for name in files(tree)} == {"bin", "share"}
    assert not (tree / "share/openh1/steam").exists()
    script = (ROOT / "tools/linux_package.py").read_text()
    assert script.count("installer=False)") == 2


def test_zip_modes(build, tmp_path):
    executables = {"install.sh", "bin/openh1", "bin/goath1", "lib/openh1/libSDL3.so.0"}
    folder = tmp_path / "tree"
    # (as GitHub's artifacts give them: no file executable)
    linux_package.stage_tree(folder, build / "halo", build / "libSDL3.so.0", notices(build))
    for path in folder.rglob("*"):
        if path.is_file():
            path.chmod(0o644)
    output = tmp_path / "tree.zip"
    linux_package.write_zip(folder, output)
    with zipfile.ZipFile(output) as archive:
        for info in archive.infolist():
            assert info.create_system == 3
            mode = info.external_attr >> 16
            if info.is_dir():
                assert stat.S_ISDIR(mode)
                continue
            assert stat.S_ISREG(mode)
            expected = 0o755 if info.filename in executables else 0o644
            assert stat.S_IMODE(mode) == expected, info.filename
        assert archive.namelist() == sorted(archive.namelist())
        assert "install.sh" in archive.namelist()


def test_tree_fits_the_updater(build, tmp_path):
    """the self-updater unpacks at most MAXIMUM_UPDATE_FILES files (updater.c)"""
    tree = tmp_path / "tree"
    linux_package.stage_tree(tree, build / "halo", build / "libSDL3.so.0", notices(build))
    updater = (ROOT / "port/linux/src/updater.c").read_text()
    maximum = int(re.search(r"#define MAXIMUM_UPDATE_FILES (\d+)", updater).group(1))
    assert len(files(tree)) <= maximum


def test_names_agree():
    """the executable's, the folders' and the desktop entry's name are install_layout.h's"""
    header = (ROOT / "port/linux/src/install_layout.h").read_text()
    assert re.search(r'#define INSTALL_NAME "([^"]+)"', header).group(1) == linux_package.NAME
    entry = dict(line.split("=", 1) for line in linux_package.DESKTOP_ENTRY.read_text().splitlines() if "=" in line)
    assert entry["Exec"] == linux_package.NAME
    assert entry["Icon"] == linux_package.NAME
    assert entry["StartupWMClass"] == linux_package.NAME
    assert sorted(linux_package.ICONS.glob(f"{linux_package.NAME}-48.png"))


def test_asset_names_agree():
    """each updating build's zip is the one its self-updater asks for (UPDATE_NAME[-debug].zip): Windows', the
    x86-64 build's, its VR build's, the 64-bit ARM build's and the Steam Frame's (test_frame_release_names_agree),
    as tools/ci_build.py names their artifacts, tools/release_names.py the release's files, and the workflow
    publishes them; there is no 32-bit build, nor a flat release"""
    updater = (ROOT / "port/linux/src/updater.c").read_text()
    platforms = set(re.findall(r'#define UPDATE_PLATFORM "([^"]+)"', updater))
    assert platforms == {"windows", "linux-x86_64", "linux-x86_64-vr", "linux-arm64", "steam-frame-vr"}
    names = re.findall(r'#define UPDATE_PLATFORM "([^"]+)"\n#define UPDATE_NAME "([^"]+)"', updater)
    assert dict(names) == {platform: release_names.NAMES[platform] for platform in platforms}
    assert '#define UPDATE_ASSET_RELEASE UPDATE_NAME ".zip"' in updater
    assert '#define UPDATE_ASSET_DEBUG UPDATE_NAME "-debug.zip"' in updater
    for platform in platforms:
        for config in ("release", "debug"):
            artifact = f"halo-{platform}-{config}"
            assert release_names.published(artifact) == release_names.NAMES[platform] + (
                "-debug" if config == "debug" else "")
    workflow = (ROOT / ".github/workflows/build.yml").read_text()
    for platform, vr in (("linux_x86_64", False), ("linux_x86_64", True), ("linux_arm64", False)):
        for config in ("debug", "release"):
            name = ci_build.dist_name(platform, config, vr)
            assert name == f"halo-{platform.replace('_', '-', 1)}{'-vr' if vr else ''}-{config}"
            assert name[len("halo-"):-len(f"-{config}")] in platforms
            if config == "release" or not vr:
                assert f"name: {name}" in workflow and f"path: dist/{name}" in workflow, name
    assert "i686" not in workflow and "halo-linux-debug" not in workflow and "halo-linux-release" not in workflow
    assert not hasattr(linux_package, "stage_flat")


def test_frame_release_names_agree():
    """the 64-bit ARM VR build is the Steam Frame's package (tools/frame_package.py), which the release has as
    halo-steam-frame-vr-release.zip, as it is, and which its self-updater (updater.c's UPDATER_PACKAGE) asks for
    as OpenH1-Steam-Frame.zip and unpacks to update.partial/<the package's folder>, where its launcher installs
    it: tools/ci_build.py names it, and the workflow builds it on master, uploads it and publishes it with the
    zip's own modes"""
    import frame_package

    updater = (ROOT / "port/linux/src/updater.c").read_text()
    platforms = dict(re.findall(r'#define UPDATE_PLATFORM "([^"]+)"\n#define UPDATE_NAME "([^"]+)"', updater))
    name = ci_build.dist_name("linux_arm64", "release", True)
    assert name == "halo-steam-frame-vr-release"
    platform = name[len("halo-"):-len("-release")]
    assert platforms[platform] == release_names.NAMES[platform] == release_names.published(name)
    assert "linux-arm64-vr" not in platforms
    frame = updater[updater.index("#elif defined(HALO_VR)\n"):updater.index("#elif defined(__aarch64__)")]
    assert f'#define UPDATE_PLATFORM "{platform}"' in frame
    assert f'#define UPDATER_PACKAGE "{frame_package.NAME}"' in frame
    launcher = (ROOT / "port/linux/frame/halo-vr.sh").read_text()
    assert f"update.partial/{frame_package.NAME}/install.sh" in launcher
    assert '#define UPDATE_DIRECTORY "update.partial"' in updater
    for name_in_both in ("HALO_FRAME_LAUNCHER", "HALO_FRAME_UPDATED", "update-ready"):
        assert name_in_both in updater and name_in_both in launcher, name_in_both
    workflow = (ROOT / ".github/workflows/build.yml").read_text()
    assert "run: python3 tools/ci_build.py linux_arm64 release --vr" in workflow
    assert f"name: {name}" in workflow and f"path: dist/{name}" in workflow
    assert 'halo-steam-frame-*) cp "$build/$name.zip" "release/$published.zip"' in workflow
    assert release_names.published(name) == "OpenH1-Steam-Frame"
    assert "/download/OpenH1-Steam-Frame.zip" in workflow


def test_frame_zip(tmp_path):
    """the Steam Frame's zip (frame_package.write_zip): its folder under the package's name, the programs 0755,
    the rest 0644, in name order"""
    import frame_package

    package = tmp_path / frame_package.NAME
    package.mkdir()
    names = ["halo", "libSDL3.so.0", "halo-vr.sh", "install.sh", "brokers.txt", "README.md", "VERSION", "SHA256SUMS"]
    for name in names:
        (package / name).write_bytes(name.encode())
        (package / name).chmod(0o644)
    output = tmp_path / "halo-steam-frame-vr-release.zip"
    frame_package.write_zip(package, output)
    with zipfile.ZipFile(output) as archive:
        assert archive.namelist() == [f"{frame_package.NAME}/"] + sorted(f"{frame_package.NAME}/{name}" for name in names)
        for info in archive.infolist():
            assert info.create_system == 3
            mode = info.external_attr >> 16
            if info.is_dir():
                assert stat.S_ISDIR(mode)
                continue
            assert stat.S_ISREG(mode)
            base = info.filename.split("/", 1)[1]
            expected = 0o755 if base in ("halo", "libSDL3.so.0", "halo-vr.sh", "install.sh") else 0o644
            assert stat.S_IMODE(mode) == expected, info.filename
            assert archive.read(info) == base.encode()


def test_frame_package_files():
    """the Frame's package has the one installer (tools/linux_install.sh) as its install.sh, the Linux release's
    menu entry, and OpenH1 VR's library artwork; the Linux tree's install.sh is the same file"""
    import frame_package

    script = (ROOT / "tools/frame_package.py").read_text()
    assert '"install.sh": ROOT / "tools/linux_install.sh"' in script
    assert '"openh1.desktop": ROOT / "port/assets/linux/openh1.desktop"' in script
    assert 'f"steam-{art.name}": art for art in sorted((ROOT / "port/assets/steam/openh1-vr").glob("*.png"))' in script
    assert linux_package.INSTALLER == ROOT / "tools/linux_install.sh"
    assert not frame_package.PLAYER_FILES & {"install.sh", "openh1.desktop", "steam-capsule.png"}
    # (tools/frame_install.sh, from a computer, runs this repository's installer on the Frame)
    assert 'installer=$repo/tools/linux_install.sh' in (ROOT / "tools/frame_install.sh").read_text()


def frame_package_folder(folder, game="halo\n", extra=()):
    """a Steam Frame package (tools/frame_package.py's files, stand-ins for the programs) in folder"""
    folder.mkdir(parents=True, exist_ok=True)
    files = {"halo": game, "libSDL3.so.0": "sdl\n", "halo-vr.sh": "#!/bin/sh\nexec ./halo \"$@\"\n",
             "brokers.txt": "brokers\n", "README.md": "readme\n", "VERSION": f"commit {hash(game) & 0xffff:x}\nbranch master\nA change\n",
             **dict(extra)}
    for name, text in files.items():
        (folder / name).write_text(text)
    shutil.copyfile(linux_package.INSTALLER, folder / "install.sh")
    shutil.copyfile(ROOT / "port/assets/icon/hicolor/openh1-256.png", folder / "openh1.png")
    shutil.copyfile(ROOT / "port/assets/linux/openh1.desktop", folder / "openh1.desktop")
    for art in sorted((linux_package.STEAM_ART / "openh1-vr").glob("*.png")):
        shutil.copyfile(art, folder / f"steam-{art.name}")
    for name in ("halo", "libSDL3.so.0", "halo-vr.sh", "install.sh"):
        (folder / name).chmod(0o755)
    names = sorted(path.name for path in folder.iterdir() if path.name != "SHA256SUMS")
    (folder / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256((folder / name).read_bytes()).hexdigest()}  {name}\n" for name in names))
    return folder


def test_frame_install_from_package(tmp_path):
    """install.sh opened in an unzipped Frame package installs that package into ~/apps/halo-vr of whoever runs it,
    and says what is left to do where the Frame's helpers (kdialog, steamos-add-to-steam) are not"""
    package = frame_package_folder(tmp_path / "home/Downloads/halo-vr-frame")
    home = tmp_path / "home"
    output = run_install(package, home, tools=())
    installed = home / "apps/halo-vr"
    for name in files(package):
        assert (installed / name).read_bytes() == (package / name).read_bytes(), name
        expected = 0o755 if name in ("halo", "libSDL3.so.0", "halo-vr.sh", "install.sh") else 0o644
        assert stat.S_IMODE((installed / name).stat().st_mode) == expected, name
    assert "Installing GOAT H1 VR to" in output and "(commit " in output
    assert "No maps here" in output and "Add a Non-Steam Game" in output and "Start SteamVR" in output
    entry = dict(line.split("=", 1) for line in
                 (home / ".local/share/applications/openh1-vr.desktop").read_text().splitlines() if "=" in line)
    assert entry["Name"] == "GOAT H1 VR" and entry["Exec"] == f'"{installed}/halo-vr.sh"'
    assert entry["Path"] == str(installed) and entry["Icon"] == f"{installed}/openh1.png"
    assert not (home / "Applications").exists()


def test_frame_install_asks_for_maps_and_adds_to_steam(tmp_path):
    """on the Frame (kdialog, steamos-add-to-steam), install.sh asks for the maps folder only (the game, in the
    headset, copies no disc image), copies it, adds the menu entry to Steam and gives the shortcut its artwork"""
    package = frame_package_folder(tmp_path / "home/Downloads/halo-vr-frame")
    home = tmp_path / "home"
    disc = tmp_path / "disc"
    (disc / "maps").mkdir(parents=True)
    (disc / "maps/a10.map").write_text("map")
    output = run_install(package, home, KDIALOG_FOLDER=disc)
    installed = home / "apps/halo-vr"
    assert (installed / "maps/a10.map").read_text() == "map"
    assert calls(home) == [f"kdialog --title GOAT H1: your game's maps --getexistingdirectory {home}",
                           f"steamos-add-to-steam {home}/.local/share/applications/openh1-vr.desktop"]
    assert "Added to Steam as GOAT H1 VR: start SteamVR, then it, from the library." in output
    appid = steam_id(f"{installed}/halo-vr.sh")
    grid = home / ".local/share/Steam/userdata/12345/config/grid"
    assert (grid / f"{appid}p.png").read_bytes() == (package / "steam-capsule.png").read_bytes()
    assert (grid / f"{appid}_logo.png").read_bytes() == (package / "steam-logo.png").read_bytes()
    assert "Steam library artwork: 5 files" in output


def test_install_takes_a_linked_maps_folder(tmp_path):
    """a maps folder that is a link to one elsewhere (as a Frame's ~/apps/halo-vr/maps -> ~/apps/halo/maps) is the
    game's maps: install.sh neither asks for maps nor says there are none"""
    package = frame_package_folder(tmp_path / "home/Downloads/halo-vr-frame")
    home = tmp_path / "home"
    elsewhere = home / "apps/halo/maps"
    elsewhere.mkdir(parents=True)
    (elsewhere / "a10.map").write_text("map")
    run_install(package, home, tools=())
    installed = home / "apps/halo-vr"
    (installed / "maps").symlink_to(elsewhere)
    output = run_install(package, home)
    assert "No maps" not in output
    assert not any("getexistingdirectory" in call for call in calls(home))
    assert (installed / "maps").is_symlink() and (installed / "maps/a10.map").read_text() == "map"

def test_frame_update_keeps_the_player_files(tmp_path):
    """a newer Frame package replaces what changed (halo, kept as halo.prev), removes what it no longer has, and
    never touches the player's files; a package whose files differ from its SHA256SUMS installs nothing"""
    home = tmp_path / "home"
    old = frame_package_folder(tmp_path / "old/halo-vr-frame", extra={"gone.txt": "an old file\n"})
    run_install(old, home, tools=())
    installed = home / "apps/halo-vr"
    player = {"config.toml": "[vr]\n", "halo-vr.env": "HALO_VR=1\n", "maps/a10.map": "mine", "debug.txt": "log",
              "halo-vr.log": "out"}
    for name, text in player.items():
        (installed / name).parent.mkdir(parents=True, exist_ok=True)
        (installed / name).write_text(text)
    new = frame_package_folder(tmp_path / "new/halo-vr-frame", game="a newer halo\n")
    output = run_install(new, home, tools=())
    assert "  halo: updated, the previous kept as halo.prev" in output
    assert "  gone.txt: removed (no longer in the package)" in output
    assert "Was: commit" in output and "Now: commit" in output
    assert (installed / "halo").read_text() == "a newer halo\n" and (installed / "halo.prev").read_text() == "halo\n"
    assert not (installed / "gone.txt").exists()
    assert (installed / "SHA256SUMS").read_bytes() == (new / "SHA256SUMS").read_bytes()
    for name, text in player.items():
        assert (installed / name).read_text() == text, name
    assert not list(installed.glob("*.new"))

    broken = frame_package_folder(tmp_path / "broken/halo-vr-frame", game="newest\n")
    (broken / "halo").write_text("not what SHA256SUMS says\n")
    result = subprocess.run([str(broken / "install.sh"), "--batch"], capture_output=True, text=True,
                            env=install_environment(home))
    assert result.returncode == 4 and "differs from its SHA256SUMS" in result.stderr
    assert (installed / "halo").read_text() == "a newer halo\n"


def frame_launcher_run(installed, tmp_path, **environment):
    """halo-vr.sh run as Steam runs it (no terminal: its output to halo-vr.log), with stand-ins for the programs
    install.sh runs"""
    stand_ins = tmp_path / "launcher-stand-ins"
    stand_ins.mkdir(exist_ok=True)
    (stand_ins / "pgrep").write_text("#!/bin/sh\nexit 1\n")
    (stand_ins / "pgrep").chmod(0o755)
    commands = command_folder(tmp_path / "commands")
    result = subprocess.run(["sh", str(installed / "halo-vr.sh")], capture_output=True, text=True,
                            stdin=subprocess.DEVNULL,
                            env={"HOME": str(tmp_path / "home"), "PATH": f"{stand_ins}:{commands}", **environment})
    return result, (installed / "halo-vr.log").read_text()


# the Frame's package's own launcher (port/linux/frame/halo-vr.sh), for frame_package_folder's extra
LAUNCHER = {"halo-vr.sh": (ROOT / "port/linux/frame/halo-vr.sh").read_text()}
# a stand-in for the game in the Frame's package: it notes how it was started, and, as the self-updater does when
# the player chooses UPDATE, it unpacks the package UPDATE_TO names into update.partial/ (if there is one, and
# this run was not just updated) and writes update-ready, then quits
FRAME_GAME = """#!/bin/sh
echo "{name} started: launcher=$HALO_FRAME_LAUNCHER updated=${{HALO_FRAME_UPDATED:-}} partial=$([ -d update.partial ] && echo yes || echo no)" >> ../game-runs.txt
if [ -n "${{UPDATE_TO:-}}" ] && [ -z "${{HALO_FRAME_UPDATED:-}}" ]; then
	mkdir -p update.partial
	cp -R "$UPDATE_TO" update.partial/halo-vr-frame
	echo "build 2" > update-ready
fi
"""


def test_frame_launcher_installs_an_update(tmp_path):
    """the Steam Frame's self-update: the game (started by halo-vr.sh, HALO_FRAME_LAUNCHER) unpacks the new package into
    update.partial/halo-vr-frame, writes update-ready and quits; halo-vr.sh then installs it with the package's own
    install.sh (its SHA256SUMS checked, the old halo kept as halo.prev, the player's files untouched), deletes the
    download and starts the new version (HALO_FRAME_UPDATED, so that it does not look again)"""
    home = tmp_path / "home"
    old = frame_package_folder(tmp_path / "old/halo-vr-frame", game=FRAME_GAME.format(name="old"), extra=LAUNCHER)
    run_install(old, home, tools=())
    installed = home / "apps/halo-vr"
    player = {"config.toml": "[vr]\n", "halo-vr.env": "HALO_VR=1\n", "maps/a10.map": "mine"}
    for name, text in player.items():
        (installed / name).parent.mkdir(parents=True, exist_ok=True)
        (installed / name).write_text(text)
    new = frame_package_folder(tmp_path / "new/halo-vr-frame", game=FRAME_GAME.format(name="new"),
                               extra={**LAUNCHER, "brokers.txt": "newer brokers\n"})

    result, log = frame_launcher_run(installed, tmp_path, UPDATE_TO=str(new))
    assert result.returncode == 0, result.stdout + result.stderr + log
    runs = (home / "apps/game-runs.txt").read_text().splitlines()
    assert runs == [f"old started: launcher={installed}/halo-vr.sh updated= partial=no",
                    f"new started: launcher={installed}/halo-vr.sh updated=1 partial=no"]
    assert "halo-vr.sh: installing the update" in (installed / "halo-vr.log.prev").read_text()
    assert "  halo: updated, the previous kept as halo.prev" in (installed / "halo-vr.log.prev").read_text()
    assert (installed / "halo").read_bytes() == (new / "halo").read_bytes()
    assert (installed / "halo.prev").read_bytes() == (old / "halo").read_bytes()
    assert (installed / "brokers.txt").read_text() == "newer brokers\n"
    assert (installed / "SHA256SUMS").read_bytes() == (new / "SHA256SUMS").read_bytes()
    for name, text in player.items():
        assert (installed / name).read_text() == text, name
    assert not (installed / "update.partial").exists() and not (installed / "update-ready").exists()


def test_frame_launcher_without_an_update(tmp_path):
    """halo-vr.sh deletes a download the player did not take (update.partial without update-ready) before it starts
    the game, and starts it once; a download that differs from its SHA256SUMS is not installed: the game starts
    again as it was"""
    home = tmp_path / "home"
    package = frame_package_folder(tmp_path / "pkg/halo-vr-frame", game=FRAME_GAME.format(name="game"), extra=LAUNCHER)
    run_install(package, home, tools=())
    installed = home / "apps/halo-vr"
    (installed / "update.partial/halo-vr-frame").mkdir(parents=True)
    result, _ = frame_launcher_run(installed, tmp_path)
    assert result.returncode == 0
    assert (home / "apps/game-runs.txt").read_text().splitlines() == [
        f"game started: launcher={installed}/halo-vr.sh updated= partial=no"]
    assert not (installed / "update.partial").exists()

    (home / "apps/game-runs.txt").unlink()
    broken = frame_package_folder(tmp_path / "broken/halo-vr-frame", game=FRAME_GAME.format(name="broken"))
    (broken / "halo").write_text("not what SHA256SUMS says\n")
    result, log = frame_launcher_run(installed, tmp_path, UPDATE_TO=str(broken))
    assert result.returncode == 0
    assert "differs from its SHA256SUMS" in (installed / "halo-vr.log.prev").read_text()
    assert "the update was not installed; starting the game as it was" in (installed / "halo-vr.log.prev").read_text()
    assert [line.split(":")[0] for line in (home / "apps/game-runs.txt").read_text().splitlines()] == \
        ["game started", "game started"]
    assert (installed / "halo").read_bytes() == (package / "halo").read_bytes()
    assert not (installed / "update.partial").exists()


def test_frame_install_from_a_computer(tmp_path):
    """tools/frame_install.sh, from a computer: --local, and over ssh (a stand-in that runs the commands here),
    copies a release's zip and this repository's installer to a folder of the Frame's /tmp, runs it there with
    --batch (no questions, no steamos-add-to-steam: it prints how to add the shortcut), and deletes the folder"""
    import frame_package

    package = frame_package_folder(tmp_path / "build/halo-vr-frame")
    archive = tmp_path / "OpenH1-Steam-Frame.zip"
    frame_package.write_zip(package, archive)
    commands = command_folder(tmp_path / "commands")
    for name in ("ssh", "mktemp", "tar", "unzip", "head", "find"):
        found = shutil.which(name)
        if found and not (commands / name).exists():
            (commands / name).symlink_to(found)

    local = tmp_path / "local-home"
    result = subprocess.run([str(ROOT / "tools/frame_install.sh"), "--package", str(archive), "--local", str(local)],
                            capture_output=True, text=True, env={"PATH": str(commands), "HOME": str(tmp_path)})
    assert result.returncode == 0, result.stdout + result.stderr
    assert (local / "apps/halo-vr/halo").read_text() == "halo\n"
    assert "Add a Non-Steam Game" in result.stdout

    frame = tmp_path / "frame-home"
    frame.mkdir()
    stand_ins = tmp_path / "ssh-stand-in"
    stand_ins.mkdir()
    # (ssh <options> -- <host> <command>: the command, run with the Frame's home, its /tmp a folder here)
    (stand_ins / "ssh").write_text(f"""#!/bin/bash
while [ $# -gt 0 ]; do
	case $1 in -O) exit 0 ;; -o) shift 2 ;; --) shift; break ;; *) break ;; esac
done
shift
printf '%s\\n' "$1" >> "{tmp_path}/ssh.txt"
HOME="{frame}" exec bash -c "$1"
""")
    (stand_ins / "ssh").chmod(0o755)
    result = subprocess.run([str(ROOT / "tools/frame_install.sh"), "--package", str(archive), "frame"],
                            capture_output=True, text=True, env={"PATH": f"{stand_ins}:{commands}", "HOME": str(tmp_path)})
    assert result.returncode == 0, result.stdout + result.stderr
    installed = frame / "apps/halo-vr"
    for name in files(package):
        assert (installed / name).read_bytes() == (package / name).read_bytes(), name
    assert "Installing GOAT H1 VR to" in result.stdout and "Add a Non-Steam Game" in result.stdout
    ssh = (tmp_path / "ssh.txt").read_text()
    remote = re.search(r"rm -rf '(/tmp/openh1-install\.[^']+)'", ssh).group(1)
    assert f"bash '{remote}/install.sh' --package '{remote}/halo-vr-frame' --batch" in ssh
    assert not Path(remote).exists()


def test_release_names():
    """the names players see: OpenH1-<machine>[-debug][-symbols]; anything unknown keeps its own"""
    assert release_names.published("halo-windows-release") == "OpenH1-Windows"
    assert release_names.published("halo-windows-debug-symbols") == "OpenH1-Windows-debug-symbols"
    assert release_names.published("halo-linux-x86_64-release") == "OpenH1-Linux"
    assert release_names.published("halo-macos-release") == "OpenH1"
    assert release_names.published("halo-macos-debug") == "OpenH1-debug"
    assert release_names.published("halo-android-release") == "OpenH1-Android"
    assert release_names.published("halo-android-debug") == "OpenH1-Android-debug"
    assert release_names.published("halo-plan9-release") == "halo-plan9-release"
    workflow = (ROOT / ".github/workflows/build.yml").read_text()
    assert 'published="$(python3 tools/release_names.py "$name")"' in workflow
    # (the release job's checkout has only the files it names)
    release_job = workflow[workflow.index("\n  release:"):]
    assert "            tools/release_names.py\n" in release_job[:release_job.index("download-artifact")]


# ---------- install.sh (tools/linux_install.sh), run from a staged release's tree

# stand-ins for SteamOS's helpers: kdialog answers as the test's environment says, steamos-add-to-steam and the
# game note that they ran; pgrep finds no game running
STAND_INS = {
    "kdialog": """#!/bin/sh
echo "kdialog $*" >> "$HOME/calls.txt"
case " $* " in
*" --yesnocancel "*) exit "${KDIALOG_ANSWER:-2}" ;;
*" --getexistingdirectory "*) [ -n "${KDIALOG_FOLDER:-}" ] && echo "$KDIALOG_FOLDER" && exit 0; exit 1 ;;
*" --getopenfilename "*) [ -n "${KDIALOG_FILE:-}" ] && echo "$KDIALOG_FILE" && exit 0; exit 1 ;;
*" Steam Input --yesno "*) exit "${KDIALOG_STEAM_INPUT:-1}" ;;
*" --yesno "*) exit "${KDIALOG_START:-1}" ;;
esac
exit 1
""",
    # (as SteamOS's: the running Steam adds the menu entry's program to its shortcuts.vdf at once, with an app ID
    # of its own, not the one Steam would compute: steam_id)
    "steamos-add-to-steam": f"""#!{sys.executable}
import os, struct, sys, zlib
from pathlib import Path
home = Path(os.environ["HOME"])
with open(home / "calls.txt", "a") as calls:
    calls.write("steamos-add-to-steam " + " ".join(sys.argv[1:]) + "\\n")
entry = dict(line.split("=", 1) for line in Path(sys.argv[1]).read_text().splitlines() if "=" in line)
exe = entry["Exec"]
appid = (zlib.crc32(exe.strip('"').encode()) ^ 0x1234567) | 0x80000000
fields = b"\\x02appid\\0" + struct.pack("<I", appid) + b"\\x01AppName\\0" + entry["Name"].encode() + b"\\0" \\
    + b"\\x01Exe\\0" + exe.encode() + b"\\0\\x01StartDir\\0" + entry["Path"].encode() + b"\\0" \\
    + b"\\x01icon\\0" + entry["Icon"].encode() + b"\\0\\x00tags\\0\\x08"
vdf = home / ".local/share/Steam/userdata/12345/config/shortcuts.vdf"
vdf.parent.mkdir(parents=True, exist_ok=True)
data = vdf.read_bytes() if vdf.exists() else b""
if not (data.startswith(b"\\0shortcuts\\0") and data.endswith(b"\\x08\\x08")):
    data = b"\\0shortcuts\\0\\x08\\x08"
vdf.write_bytes(data[:-2] + b"\\0" + str(len(data)).encode() + b"\\0" + fields + b"\\x08\\x08\\x08")
""",
    # (a process runs while its name is a line of ~/running: Steam's, as the steam stand-in leaves it)
    "pgrep": """#!/bin/sh
[ "$1" = -x ] && [ -f "$HOME/running" ] && grep -qx "$2" "$HOME/running" && echo 7
""",
    # (as Steam's command: "steam -shutdown" closes the running Steam, which writes its localconfig.vdf as it
    # exits: the file in ~/steam-exit-writes, if there is one, as Steam's own newer state; STEAM_STAYS, a Steam
    # that does not close. "steam" starts it, noting whether its Steam Input setting for the shortcut is there.
    # install.sh runs both in the background, so the call is noted before ~/running changes: a test that sees
    # Steam closed or started (started_again) has its call already, never one that comes after it has read them)
    "steam": f"""#!{sys.executable}
import os, sys
from pathlib import Path
home = Path(os.environ["HOME"])
running = home / "running"
localconfig = home / ".local/share/Steam/userdata/12345/config/localconfig.vdf"
names = running.read_text().split() if running.exists() else []
if sys.argv[1:] == ["-shutdown"]:
    note = "steam -shutdown"
    if "steam" in names and not os.environ.get("STEAM_STAYS"):
        flushed = home / "steam-exit-writes"
        if flushed.exists():
            localconfig.write_bytes(flushed.read_bytes())
        names.remove("steam")
else:
    note = " ".join(["steam"] + sys.argv[1:]) + (" (its localconfig.vdf has UseSteamControllerConfig)"
        if localconfig.exists() and b"UseSteamControllerConfig" in localconfig.read_bytes() else "")
    names.append("steam")
with open(home / "calls.txt", "a") as calls:
    calls.write(note + "\\n")
running.write_text("".join(name + "\\n" for name in names))
""",
}
GAME = "#!/bin/sh\necho \"started in $(pwd)\" >> \"$HOME/calls.txt\"\n"
# what install.sh runs besides them: the only commands on its PATH, so that a computer's own kdialog, konsole or
# steamos-add-to-steam is never reached
COMMANDS = ("basename", "bash", "cat", "chmod", "cmp", "cp", "dirname", "find", "grep", "ln", "ls", "mkdir", "mv", "nohup",
            "printf", "python3", "readlink", "rm", "sed", "sh", "sha256sum", "sleep", "sort")


def steam_id(program):
    """the app ID the steamos-add-to-steam stand-in gives a program's shortcut"""
    return (zlib.crc32(str(program).encode()) ^ 0x1234567) | 0x80000000


def shortcuts_vdf(*shortcuts):
    """a binary shortcuts.vdf of shortcuts, each a list of (field, value) pairs: a str a string, an int an int32"""
    data = b"\0shortcuts\0"
    for index, fields in enumerate(shortcuts):
        data += b"\0" + str(index).encode() + b"\0"
        for name, value in fields:
            if isinstance(value, int):
                data += b"\x02" + name.encode() + b"\0" + struct.pack("<I", value)
            else:
                data += b"\x01" + name.encode() + b"\0" + value.encode() + b"\0"
        data += b"\0tags\0\x08\x08"
    return data + b"\x08\x08"


def install_environment(home, path=None, **answers):
    return {"HOME": str(home), "PATH": path or str(command_folder(home.parent / "commands")),
            "OPENH1_INSTALL_WINDOW": "1", **{key: str(value) for key, value in answers.items()}}


def command_folder(folder):
    """a folder of links to COMMANDS, for a PATH of its own"""
    folder.mkdir(exist_ok=True)
    for name in COMMANDS:
        found = shutil.which(name)
        if found and not (folder / name).exists():
            (folder / name).symlink_to(found)
    return folder


@pytest.fixture
def release(tmp_path):
    """an unzipped release (OpenH1-Linux.zip in ~/Downloads), its bin/openh1 a script that notes it started"""
    return unzipped_release(tmp_path, "OpenH1-Linux")


def unzipped_release(tmp_path, name, vr=False):
    """the release name.zip (the VR build's: vr) as the file manager unpacks it in ~/Downloads"""
    build = tmp_path / f"build-{name}"
    build.mkdir()
    (build / "halo").write_text(GAME + f"# {name}\n")
    for part in ("libSDL3.so.0", "SDL3-LICENSE.txt", "openh1.assets"):
        (build / part).write_bytes(part.encode())
    folder = tmp_path / "home/Downloads" / name
    linux_package.stage_tree(folder, build / "halo", build / "libSDL3.so.0", notices(build), vr=vr)
    archive = tmp_path / f"{name}.zip"
    linux_package.write_zip(folder, archive)
    shutil.rmtree(folder)
    # (as the file manager unpacks it: with the zip's modes)
    with zipfile.ZipFile(archive) as opened:
        for info in opened.infolist():
            opened.extract(info, folder)
            (folder / info.filename).chmod((info.external_attr >> 16) & 0o777)
    return folder


def run_install(tree, home, tools=("kdialog", "steamos-add-to-steam"), **answers):
    stand_ins = home.parent / "stand-ins"
    stand_ins.mkdir(exist_ok=True)
    for name in set(tools) | {"pgrep"}:
        (stand_ins / name).write_text(STAND_INS[name])
        (stand_ins / name).chmod(0o755)
    for name in set(STAND_INS) - set(tools) - {"pgrep"}:
        (stand_ins / name).unlink(missing_ok=True)
    commands = command_folder(home.parent / "commands")
    # (PATH_FIRST: a folder of commands before the others)
    first = f"{answers.pop('PATH_FIRST')}:" if "PATH_FIRST" in answers else ""
    environment = {"HOME": str(home), "PATH": f"{first}{stand_ins}:{commands}", "OPENH1_INSTALL_WINDOW": "1",
                   **{key: str(value) for key, value in answers.items()}}
    result = subprocess.run([str(tree / "install.sh")], env=environment, capture_output=True, text=True,
                            stdin=subprocess.DEVNULL)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def calls(home):
    path = home / "calls.txt"
    lines = path.read_text().splitlines() if path.exists() else []
    path.unlink(missing_ok=True)
    return lines


def tree_files(folder):
    return {name for name in files(folder) if name.split("/")[0] in ("install.sh", "bin", "lib", "share")}


def desktop_entry(home):
    text = (home / ".local/share/applications/openh1.desktop").read_text()
    return dict(line.split("=", 1) for line in text.splitlines() if "=" in line)


def test_install_sh_installs_the_tree(release, tmp_path):
    """install.sh, opened in the unzipped release, copies the tree to ~/Applications/OpenH1 (its modes as the zip's,
    the self-updater's tree: bin/openh1 beside share/openh1), copies the maps folder the player names, writes the
    menu entry (the tree's, with the installed program, folder and icon) and adds it to Steam"""
    home = tmp_path / "home"
    disc = tmp_path / "disc"
    (disc / "maps").mkdir(parents=True)
    for name in ("a10.map", "ui.map", "loading.tga"):
        (disc / "maps" / name).write_bytes(name.encode())
    (disc / "maps/notes.txt").write_text("not a map")
    output = run_install(release, home, KDIALOG_ANSWER=1, KDIALOG_FOLDER=disc)
    installed = home / "Applications/OpenH1"
    assert tree_files(installed) == tree_files(release)
    for name in tree_files(release):
        assert (installed / name).read_bytes() == (release / name).read_bytes(), name
        expected = 0o755 if name in ("install.sh", "bin/openh1", "bin/goath1", "lib/openh1/libSDL3.so.0") else 0o644
        assert stat.S_IMODE((installed / name).stat().st_mode) == expected, name
    assert (installed / "share/openh1").is_dir()
    assert not list(installed.rglob("*.new"))
    assert sorted(path.name for path in (installed / "maps").iterdir()) == ["a10.map", "loading.tga", "ui.map"]
    assert not (installed / "maps.partial").exists()
    entry = desktop_entry(home)
    assert entry["Name"] == "GOAT H1"
    assert entry["Exec"] == f'"{installed}/bin/openh1"'
    assert entry["Path"] == str(installed)
    assert entry["Icon"] == f"{installed}/share/icons/hicolor/256x256/apps/openh1.png"
    assert Path(entry["Icon"]).is_file()
    tree_entry = dict(line.split("=", 1) for line in linux_package.DESKTOP_ENTRY.read_text().splitlines() if "=" in line)
    assert {key: value for key, value in entry.items() if key not in ("Exec", "Icon", "Path")} == \
        {key: value for key, value in tree_entry.items() if key not in ("Exec", "Icon")}
    assert calls(home) == [
        "kdialog --title GOAT H1: your game's maps --yes-label A disc image --no-label A maps folder --yesnocancel "
        + "GOAT H1 plays the maps of your own Halo: Combat Evolved for the Xbox.",
        "",
        "Where are they: in a disc image of the game (.iso or .xiso), or in a maps folder copied from its disc?",
        f"kdialog --title GOAT H1: your game's maps --getexistingdirectory {home}",
        f"steamos-add-to-steam {home}/.local/share/applications/openh1.desktop",
    ]
    assert "Added to Steam as GOAT H1" in output
    assert f"{release} can be deleted" in output


def test_install_sh_again_and_update(release, tmp_path):
    """run again, install.sh changes nothing, asks nothing, and adds no second shortcut once Steam has one; a newer
    release's replaces the files that differ and never the player's (config.toml, maps, debug.txt, the
    self-updater's leftovers), nor touches the saved games"""
    home = tmp_path / "home"
    run_install(release, home, KDIALOG_ANSWER=2)
    installed = home / "Applications/OpenH1"
    # (the shortcut the stand-in for steamos-add-to-steam added)
    assert steam_calls(home) == [f"steamos-add-to-steam {home}/.local/share/applications/openh1.desktop"]
    player = {"config.toml": "[display]\nvsync = false\n", "maps/a10.map": "mine", "debug.txt": "log",
              "bin/openh1.old": "an update's", "update-old.txt": "bin/openh1.old\n"}
    for name, text in player.items():
        (installed / name).parent.mkdir(parents=True, exist_ok=True)
        (installed / name).write_text(text)
    saves = home / ".local/share/halo-linux/z/saved.bin"
    saves.parent.mkdir(parents=True)
    saves.write_text("save")

    output = run_install(release, home)
    assert "Nothing changed" in output
    assert calls(home) == []
    assert "Steam has GOAT H1 already" in output

    (release / "bin/openh1").write_text(GAME + "# a newer build\n")
    (release / "share/openh1/brokers.txt").write_text("newer brokers\n")
    output = run_install(release, home)
    assert "  bin/openh1: updated" in output and "  share/openh1/brokers.txt: updated" in output
    assert (installed / "bin/openh1").read_bytes() == (release / "bin/openh1").read_bytes()
    assert stat.S_IMODE((installed / "bin/openh1").stat().st_mode) == 0o755
    assert (installed / "share/openh1/brokers.txt").read_text() == "newer brokers\n"
    for name, text in player.items():
        assert (installed / name).read_text() == text, name
    assert saves.read_text() == "save"
    assert not list(installed.rglob("*.new"))
    assert calls(home) == []


def test_install_sh_without_steamos(release, tmp_path):
    """on a desktop without kdialog or steamos-add-to-steam, install.sh installs, writes the menu entry and says
    what is left: the maps (which the game asks for at its first start) and Steam's shortcut, if Steam is there"""
    home = tmp_path / "home"
    (home / ".local/share/Steam").mkdir(parents=True)
    output = run_install(release, home, tools=())
    assert (home / "Applications/OpenH1/bin/openh1").is_file()
    assert desktop_entry(home)["Name"] == "GOAT H1"
    assert "No maps yet: at its first start" in output
    assert "Add a Non-Steam Game to My Library" in output and "Tick GOAT H1 in the list" in output
    assert calls(home) == []


def test_install_sh_disc_image(release, tmp_path):
    """a disc image is the game's own to copy from (its first start: first_start.c's platform_offer_game_data,
    which finds an image in the game's folder among other places): install.sh links it there, and, if the
    player says so, starts the game in its folder"""
    home = tmp_path / "home"
    image = tmp_path / "Halo.iso"
    image.write_bytes(b"MICROSOFT*XBOX*MEDIA")
    output = run_install(release, home, KDIALOG_ANSWER=0, KDIALOG_FILE=image, KDIALOG_START=0)
    installed = home / "Applications/OpenH1"
    link = installed / "Halo.iso"
    assert link.is_symlink() and os.readlink(link) == str(image)
    assert not (installed / "maps").exists()
    assert f"copies the maps from {image}" in output
    for _ in range(100):
        if (home / "calls.txt").exists() and "started" in (home / "calls.txt").read_text():
            break
        subprocess.run(["sleep", "0.05"])
    assert f"started in {installed}" in calls(home)
    # (once linked, not asked again)
    run_install(release, home, KDIALOG_START=1)
    assert not [line for line in calls(home) if "--yesnocancel" in line]


def test_install_sh_in_place(release, tmp_path):
    """a tree already played (config.toml or maps beside bin/: a release unpacked before install.sh came with it,
    which the self-updater then gave it) stays where it is: install.sh copies nothing, and writes its menu entry"""
    home = tmp_path / "home"
    (release / "maps").mkdir()
    (release / "maps/a10.map").write_text("mine")
    output = run_install(release, home)
    assert not (home / "Applications").exists()
    assert desktop_entry(home)["Exec"] == f'"{release}/bin/openh1"'
    assert "it stays there" in output
    assert calls(home) == [f"steamos-add-to-steam {home}/.local/share/applications/openh1.desktop"]


def test_install_sh_refuses_another_folder(release, tmp_path):
    """~/Applications/OpenH1 holding something else is left alone"""
    home = tmp_path / "home"
    (home / "Applications/OpenH1").mkdir(parents=True)
    (home / "Applications/OpenH1/something.txt").write_text("not the game's")
    commands = command_folder(tmp_path / "commands")
    result = subprocess.run([str(release / "install.sh")], capture_output=True, text=True,
                            env={"HOME": str(home), "PATH": str(commands), "OPENH1_INSTALL_WINDOW": "1"})
    assert result.returncode == 1 and "holds no OpenH1" in result.stderr
    assert sorted(path.name for path in (home / "Applications/OpenH1").iterdir()) == ["something.txt"]


def test_install_sh_refuses_while_running(release, tmp_path):
    """while the game runs (pgrep -x openh1), install.sh copies nothing"""
    home = tmp_path / "home"
    stand_ins = tmp_path / "running"
    stand_ins.mkdir()
    (stand_ins / "pgrep").write_text("#!/bin/sh\nprintf '41\\n42\\n'\n")
    (stand_ins / "pgrep").chmod(0o755)
    commands = command_folder(tmp_path / "commands")
    result = subprocess.run([str(release / "install.sh")], capture_output=True, text=True,
                            env={"HOME": str(home), "PATH": f"{stand_ins}:{commands}", "OPENH1_INSTALL_WINDOW": "1"})
    assert result.returncode == 3 and "GOAT H1 is running (process 41 42)" in result.stderr
    assert not (home / "Applications/OpenH1/bin").exists()


def snapshot(folder):
    """every file under folder, with its contents and modification time"""
    return {path: (path.read_bytes(), path.stat().st_mtime_ns) for path in folder.rglob("*") if path.is_file()}


def steam_calls(home):
    return [line for line in calls(home) if line.startswith("steamos-add-to-steam")]


def test_vr_tree_marker(build, tmp_path):
    """the VR build's tree (tools/ci_build.py --vr) has share/openh1/vr, install.sh's sign of it; the other has not"""
    linux_package.stage_tree(tmp_path / "vr", build / "halo", None, notices(build), vr=True)
    linux_package.stage_tree(tmp_path / "flat", build / "halo", None, notices(build))
    assert (tmp_path / "vr/share/openh1/vr").is_file() and not (tmp_path / "flat/share/openh1/vr").exists()
    # (each with its own library artwork: OpenH1 VR's, and OpenH1's)
    for tree, variant in (("vr", "openh1-vr"), ("flat", "openh1")):
        assert (tmp_path / tree / "share/openh1/steam/capsule.png").read_bytes() == \
            (ROOT / f"port/assets/steam/{variant}/capsule.png").read_bytes()
    assert (ROOT / "port/assets/steam/openh1/capsule.png").read_bytes() != \
        (ROOT / "port/assets/steam/openh1-vr/capsule.png").read_bytes()
    assert "linux_package.stage_tree(dist, executable, library, notices, vr=args.vr)" in \
        (ROOT / "tools/ci_build.py").read_text()


def test_install_sh_vr_beside_the_other(release, tmp_path):
    """the VR build's install.sh installs it in ~/Applications/OpenH1-VR, with the menu entry openh1-vr.desktop
    named OpenH1 VR (as the Steam Frame's) and a Steam shortcut of its own, and never touches the other build, its
    entry or its shortcut; nor the other way round"""
    home = tmp_path / "home"
    vr_release = unzipped_release(tmp_path, "OpenH1-Linux-VR", vr=True)
    run_install(release, home)
    desktop, vr = home / "Applications/OpenH1", home / "Applications/OpenH1-VR"
    entries = home / ".local/share/applications"
    assert steam_calls(home) == [f"steamos-add-to-steam {entries}/openh1.desktop"]
    (desktop / "config.toml").write_text("mine")
    shortcuts = home / ".local/share/Steam/userdata/12345/config/shortcuts.vdf"
    before =snapshot(desktop), (entries / "openh1.desktop").read_bytes()

    output = run_install(vr_release, home)
    assert f"Installing GOAT H1 VR to {vr}" in output and "Added to Steam as GOAT H1 VR" in output
    assert (vr / "bin/openh1").read_bytes() == (vr_release / "bin/openh1").read_bytes()
    assert (vr / "share/openh1/vr").is_file()
    entry = dict(line.split("=", 1) for line in (entries / "openh1-vr.desktop").read_text().splitlines() if "=" in line)
    assert entry["Name"] == "GOAT H1 VR" and entry["Exec"] == f'"{vr}/bin/openh1"' and entry["Path"] == str(vr)
    assert entry["Icon"] == f"{vr}/share/icons/hicolor/256x256/apps/openh1.png"
    # (Steam's shortcut to the other build is not this one's; each has its own art)
    assert steam_calls(home) == [f"steamos-add-to-steam {entries}/openh1-vr.desktop"]
    grid = shortcuts.parent / "grid"
    assert (grid / f"{steam_id(vr / 'bin/openh1')}p.png").read_bytes() == \
        (ROOT / "port/assets/steam/openh1-vr/capsule.png").read_bytes()
    assert (grid / f"{steam_id(desktop / 'bin/openh1')}p.png").read_bytes() == \
        (ROOT / "port/assets/steam/openh1/capsule.png").read_bytes()
    assert (snapshot(desktop), (entries / "openh1.desktop").read_bytes()) == before

    # the other build again: its own shortcut found, the VR build's untouched
    shortcuts.write_bytes(shortcuts_vdf([("appid", steam_id(vr / "bin/openh1")), ("AppName", "OpenH1 VR"),
                                         ("Exe", f'"{vr}/bin/openh1"')]))
    before_vr = snapshot(vr), (entries / "openh1-vr.desktop").read_bytes()
    output = run_install(release, home)
    assert "Nothing changed" in output and "Added to Steam as GOAT H1:" in output
    assert steam_calls(home) == [f"steamos-add-to-steam {entries}/openh1.desktop"]
    assert (snapshot(vr), (entries / "openh1-vr.desktop").read_bytes()) == before_vr
    assert desktop_entry(home)["Name"] == "GOAT H1"


def test_steam_artwork(release, tmp_path):
    """install.sh gives Steam's shortcut its library artwork, in the grid folder of each Steam user whose
    shortcuts.vdf has a shortcut to the game, named after the shortcut's app ID there (or, in an entry without
    one, Steam's own for it); a player's own picture is kept, and the art install.sh wrote before is replaced;
    shortcuts.vdf is only read"""
    home = tmp_path / "home"
    program = f"{home}/Applications/OpenH1/bin/openh1"
    userdata = home / ".local/share/Steam/userdata"
    vdfs = {
        # (as steamos-add-to-steam writes them, among the player's other shortcuts)
        "111": shortcuts_vdf([("appid", 4267074786), ("AppName", "Another game"), ("Exe", '"/home/deck/game.sh"')],
                             [("appid", 3000000001), ("AppName", "OpenH1"), ("Exe", f'"{program}"'),
                              ("StartDir", f'"{home}/Applications/OpenH1/"'), ("LastPlayTime", 0)]),
        # (an older Steam's, without the app ID)
        "222": shortcuts_vdf([("AppName", "OpenH1"), ("Exe", f'"{program}"')]),
        # (a user without the shortcut)
        "333": shortcuts_vdf([("appid", 5), ("AppName", "Another game"), ("Exe", '"/home/deck/game.sh"')]),
    }
    for user, data in vdfs.items():
        (userdata / user / "config").mkdir(parents=True)
        (userdata / user / "config/shortcuts.vdf").write_bytes(data)
    own = userdata / "111/config/grid/3000000001_hero.png"
    own.parent.mkdir()
    own.write_bytes(b"the player's own hero")
    before = {path: (path.read_bytes(), path.stat().st_mtime_ns) for path in userdata.glob("*/config/shortcuts.vdf")}

    output = run_install(release, home)
    assert "Steam has GOAT H1 already" in output and steam_calls(home) == []
    art = release / "share/openh1/steam"
    # Steam's app ID for a shortcut without one: the CRC-32 of its Exe (quoted) and AppName, the top bit set, as
    # a Steam Frame's shortcuts.vdf has it ("NFL Blitz 2027": 3691653383)
    assert zlib.crc32(b'"/home/steamos/Blitz2000/Blitz2000.sh"NFL Blitz 2027') | 0x80000000 == 3691653383
    computed = zlib.crc32(f'"{program}"OpenH1'.encode()) | 0x80000000
    for user, appid in (("111", 3000000001), ("222", computed)):
        grid = userdata / user / "config/grid"
        for name, kind in ((f"{appid}p.png", "capsule"), (f"{appid}.png", "wide"), (f"{appid}_logo.png", "logo"),
                           (f"{appid}_icon.png", "icon"), (f"{appid}_hero.png", "hero")):
            if user == "111" and kind == "hero":
                assert (grid / name).read_bytes() == b"the player's own hero"
            else:
                assert (grid / name).read_bytes() == (art / f"{kind}.png").read_bytes(), (user, name)
    assert not (userdata / "333/config/grid").exists()
    assert sorted(path.name for path in (userdata / "111/config/grid").iterdir()) == \
        sorted(f"3000000001{suffix}" for suffix in ("p.png", ".png", "_hero.png", "_logo.png", "_icon.png"))
    assert "Steam library artwork: 9 files (if Steam shows a blank tile, restart Steam)" in output
    assert "Steam library artwork of your own kept: 3000000001_hero.png" in output
    assert {path: (path.read_bytes(), path.stat().st_mtime_ns) for path in userdata.glob("*/config/shortcuts.vdf")} == before

    # a newer release's art replaces what install.sh wrote, never the player's
    (art / "capsule.png").write_bytes(b"a newer capsule")
    output = run_install(release, home)
    assert (userdata / "111/config/grid/3000000001p.png").read_bytes() == b"a newer capsule"
    assert (userdata / "222/config/grid" / f"{computed}p.png").read_bytes() == b"a newer capsule"
    assert own.read_bytes() == b"the player's own hero"
    assert "Steam library artwork: 2 files" in output
    output = run_install(release, home)
    assert "Steam library artwork:" not in output


# ---------- Steam Input, turned off for a shortcut install.sh has just added (steam_input_off)

# a localconfig.vdf as Steam writes it (text VDF, tabs; its key and value apart by two tabs; quotes and
# backslashes escaped), with other games' entries: the "apps" in the root, where Steam keeps a game's Steam Input
# (another game's set to on, "2"), and Software's own "apps" (play times), which has the shortcut's app ID too
def localconfig(shortcut_id, root_apps=True):
    apps = f'''	"apps"
	{{
		"620"
		{{
			"UseSteamControllerConfig"		"2"
			"SteamControllerRumble"		"-1"
			"SteamControllerRumbleIntensity"		"320"
		}}
	}}
''' if root_apps else ""
    return f'''"UserLocalConfigStore"
{{
	"Broadcast"
	{{
		"Permissions"		"1"
	}}
	"Software"
	{{
		"Valve"
		{{
			"Steam"
			{{
				"apps"
				{{
					"{shortcut_id}"
					{{
						"LastPlayed"		"1793498009"
					}}
					"730"
					{{
						"LastPlayed"		"1793400000"
						"Playtime"		"12"
					}}
				}}
			}}
		}}
	}}
	"WebStorage"
	{{
		"LocalizedTagNames2_english"		"{{\\"version_hash\\":\\"711684454\\",\\"tags\\":[]}}"
		"Path"		"C:\\\\Games\\\\Halo"
	}}
{apps}	"ControllerTypesUsed"		"controller_generic,controller_xboxone,"
}}
'''


def read_vdf(text):
    """text VDF as a list of (key, value) pairs, a block's value such a list (the test's own reader, apart from
    install.sh's)"""
    root, key = [], None
    stack = [root]
    for quoted, brace in re.findall(r'"((?:[^"\\]|\\.)*)"|([{}])', text):
        if brace == "{":
            block = []
            stack[-1].append((key, block))
            stack.append(block)
            key = None
        elif brace == "}":
            stack.pop()
        elif key is None:
            key = quoted
        else:
            stack[-1].append((key, quoted))
            key = None
    assert len(stack) == 1 and key is None
    return root


def write_vdf(items, depth=0):
    """items as Steam writes a text VDF"""
    text = ""
    for key, value in items:
        if isinstance(value, list):
            text += "\t" * depth + f'"{key}"\n' + "\t" * depth + "{\n" + write_vdf(value, depth + 1) + "\t" * depth + "}\n"
        else:
            text += "\t" * depth + f'"{key}"\t\t"{value}"\n'
    return text


def without(items, *path):
    """items without the last key of path (in the blocks path names), and blocks left empty by that"""
    result = []
    for key, value in items:
        if key == path[0]:
            if len(path) == 1:
                continue
            value = without(value, *path[1:])
            if not value:
                continue
        result.append((key, value))
    return result


def steam_input_home(tmp_path, shortcut_id, root_apps=True):
    """a home whose Steam (running) has user 12345's localconfig.vdf, and another user's files, with others'
    shortcuts; ~/steam-exit-writes, what Steam writes as it exits: its newer play time for the game"""
    home = tmp_path / "home"
    config = home / ".local/share/Steam/userdata/12345/config"
    config.mkdir(parents=True)
    (config / "localconfig.vdf").write_text(localconfig(shortcut_id, root_apps))
    (home / "steam-exit-writes").write_text(localconfig(shortcut_id, root_apps).replace("1793498009", "1793499999"))
    other = home / ".local/share/Steam/userdata/999/config"
    other.mkdir(parents=True)
    (other / "localconfig.vdf").write_text(localconfig(4000000000))
    (other / "shortcuts.vdf").write_bytes(shortcuts_vdf([("appid", 5), ("AppName", "Another game"),
                                                         ("Exe", '"/home/deck/game.sh"')]))
    (home / "running").write_text("steam\n")
    return home, config / "localconfig.vdf"


def started_again(home):
    """the calls, once Steam has started again (install.sh leaves it starting)"""
    for _ in range(50):
        if "steam" in (home / "running").read_text().split():
            break
        time.sleep(0.1)
    return calls(home)


def called(home, call):
    """the calls, once call is among them: a call install.sh leaves running in the background (steam -shutdown,
    to a Steam that does not close) may note itself after install.sh has finished"""
    path = home / "calls.txt"
    for _ in range(50):
        if path.exists() and call in path.read_text().splitlines():
            break
        time.sleep(0.1)
    return calls(home)


STEAM_TOOLS = ("kdialog", "steamos-add-to-steam", "steam")
HOW = "in Game Mode, Steam > GOAT H1 > the controller icon > Disable Steam Input."


@pytest.mark.parametrize("root_apps", [True, False])
def test_steam_input_off_for_a_new_shortcut(release, tmp_path, root_apps):
    """a new install on SteamOS, asked (Yes), closes Steam, then sets Steam Input off for the shortcut Steam has
    just added: UseSteamControllerConfig "0" under localconfig.vdf's root "apps", by the shortcut's app ID
    (unsigned), in the file Steam wrote as it exited, a copy of which it keeps; nothing else in it changes, and it
    is as Steam writes it; then starts Steam again. Another user's files, other games' entries and Software's own
    "apps" (with the same app ID) stay as they were"""
    shortcut_id = steam_id(tmp_path / "home/Applications/OpenH1/bin/openh1")
    assert shortcut_id >= 0x80000000  # (its int32 negative: the key is the unsigned one)
    home, path = steam_input_home(tmp_path, shortcut_id, root_apps)
    other = snapshot(home / ".local/share/Steam/userdata/999")
    output = run_install(release, home, tools=STEAM_TOOLS, KDIALOG_STEAM_INPUT=0)
    entries = home / ".local/share/applications"
    noted = started_again(home)
    assert noted[next(index for index, line in enumerate(noted) if line.startswith("steamos")):] == [
        f"steamos-add-to-steam {entries}/openh1.desktop",
        "kdialog --title GOAT H1: Steam Input --yesno Turn Steam Input off for GOAT H1?",
        "",
        "GOAT H1 then reads the Steam Deck's controls itself: gyro aiming, and the back buttons (L4, L5, R4, R5) in "
        "Button Remapping. The Steam and Quick Access buttons stay Steam's.",
        "",
        "Steam closes for a moment and opens again.",
        "steam -shutdown",
        "steam (its localconfig.vdf has UseSteamControllerConfig)",
    ]
    text = path.read_text()
    flushed = (home / "steam-exit-writes").read_text()
    # (the file Steam wrote as it exited, its newer play time kept, with the setting added)
    assert "1793499999" in text
    assert (path.parent / "localconfig.vdf.before-openh1").read_text() == flushed
    items = read_vdf(text)
    store = dict(items)["UserLocalConfigStore"]
    assert dict(dict(store)["apps"])[str(shortcut_id)] == [("UseSteamControllerConfig", "0")]
    assert without(items, "UserLocalConfigStore", "apps", str(shortcut_id), "UseSteamControllerConfig") == \
        read_vdf(flushed)
    # (as Steam writes it, so that its own writing of it, at its next exit, is the same)
    assert write_vdf(items) == text
    assert snapshot(home / ".local/share/Steam/userdata/999") == other
    assert "Steam Input: off for this shortcut" in output and HOW not in output
    assert "Steam started again." in output

    # negative control: the reader sees a setting that is not there, and Steam's format apart from another's
    assert without(read_vdf(flushed), "UserLocalConfigStore", "apps", str(shortcut_id), "UseSteamControllerConfig") \
        == read_vdf(flushed)
    assert read_vdf(text) != read_vdf(flushed)
    assert write_vdf(items) != text.replace('"\t\t"0"', '" "0"')


def test_steam_input_kept_on_reinstall_and_update(release, tmp_path):
    """install.sh again (a reinstall, an update) never changes Steam Input, nor closes Steam: Steam has the
    shortcut, so whatever its setting is (the player's own, or none) it is the player's"""
    shortcut_id = steam_id(tmp_path / "home/Applications/OpenH1/bin/openh1")
    home, path = steam_input_home(tmp_path, shortcut_id)
    run_install(release, home, tools=STEAM_TOOLS, KDIALOG_STEAM_INPUT=0)
    started_again(home)
    for players in ('"UseSteamControllerConfig"\t\t"2"', '"UseSteamControllerConfig"\t\t"1"', None):
        text = path.read_text()
        old = '"UseSteamControllerConfig"\t\t"0"'
        changed = text.replace(old, players) if players else without_setting(text, old)
        path.write_text(changed)
        before = snapshot(home / ".local/share/Steam")
        output = run_install(release, home, tools=STEAM_TOOLS, KDIALOG_STEAM_INPUT=0)
        assert "Steam has GOAT H1 already" in output
        assert steam_input_calls(home) == []
        assert snapshot(home / ".local/share/Steam") == before
        path.write_text(text)
    # (an update too)
    (release / "bin/openh1").write_text(GAME + "# a newer build\n")
    before = snapshot(home / ".local/share/Steam")
    output = run_install(release, home, tools=STEAM_TOOLS, KDIALOG_STEAM_INPUT=0)
    assert "bin/openh1: updated" in output and steam_input_calls(home) == []
    assert snapshot(home / ".local/share/Steam") == before


def steam_input_calls(home):
    """the calls that concern Steam Input: its question, and Steam's command"""
    return [line for line in calls(home) if "Steam Input" in line or line.startswith("steam ")]


def without_setting(text, line):
    """text without the line that holds line (the player's choosing Steam's default again)"""
    return "".join(part for part in text.splitlines(keepends=True) if line not in part)


@pytest.mark.parametrize("value", ["0", "1", "2"])
def test_steam_input_players_setting_kept_for_a_new_shortcut(release, tmp_path, value):
    """a shortcut Steam adds whose app ID has a Steam Input setting already (the player's, for a shortcut of the
    same program and name removed before): kept, nothing asked and Steam not closed"""
    shortcut_id = steam_id(tmp_path / "home/Applications/OpenH1/bin/openh1")
    home, path = steam_input_home(tmp_path, shortcut_id)
    text = localconfig(shortcut_id).replace('\t\t"620"\n', f'\t\t"{shortcut_id}"\n\t\t{{\n\t\t\t'
                                            f'"UseSteamControllerConfig"\t\t"{value}"\n\t\t}}\n\t\t"620"\n')
    path.write_text(text)
    output = run_install(release, home, tools=STEAM_TOOLS, KDIALOG_STEAM_INPUT=0)
    assert steam_input_calls(home) == []
    assert path.read_text() == text
    assert not (path.parent / "localconfig.vdf.before-openh1").exists()
    assert HOW not in output


def test_steam_input_set_in_an_app_block_without_it(release, tmp_path):
    """an app ID whose root "apps" block has other settings but no Steam Input: the setting goes in that block"""
    shortcut_id = steam_id(tmp_path / "home/Applications/OpenH1/bin/openh1")
    home, path = steam_input_home(tmp_path, shortcut_id)
    text = localconfig(shortcut_id).replace('\t\t"620"\n', f'\t\t"{shortcut_id}"\n\t\t{{\n\t\t\t'
                                            f'"SteamControllerRumble"\t\t"-1"\n\t\t}}\n\t\t"620"\n')
    path.write_text(text)
    (home / "steam-exit-writes").write_text(text)
    run_install(release, home, tools=STEAM_TOOLS, KDIALOG_STEAM_INPUT=0)
    started_again(home)
    items = read_vdf(path.read_text())
    assert dict(dict(dict(items)["UserLocalConfigStore"])["apps"])[str(shortcut_id)] == \
        [("SteamControllerRumble", "-1"), ("UseSteamControllerConfig", "0")]
    assert write_vdf(items) == path.read_text()


@pytest.mark.parametrize("case", ["declined", "steam stays", "unreadable", "no localconfig", "no steam command"])
def test_steam_input_left_as_it_was(release, tmp_path, case):
    """where Steam Input cannot be turned off safely (the player says No; Steam does not close; a localconfig.vdf
    install.sh cannot read, or none; no steam command to close it with), localconfig.vdf is not written and
    install.sh says how to do it in Steam"""
    shortcut_id = steam_id(tmp_path / "home/Applications/OpenH1/bin/openh1")
    home, path = steam_input_home(tmp_path, shortcut_id)
    answers, tools = {"KDIALOG_STEAM_INPUT": 0}, STEAM_TOOLS
    if case == "declined":
        answers["KDIALOG_STEAM_INPUT"] = 1
    elif case == "steam stays":
        answers["STEAM_STAYS"] = 1
    elif case == "unreadable":
        path.write_text(localconfig(shortcut_id)[:-2])  # (its last brace gone)
    elif case == "no localconfig":
        path.unlink()
    else:
        tools = ("kdialog", "steamos-add-to-steam")
    before = path.read_bytes() if path.exists() else None
    if case == "steam stays":
        # (90 seconds' wait, shortened: sleep returns at once)
        fast = tmp_path / "fast"
        fast.mkdir()
        (fast / "sleep").write_text("#!/bin/sh\nexit 0\n")
        (fast / "sleep").chmod(0o755)
        answers["PATH_FIRST"] = fast
    output = run_install(release, home, tools=tools, **answers)
    assert (path.read_bytes() if path.exists() else None) == before
    assert not (path.parent / "localconfig.vdf.before-openh1").exists()
    assert HOW in output
    noted = called(home, "steam -shutdown") if case == "steam stays" else calls(home)
    steam = [line for line in noted if line.startswith("steam ")]
    assert steam == (["steam -shutdown"] if case == "steam stays" else [])
    assert (home / "running").read_text().split() == ["steam"]


def test_steam_input_with_steam_closed(release, tmp_path):
    """with Steam not running, the setting is written at once: Steam neither closed nor started"""
    shortcut_id = steam_id(tmp_path / "home/Applications/OpenH1/bin/openh1")
    home, path = steam_input_home(tmp_path, shortcut_id)
    (home / "running").unlink()
    run_install(release, home, tools=STEAM_TOOLS, KDIALOG_STEAM_INPUT=0)
    assert [line for line in calls(home) if line.startswith("steam")] == \
        [f"steamos-add-to-steam {home}/.local/share/applications/openh1.desktop"]
    assert dict(dict(dict(read_vdf(path.read_text()))["UserLocalConfigStore"])["apps"])[str(shortcut_id)] == \
        [("UseSteamControllerConfig", "0")]


def test_steam_input_not_for_the_vr_build(tmp_path):
    """the VR build's new shortcut keeps Steam's Steam Input (its controllers are the headset's)"""
    vr_release = unzipped_release(tmp_path, "OpenH1-Linux-VR", vr=True)
    shortcut_id = steam_id(tmp_path / "home/Applications/OpenH1-VR/bin/openh1")
    home, path = steam_input_home(tmp_path, shortcut_id)
    before = path.read_bytes()
    output = run_install(vr_release, home, tools=STEAM_TOOLS, KDIALOG_STEAM_INPUT=0)
    assert "Added to Steam as GOAT H1 VR" in output
    assert steam_input_calls(home) == []
    assert path.read_bytes() == before and HOW not in output


def steam_tool():
    """install.sh's python (steam_python), its functions, run with no mode"""
    script = linux_package.INSTALLER.read_text()
    source = script.split("steam_python() {", 1)[1].split("<<'PYTHON'\n", 1)[1].split("\nPYTHON\n", 1)[0]
    namespace = {"__name__": "steam_tool"}
    saved = sys.argv
    sys.argv = ["-", "none", "/nonexistent", "/nonexistent", "/nonexistent", "/nonexistent"]
    try:
        exec(compile(source, "steam_python", "exec"), namespace)
    finally:
        sys.argv = saved
    return namespace


def test_steam_input_edit_checked():
    """install.sh's localconfig.vdf edit: added text only, the rest byte for byte (CRLF kept, a brace after
    something on its line, a // comment and a [$WIN32] condition read); a second edit changes nothing; its check
    refuses any other change (negative controls), and the reader refuses a file it cannot read"""
    tool = steam_tool()
    text = localconfig(3000000001)
    new = tool["steam_input_off"](text, [3000000001, 2547667450])
    assert new.startswith(text[:text.index('\t\t"620"')]) and len(new) > len(text)
    assert tool["checked"](text, new, [3000000001, 2547667450]) == new
    assert tool["steam_input_off"](new, [3000000001, 2547667450]) == new
    assert tool["unset"](new, [3000000001, 2547667450, 5]) == [5]
    for bad in (new.replace('"Playtime"\t\t"12"', '"Playtime"\t\t"13"'),  # another value changed
                new.replace('\t\t\t\t\t\t"LastPlayed"\t\t"1793400000"\n', ""),  # another key gone
                new.replace('"UseSteamControllerConfig"\t\t"0"', '"UseSteamControllerConfig"\t\t"2"', 1),
                new.replace('"UseSteamControllerConfig"\t\t"0"', '"Other"\t\t"0"', 1),  # not the setting
                new + '"More"\t\t"1"\n'):  # a key outside the store
        with pytest.raises(ValueError):
            tool["checked"](text, bad, [3000000001, 2547667450])
    crlf = text.replace("\n", "\r\n")
    edited = tool["steam_input_off"](crlf, [3000000001])
    assert "\n" not in edited.replace("\r\n", "") and edited.replace("\r\n", "\n") == \
        tool["steam_input_off"](text, [3000000001])
    odd = '// a comment\n"UserLocalConfigStore"\n{\n\t"a"\t\t"b" [$WIN32]\n\t"apps" { "7" { "x" "y" } }\n}\n'
    edited = tool["checked"](odd, tool["steam_input_off"](odd, [7, 8]), [7, 8])
    assert dict(dict(dict(read_vdf(edited))["UserLocalConfigStore"])["apps"]) == \
        {"7": [("x", "y"), ("UseSteamControllerConfig", "0")], "8": [("UseSteamControllerConfig", "0")]}
    for unreadable in (text[:-3], text + "}\n", '"UserLocalConfigStore"\n{\n\t"apps"\t\t"x"\n}\n',
                       '"Other"\n{\n}\n', text + '"unterminated',
                       text.replace('"Broadcast"\n\t{', '{')):
        with pytest.raises(ValueError):
            tool["steam_input_off"](unreadable, [3000000001])


def test_install_sh_refuses_the_other_build(release, tmp_path):
    """a folder that holds the other build (moved there by hand) is not replaced by this one"""
    home = tmp_path / "home"
    vr_release = unzipped_release(tmp_path, "OpenH1-Linux-VR", vr=True)
    shutil.copytree(release, home / "Applications/OpenH1-VR")
    commands = command_folder(tmp_path / "commands")
    result = subprocess.run([str(vr_release / "install.sh")], capture_output=True, text=True,
                            env={"HOME": str(home), "PATH": str(commands), "OPENH1_INSTALL_WINDOW": "1"})
    assert result.returncode == 1 and "holds the other build" in result.stderr
    assert not (home / "Applications/OpenH1-VR/share/openh1/vr").exists()


def test_install_sh_shell():
    """bash -n, and shellcheck where there is one"""
    for script in (linux_package.INSTALLER, ROOT / "tools/frame_install.sh"):
        subprocess.run(["bash", "-n", str(script)], check=True)
        if shutil.which("shellcheck"):
            subprocess.run(["shellcheck", str(script)], check=True)


def test_android_release():
    """the Android app is published as its APK alone (OpenH1-Android[-debug].apk), which carries the licences
    (tools/android_build.py's assets/licenses, as tools/ci_build.py's dist has none beside it), and is the file its
    self-updater asks for (Updater.java)"""
    workflow = (ROOT / ".github/workflows/build.yml").read_text()
    package = workflow[workflow.index("- name: Package"):workflow.index("- name: Publish")]
    android = package[package.index("halo-android-*)"):]
    android = android[:android.index(";;")]
    assert 'cp "${apks[0]}" "release/$published.apk"' in android
    publish = workflow[workflow.index("- name: Publish"):]
    create = publish[publish.index("gh release create"):]
    assert "release/*.apk" in create[:create.index("\n")]
    assert "halo-android-release.zip" not in workflow and "OpenH1-Android.apk" in workflow
    # the artifacts the Package step reads: the debug build's on every push, the release build's on master
    for config in ("debug", "release"):
        assert f"name: halo-android-{config}" in workflow and f"path: dist/halo-android-{config}" in workflow
    updater = (ROOT / "port/android/app/src/main/java/com/halo/decomp/Updater.java").read_text()
    asset = re.search(r'String asset = "([^"]+)" \+ \(BuildConfig\.DEBUG \? "([^"]+)" : ""\) \+ "\.apk";', updater)
    assert asset, "Updater.java's asset name"
    assert asset.group(1) == release_names.published("halo-android-release")
    assert asset.group(1) + asset.group(2) == release_names.published("halo-android-debug")
    assert "ZipInputStream" not in updater and "Halo:" not in updater
    builder = (ROOT / "tools/android_build.py").read_text()
    assert 'assets_dir / "licenses"' in builder and 'release_notices("android")' in builder
    assert '(SDL_DIR / "LICENSE.txt", "SDL3-LICENSE.txt")' in builder


def test_embedded_fonts_carry_their_licenses():
    """Every font the builds embed (port/assets/fonts/fonts.json: each tag's font and its fallback, tools/
    embed_assets.py) has its licence in every platform's release (desktop and dedicated server alike) and the Steam
    Frame's package: a font file Family-Style.ttf, a notice Family-*.txt."""
    import embed_assets
    import frame_package
    families = {name.split("-", 1)[0] for name in embed_assets.font_files()}
    assert {"CyborgInterfaceGothic", "Overpass"} <= families
    for platform in ("linux_x86_64", "linux_arm64", "windows", "macos", "ios", "android"):
        names = {name for _, name in linux_package.notices(platform)}
        for family in families:
            assert any(name.startswith(f"{family}-") for name in names), (platform, family)
    for family in families:
        assert any(name.startswith(f"{family}-") for name in frame_package.LICENSES), family
    for source, name in linux_package.notices("linux_x86_64"):
        assert source.is_file(), source
    # (Cyborg Interface Gothic's notice: both its copyright lines, and the licence's whole text)
    cyborg = (ROOT / "port/assets/fonts/CyborgInterfaceGothic-OFL.txt").read_text()
    assert "Copyright 2021 The Overpass Project Authors" in cyborg and "Copyright (c) 2026, fizzy" in cyborg
    assert "SIL OPEN FONT LICENSE Version 1.1 - 26 February 2007" in cyborg and "DISCLAIMER" in cyborg
