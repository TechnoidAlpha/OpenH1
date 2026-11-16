"""tools/determinism_compare.py on logs written here, as the game writes
them (port/linux/game/determinism_log.c)"""

import struct
import subprocess
import sys
from pathlib import Path

TOOL = Path(__file__).resolve().parent / "determinism_compare.py"


def write_log(path, ticks, frames=None, local_takers=None):
    lines = ["alloc 0 0 16 header", "alloc 1 10 64 objects"]
    for index, (number, state) in enumerate(ticks):
        lines.append(f"a {state:08x} 00000001")
        lines.append(f"t {number} g 00000001 l 00000002 h {state:08x}")
        lines.append("G 2: 88001000*2")
        if local_takers and index in local_takers:
            lines.append(f"l 1: {local_takers[index]}")
        for frame in (frames or {}).get(index, []):
            lines.append(f"f {number} 0 {frame}")
    path.write_text("\n".join(lines) + "\n")


def run(*args):
    result = subprocess.run([sys.executable, str(TOOL), *map(str, args)], capture_output=True, text=True)
    return result.returncode, result.stdout


def test_alike(tmp_path):
    ticks = [(1, 10), (2, 11), (2, 12), (3, 13)]
    write_log(tmp_path / "a.txt", ticks, {1: ["aa"]})
    write_log(tmp_path / "b.txt", ticks, {1: ["aa"]})
    status, out = run(tmp_path / "a.txt", tmp_path / "b.txt")
    assert status == 0
    assert "4 ticks alike" in out and "all alike" in out


def test_first_difference_in_order(tmp_path):
    # (tick 2 twice, as after a checkpoint is loaded: the second differs)
    write_log(tmp_path / "a.txt", [(1, 10), (2, 11), (2, 12), (3, 13)])
    write_log(tmp_path / "b.txt", [(1, 10), (2, 11), (2, 99), (3, 13)])
    status, out = run(tmp_path / "a.txt", tmp_path / "b.txt")
    assert status == 1
    assert "first difference at tick 2, the 3th logged" in out
    assert "differs in 1 allocations: header" in out


def test_frames_and_takers(tmp_path):
    ticks = [(1, 10), (2, 11)]
    write_log(tmp_path / "a.txt", ticks, {1: ["aa"]}, {0: "88002000"})
    write_log(tmp_path / "b.txt", ticks, {1: ["bb"]}, {0: "88003000"})
    status, out = run(tmp_path / "a.txt", tmp_path / "b.txt")
    assert status == 1
    assert "between ticks differ" in out
    # --simulation compares neither the frames nor the takers between ticks
    status, out = run(tmp_path / "a.txt", tmp_path / "b.txt", "--simulation")
    assert status == 0, out


def test_game_after_a_lobby(tmp_path):
    # (a network test's lobby, of a length the network sets, then its game from tick 1)
    write_log(tmp_path / "a.txt", [(1, 10), (2, 11), (3, 12), (1, 20), (2, 21)])
    write_log(tmp_path / "b.txt", [(1, 10), (2, 11), (1, 20), (2, 21)])
    status, out = run(tmp_path / "a.txt", tmp_path / "b.txt")
    assert status == 1
    status, out = run(tmp_path / "a.txt", tmp_path / "b.txt", "--game")
    assert status == 0, out
    assert "2 ticks alike" in out


def pool(blocks, magic):
    """an objects' memory pool with blocks of 0x200 bytes, each object's
    magic number given"""
    base_address = 0x1000
    data = bytearray(0x38 + 0x200 * len(blocks) + 0x100)
    first = base_address if blocks else 0
    struct.pack_into("<4s", data, 0, b"loop")
    struct.pack_into("<LLLLL", data, 36, base_address, len(data) - 0x38, 0, first, 0)
    for index in range(len(blocks)):
        offset = 0x38 + 0x200 * index
        address = base_address + 0x200 * index
        following = address + 0x200 if index + 1 < len(blocks) else 0
        struct.pack_into("<LlLLLL", data, offset, 0x68656164, 0x200, 0, following, 0, 0x7461696C)
        struct.pack_into("<L", data, offset + 0x18, blocks[index])
        struct.pack_into("<l", data, offset + 0x18 + 8, magic[index])
    return bytes(data)


def test_dump_leaves_out_the_objects_marks(tmp_path):
    write_log(tmp_path / "a.txt", [(1, 10)])
    a = pool([0xE24300CF, 0xEA0D0899], [5, 6])
    b = bytearray(pool([0xE24300CF, 0xEA0D0899], [9, 9]))
    # (the free space after the last block differs too)
    b[-4:] = b"\xff\xff\xff\xff"
    header = bytes(16)
    lines = (tmp_path / "a.txt").read_text().replace("alloc 1 10 64 objects", f"alloc 1 10 {len(a)} objects")
    (tmp_path / "a.txt").write_text(lines)
    (tmp_path / "a.bin").write_bytes(header + a)
    (tmp_path / "b.bin").write_bytes(header + bytes(b))
    status, out = run(tmp_path / "a.txt", tmp_path / "a.txt", "--dump", tmp_path / "a.bin", tmp_path / "b.bin")
    assert status == 0, out
    # a field that is the game's differs
    b[0x38 + 0x200 + 0x18 + 0x20] = 1
    (tmp_path / "b.bin").write_bytes(header + bytes(b))
    status, out = run(tmp_path / "a.txt", tmp_path / "a.txt", "--dump", tmp_path / "a.bin", tmp_path / "b.bin")
    assert status == 1
    assert "block 1 of 512 bytes, tag ea0d0899, +20 in it" in out


def test_rates(tmp_path):
    # a log names its simulation rate (game.sim_rate) before its first tick;
    # one that names none (a build before it) ran at 30
    ticks = [(1, 10), (2, 11)]
    write_log(tmp_path / "old.txt", ticks)
    write_log(tmp_path / "30.txt", ticks)
    write_log(tmp_path / "60.txt", ticks)
    for name, rate in (("30.txt", 30), ("60.txt", 60)):
        text = (tmp_path / name).read_text()
        (tmp_path / name).write_text(text.replace("t 1 ", f"rate {rate}\nt 1 ", 1))
    status, out = run(tmp_path / "old.txt", tmp_path / "30.txt")
    assert status == 0, out
    # runs at two rates are compared only when asked to
    status, out = run(tmp_path / "30.txt", tmp_path / "60.txt")
    assert status == 2 and "different rates (A 30 Hz, B 60 Hz" in out
    status, out = run(tmp_path / "30.txt", tmp_path / "60.txt", "--across-rates")
    assert status == 0 and "comparing across simulation rates" in out, out


def test_asserts_noted(tmp_path):
    # (the asserts line, before a tick's line, gives the run's totals by its end: a note, not a difference)
    ticks = [(1, 10), (2, 11), (3, 12)]
    write_log(tmp_path / "a.txt", ticks)
    lines = (tmp_path / "a.txt").read_text().splitlines()
    lines.insert(lines.index("t 2 g 00000001 l 00000002 h 0000000b"), "asserts 1 3")
    (tmp_path / "b.txt").write_text("\n".join(lines) + "\n")
    status, out = run(tmp_path / "a.txt", tmp_path / "b.txt")
    assert status == 0, out
    assert "note: B's run failed 1 assertions and 3 warnings, the first by tick 2" in out
    assert "A's run" not in out


def test_dump_leaves_out_the_frames_attachments(tmp_path):
    # (an object's attachments: a contrail's index, the frames', and an effect's, the simulation's)
    write_log(tmp_path / "a.txt", [(1, 10)])
    a = bytearray(pool([0xE4090295], [5]))
    datum = 0x38 + 0x18
    a[datum + 0xF4:datum + 0xF6] = bytes([3, 2])
    struct.pack_into("<LL", a, datum + 0xFC, 0xF1000000, 0xE6650004)
    b = bytearray(a)
    struct.pack_into("<L", b, datum + 0xFC, 0xF1000001)
    lines = (tmp_path / "a.txt").read_text().replace("alloc 1 10 64 objects", f"alloc 1 10 {len(a)} objects")
    (tmp_path / "a.txt").write_text(lines)
    (tmp_path / "a.bin").write_bytes(bytes(16) + bytes(a))
    (tmp_path / "b.bin").write_bytes(bytes(16) + bytes(b))
    status, out = run(tmp_path / "a.txt", tmp_path / "a.txt", "--dump", tmp_path / "a.bin", tmp_path / "b.bin")
    assert status == 0, out
    struct.pack_into("<L", b, datum + 0x100, 0xE6650005)
    (tmp_path / "b.bin").write_bytes(bytes(16) + bytes(b))
    status, out = run(tmp_path / "a.txt", tmp_path / "a.txt", "--dump", tmp_path / "a.bin", tmp_path / "b.bin")
    assert status == 1
    assert "+100 in it" in out


def test_free_slots_left_out_of_the_simulation(tmp_path):
    # (a data array's free slots apart, its datums in use alike: the u line)
    for name, whole in (("a.txt", "0000000a"), ("b.txt", "0000000b")):
        lines = ["alloc 0 0 16 header", "alloc 1 10 64 effect"]
        for number in (1, 2):
            lines += [f"a 00000001 {whole}", "u = 00000005", f"t {number} g 00000001 l 00000002 h 00000001"]
        (tmp_path / name).write_text("\n".join(lines) + "\n")
    status, out = run(tmp_path / "a.txt", tmp_path / "b.txt")
    assert status == 1 and "differs in 1 allocations: effect" in out
    status, out = run(tmp_path / "a.txt", tmp_path / "b.txt", "--simulation")
    assert status == 0, out


def test_another_builds_allocations_by_name(tmp_path):
    # (B's build has an allocation more, before objects: compared by name)
    write_log(tmp_path / "a.txt", [(1, 10), (2, 11)])
    lines = ["alloc 0 0 16 header", "alloc 1 10 8 speakers", "alloc 2 18 64 objects"]
    for number, state, objects in ((1, 10, 1), (2, 11, 1)):
        lines += [f"a {state:08x} 00000007 {objects:08x}", f"t {number} g 00000001 l 00000002 h {state:08x}",
                  "G 2: 88001000*2"]
    (tmp_path / "b.txt").write_text("\n".join(lines) + "\n")
    status, out = run(tmp_path / "a.txt", tmp_path / "b.txt")
    assert status == 0, out
    assert "compared by name, not: speakers" in out
