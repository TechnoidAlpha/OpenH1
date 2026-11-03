"""Drives one live OpenH1 process for camera re-renders: offscreen, the
telnet script console for commands, every frame streamed as raw BGRA through
a named pipe (debug.screenshot_stream), the camera placed with
debug_camera_load (the director's saved-camera file, d:\\camera.txt).

While the frames are not read the game blocks on the pipe, so it is paused
(no GPU work) between bursts; the GPU lock (RR_GPU_LOCK, if set) is held
only during a burst."""
import fcntl
import math
import os
import socket
import subprocess
import threading
import time
from contextlib import contextmanager
from pathlib import Path

import numpy as np

import view as rr_view

ROOT = Path(__file__).resolve().parents[2]
HALO = Path(os.environ.get("RR_HALO", ROOT / "build" / "linux_x86_64" / "halo"))
MAPS = Path(os.environ.get("RR_MAPS", ROOT / "assets" / "maps"))
# (a lock file shared with other GPU users of the machine, held per burst; empty: none)
LOCK = os.environ.get("RR_GPU_LOCK", "")


def _die_with_parent():
    import ctypes
    import signal
    ctypes.CDLL("libc.so.6").prctl(1, signal.SIGKILL)  # PR_SET_PDEATHSIG


class Game:
    def __init__(self, folder, scenario, width, height, port=23471, extra_env=None):
        self.folder = Path(folder).resolve()
        self.width, self.height = width, height
        self.folder.mkdir(parents=True, exist_ok=True)
        (self.folder / "saves").mkdir(exist_ok=True)
        if not (self.folder / "maps").exists():
            (self.folder / "maps").symlink_to(MAPS)
        (self.folder / "init.txt").write_text(f"map_name {scenario}\n")
        self.fifo = self.folder / "frames.fifo"
        if self.fifo.exists():
            self.fifo.unlink()
        os.mkfifo(self.fifo)
        env = dict(os.environ, HALO_DATA_ROOT=str(self.folder), HALO_SAVE_ROOT=str(self.folder / "saves"),
                   HALO_NET_ONLINE="false", HALO_NET_BROADCAST="127.255.255.255", HALO_NET_ADDRESS="127.0.0.77",
                   HALO_NO_CLIPBOARD="1", HALO_TELNET_CONSOLE="1", HALO_TELNET_CONSOLE_PORT=str(port),
                   HALO_SCREENSHOT_EVERY="1", HALO_SCREENSHOT_STREAM=str(self.fifo),
                   HALO_SKIP_CINEMATICS="1", HALO_DISPLAY_MODE="windowed",
                   HALO_WINDOW_SIZE=f"{width}x{height}", HALO_GRAPHICS="enhanced",
                   HALO_INTERPOLATION="false", HALO_MAX_FPS="-1",
                   SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy")
        env.update(extra_env or {})
        self.port = port
        self.sock = None
        self.frame = None
        self.count = 0
        self.reading = threading.Event()
        self.cond = threading.Condition()
        self.lock_file = None
        self.lock_held = 0.0
        self.stdout = open(self.folder / "stdout.txt", "w")
        self.lock()
        self.process = subprocess.Popen([str(HALO)], cwd=self.folder, env=env, stdout=self.stdout,
                                        stderr=subprocess.STDOUT, preexec_fn=_die_with_parent)
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    # ----- the GPU lock, and the game paused without it

    def lock(self):
        if self.lock_file is None and LOCK:
            self.lock_file = open(LOCK, "a")
            fcntl.flock(self.lock_file, fcntl.LOCK_EX)
            self.lock_since = time.time()
        self.reading.set()

    def unlock(self):
        self.reading.clear()
        time.sleep(0.1)
        if self.lock_file is not None:
            self.lock_held += time.time() - self.lock_since
            fcntl.flock(self.lock_file, fcntl.LOCK_UN)
            self.lock_file.close()
            self.lock_file = None

    @contextmanager
    def burst(self):
        self.lock()
        try:
            yield
        finally:
            self.unlock()

    def _read(self):
        size = self.width * self.height * 4
        with open(self.fifo, "rb", buffering=0) as stream:
            buffer = bytearray(size)
            view = memoryview(buffer)
            while True:
                self.reading.wait()
                got = 0
                while got < size:
                    n = stream.readinto(view[got:])
                    if not n:
                        return
                    got += n
                with self.cond:
                    self.frame = bytes(buffer)
                    self.count += 1
                    self.cond.notify_all()

    # ----- the console

    def connect(self, timeout=180):
        end = time.time() + timeout
        while time.time() < end:
            if self.process.poll() is not None:
                raise RuntimeError(f"game exited {self.process.returncode}")
            try:
                self.sock = socket.create_connection(("127.0.0.1", self.port), timeout=2)
                self.sock.settimeout(0.05)
                return
            except OSError:
                time.sleep(0.5)
        raise RuntimeError("no telnet console")

    def drain(self):
        out = b""
        try:
            while True:
                data = self.sock.recv(65536)
                if not data:
                    break
                out += data
        except (socket.timeout, BlockingIOError):
            pass
        return out.decode(errors="replace")

    def command(self, line, frames=0, reply=True):
        self.sock.sendall(line.encode() + b"\r\n")
        if frames:
            self.wait_frames(frames)
        return self.drain() if reply else ""

    # ----- frames

    def wait_frames(self, n, timeout=60):
        with self.cond:
            target = self.count + n
            end = time.time() + timeout
            while self.count < target:
                if not self.cond.wait(max(0.0, end - time.time())):
                    raise RuntimeError("no frames")
            return self.image()

    def image(self):
        a = np.frombuffer(self.frame, np.uint8).reshape(self.height, self.width, 4)
        return a[..., [2, 1, 0]].copy()  # RGB

    def render(self, pose, settle=2):
        """pose: x y z yaw pitch roll vfov (degrees; vfov the vertical field of
        view, the game's own being across at 4:3). The console reads 32 bytes
        a frame, so the frame waited for is that many reads plus settle"""
        x, y, z, yaw, pitch, roll, vfov = pose
        fov = math.degrees(rr_view.fov_from_vfov(vfov))
        numbers = " ".join(("%.2f" % v).rstrip("0").rstrip(".") for v in (x, y, z, yaw % 360, pitch, roll, fov))
        line = 'debug_camera_set "%s"' % numbers
        self.command(line, reply=False)
        return self.wait_frames(math.ceil((len(line) + 2) / 32) + settle)

    def panorama(self, x, y, z, rings=((0, 8, 60), (-40, 6, 60), (40, 6, 60)), settle=2):
        """whole frames from a place, a ring of yaws at each pitch: a list of
        (image, yaw, pitch, vfov)"""
        out = []
        for pitch, count, vfov in rings:
            for k in range(count):
                yaw = 360.0 * k / count
                out.append((self.render((x, y, z, yaw, pitch, 0, vfov), settle), yaw, pitch, vfov))
        return out

    def prepare_scene(self):
        """no HUD, no console text, no AI, the player's body gone"""
        for line in ("terminal_render 0", "show_hud 0", "show_hud_help_text 0", "hud_clear_messages", "ai_erase_all", "cinematic_show_letterbox 0",
                     "object_destroy (unit (list_get (players) 0))", "game_speed 0"):
            self.command(line, frames=2)
        # (the first camera set takes the script's camera: a frame later)
        self.render((0, 0, 0, 0, 0, 0, 50), settle=4)
        return self.drain()

    def place_objects(self, objects, frames=3, settle_seconds=3.0):
        """a pose file's objects: {"tag", "x", "y", "z", "yaw", "pitch",
        "roll"} each, or {"tag", "seat"} (into the last one placed), either
        with "color": [r, g, b]; "clear" first"""
        replies = [self.command('debug_object_place "clear"', frames=frames)]
        for o in objects:
            if "seat" in o:
                words = [o["tag"], "seat", o["seat"]]
            else:
                words = [o["tag"]] + ["%.3f" % o[k] for k in ("x", "y", "z", "yaw", "pitch", "roll")]
            if "color" in o:
                words += ["color"] + ["%.3f" % c for c in o["color"]]
            replies.append(self.command('debug_object_place "%s"' % " ".join(words),
                                        frames=frames))
        if objects and settle_seconds > 0:
            # (the world let run a moment: a vehicle comes to rest on the
            # ground, a rider takes its seat; then stopped again)
            self.command("game_speed 1", frames=2)
            end = time.time() + settle_seconds
            while time.time() < end:
                self.wait_frames(5)
            self.command("game_speed 0", frames=3)
        return "".join(replies)

    def close(self):
        self.reading.set()
        try:
            if self.sock:
                self.sock.close()
        except OSError:
            pass
        if self.process.poll() is None:
            self.process.kill()  # (headless ignores SIGTERM)
            try:
                self.process.wait(15)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.stdout.close()
        if self.lock_file is not None:
            self.unlock()
