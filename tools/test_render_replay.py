"""The replays' shared part (port/tools/replay/host_render_replay.c), without a
GPU or the game: port/tools/replay/tests/host_render_replay_test.c built with
the build machine's C compiler (with the render stream and its recorder, as
tools/test_render_stream.py builds them), its units run (Apple's LZ4 frames
decoded on a Mac, base64, the PNG writer, the requests' parser), and its
service (--serve) driven over its pipes on a recording it makes: frames of a
CPU backend's rectangles, read back exactly however they are reached (on,
back, again), their ID buffers and pick's stacks, their draws, the
timeline's thumbnails, the GPU's cost of their passes and draws (the CPU
backend's made-up times, by both methods) and the errors.

The Metal and Vulkan replays are the same code with a GPU's backend
(halo_replay.m, port/linux/vulkan/tools/halo_replay.c); a recording of the
game played on them is checked by hand (port/linux/vulkan/README.md)."""
import base64
import json
import platform
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
FRAMES = 50
WIDTH, HEIGHT = 64, 32


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


@pytest.fixture(scope="module")
def binary(tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    if sys.platform not in ("linux", "darwin") or platform.architecture()[0] != "64bit":
        pytest.skip("needs a 64-bit Linux or macOS host")
    folder = tmp_path_factory.mktemp("replay")
    output = folder / "host_render_replay_test"
    subprocess.run(
        [cc, "-std=gnu11", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-sign-compare", "-Werror", "-O1", "-g",
         "-D_GNU_SOURCE", "-D_DARWIN_C_SOURCE", "-DHALO_GUEST_BASE=1", "-DXGPU_STREAM_TEST=1", "-DHALO_REPLAY_NO_SDL=1",
         f"-I{ROOT / 'port' / 'linux' / 'src'}", f"-I{ROOT / 'port' / 'android' / 'host'}",
         f"-I{ROOT / 'port' / 'android' / 'include'}", f"-I{ROOT / 'port' / 'tools' / 'replay'}",
         str(ROOT / "port" / "tools" / "replay" / "tests" / "host_render_replay_test.c"),
         str(ROOT / "port" / "android" / "host" / "host_render_stream.c"),
         str(ROOT / "port" / "android" / "host" / "host_render_record.c"),
         "-o", str(output), "-lpthread", "-lm",
         *(["-lcompression"] if sys.platform == "darwin" else [])],
        check=True)
    return output


def png_pixels(data):
    """an RGB PNG's rows (as the replay writes them: unfiltered, or with the sub filter)"""
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    place, idat, width, height = 8, b"", 0, 0
    while place < len(data):
        length, kind = struct.unpack(">I4s", data[place:place + 8])
        body = data[place + 8:place + 8 + length]
        assert zlib.crc32(data[place + 4:place + 8 + length]) == struct.unpack(">I", data[place + 8 + length:
                                                                                         place + 12 + length])[0]
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", body[:10])
            assert (depth, colour) == (8, 2)
        elif kind == b"IDAT":
            idat += body
        place += 12 + length
    raw = zlib.decompress(idat)
    rows = []
    for y in range(height):
        row = bytearray(raw[y * (width * 3 + 1):(y + 1) * (width * 3 + 1)])
        assert row[0] in (0, 1)
        if row[0] == 1:
            for at in range(4, len(row)):
                row[at] = (row[at] + row[at - 3]) & 0xff
        rows.append([tuple(row[1 + x * 3:4 + x * 3]) for x in range(width)])
    return width, height, rows


def qoi_pixels(data):
    """a QOI file's rows (RGB), decoded as qoiformat.org's specification says"""
    assert data[:4] == b"qoif" and data[-8:] == b"\0" * 7 + b"\1"
    width, height, channels = struct.unpack(">IIB", data[4:13])
    assert channels == 3
    seen, pixel, out, place = [(0, 0, 0, 0)] * 64, (0, 0, 0, 255), [], 14
    while len(out) < width * height:
        byte = data[place]
        place += 1
        if byte == 0xfe:
            pixel = (*data[place:place + 3], pixel[3])
            place += 3
        elif byte == 0xff:
            pixel = tuple(data[place:place + 4])
            place += 4
        elif byte >> 6 == 0:
            pixel = seen[byte]
        elif byte >> 6 == 1:
            pixel = (((pixel[0] + (byte >> 4 & 3) - 2) & 255), ((pixel[1] + (byte >> 2 & 3) - 2) & 255),
                     ((pixel[2] + (byte & 3) - 2) & 255), pixel[3])
        elif byte >> 6 == 2:
            green = (byte & 63) - 32
            second = data[place]
            place += 1
            pixel = ((pixel[0] + green + (second >> 4) - 8) & 255, (pixel[1] + green) & 255,
                     (pixel[2] + green + (second & 15) - 8) & 255, pixel[3])
        else:
            out += [pixel] * ((byte & 63) + 1)
            continue
        seen[(pixel[0] * 3 + pixel[1] * 5 + pixel[2] * 7 + pixel[3] * 11) % 64] = pixel
        out.append(pixel)
    return width, height, [[out[y * width + x][:3] for x in range(width)] for y in range(height)]


def expected(frame, x, y):
    """frame's pixel (RGB) and the draws that changed it, topmost first"""
    colour, draws = (frame, 0x20, 0x40), []
    if frame % 40 <= x < frame % 40 + 16 and 4 <= y < 12:
        colour, draws = (0, 0, 255), [1] + draws
    if 20 <= x < 40 and 8 <= y < 24:
        colour, draws = (0, 255, 0), [2] + draws
    return colour, draws


def test_units(binary, tmp_path):
    picture = tmp_path / "unit.png"
    result = subprocess.run([str(binary), "unit", str(picture)], capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stderr
    width, height, rows = png_pixels(picture.read_bytes())
    assert (width, height) == (300, 230)
    assert rows[7][250] == (250, 7, 250 ^ 7)
    for index in range(5):
        assert zlib.decompress((tmp_path / f"deflate_{index}.bin").read_bytes()) == \
            (tmp_path / f"deflate_{index}.raw").read_bytes(), index


class Service:
    def __init__(self, binary, recording):
        self.process = subprocess.Popen([str(binary), str(recording), "--serve"], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def ask(self, **request):
        self.process.stdin.write(json.dumps(request) + "\n")
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        assert line, self.process.stderr.read()
        return json.loads(line)

    def close(self):
        self.process.stdin.close()
        assert self.process.wait(timeout=60) == 0


def test_service(binary, tmp_path):
    recording = tmp_path / "rectangles.hrpl"
    subprocess.run([str(binary), "make", str(recording), str(FRAMES)], check=True)
    service = Service(binary, recording)
    try:
        info = service.ask(cmd="info")
        assert info["ok"] and info["frames"] == FRAMES and (info["width"], info["height"]) == (WIDTH, HEIGHT)
        assert info["backend"] == "test"
        # each frame exactly, however it is reached: on, back one (a picture kept), far back (played again)
        for frame in (5, 6, 30, 29, 3, 3, FRAMES - 1, 0, 12):
            reply = service.ask(cmd="frame", n=frame, png=str(tmp_path / "f.png"), ids=str(tmp_path / "f.ids"))
            assert reply["ok"], reply
            width, height, rows = png_pixels((tmp_path / "f.png").read_bytes())
            ids = struct.unpack(f"<{WIDTH * HEIGHT}I", (tmp_path / "f.ids").read_bytes())
            for y in range(HEIGHT):
                for x in range(WIDTH):
                    colour, draws = expected(frame, x, y)
                    assert rows[y][x] == colour, (frame, x, y)
                    assert ids[y * WIDTH + x] == (draws[0] if draws else 0), (frame, x, y)
        # inline, and a step back to a picture kept (no ID buffer asked for)
        service.ask(cmd="frame", n=20, inline=True)
        reply = service.ask(cmd="frame", n=19, inline=True)
        assert reply["ok"] and "ids_base64" not in reply
        assert png_pixels(base64.b64decode(reply["png_base64"]))[2][5][19 % 40] == (0, 0, 255)
        reply = service.ask(cmd="frame", n=19, inline=True, ids=True)
        ids = struct.unpack(f"<{WIDTH * HEIGHT}I", base64.b64decode(reply["ids_base64"]))
        assert ids[10 * WIDTH + 25] == 2
        # "compress": the PNG deflated, the ID buffer as zlib's (the replay's own deflate, read by Python's)
        reply = service.ask(cmd="frame", n=19, inline=True, ids=True, compress=True)
        assert "ids_base64" not in reply
        zipped = base64.b64decode(reply["ids_zlib_base64"])
        assert struct.unpack(f"<{WIDTH * HEIGHT}I", zlib.decompress(zipped)) == ids and len(zipped) < len(ids)
        rows = png_pixels(base64.b64decode(reply["png_base64"]))[2]
        assert all(rows[y][x] == expected(19, x, y)[0] for y in range(HEIGHT) for x in range(WIDTH))
        thumbs = service.ask(cmd="thumbs", frames=[4], width=32, inline=True, compress=True)
        assert png_pixels(base64.b64decode(thumbs["thumbs"][0]["png_base64"]))[:2] == (32, 16)
        # "image": "qoi", inline and into a file; thumbnails too
        for frame in (21, 20):
            reply = service.ask(cmd="frame", n=frame, inline=True, image="qoi")
            assert "png_base64" not in reply
            rows = qoi_pixels(base64.b64decode(reply["qoi_base64"]))[2]
            assert all(rows[y][x] == expected(frame, x, y)[0] for y in range(HEIGHT) for x in range(WIDTH))
        assert service.ask(cmd="frame", n=22, image="qoi", qoi=str(tmp_path / "f.qoi"))["ok"]
        assert qoi_pixels((tmp_path / "f.qoi").read_bytes())[2][5][22 % 40] == (0, 0, 255)
        thumbs = service.ask(cmd="thumbs", frames=[4], width=32, inline=True, image="qoi")
        assert qoi_pixels(base64.b64decode(thumbs["thumbs"][0]["qoi_base64"]))[:2] == (32, 16)
        # the ID buffers of the frames asked for with theirs, kept: a step back to one is the same
        first = service.ask(cmd="frame", n=30, inline=True, ids=True)
        service.ask(cmd="frame", n=31, inline=True, ids=True)
        again = service.ask(cmd="frame", n=30, inline=True, ids=True)
        assert again["ids_base64"] == first["ids_base64"] and again["png_base64"] == first["png_base64"]
        assert [d["i"] + 1 for d in service.ask(cmd="pick", n=30, x=35, y=10)["stack"]] == expected(30, 35, 10)[1] == [2, 1]
        # the draws, and a pixel's stack (the green over the red; the third draw changed nothing)
        draws = service.ask(cmd="draws", n=19)
        assert draws["ok"] and [draw["i"] for draw in draws["draws"]] == [0, 1, 2]
        assert draws["draws"][1]["record"] == "draw" and draws["draws"][1]["target"] == 1
        pick = service.ask(cmd="pick", n=19, x=25, y=10)
        assert [draw["i"] for draw in pick["stack"]] == [1, 0]
        assert service.ask(cmd="pick", n=19, x=5, y=28)["stack"] == []
        # the timeline's thumbnails, into a folder and inline
        thumbs = service.ask(cmd="thumbs", every=10, width=32, dir=str(tmp_path / "thumbs"))
        assert [thumb["n"] for thumb in thumbs["thumbs"]] == [0, 10, 20, 30, 40]
        assert png_pixels(Path(thumbs["thumbs"][1]["png"]).read_bytes())[:2] == (32, 16)
        thumbs = service.ask(cmd="thumbs", frames=[7, 3], width=16, inline=True)
        assert [thumb["n"] for thumb in thumbs["thumbs"]] == [7, 3]
        assert png_pixels(base64.b64decode(thumbs["thumbs"][1]["png_base64"]))[:2] == (16, 8)
        # errors
        assert service.ask(cmd="frame", n=FRAMES)["ok"] is False
        assert service.ask(cmd="pick", n=1, x=WIDTH, y=0)["ok"] is False
        assert service.ask(cmd="nothing")["ok"] is False
        service.process.stdin.write("not json\n")
        service.process.stdin.flush()
        assert json.loads(service.process.stdout.readline())["ok"] is False
    finally:
        service.close()


def test_cost(binary, tmp_path):
    """protocol 2's cost: the frame played again where it is, its passes' and its draws' GPU time (the test
    backend's made up: a clear 0.05 ms, a draw 0.001 ms a pixel of its rectangle, the present 0.01 ms),
    subtracted (each draw left out) and the backend's own (timestamps); the frame asked for after it played
    exactly again"""
    recording = tmp_path / "rectangles.hrpl"
    subprocess.run([str(binary), "make", str(recording), str(FRAMES)], check=True)
    service = Service(binary, recording)
    areas = [16 * 8, 20 * 16, 64 * 6]
    try:
        info = service.ask(cmd="info")
        assert info["protocol"] == 2 and 1 in info["protocols"]
        assert {"cost", "ids", "pick", "frame", "draws", "thumbs"} <= set(info["capabilities"])
        assert info["cost_methods"] == ["subtract", "timestamps"]
        service.ask(cmd="frame", n=12, png=str(tmp_path / "f.png"))
        reply = service.ask(cmd="cost", n=12, all=True, method="subtract", repeats=3)
        assert reply["ok"], reply
        assert reply["method"] == "subtract" and reply["repeat_exact"] is True and reply["frame_draws"] == 3
        whole = 0.05 + sum(areas) * 0.001 + 0.01
        assert reply["frame_ms"] == pytest.approx(whole, abs=1e-4) and reply["frame_spread"] == 0
        assert [draw["i"] for draw in reply["draws"]] == [0, 1, 2]
        for draw, area in zip(reply["draws"], areas):
            assert draw["ms"] == pytest.approx(area * 0.001, abs=1e-4) and draw["spread"] == 0 and draw["pass"] == 0
        assert reply["draws_ms"] == pytest.approx(sum(areas) * 0.001, abs=1e-4)
        first, present = reply["passes"]
        assert (first["kind"], first["draws"], present["kind"], present["draws"]) == ("test", 3, "present", 0)
        assert first["ms"] == pytest.approx(0.05 + sum(areas) * 0.001, abs=1e-4)
        assert first["draws_ms"] == pytest.approx(first["draws_sum_ms"], abs=1e-4) == pytest.approx(sum(areas) * 0.001)
        assert "draws_ms" not in present
        # (each repeat's whole plays and the left-out ones: 3 draws and the pass's group, a batch, two wholes)
        assert reply["plays"] == 3 * (2 + 4)
        # the backend's own times, by default; a few draws
        reply = service.ask(cmd="cost", n=12, draws=[1], repeats=2)
        assert reply["method"] == "timestamps" and [draw["i"] for draw in reply["draws"]] == [1]
        assert reply["draws"][0]["ms"] == pytest.approx(areas[1] * 0.001)
        reply = service.ask(cmd="cost", n=12, draws=[2, 0], method="subtract", repeats=1)
        assert [(draw["i"], round(draw["ms"], 4)) for draw in reply["draws"]] == [(2, 0.384), (0, 0.128)]
        # the frames after it, and the frame itself, exactly as before
        for frame in (13, 12, 12, 40):
            reply = service.ask(cmd="frame", n=frame, png=str(tmp_path / "f.png"))
            rows = png_pixels((tmp_path / "f.png").read_bytes())[2]
            assert all(rows[y][x] == expected(frame, x, y)[0] for y in range(HEIGHT) for x in range(WIDTH)), frame
        # errors
        assert service.ask(cmd="cost", n=FRAMES)["ok"] is False
        assert service.ask(cmd="cost", n=1, draws=[3])["ok"] is False
        assert service.ask(cmd="cost", n=1, method="nothing")["ok"] is False
        assert service.ask(cmd="cost")["ok"] is False
    finally:
        service.close()


def test_marks(binary, tmp_path):
    """debug.render_marks' records read: each draw's owner (its kind, label, id, tags, a menu widget's tag and
    generator source), the frame's view, the category; a frame reached far ahead (the frames well before it
    played without their draws) and back again drawn exactly; a recording of before the marks (version 1)
    played as it was"""
    recording = tmp_path / "marked.hrpl"
    subprocess.run([str(binary), "make", str(recording), str(FRAMES), "marks"], check=True)
    service = Service(binary, recording)
    try:
        assert service.ask(cmd="info")["marks"] is True
        for frame in (40, 41, 2, 45, 30):
            reply = service.ask(cmd="frame", n=frame, png=str(tmp_path / "f.png"), ids=str(tmp_path / "f.ids"))
            assert reply["ok"], reply
            width, height, rows = png_pixels((tmp_path / "f.png").read_bytes())
            ids = struct.unpack(f"<{WIDTH * HEIGHT}I", (tmp_path / "f.ids").read_bytes())
            for y in range(HEIGHT):
                for x in range(WIDTH):
                    colour, draws = expected(frame, x, y)
                    assert rows[y][x] == colour, (frame, x, y)
                    assert ids[y * WIDTH + x] == (draws[0] if draws else 0), (frame, x, y)
        draws = service.ask(cmd="draws", n=30)
        assert draws["ok"], draws
        frame = draws["frame"]
        assert frame["map"] == "levels\\test\\test" and frame["tick"] == 1030 and frame["window"] == 0
        assert frame["camera"]["position"] == [30, 0, 0] and frame["camera"]["fov"] == 1.5
        assert frame["viewport"] == [0, 0, WIDTH, HEIGHT] and len(frame["views"]) == 1
        first, second, third = draws["draws"]
        assert first["kind"] == "world_object" and first["label"] == "warthog #2" and first["id"] == 0x10002
        assert first["tags"] == {"object": "vehicles\\warthog\\warthog",
                                 "shader": "vehicles\\warthog\\shaders\\warthog hull"}
        assert first["source"] is None and first["category"] is None
        assert second["kind"] == "menu_widget" and second["label"] == "caf?"
        assert second["widget"] == "pc\\main_menu\\board_start"
        assert second["source"] == {"file": "tools/port_settings.py",
                                    "text": "tools/port_settings.py multiplayer_files/_lobby/_lobby_board"}
        assert third["kind"] == "other" and third["label"].startswith("draw ") and third["id"] is None
        pick = service.ask(cmd="pick", n=30, x=35, y=10)
        assert [(draw["i"], draw["kind"]) for draw in pick["stack"]] == [(1, "menu_widget"), (0, "world_object")]
    finally:
        service.close()
    old = tmp_path / "old.hrpl"
    subprocess.run([str(binary), "make", str(old), str(FRAMES), "old"], check=True)
    service = Service(binary, old)
    try:
        assert service.ask(cmd="info")["marks"] is False
        reply = service.ask(cmd="frame", n=33, png=str(tmp_path / "o.png"))
        assert reply["ok"], reply
        assert png_pixels((tmp_path / "o.png").read_bytes())[2][5][33 % 40] == (0, 0, 255)
        assert service.ask(cmd="draws", n=33)["draws"][0]["kind"] == "other"
    finally:
        service.close()


def test_play(binary, tmp_path):
    """the measuring play, twice over (the recording played again from its start)"""
    recording = tmp_path / "rectangles.hrpl"
    subprocess.run([str(binary), "make", str(recording), str(FRAMES)], check=True)
    result = subprocess.run([str(binary), str(recording), "--loop", "2", "--quiet"], capture_output=True, text=True,
                            timeout=120)
    assert result.returncode == 0, result.stderr
    assert "0 records out of place" in result.stdout, result.stdout
    assert result.stdout.count("loop ") == 2


def test_inspect_shader(tmp_path):
    """the Vulkan backend's ID buffer pass (tools/vulkan_inspect_shader.py): its SPIR-V valid for Vulkan 1.1
    where spirv-val is installed, and what its GLSL makes where glslangValidator is the one it was made with"""
    import re
    text = (ROOT / "port/linux/vulkan/xgpu_vulkan_inspect_shader.c").read_text()
    words = [int(word, 16) for word in re.findall(r"0x([0-9a-f]{8})", text.split("{", 1)[1])]
    assert words[0] == 0x07230203
    validator = shutil.which("spirv-val")
    if validator:
        module = tmp_path / "inspect.spv"
        module.write_bytes(struct.pack(f"<{len(words)}I", *words))
        subprocess.run([validator, "--target-env", "vulkan1.1", str(module)], check=True)
    glslang = shutil.which("glslangValidator")
    if glslang and validator and "16.6.0" in subprocess.run([glslang, "--version"], capture_output=True,
                                                            text=True).stdout:
        subprocess.run([sys.executable, str(ROOT / "tools/vulkan_inspect_shader.py"), "--check"], check=True)
