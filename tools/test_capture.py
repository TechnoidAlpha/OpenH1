"""tools/capture.py without the game: its arguments, its scenes, its pictures (the BMP files the game
writes read, made PNG files, cropped, put side by side, a Retina window's made the size asked for), and
--remote's command line and script (run here by bash, as ssh would run it there)."""
import io
import subprocess
import struct
import sys
import tarfile
import zlib
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import capture  # noqa: E402


def parse(*argv):
    parser = capture.make_parser()
    arguments = parser.parse_args(list(argv))
    capture.check_arguments(parser, arguments)
    return arguments


def test_arguments():
    arguments = parse("--scene", "sniper-zoom-10x", "--crop", "1,2,30,40", "--size", "1280x720",
                      "--set", "display.fov=100", "--frames", "4", "--every", "2")
    assert arguments.crop == (1, 2, 30, 40)
    assert arguments.size == (1280, 720)
    assert arguments.set == [("display.fov", "100")]
    assert (arguments.frames, arguments.every) == (4, 2)


@pytest.mark.parametrize("argv", [
    ["--scene", "no-such-scene"],
    [],
    ["--scene", "spawn", "--crop", "1,2,3"],
    ["--scene", "spawn", "--crop", "1,2,0,4"],
    ["--scene", "spawn", "--size", "1920"],
    ["--scene", "spawn", "--set", "display.no_such_setting=1"],
    ["--scene", "spawn", "--set", "novalue"],
    ["--scene", "spawn", "--frames", "0"],
    ["--scene", "camera:1 2 3"],
    ["--scene", "campaign:"],
    ["--scene", "spawn", "--remote", "box", "--keep"],
    ["--scene", "spawn", "--remote", "box", "--data", "maps"],
    ["--scene", "spawn", "--remote", "box", "--tree", "/elsewhere"],
    # (no abbreviations, which --remote could not leave out)
    ["--scene", "spawn", "--rem", "box"],
])
def test_bad_arguments(argv):
    with pytest.raises(SystemExit):
        parse(*argv)


def test_every_setting_has_a_variable():
    variables = capture.setting_variables()
    assert variables["display.fov"].startswith("HALO_")
    assert variables["debug.network_test_vehicle_seat"] == "HALO_NETWORK_TEST_VEHICLE_SEAT"
    # (every variable the scenes set is a setting's)
    known = set(variables.values())
    for scene in capture.SCENES.values():
        assert set(scene.environment) <= known, scene.description


def test_scenes():
    for name, scene in capture.SCENES.items():
        assert scene.at > 0 and scene.description, name
        if scene.script:
            for event in scene.script.split(";"):
                start = event.split()[0]
                assert float(start.split("-")[-1]) < scene.at + 5, (name, event)
    campaign = capture.scene_for("campaign:a10")
    assert (campaign.map, campaign.variant) == ("levels\\a10\\a10", "coop")
    assert capture.scene_for("campaign:levels/b30/b30").map == "levels\\b30\\b30"
    camera = capture.scene_for("camera:10 -20.5 3 90 -5", "hangemhigh")
    assert camera.map == "hangemhigh"
    assert camera.environment["HALO_NETWORK_TEST_CUES"] == '1 debug_camera_set "10 -20.5 3 90 -5 0 70"'
    assert capture.scene_for("spawn", "damnation").map == "damnation"
    assert capture.SCENES["spawn"].map == "bloodgulch"


def test_file_names():
    assert capture.scene_file_name("sniper-zoom-10x") == "sniper-zoom-10x"
    assert capture.scene_file_name("campaign:a10") == "campaign-a10"
    assert capture.scene_file_name("camera:1 2 3 4 5") == "camera"


def bmp(width, height, pixel, top_down=True):
    body = b"".join(bytes(pixel(x, y if top_down else height - 1 - y)) + b"\xff"
                    for y in range(height) for x in range(width))
    header = bytearray(54)
    header[:2] = b"BM"
    struct.pack_into("<IIIIiiHH", header, 2, 54 + len(body), 0, 54, 40, width, -height if top_down else height, 1, 32)
    return bytes(header) + body


def read_png(path):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    width, height = struct.unpack(">II", data[16:24])
    start = data.index(b"IDAT") + 4
    length = struct.unpack(">I", data[start - 8:start - 4])[0]
    raw = zlib.decompress(data[start:start + length])
    rows = [raw[y * (width * 3 + 1) + 1:(y + 1) * (width * 3 + 1)] for y in range(height)]
    return width, height, rows


@pytest.mark.parametrize("top_down", [True, False])
def test_bmp_to_png(tmp_path, top_down):
    # (BGRA: blue is x, green is y)
    (tmp_path / "frame.bmp").write_bytes(bmp(4, 3, lambda x, y: (x * 10, y * 20, 200), top_down))
    assert capture.bmp_complete(tmp_path / "frame.bmp")
    picture = capture.read_bmp(tmp_path / "frame.bmp")
    capture.write_png(picture, tmp_path / "frame.png")
    width, height, rows = read_png(tmp_path / "frame.png")
    assert (width, height) == (4, 3)
    assert rows[2][3 * 3:3 * 3 + 3] == bytes((200, 40, 30))
    crop = picture.crop(1, 1, 2, 2)
    assert (crop.width, crop.height) == (2, 2) and crop.rows[0][:3] == bytes((200, 20, 10))
    with pytest.raises(ValueError):
        picture.crop(10, 10, 2, 2)
    pair = capture.side_by_side(picture, crop, gap=1)
    assert (pair.width, pair.height) == (7, 3) and len(pair.rows[2]) == 21


def test_bmp_being_written(tmp_path):
    data = bmp(4, 3, lambda x, y: (0, 0, 0))
    (tmp_path / "frame.bmp").write_bytes(data[:-5])
    assert not capture.bmp_complete(tmp_path / "frame.bmp")


def test_fit():
    picture = capture.Picture(4, 2, [bytes([0, 0, 0, 100, 100, 100] * 2), bytes([100, 100, 100, 100, 100, 100] * 2)])
    small = capture.fit(picture, (2, 1))
    assert (small.width, small.height) == (2, 1) and small.rows[0] == bytes([75] * 6)
    assert capture.fit(picture, (3, 1)) is picture


def test_tree(tmp_path):
    (tmp_path / "port" / "linux" / "src").mkdir(parents=True)
    (tmp_path / "port" / "linux" / "src" / "port_config.c").write_text(
        '{ "display.fov", _config_real, "70", "HALO_FOV", _environment_value },')
    assert capture.setting_variables(tmp_path) == {"display.fov": "HALO_FOV"}
    if sys.platform != "darwin":
        (tmp_path / "build" / "linux_arm64").mkdir(parents=True)
        (tmp_path / "build" / "linux_arm64" / "halo").write_bytes(b"")
        assert capture.default_build(tmp_path) == tmp_path / "build" / "linux_arm64"
    else:
        assert capture.default_build(tmp_path) == tmp_path / "build" / "macos"


def test_remote_arguments():
    argv = ["--remote", "box", "--scene", "campaign:a10", "--out", "shots", "--remote-tree=~/clone",
            "--remote-data", "/maps", "--compare", "~/old/build/linux_x86_64", "--build=build/linux_x86_64",
            "--set", "display.fov=100", "--xvfb", "--scene=camera:1 2 3 4 5"]
    assert capture.remote_arguments(argv) == [
        "--scene", "campaign:a10", "--compare", '"$HOME"/old/build/linux_x86_64', "--build", "build/linux_x86_64",
        "--set", "display.fov=100", "--xvfb", "'--scene=camera:1 2 3 4 5'"]
    assert capture.remote_path("~") == '"$HOME"'
    assert capture.remote_path("/a b") == "'/a b'"
    # (a backslash, which fish would read in quotes, never reaches a shell's command line but bash's input)
    assert capture.remote_arguments(["--scene", "campaign:levels\\a10\\a10"]) == ["--scene", "'campaign:levels\\a10\\a10'"]


def test_remote_script(tmp_path):
    """the script, run by bash -s as ssh runs it: the stand-in for capture.py gets the clone, the maps and the
    options, its PNG file comes back as the tar on the output, its folder is deleted, and what it runs reads
    nothing of the script"""
    tree = tmp_path / "clone"
    tree.mkdir()
    source = """import sys, pathlib
arguments = sys.argv[1:]
out = pathlib.Path(arguments[arguments.index("--out") + 1])
(out / "spawn.png").write_bytes(b"png")
(out / "notes.txt").write_text("not a picture")
print("folder", pathlib.Path(__file__).parent, "cwd", pathlib.Path.cwd(), "stdin", repr(sys.stdin.read()))
print("arguments", arguments)
sys.exit(3)
"""
    arguments = parse("--remote", "box", "--scene", "spawn", "--remote-tree", str(tree),
                      "--remote-data", "~/maps", "--out", str(tmp_path / "shots"), "--xvfb")
    script = capture.remote_script(arguments, ["--remote", "box", "--scene", "spawn", "--xvfb"], source)
    result = subprocess.run(["bash", "-s"], input=script.encode(), capture_output=True, timeout=30)
    assert result.returncode == 3, result.stderr
    with tarfile.open(fileobj=io.BytesIO(result.stdout)) as archive:
        names = {member.name for member in archive.getmembers() if member.isfile()}
        assert archive.extractfile("./spawn.png").read() == b"png"
    assert names == {"./spawn.png", "./notes.txt"}
    log = result.stderr.decode()
    folder = Path(log.split("folder ")[1].split()[0])
    assert folder.name.startswith("capture-remote-") and not folder.exists()
    assert f"cwd {tree.resolve()}" in log or f"cwd {tree}" in log
    assert "stdin ''" in log
    assert f"'--tree', '{tree}'" in log and f"'--data', '{Path.home()}/maps'" in log
    assert "'--scene', 'spawn', '--xvfb'" in log


def test_remote_brings_back_pictures(tmp_path, monkeypatch):
    """remote(): ssh's output as the tar of PNG files, only those put in --out, ssh's status returned"""
    archive_bytes = io.BytesIO()
    with tarfile.open(fileobj=archive_bytes, mode="w") as archive:
        for name, body in (("./hud.png", b"a"), ("./notes.txt", b"b"), ("./../escape.png", b"c")):
            member = tarfile.TarInfo(name)
            member.size = len(body)
            archive.addfile(member, io.BytesIO(body))
    calls = []

    def fake_run(command, **options):
        calls.append(command)
        return subprocess.CompletedProcess(command, 0, stdout=archive_bytes.getvalue())

    monkeypatch.setattr(capture.subprocess, "run", fake_run)
    out = tmp_path / "shots"
    arguments = parse("--remote", "box", "--scene", "hud", "--out", str(out))
    assert capture.remote(arguments, ["--remote", "box", "--scene", "hud"]) == 0
    assert calls == [["ssh", "-o", "BatchMode=yes", "box", "bash -s"]]
    assert sorted(path.name for path in out.iterdir()) == ["hud.png"]
    assert not (tmp_path / "escape.png").exists()
