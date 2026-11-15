#!/usr/bin/env python3
"""A demo's contents (port/linux/game/demo.c, docs/demos.md): its header,
its blocks and its records by type, their sizes packed and unpacked, its
ticks, bookmarks, snapshots and the host's commands to its bots.

    python tools/demo_info.py demos/20261108-014642-a10.demo
    python tools/demo_info.py --patches demos/....demo    # each difference from outside the ticks
    python tools/demo_info.py --check demos/....demo      # each tick's shape (format 3), exit 1 if not
"""

import argparse
import struct
import sys
import zlib
from collections import Counter, defaultdict
from pathlib import Path

HEADER_SIZE = 512
BLOCK_HEADER_SIZE = 20
BLOCK_MAGIC = 0x4B4C4244
CONNECTIONS = {0: "local", 1: "network client", 2: "network server", 3: "film playback"}
SNAPSHOT_FLAGS = {0: "start", 1: "loaded", 2: "reset", 3: "seek"}
# the game's rules the header holds (demo.h's _demo_rule_*), from format 3
RULES = {0: "enhanced animations", 1: "turns with the aim", 2: "walks over a second weapon",
         3: "simulation fixes"}
# the records the tick's code takes as it runs, which only a tick holds,
# between its sub-steps' U and its Z
TICK_CODE_RECORDS = set("DFIL")


class Demo:
    def __init__(self, data: bytes):
        if len(data) < HEADER_SIZE or data[:7] != b"OH1DEMO":
            raise ValueError("not a demo")
        self.version = data[8]
        self.build = data[16:80].split(b"\0")[0].decode("ascii", "replace")
        values = struct.unpack_from("<11I", data, 80)
        (self.network_version, self.allocation_checksum, self.state_size, self.connection, self.rate,
         self.difficulty, self.start_tick, self.time, self.local_players, self.server, self.rules) = values
        self.map_name = data[128:256].split(b"\0")[0].decode("ascii", "replace")
        self.blocks = []
        offset = HEADER_SIZE
        while offset + BLOCK_HEADER_SIZE <= len(data):
            magic, kind, first_tick, size, packed_size = struct.unpack_from("<5I", data, offset)
            if magic != BLOCK_MAGIC or offset + BLOCK_HEADER_SIZE + packed_size > len(data):
                break
            packed = data[offset + BLOCK_HEADER_SIZE:offset + BLOCK_HEADER_SIZE + packed_size]
            self.blocks.append((chr(kind), first_tick, size, packed_size, packed))
            offset += BLOCK_HEADER_SIZE + packed_size
        self.trailing = len(data) - offset

    def records(self, kinds="RK"):
        """each record of the blocks of those kinds: (block kind, type, bytes)"""
        for kind, _, size, _, packed in self.blocks:
            if kind not in kinds:
                continue
            raw = zlib.decompress(packed)
            if len(raw) != size:
                raise ValueError("a block unpacks to another size")
            position = 0
            while position + 5 <= len(raw):
                record_type = chr(raw[position])
                (length,) = struct.unpack_from("<I", raw, position + 1)
                yield kind, record_type, raw[position + 5:position + 5 + length]
                position += 5 + length


def shape_problems(records, limit=20):
    """where a format 3 demo's records are not each tick's fixed shape: a T,
    what came from outside the tick, the players' input (A), each sub-step's
    U (the tick's number, the sub-step's, their count) in order, the tick's
    code's records (D, F, I, L) only inside the tick, and a Z with the
    tick's number; between two ticks only what came from outside them. A
    demo may end inside a tick (a recorder stopped there writes its E)"""
    problems = []
    tick = None
    substeps = None
    next_substep = 0
    ticks = 0

    def problem(text):
        if len(problems) < limit:
            problems.append(text)

    for _, record_type, data in records:
        if record_type == "T":
            if tick is not None:
                problem(f"tick {tick} has no Z before the T of tick {struct.unpack_from('<I', data)[0]}")
            tick = struct.unpack_from("<I", data)[0]
            substeps = None
            next_substep = 0
            ticks += 1
        elif record_type == "U":
            if len(data) < 6:
                problem(f"a U of {len(data)} bytes")
                continue
            u_tick, index, count = struct.unpack_from("<IBB", data)
            if tick is None or u_tick != tick:
                problem(f"a U of tick {u_tick} in tick {tick}")
            if index != next_substep or (substeps is not None and count != substeps):
                problem(f"tick {tick}'s sub-step {index} of {count} where {next_substep} was next")
            substeps = count
            next_substep = index + 1
        elif record_type == "Z":
            z_tick = struct.unpack_from("<I", data)[0] if len(data) >= 4 else None
            if tick is None or z_tick != tick:
                problem(f"a Z of tick {z_tick} in tick {tick}")
            elif substeps is not None and next_substep != substeps:
                problem(f"tick {tick} ended after {next_substep} of its {substeps} sub-steps")
            tick = None
        elif record_type in TICK_CODE_RECORDS:
            if tick is None:
                problem(f"a {record_type} between two ticks")
        elif record_type == "A":
            if tick is None or next_substep:
                problem(f"tick {tick}'s input (A) not before its first sub-step")
        elif record_type == "E":
            tick = None
    return ticks, problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("demo", type=Path)
    parser.add_argument("--patches", action="store_true", help="list each difference (P record) by its tick")
    parser.add_argument("--check", action="store_true", help="check each tick's shape (format 3); exit 1 if not")
    args = parser.parse_args()
    demo = Demo(args.demo.read_bytes())
    if args.check:
        if demo.version < 3:
            print(f"{args.demo.name}: format {demo.version}, which has no bounded ticks to check (3 and later)")
            return 1
        ticks, problems = shape_problems(demo.records("R"))
        for text in problems:
            print(f"  {text}")
        print(f"{args.demo.name}: {ticks} ticks, " + ("each of the fixed shape" if not problems else "not of the fixed shape"))
        return 1 if problems else 0
    print(f"{args.demo.name}: format {demo.version}, build {demo.build}, network version {demo.network_version}")
    print(f"  map {demo.map_name}, {CONNECTIONS.get(demo.connection, demo.connection)}, rate {demo.rate}, "
          f"difficulty {demo.difficulty}, from tick {demo.start_tick}, {demo.local_players} local players"
          f"{', a dedicated server' if demo.server else ''}")
    if demo.version >= 3:
        rules = [name for bit, name in RULES.items() if demo.rules & (1 << bit)]
        print(f"  the game's rules: {', '.join(rules) if rules else 'none of the enhancements'}")
    by_kind = defaultdict(lambda: [0, 0, 0])
    for kind, _, size, packed_size, _ in demo.blocks:
        by_kind[kind][0] += 1
        by_kind[kind][1] += size
        by_kind[kind][2] += packed_size
    total = sum(entry[2] for entry in by_kind.values())
    for kind, (count, size, packed) in sorted(by_kind.items()):
        name = {"R": "records", "K": "snapshots taken", "k": "seek snapshots"}.get(kind, kind)
        print(f"  {name}: {count} blocks, {size / 1e6:.2f} MB unpacked, {packed / 1e6:.2f} MB packed")
    types = Counter()
    sizes = Counter()
    ticks = 0
    first = last = None
    patches = []
    tick = None
    bookmarks = []
    bot_commands = []
    for kind, record_type, data in demo.records("R"):
        types[record_type] += 1
        sizes[record_type] += len(data) + 5
        if record_type == "T":
            tick = struct.unpack_from("<I", data)[0]
            ticks += 1
            first = tick if first is None else first
            last = tick
        elif record_type == "P":
            patches.append((tick, len(data)))
        elif record_type == "B":
            bookmarks.append(struct.unpack_from("<II", data))
        elif record_type == "W" and len(data) >= 4:
            # (the host's line to its bots, as its tick applied it: port/linux/game/bots_command.c)
            bot_commands.append((struct.unpack_from("<I", data)[0], data[4:].decode("ascii", "replace")))
    seconds = ticks / 30.0
    print(f"  {ticks} ticks ({seconds / 60:.1f} min of game), ticks {first} to {last}; "
          f"{total / 1e6:.2f} MB, {total / 1e6 / max(seconds / 60, 1e-9):.2f} MB a minute"
          + (f"; {demo.trailing} bytes cut short at the end" if demo.trailing else ""))
    for record_type in sorted(types):
        print(f"    {record_type}: {types[record_type]} records, {sizes[record_type] / 1e6:.3f} MB unpacked")
    for tick_value, milliseconds in bookmarks:
        print(f"  bookmark at tick {tick_value}: the last {milliseconds / 1000:.0f} s")
    for tick_value, line in bot_commands:
        print(f"  bots at tick {tick_value}: {line}")
    if args.patches:
        for tick_value, size in patches:
            print(f"  P at tick {tick_value}: {size} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
