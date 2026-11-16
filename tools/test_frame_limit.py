"""The frame limit (display.max_fps, vsync off) holds while the player presses keys, clicks and moves the mouse (#543):
a local SWAT game on Blood Gulch with debug.test_input's scripted presses (the controller's and the mouse's, which
come as the window's events do), its frames' times from the Vulkan renderer's frame log (HALO_VULKAN_FRAME_LOG). No
two frames may start closer than the limit's interval (a tenth of it allowed for the clock), and no second may hold
more frames than the limit. Needs the retail maps in assets/maps, a Linux build (ninja linux_x86_64 or linux_arm64)
and a Vulkan device; the game draws offscreen (SDL's offscreen video driver), with no audio, internet play or update
check.

Negative control: HALO_GAME=<a build from before the fix>/halo, whose wait a press ended, fails it (on a Ryzen 5900X:
88 frames too close in 26 s of 17 presses, 61.8 frames a second where the limit is 60)."""

import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
RETAIL_MAPS = ROOT / "assets" / "maps"
GAME = Path(os.environ["HALO_GAME"]) if os.environ.get("HALO_GAME") else next(
    (path for path in (ROOT / "build/linux_x86_64/halo", ROOT / "build/linux_arm64/halo") if path.is_file()),
    ROOT / "build/linux_x86_64/halo")
LIMIT = 60
SECONDS = 30


def test_frame_limit_holds_while_pressing(tmp_path):
    if not GAME.is_file() or not (RETAIL_MAPS / "bloodgulch.map").is_file():
        pytest.skip(f"needs bloodgulch.map in assets/maps and a Linux build ({GAME})")
    run = tmp_path / "run"
    (run / "runtime").mkdir(parents=True)
    (run / "runtime").chmod(0o700)
    (run / "maps").symlink_to(RETAIL_MAPS)
    shutil.copy2(GAME, run / "halo")
    for library in GAME.parent.glob("lib*.so*"):
        shutil.copy2(library, run / library.name)
    # (the assets' file the game reads beside it: embedded_assets.h)
    shutil.copy2(GAME.parent / "openh1.assets", run / "openh1.assets")
    environment = {key: value for key, value in os.environ.items() if not key.startswith(("HALO_", "SDL_"))}
    environment.update(HOME=str(run), XDG_RUNTIME_DIR=str(run / "runtime"), HALO_DATA_ROOT=str(run),
                       HALO_SAVE_ROOT=str(run / "save"), HALO_NET_ONLINE="false", HALO_NO_CLIPBOARD="1",
                       HALO_UPDATE_AUTO="false", HALO_NO_AUDIO="1", SDL_AUDIODRIVER="dummy",
                       SDL_VIDEODRIVER="offscreen", HALO_NO_VSYNC="1", HALO_MAX_FPS=str(LIMIT),
                       HALO_NETWORK_TEST="local:bloodgulch:swat", HALO_NETWORK_TEST_START="1",
                       HALO_TEST_INPUT="latency:7:fire,move,click,turn", HALO_LATENCY_LOG="latency.txt",
                       HALO_VULKAN_FRAME_LOG="frames.txt", HALO_EXIT_AFTER=str(SECONDS))
    with (run / "game.log").open("w") as log:
        subprocess.run(["./halo"], cwd=run, env=environment, stdout=log, stderr=subprocess.STDOUT,
                       timeout=SECONDS + 120)
    frames_log = run / "frames.txt"
    if not frames_log.is_file():
        pytest.skip("no Vulkan frame log (no Vulkan device?): " + (run / "game.log").read_text()[-500:])
    presses = [line for line in (run / "latency.txt").read_text().splitlines() if line and not line.startswith("#")]
    assert presses, "the scripted presses never came"
    # (each frame's input read; the frames from a third of the way in, the game playing and the presses coming)
    frames = [int(line.split()[1]) for line in frames_log.read_text().splitlines() if line.startswith("frame ")]
    frames = frames[len(frames) // 3:]
    interval = 1e9 / LIMIT
    close = [b - a for a, b in zip(frames, frames[1:]) if b - a < 0.9 * interval]
    most, first = 0, 0
    for index, frame in enumerate(frames):
        while frame - frames[first] >= 1e9:
            first += 1
        most = max(most, index - first + 1)
    assert not close, f"{len(close)} frames began closer than the limit's {interval / 1e6:.1f} ms " \
                      f"(as close as {min(close) / 1e6:.2f} ms), with {len(presses)} presses"
    assert most <= LIMIT + 1, f"{most} frames in a second, the limit {LIMIT}"
