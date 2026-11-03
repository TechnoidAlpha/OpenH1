#!/usr/bin/env python3
"""Compare two runs' determinism logs (debug.determinism_log).

Two runs of the same input (the same map, debug.frame_time, a scripted
player on the game's clock, debug.test_input) should leave the same game
state after every tick, take the random seeds at the same places in the
same order, and draw the same frames. This reads both logs and reports the
first tick where they part: the game state's allocations whose hashes
differ, the seeds, and the seeds' takers (by function, named from the
executable's symbols: the guest image, build/linux_x86_64/halo_guest.elf
or build/linux_arm64/halo_guest.elf), and the first frame whose pixels
differ.

    python tools/determinism_compare.py a/determinism.txt b/determinism.txt \\
        --symbols build/linux_x86_64/halo_guest.elf

With --dump, two game states written at one tick (debug.determinism_dump)
are compared instead, byte by byte: each allocation that differs, where,
and for a data array the elements and the offsets in them:

    python tools/determinism_compare.py a/determinism.txt b/determinism.txt \\
        --dump a/determinism.txt.300.bin b/determinism.txt.300.bin

A log names the simulation's rate it ran at (game.sim_rate, 60 by default;
none: 30). Two runs at different rates are compared only with
--across-rates: a run to compare against an older build's is made at 30
(HALO_SIM_RATE=30).

Exit status: 0 when the logs agree for every tick both have, 1 when they
part, 2 when a log cannot be read or has no ticks.
"""

import argparse
import struct
import subprocess
import sys
from pathlib import Path

TAKER_LABELS = {
    "G": "global seed, in the tick",
    "L": "local seed, in the tick",
    "g": "global seed, between ticks",
    "l": "local seed (the frames'), between ticks",
    "o": "either seed, on another thread",
    "C": "clock, read in the tick",
}


# the game state's allocations that the frames move, not the ticks (the
# particles and the decals' vertices as they are drawn, the time left over
# from the last tick, the lights each frame, the sound's update count, the
# motion sensor's sweep as the HUD is drawn, the fog and the sound
# environment as they ease toward the camera's, the first-person weapon
# posed at the camera), and the input as the
# frames take it (player control's look, each frame's turn): two
# runs at different frame rates differ there, and nothing the ticks do
# reads them
FRAME_ALLOCATIONS = ["game time globals", "lights", "decal vertex cache", "particle systems",
                     "particle system particles", "particle", "contrail", "contrail point", "game sound globals",
                     "cached object render states", "motion sensor (radar)", "object looping sounds",
                     "scenario globals", "player control globals", "first person weapons"]


# the simulation's rate of a log that names none (game.sim_rate: a build
# before it, which ran at 30)
CLASSIC_RATE = 30


class Tick:
    def __init__(self, number, global_seed, local_seed, state_hash, rate=CLASSIC_RATE, asserts=(0, 0)):
        self.number = number
        # (the run's failed assertions and warnings by this tick's end:
        # debug.fatal_asserts, assert_report.c)
        self.asserts = asserts
        self.global_seed = global_seed
        self.local_seed = local_seed
        self.state_hash = state_hash
        self.rate = rate
        self.allocations = []
        self.takers = {}
        self.frames = []


def expand(sites):
    """a takers' line's sites, each run (<site>*<count>) written out; the
    other threads' sites keep their seed's letter (g or l) before them"""
    out = []
    for site in sites:
        if "*" in site:
            address, count = site.split("*")
            out.extend([address] * int(count))
        else:
            out.append(site)
    return out


def read_log(path):
    allocations = {}
    ticks = []
    pending_allocations = None
    current = None
    rate = CLASSIC_RATE
    asserts = (0, 0)
    with open(path, errors="replace") as log:
        for line in log:
            parts = line.split()
            if not parts:
                continue
            kind = parts[0]
            if kind == "alloc" and len(parts) >= 5:
                allocations[int(parts[1])] = (" ".join(parts[4:]), int(parts[2], 16), int(parts[3]))
            elif kind == "a":
                pending_allocations = parts[1:]
            elif kind == "rate" and len(parts) >= 2 and parts[1].isdigit():
                rate = int(parts[1])
            elif kind == "asserts" and len(parts) >= 3 and parts[1].isdigit() and parts[2].isdigit():
                asserts = (int(parts[1]), int(parts[2]))
            elif kind == "t" and len(parts) >= 8:
                current = Tick(int(parts[1]), parts[3], parts[5], parts[7], rate, asserts)
                current.allocations = pending_allocations or []
                pending_allocations = None
                ticks.append(current)
            elif kind in TAKER_LABELS and current is not None and len(parts) >= 2:
                current.takers[kind] = [int(parts[1].rstrip(":"))] + expand(parts[2:])
            elif kind == "f" and len(parts) >= 4:
                # (the frames before the first tick, the loading's, as many
                # as it took, are not compared)
                if current is not None and int(parts[1]) == current.number:
                    current.frames.append(parts[3])
    return allocations, ticks


class Symbols:
    def __init__(self, path):
        self.path = path
        self.cache = {}

    def name(self, addresses):
        wanted = sorted({a.lstrip("gl") for a in addresses} - set(self.cache))
        if wanted and self.path:
            try:
                result = subprocess.run(["addr2line", "-f", "-e", str(self.path)] + ["0x" + a for a in wanted],
                                        capture_output=True, text=True, check=False)
                lines = result.stdout.splitlines()
                for index, address in enumerate(wanted):
                    self.cache[address] = lines[index * 2] if index * 2 < len(lines) else "?"
            except OSError:
                pass
        for address in wanted:
            self.cache.setdefault(address, "?")
        return [self.cache[a.lstrip("gl")] for a in addresses]


def describe_takers(symbols, sites, in_order=True):
    if not sites:
        return "none"
    count, addresses = sites[0], sites[1:]
    names = symbols.name(addresses)
    if not in_order:
        totals = {}
        for name in names:
            totals[name] = totals.get(name, 0) + 1
        shown = ", ".join(f"{name} x{n}" for name, n in sorted(totals.items(), key=lambda item: -item[1]))
        more = count - len(addresses)
        return f"{count}: {shown}" + (f" (and {more} not kept)" if more > 0 else "")
    runs = []
    for address, name in zip(addresses, names):
        label = f"{name}({address})"
        if runs and runs[-1][0] == label:
            runs[-1][1] += 1
        else:
            runs.append([label, 1])
    shown = ", ".join(label if n == 1 else f"{label} x{n}" for label, n in runs)
    more = count - len(addresses)
    return f"{count}: {shown}" + (f" (and {more} not kept)" if more > 0 else "")


def takers_apart(kind, a, b):
    """whether two runs' takers of a tick differ: for the clock, by the call
    sites that read it, not how often (a load waiting for the disk reads it
    in a loop, as often as the disk takes)"""
    if kind == "C":
        return set((a or [0])[1:]) != set((b or [0])[1:])
    return a != b


def first_taker_difference(a, b):
    """the first site where two takers' lists part, as (index, a's, b's)"""
    for index in range(max(len(a), len(b))):
        x = a[index] if index < len(a) else None
        y = b[index] if index < len(b) else None
        if x != y:
            return index, x, y
    return None


DATA_ARRAY_HEADER_SIZE = 0x38


MEMORY_POOL_HEADER_SIZE = 0x38
# (in an object datum: objects.h)
OBJECT_MAGIC_NUMBER = 0x8
OBJECT_CACHED_RENDER_STATE_INDEX = 0x120
MEMORY_POOL_BLOCK_HEADER_SIZE = 0x18


def memory_pool_blocks(pool):
    """a memory pool's blocks, as (offset, size, the first word in it)"""
    base_address, = struct.unpack_from("<L", pool, 36)
    first_block, = struct.unpack_from("<L", pool, 48)
    blocks = []
    address = first_block
    while address and len(blocks) < 100000:
        offset = address - base_address + MEMORY_POOL_HEADER_SIZE
        if offset < 0 or offset + MEMORY_POOL_BLOCK_HEADER_SIZE > len(pool):
            break
        signature, size, _, next_block = struct.unpack_from("<LlLL", pool, offset)
        if signature != 0x68656164:
            break
        first_word, = struct.unpack_from("<L", pool, offset + MEMORY_POOL_BLOCK_HEADER_SIZE)
        blocks.append((offset, size, first_word))
        address = next_block
    return blocks


def compare_dumps(allocations, path_a, path_b, ignored, limit=12):
    a = Path(path_a).read_bytes()
    b = Path(path_b).read_bytes()
    if len(a) != len(b):
        print(f"the dumps are of different sizes ({len(a)}, {len(b)})")
    position = 0
    differing = 0
    for index in sorted(allocations):
        name, _, size = allocations[index]
        x, y = a[position:position + size], b[position:position + size]
        position += size
        if name == "objects" and x[:4] == b"loop":
            # (not the objects' magic numbers, the collision tests' marks,
            # their cached render states' indices, the renderer's, nor the
            # free space after the last block, as the log's hash leaves
            # them out)
            x, y = bytearray(x), bytearray(y)
            for blocks, data in ((memory_pool_blocks(x), x), (memory_pool_blocks(y), y)):
                for start, size, _ in blocks:
                    for field in (OBJECT_MAGIC_NUMBER, OBJECT_CACHED_RENDER_STATE_INDEX):
                        if field + 4 <= size - MEMORY_POOL_BLOCK_HEADER_SIZE:
                            offset = start + MEMORY_POOL_BLOCK_HEADER_SIZE + field
                            data[offset:offset + 4] = b"\0\0\0\0"
                end = blocks[-1][0] + blocks[-1][1] if blocks else MEMORY_POOL_HEADER_SIZE
                data[end:] = bytes(len(data) - end)
            x, y = bytes(x), bytes(y)
        if x == y or name in ignored:
            continue
        differing += 1
        offsets = [o for o in range(min(len(x), len(y))) if x[o] != y[o]]
        print(f"{name} (#{index}, {size} bytes): {len(offsets)} bytes differ")
        element_size = None
        blocks = memory_pool_blocks(x) if x[:4] == b"loop" else None
        if blocks is not None:
            print(f"  a memory pool of {len(blocks)} blocks")
        if size >= DATA_ARRAY_HEADER_SIZE and x[40:44] == b"@t@d":
            maximum_count, element_size = struct.unpack_from("<hh", x, 32)
            print(f"  a data array of {maximum_count} elements of {element_size} bytes")
        ranges = []
        for o in offsets:
            if ranges and o <= ranges[-1][1] + 4:
                ranges[-1][1] = o
            else:
                ranges.append([o, o])
        for first, last in ranges[:limit]:
            where = f"+{first:x}"
            if element_size and first >= DATA_ARRAY_HEADER_SIZE:
                element, inside = divmod(first - DATA_ARRAY_HEADER_SIZE, element_size)
                where += f" (element {element} +{inside:x})"
            elif element_size:
                where += " (header)"
            elif blocks:
                for block_index, (start, block_size, first_word) in enumerate(blocks):
                    if start <= first < start + block_size:
                        where += f" (block {block_index} of {block_size} bytes, tag {first_word:08x}, +{first - start - MEMORY_POOL_BLOCK_HEADER_SIZE:x} in it)"
                        break
            print(f"  {where}..+{last:x}: A {x[first:last + 1][:16].hex()} B {y[first:last + 1][:16].hex()}")
        if len(ranges) > limit:
            print(f"  ... {len(ranges) - limit} more ranges")
    if not differing:
        print("the game states are alike")
    return 1 if differing else 0


def last_game(ticks):
    """the ticks from the last time the tick count began again at 1 (a network test's game after its lobby,
    whose length is the network's: its messages and joins, by the computer's clock), else all of them"""
    starts = [index for index, tick in enumerate(ticks) if tick.number == 1]
    return ticks[starts[-1]:] if len(starts) > 1 else ticks


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("a")
    parser.add_argument("b")
    parser.add_argument("--symbols", help="the executable (or guest image) that ran, to name the takers")
    parser.add_argument("--ignore", action="append", default=[],
                        help="a game state allocation not to compare (by name; repeatable)")
    parser.add_argument("--from-tick", type=int, default=0, help="compare from this tick on")
    parser.add_argument("--game", action="store_true",
                        help="compare from each log's last game's start, where the tick count began again at 1: "
                             "a network test's game (debug.network_test), after its lobby, whose length the "
                             "network's clock sets")
    parser.add_argument("--dump", nargs=2, metavar=("A_BIN", "B_BIN"),
                        help="compare two game states written at one tick (debug.determinism_dump)")
    parser.add_argument("--simulation", action="store_true",
                        help="compare only what the ticks do (for runs at different frame rates): not the frames, "
                             "the takers between ticks, nor the allocations the frames move (FRAME_ALLOCATIONS)")
    parser.add_argument("--context", type=int, default=2, help="ticks shown before the first difference")
    parser.add_argument("--builds", action="store_true",
                        help="two builds' runs (a change that must leave the game as it was): the game state, the "
                             "seeds and how many took each seed in each tick, not the takers' sites (addresses in "
                             "each build's code), the frames nor the clock's readers")
    parser.add_argument("--across-rates", action="store_true",
                        help="compare runs at different simulation rates (game.sim_rate): alike only while nothing "
                             "runs in the fast phase, or in what stays on the 30 Hz tick")
    args = parser.parse_args()

    try:
        allocations_a, ticks_a = read_log(args.a)
        allocations_b, ticks_b = read_log(args.b)
    except OSError as error:
        print(f"error: {error}")
        return 2
    if args.game:
        ticks_a, ticks_b = last_game(ticks_a), last_game(ticks_b)
    if not ticks_a or not ticks_b:
        print("error: a log has no ticks")
        return 2
    if allocations_a != allocations_b:
        print("note: the two runs' game state allocations differ (another build?)")
    rates_a = sorted({tick.rate for tick in ticks_a})
    rates_b = sorted({tick.rate for tick in ticks_b})
    if rates_a != rates_b:
        rates = f"A {'/'.join(map(str, rates_a))} Hz, B {'/'.join(map(str, rates_b))} Hz"
        if not args.across_rates:
            print(f"error: the runs simulated at different rates ({rates}; game.sim_rate): a run to compare "
                  "against is made at the same rate (HALO_SIM_RATE), or --across-rates compares them anyway")
            return 2
        print(f"note: comparing across simulation rates ({rates})")
    for name, ticks in (("A", ticks_a), ("B", ticks_b)):
        failed, warned = ticks[-1].asserts
        if failed or warned:
            first = next(tick.number for tick in ticks if tick.asserts != (0, 0))
            print(f"note: {name}'s run failed {failed} assertions and {warned} warnings, the first by tick {first} "
                  "(its stderr's \"asserts:\" lines, or debug.txt, name them)")
    symbols = Symbols(args.symbols)
    if args.simulation:
        args.ignore += FRAME_ALLOCATIONS
    ignored = {index for index, (name, _, _) in allocations_a.items() if name in args.ignore}
    if args.dump:
        return compare_dumps(allocations_a, args.dump[0], args.dump[1], set(args.ignore))

    # (in order: a tick's number comes again after the game state is
    # loaded, as a cinematic skipped goes back to the checkpoint before it)
    compared = 0
    frames_compared = 0
    first_frame_difference = None
    previous = []
    for tick_a, tick_b in zip(ticks_a, ticks_b):
        if tick_a.number < args.from_tick:
            continue
        if tick_a.number != tick_b.number:
            print(f"the runs' ticks part after {compared} alike: A's tick {tick_a.number}, B's {tick_b.number}")
            return 1
        compared += 1
        if args.simulation or args.builds:
            tick_a.frames, tick_b.frames = [], []
        for index, (x, y) in enumerate(zip(tick_a.frames, tick_b.frames)):
            frames_compared += 1
            if x != y and first_frame_difference is None:
                first_frame_difference = (tick_a.number, index)
        differing = [index for index, (x, y) in enumerate(zip(tick_a.allocations, tick_b.allocations))
                     if x != y and index not in ignored]
        seeds_differ = (tick_a.global_seed, tick_a.local_seed) != (tick_b.global_seed, tick_b.local_seed)
        tick_kinds = ("G", "L", "C") if args.simulation else tuple(TAKER_LABELS)
        if args.builds:
            # (the sites are addresses in each build's code: how many took
            # each seed in the tick, not where)
            tick_kinds = ("G", "L")
            takers_differ = any((tick_a.takers.get(kind) or [0])[0] != (tick_b.takers.get(kind) or [0])[0]
                                for kind in tick_kinds)
        else:
            takers_differ = any(takers_apart(kind, tick_a.takers.get(kind), tick_b.takers.get(kind))
                                for kind in tick_kinds)
        if not differing and not seeds_differ and not takers_differ:
            previous = (previous + [tick_a])[-args.context:]
            continue
        print(f"first difference at tick {tick_a.number}, the {compared}th logged (debug.determinism_dump={compared})")
        print(f"  seeds: A g {tick_a.global_seed} l {tick_a.local_seed}, B g {tick_b.global_seed} l {tick_b.local_seed}")
        if differing:
            names = [allocations_a.get(index, (f"#{index}", 0, 0))[0] for index in differing]
            print(f"  game state differs in {len(differing)} allocations: " + ", ".join(names))
        for kind in tick_kinds:
            x, y = tick_a.takers.get(kind), tick_b.takers.get(kind)
            if args.builds and (x or [0])[0] != (y or [0])[0]:
                print(f"  takers of the {TAKER_LABELS[kind]} differ: A {(x or [0])[0]}, B {(y or [0])[0]}")
            elif not args.builds and takers_apart(kind, x, y):
                print(f"  takers of the {TAKER_LABELS[kind]} differ:")
                place = first_taker_difference((x or [0])[1:], (y or [0])[1:])
                if place:
                    index, site_a, site_b = place
                    name_a = symbols.name([site_a])[0] if site_a else "-"
                    name_b = symbols.name([site_b])[0] if site_b else "-"
                    print(f"    first apart at the {index + 1}th: A {name_a}({site_a}), B {name_b}({site_b})")
                print(f"    A {describe_takers(symbols, x, False)}")
                print(f"    B {describe_takers(symbols, y, False)}")
        for tick in previous:
            for kind in TAKER_LABELS:
                if kind in tick.takers:
                    print(f"  (tick {tick.number}, both: {TAKER_LABELS[kind]}: {describe_takers(symbols, tick.takers[kind], False)})")
        if first_frame_difference:
            print(f"  first frame apart: tick {first_frame_difference[0]} frame {first_frame_difference[1]}")
        return 1
    print(f"{compared} ticks alike (ticks {max(ticks_a[0].number, ticks_b[0].number)}-"
          f"{min(ticks_a[-1].number, ticks_b[-1].number)}), " +
          ("the game state and the seeds" if args.builds else
           "the simulation only" if args.simulation else f"{frames_compared} frames compared"), end="")
    if first_frame_difference:
        print(f"; first frame apart: tick {first_frame_difference[0]} frame {first_frame_difference[1]}")
        return 1
    print(", all alike")
    return 0 if compared else 2


if __name__ == "__main__":
    sys.exit(main())
