"""The bots' map profiles from play logs (debug.play_log): how people played each map, boiled down to what the bots
read (port/linux/game/bots_profile.c).

    python tools/play_log/profile.py LOG... [--map NAME] [--out port/assets/bots]

reads the play logs (each game's map from its start line; --map names it for a log written before the start line
named one) and writes a profile of each map, port/assets/bots/<map>.txt, from the people's samples (kinds R and L: the
bots' own, B, are left out), and the general one, general.txt, from every map's, for a map with none; then
port/linux/game/bots_profiles.inc, the profiles as the game's C table (bots_profile.c). Every profile is made from the
logs given, so give it every log kept (the logs are not in the repository: a profile's map line says how many games
and player-minutes it is of); a profile of a map none of them played is left as it is. --check writes nothing and fails if the table is not the profiles' (tools/test_bots_profile.py).

A profile's lines (world units; a weight is of 1000 for the map):

    map <name> <games> <player-minutes>
    spot <x> <y> <z> <weight> <fights>     where people were (a 4-unit square's middle, its share of their time,
                                           and the share of that they fired in, of 100)
    teleporter <x> <y> <z> <uses>          an entrance people took, its uses an hour a player
    pickup <weapon> <x> <y> <z> <count>    where people picked a weapon up (one they had not held that life)
    range <weapon> <median> <p25> <p75> <shots>
                                           how far the nearest enemy was as they fired it

The general profile has only the ranges and each weapon's share of the pickups (want <weapon> <share of 1000>)."""

import argparse
import math
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "port" / "assets" / "bots"
TABLE = ROOT / "port" / "linux" / "game" / "bots_profiles.inc"
SPOT_CELL = 4.0
TELEPORTER_CELL = 2.0
PICKUP_CELL = 6.0
SPOTS = 48
# a weapon's range needs this many shots
RANGE_SHOTS = 30
FIRE = 0x800


def weapon_name(tag):
    """a weapon tag's last part, its spaces as _ (weapons\\assault rifle\\assault rifle: assault_rifle), - for
    none"""
    return tag.split("\\")[-1].replace(" ", "_") if tag not in ("-", "") else "-"


def percentile(values, fraction):
    values = sorted(values)
    return values[min(len(values) - 1, int(fraction * len(values)))]


class Map:
    def __init__(self):
        self.games = 0
        self.samples = 0
        self.cells = defaultdict(lambda: [0, 0, 0.0])          # (cx, cy) -> samples, firing, z sum
        self.teleports = defaultdict(lambda: [0, 0.0, 0.0, 0.0])
        self.pickups = defaultdict(lambda: [0, 0.0, 0.0, 0.0])  # (weapon, cx, cy)
        self.ranges = defaultdict(list)
        self.rate = 30


def read_log(path, default_map, maps, kinds):
    """one log's games into maps (by name)"""
    game = None
    last = {}       # player -> (tick, position, in a vehicle)
    held = {}       # player -> the weapons held this life
    frame_tick, frame = None, []

    def flush():
        if game is None or not frame:
            return
        teams = len({sample[1] for sample in frame}) > 1
        for index, team, kind, position, vehicle, flags, weapon in frame:
            if kind not in kinds or vehicle or not flags & FIRE:
                continue
            nearest = None
            for other, other_team, _, other_position, _, _, _ in frame:
                if other == index or (teams and other_team == team):
                    continue
                distance = math.dist(position, other_position)
                if distance < 60 and (nearest is None or distance < nearest):
                    nearest = distance
            if nearest is not None and weapon != "-":
                game.ranges[weapon].append(nearest)

    for line in open(path, errors="replace"):
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "start":
            flush()
            frame_tick, frame = None, []
            # (the scenario's name, a path: its last part; a log before the map's name had none)
            name = fields[3].replace("/", "\\").split("\\")[-1] if len(fields) > 3 and fields[3] != "-" else default_map
            if not name:
                sys.exit(f"{path}: a game with no map named (--map)")
            game = maps.setdefault(name, Map())
            game.games += 1
            game.rate = int(fields[1])
            last.clear()
            held.clear()
            continue
        if game is None:
            continue
        if fields[0] in ("spawn", "death"):
            player = int(fields[2])
            last.pop(player, None)
            held.pop(player, None)
            continue
        if fields[0] != "p" or len(fields) < 22:
            continue
        tick, player, kind = int(fields[1]), int(fields[2]), fields[3]
        if tick != frame_tick:
            flush()
            frame_tick, frame = tick, []
        team = int(fields[4])
        position = tuple(map(float, fields[5:8]))
        flags = int(fields[16], 16)
        vehicle = fields[20] not in ("0", "-")
        # (a log before the map's name wrote the tags' spaces as they are)
        weapon = weapon_name(" ".join(fields[21:-1]))
        frame.append((player, team, kind, position, vehicle, flags, weapon))
        if kind not in kinds:
            continue
        game.samples += 1
        if not vehicle:
            cell = game.cells[(round(position[0] / SPOT_CELL), round(position[1] / SPOT_CELL))]
            cell[0] += 1
            cell[1] += 1 if flags & FIRE else 0
            cell[2] += position[2]
        previous = last.get(player)
        if previous and not vehicle and not previous[2] and tick - previous[0] <= 2 and \
                math.dist(previous[1], position) > 4.0:
            entrance = previous[1]
            teleport = game.teleports[(round(entrance[0] / TELEPORTER_CELL), round(entrance[1] / TELEPORTER_CELL))]
            teleport[0] += 1
            for axis in range(3):
                teleport[1 + axis] += entrance[axis]
        weapons = held.setdefault(player, set())
        if weapon != "-" and weapon not in weapons:
            if weapons and not vehicle:
                pickup = game.pickups[(weapon, round(position[0] / PICKUP_CELL), round(position[1] / PICKUP_CELL))]
                pickup[0] += 1
                for axis in range(3):
                    pickup[1 + axis] += position[axis]
            weapons.add(weapon)
        last[player] = (tick, position, vehicle)
    flush()


def ranges_lines(ranges):
    lines = []
    for weapon in sorted(ranges):
        shots = ranges[weapon]
        if len(shots) >= RANGE_SHOTS:
            lines.append(f"range {weapon} {percentile(shots, 0.5):.1f} {percentile(shots, 0.25):.1f} "
                         f"{percentile(shots, 0.75):.1f} {len(shots)}")
    return lines


def map_profile(name, game):
    minutes = game.samples / game.rate / 60
    lines = [f"# how people played {name}: tools/play_log/profile.py", f"map {name} {game.games} {minutes:.0f}"]
    total = sum(cell[0] for cell in game.cells.values()) or 1
    for (cx, cy), (count, firing, z) in sorted(game.cells.items(), key=lambda item: -item[1][0])[:SPOTS]:
        weight = round(1000 * count / total)
        if weight >= 2:
            lines.append(f"spot {cx * SPOT_CELL:.1f} {cy * SPOT_CELL:.1f} {z / count:.2f} {weight} "
                         f"{round(100 * firing / count)}")
    for (count, x, y, z) in sorted(game.teleports.values(), key=lambda item: -item[0]):
        if count >= 2:
            lines.append(f"teleporter {x / count:.1f} {y / count:.1f} {z / count:.2f} {count / max(minutes, 1e-9) * 60:.0f}")
    for (weapon, _, _), (count, x, y, z) in sorted(game.pickups.items(), key=lambda item: -item[1][0]):
        if count >= 2:
            lines.append(f"pickup {weapon} {x / count:.1f} {y / count:.1f} {z / count:.2f} {count}")
    lines += ranges_lines(game.ranges)
    return "\n".join(lines) + "\n"


def general_profile(maps):
    ranges = defaultdict(list)
    wants = defaultdict(int)
    minutes = 0.0
    for game in maps.values():
        minutes += game.samples / game.rate / 60
        for weapon, shots in game.ranges.items():
            ranges[weapon] += shots
        for (weapon, _, _), (count, _, _, _) in game.pickups.items():
            wants[weapon] += count
    lines = ["# how people played every map profiled, for a map with no profile: tools/play_log/profile.py",
             f"map general {sum(game.games for game in maps.values())} {minutes:.0f}"]
    total = sum(wants.values()) or 1
    for weapon, count in sorted(wants.items(), key=lambda item: -item[1]):
        if count >= 2:
            lines.append(f"want {weapon} {round(1000 * count / total)}")
    lines += ranges_lines(ranges)
    return "\n".join(lines) + "\n"


def c_string(text):
    out = []
    for line in text.splitlines():
        if line.startswith("#"):
            continue
        out.append('\t\t"' + line.replace("\\", "\\\\").replace('"', '\\"') + '\\n"')
    return "\n".join(out)


def table(out):
    """the profiles in out as the C table (port/linux/game/bots_profile.c)"""
    lines = ["/* the bots' map profiles, port/assets/bots: made by tools/play_log/profile.py, not by hand */"]
    for path in sorted(out.glob("*.txt")):
        lines.append(f'\t{{ "{path.stem}",')
        lines.append(c_string(path.read_text()) + " },")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("logs", nargs="*")
    parser.add_argument("--map", default="", help="the map of games whose start line names none")
    parser.add_argument("--out", type=Path, default=OUT)
    parser.add_argument("--table", type=Path, default=TABLE)
    parser.add_argument("--kinds", default="RL", help="the players' kinds taken as people")
    parser.add_argument("--check", action="store_true", help="fail if the table is not the profiles'")
    args = parser.parse_args()
    if args.check:
        if not args.table.is_file() or args.table.read_text() != table(args.out):
            sys.exit(f"{args.table} is not the profiles of {args.out}: run tools/play_log/profile.py")
        return
    maps = {}
    for log in args.logs:
        read_log(log, args.map, maps, set(args.kinds))
    args.out.mkdir(parents=True, exist_ok=True)
    for name, game in sorted(maps.items()):
        if game.samples:
            (args.out / f"{name}.txt").write_text(map_profile(name, game))
            print(f"{name}: {game.games} games, {game.samples / game.rate / 60:.0f} player-minutes")
    if maps:
        (args.out / "general.txt").write_text(general_profile(maps))
    args.table.write_text(table(args.out))


if __name__ == "__main__":
    main()
