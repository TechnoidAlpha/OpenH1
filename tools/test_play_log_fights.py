"""tools/play_log/fights.py on made-up play logs (no game, no maps): a person's flick onto an enemy is measured (its
reaction, its overshoot, its settling), shots fired with the aim on an enemy are on target and off it not, a bot's
blow that takes the enemy's shield is a hit and one that does not a whiff, and a log named for a group puts its bots
in that group, not its scripted player."""

import json
import math
import subprocess
import sys
from pathlib import Path

FIGHTS = Path(__file__).resolve().parent / "play_log" / "fights.py"
FIRE, MELEE = 0x800, 0x80


def line(tick, index, kind, team, position, aim_degrees, flags=0, health=(1.0, 1.0), weapon="weapons\\pistol\\pistol"):
    yaw = math.radians(aim_degrees)
    return (f"p {tick} {index} {kind} {team} {position[0]:.3f} {position[1]:.3f} {position[2]:.3f} 0 0 0 "
            f"{math.cos(yaw):.4f} {math.sin(yaw):.4f} 0.0000 0 0 {flags:x} 0 {health[0]:.3f} {health[1]:.3f} - "
            f"{weapon} p{index}\n")


def flick_log(path):
    """a person (0) at the origin, an enemy (1) 10 units along x: the person's aim 60 degrees off, then a flick that
    goes 5 degrees past and settles on the enemy, firing once on it; then off it by 20 degrees, firing"""
    aims = [60.0] * 5 + [40, 20, 5, -5, -3, -1, 0] + [0.0] * 20 + [20.0] * 10
    with open(path, "w") as f:
        f.write("start 30 1 levels\\test\\bloodgulch\\bloodgulch\n")
        for tick, aim in enumerate(aims, 100):
            fire = FIRE if tick - 100 in (20, 21, 22, 35, 36) else 0
            f.write(line(tick, 0, "R", 0, (0, 0, 0), aim, fire))
            f.write(line(tick, 1, "R", 1, (10, 0, 0), 180))


def blow_log(path):
    """a bot (0) and an enemy (1) a unit off: a blow that takes half the enemy's shield, and one that takes nothing; the
    scripted player (2, kind L) firing at the enemy, which a group's log leaves out"""
    with open(path, "w") as f:
        f.write("start 30 1 levels\\test\\bloodgulch\\bloodgulch\n")
        for tick in range(100, 200):
            press = MELEE if tick in (120, 160) else 0
            shield = 0.5 if 122 <= tick < 160 else 1.0 if tick < 122 else 0.5
            f.write(line(tick, 0, "B", 0, (0, 0, 0), 0, press))
            f.write(line(tick, 1, "B", 1, (1.0, 0, 0), 180, health=(1.0, shield)))
            f.write(line(tick, 2, "L", 0, (-5, 0, 0), 0, FIRE))


def fights(tmp_path, *logs):
    out = tmp_path / "fights.json"
    subprocess.run([sys.executable, str(FIGHTS), *map(str, logs), "--json", str(out)], check=True, capture_output=True)
    return json.loads(out.read_text())


def test_flick_and_shots(tmp_path):
    log = tmp_path / "flick.log"
    flick_log(log)
    table = fights(tmp_path, log)
    # (the enemy came within 25 degrees at the flick's 20, the aim within 4 at its -1... 0 two ticks later: 67 ms)
    assert table["flicks (n)"]["people"] >= 1
    assert 30 <= table["reaction ms (median)"]["people"] <= 120
    assert table["flick overshoots %"]["people"] == 100.0
    assert 4.0 <= table["overshoot deg (median)"]["people"] <= 6.0
    # (three shots on the enemy, two 20 degrees off it: those are not within 10 of it, not counted)
    assert table["on target %"]["people"] == 100.0
    # (level with the enemy's origin: the eye 0.6 up, its middle 0.5, a tenth of a unit over 10)
    assert abs(table["fire err deg p50"]["people"] - math.degrees(math.atan(0.1 / 10))) < 0.05


def test_blows_and_groups(tmp_path):
    log = tmp_path / "blows.log"
    blow_log(log)
    table = fights(tmp_path, f"legendary={log}")
    assert table["blows (n)"]["legendary"] == 2
    assert table["blow whiff % (enemy <3)"]["legendary"] == 50.0
    assert table["blow distance p10/p50/p90"]["legendary"].startswith("1.00")
    # (the scripted player's shots are not the bots')
    assert table["fire err deg p50"]["legendary"] != table["fire err deg p50"]["legendary"]  # nan: no shots
    assert table["blows (n)"]["people"] == 0
