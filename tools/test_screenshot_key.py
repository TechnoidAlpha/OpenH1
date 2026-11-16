"""The Screenshot key (controls.screenshot) in the game: a hidden run on the
main menu presses it twice (debug.test_input "menu:... screenshot"), and two
PNG files of the window's size, named for the date and time, appear in the
data folder's screenshots folder. The guest asks the host for the picture
(XGPU_STREAM_SCREENSHOT), which writes it (port/android/host/host_screenshot.c).

Needs a build (build/macos/halo; or build/linux_x86_64/halo or
build/linux_arm64/halo with xvfb-run and a Vulkan driver) and assets/maps.
The run has its own folders (HOME too, on macOS), no sound and no network."""
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
MAPS = ROOT / "assets" / "maps"
NAME = re.compile(r"^\d{4}-\d{2}-\d{2}_\d{2}\.\d{2}\.\d{2}(_\d+)?\.png$")


def build_folder():
    if sys.platform == "darwin":
        return ROOT / "build" / "macos"
    for name in ("linux_x86_64", "linux_arm64"):
        if (ROOT / "build" / name / "halo").is_file():
            return ROOT / "build" / name
    return None


@pytest.fixture(scope="module")
def game(tmp_path_factory) -> Path:
    """the build's executable, copied with what it loads beside it to a folder of its own: its config.toml,
    which it writes beside itself, is the test's"""
    folder = build_folder()
    if not folder or not (folder / "halo").is_file() or not (MAPS / "ui.map").is_file():
        pytest.skip("needs a macOS or Linux build and assets/maps")
    if sys.platform != "darwin" and not shutil.which("xvfb-run"):
        pytest.skip("needs xvfb-run")
    copy = tmp_path_factory.mktemp("game")
    for name in ("halo", "libSDL3.0.dylib", "libSDL3.so.0", "shaders.metallib", "openh1.assets"):
        if (folder / name).is_file():
            shutil.copy2(folder / name, copy / name)
    return copy / "halo"


def png_size(path: Path):
    data = path.read_bytes()[:24]
    assert data[:8] == b"\x89PNG\r\n\x1a\n" and data[12:16] == b"IHDR", f"{path.name} is no PNG"
    return struct.unpack(">II", data[16:24])


def test_screenshot_key(game, tmp_path):
    data, saves, home = tmp_path / "data", tmp_path / "saves", tmp_path / "home"
    for folder in (data, saves, home):
        folder.mkdir()
    (data / "maps").symlink_to(MAPS.resolve())
    environment = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(home), "TMPDIR": str(home),
        "HALO_DATA_ROOT": str(data), "HALO_SAVE_ROOT": str(saves),
        "HALO_EXIT_AFTER": "14", "HALO_HIDDEN_WINDOW": "1", "HALO_NO_CLIPBOARD": "1",
        "HALO_DISPLAY_MODE": "windowed", "HALO_WINDOW_SIZE": "640x480", "HALO_RESOLUTION_SCALING": "original",
        "HALO_VOLUME": "0", "SDL_AUDIO_DRIVER": "dummy", "HALO_UPDATE_AUTO": "false",
        "HALO_NET_ONLINE": "false", "HALO_NET_ADDRESS": "127.0.0.1", "HALO_NET_BROADCAST": "127.255.255.255",
        "HALO_NET_ALLOW_UPNP": "false", "HALO_NET_PUBLIC_LOBBY": "false",
        # (on the main menu, two presses a second apart)
        "HALO_TEST_INPUT": "menu:wait wait wait wait wait wait wait screenshot wait screenshot wait",
    }
    command = [str(game)]
    if sys.platform != "darwin":
        environment.update({"SDL_VIDEO_DRIVER": "x11", "DISPLAY": os.environ.get("DISPLAY", "")})
        command = ["xvfb-run", "-a", "-s", "-screen 0 640x480x24", str(game)]
    process = subprocess.Popen(command, cwd=game.parent, env=environment, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, start_new_session=True)
    try:
        output, _ = process.communicate(timeout=120)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.communicate()
        pytest.fail("the game did not exit")
    assert process.returncode == 0, output[-4000:]
    shots = sorted((data / "screenshots").glob("*")) if (data / "screenshots").is_dir() else []
    assert len(shots) == 2, output[-4000:]
    for shot in shots:
        assert NAME.match(shot.name), shot.name
        width, height = png_size(shot)
        assert (width, height) == (640, 480), (shot.name, width, height)
        assert f"screenshot: saved {data / 'screenshots' / shot.name}" in output
