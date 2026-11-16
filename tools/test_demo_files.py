"""The demos' files (port/linux/src/demo_files.c, demo_unpack.c;
docs/demos.md) without the game or its maps: port/linux/tests/demo_files_test.c
writes a demo with the writer's thread and reads it back, its blocks packed
with deflate (the system's here, the game's in the builds) and unpacked with
the port's zlib, a damaged block
refused, and keeps the demos as demo.keep_count and demo.keep_megabytes say
(never a pinned one); then tools/demo_info.py reads the demo it wrote."""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src"
PORT_ZLIB = ROOT / "port" / "third_party" / "zlib"


@pytest.fixture(scope="module")
def demo_files_test(tmp_path_factory):
    compiler = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    folder = tmp_path_factory.mktemp("demo_files_build")
    for name in ("demo_files.c", "demo_files.h", "demo_unpack.c", "posix.h"):
        shutil.copy(SOURCE / name, folder / name)
    (folder / "platform.h").write_text("void platform_log(const char *format, ...);\n"
                                       "const char *platform_data_root(void);\n")
    objects = []
    # (the writer's deflate the system's zlib here, the game's own being
    # tied to the game's allocator; the reader's inflate the port's zlib,
    # prefixed, as the builds have it)
    (folder / "memory" / "zlib").mkdir(parents=True)
    (folder / "memory" / "zlib" / "zlib.h").write_text("#include <zlib.h>\n")
    for name in ("adler32.c", "crc32.c", "inffast.c", "inflate.c", "inftrees.c", "uncompr.c", "zutil.c"):
        output = folder / f"port_{name}.o"
        subprocess.run([compiler, "-c", "-w", "-O1", "-DZ_PREFIX", "-Dz_errmsg=z_port_errmsg",
                        str(PORT_ZLIB / name), "-o", str(output)], check=True)
        objects.append(output)
    binary = folder / "demo_files_test"
    command = [compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-O1", "-g",
               "-D_GNU_SOURCE", f"-I{folder}", "-pthread",
               str(folder / "demo_files.c"), str(SOURCE / "demo_unpack.c"), str(SOURCE / "posix_files.c"),
               str(ROOT / "port" / "linux" / "tests" / "demo_files_test.c"), *map(str, objects), "-lz", "-o", str(binary)]
    subprocess.run(command, check=True)
    return binary


def test_demo_written_read_and_kept(demo_files_test, tmp_path):
    result = subprocess.run([str(demo_files_test), str(tmp_path)], capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stderr
    # (the retention's deletions logged)
    assert "c-3.demo deleted" in result.stderr


def test_demo_info_reads_a_demo(demo_files_test, tmp_path):
    result = subprocess.run([str(demo_files_test), str(tmp_path), "write"], capture_output=True, text=True,
                            timeout=120)
    assert result.returncode == 0, result.stderr
    path = Path(result.stdout.strip().splitlines()[0])
    assert path.parent == tmp_path / "demos"
    sys.path.insert(0, str(ROOT / "tools"))
    import demo_info

    demo = demo_info.Demo(path.read_bytes())
    assert demo.map_name == "levels\\test\\demo\\demo"
    assert [block[0] for block in demo.blocks] == ["R", "R", "K", "k"]
    assert [block[1] for block in demo.blocks] == [0, 30, 60, 90]
    assert demo.trailing == 0
    ticks = [record for _, kind, record in demo.records("R") if kind == "T"]
    assert len(ticks) == 2 * (3000 // 13)
    output = subprocess.run([sys.executable, str(ROOT / "tools" / "demo_info.py"), str(path)], capture_output=True,
                            text=True, check=True).stdout
    assert "map levels\\test\\demo\\demo" in output


def test_demo_of_a_killed_recorder_plays_to_its_last_whole_block(demo_files_test, tmp_path):
    """a recorder killed as it writes (a crash, SIGKILL; the game's own SIGINT
    and SIGTERM end the demo as it quits) leaves its whole blocks readable: the
    index the playback makes (demo.c's demo_playback_open, which demo_info.py
    reads as) stops at the first block cut short, and every block before it
    unpacks to whole records"""
    import signal
    import time

    sys.path.insert(0, str(ROOT / "tools"))
    import demo_info

    process = subprocess.Popen([str(demo_files_test), str(tmp_path), "killed"], stdout=subprocess.PIPE, text=True)
    path = Path(process.stdout.readline().strip())
    # (killed once some blocks are written, most likely in the middle of one)
    deadline = time.time() + 30
    while time.time() < deadline and (not path.exists() or path.stat().st_size < 3 << 20):
        time.sleep(0.01)
    process.send_signal(signal.SIGKILL)
    process.wait(timeout=30)
    data = path.read_bytes()
    assert len(data) >= 3 << 20

    def whole_blocks(cut):
        demo = demo_info.Demo(cut)
        ticks = 0
        for _, kind, record in demo.records("R"):
            assert kind in "TP"
            ticks += kind == "T"
        return demo, ticks

    demo, ticks = whole_blocks(data)
    assert len(demo.blocks) >= 2 and ticks > 0
    assert [block[1] for block in demo.blocks] == [index * 30 for index in range(len(demo.blocks))]
    # (and cut anywhere, as the kill could have: the blocks before the cut
    # play, none is taken for damaged)
    offsets = [512 + 20 * index for index in range(3)] + list(range(600, len(data), max(1, len(data) // 97)))
    for offset in offsets:
        cut_demo, cut_ticks = whole_blocks(data[:offset])
        assert len(cut_demo.blocks) <= len(demo.blocks)
        assert all(a[:4] == b[:4] for a, b in zip(cut_demo.blocks, demo.blocks))
        assert cut_ticks <= ticks


def test_each_tick_of_one_shape_whatever_came_when():
    """a tick's records (format 3: demo.c, docs/demos.md's "The file") are of
    one shape however a remote client's input and hits fall among the host's
    sub-steps: its T, its input, each sub-step's U in order with the records
    its code made between them, its Z; between ticks only what came from
    outside them. tools/demo_info.py --check holds a demo to it; these are
    the shapes a real client's game gives (a hit reported late, in the last
    sub-step; another dealt between two ticks; what the netcode put in before
    the first sub-step), and the faults it must refuse"""
    import struct

    sys.path.insert(0, str(ROOT / "tools"))
    import demo_info

    def tick(number, substeps=4, inside=None, between=()):
        """a tick's records, with inside {sub-step: [record types]} after each
        sub-step's U (and -1: before the first, the tick's own work), and
        between: what came after its Z"""
        inside = inside or {}
        records = [("T", struct.pack("<II", number, 30 * substeps))]
        records += [("P", b""), ("A", b"")]
        records += [(kind, b"\x01") for kind in inside.get(-1, [])]
        for index in range(substeps):
            records.append(("U", struct.pack("<IBB", number, index, substeps)))
            records += [(kind, b"\x01") for kind in inside.get(index, [])]
        records.append(("Z", struct.pack("<I", number)))
        records += [(kind, b"\x00" * 8) for kind in between]
        return records

    def problems(records):
        return demo_info.shape_problems(("R", kind, data) for kind, data in records)[1]

    good = (tick(0, inside={0: ["D"]}) +
            tick(1, inside={-1: ["F"], 2: ["D", "D"], 3: ["D", "L"]}, between=["M", "X"]) +
            tick(2, inside={1: ["I", "I", "L"]}) +
            tick(3, substeps=1, inside={0: ["D"]}))
    # (a recorder stopped inside a tick: its E there)
    good += tick(4)[:4] + [("E", struct.pack("<I", 4))]
    assert problems(good) == []
    assert demo_info.shape_problems(("R", kind, data) for kind, data in good)[0] == 5
    # a hit's choice between two ticks, where no tick's code reads it
    assert problems(tick(0, between=["D"]) + tick(1))
    # a sub-step left out, or out of order
    missing = tick(0)
    del missing[4]
    assert problems(missing)
    swapped = tick(0)
    swapped[3], swapped[4] = swapped[4], swapped[3]
    assert problems(swapped)
    # a U or a Z of another tick
    other = tick(5)
    other[3] = ("U", struct.pack("<IBB", 4, 0, 4))
    assert problems(other)
    assert problems(tick(0)[:-1] + [("Z", struct.pack("<I", 1))])
    # a tick without its end
    assert problems(tick(0)[:-1] + tick(1))
