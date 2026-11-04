"""The bots' map profiles (tools/play_log/profile.py, port/assets/bots, port/linux/game/bots_profiles.inc): the table
the game is built with is the profiles', and a play log is boiled down to the places people were and fought in, the
teleporters they took, the weapons they picked up and how far off they fired them, people's samples alone."""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "play_log"))
import profile as bots_profile  # noqa: E402


def test_table_is_the_profiles():
    result = subprocess.run([sys.executable, str(ROOT / "tools" / "play_log" / "profile.py"), "--check"],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def _sample(tick, player, kind, team, position, fire=False, weapon="weapons\\pistol\\pistol"):
    x, y, z = position
    return (f"p {tick} {player} {kind} {team} {x} {y} {z} 0 0 0 1 0 0 0 0 {0x800 if fire else 0:x} 0 1 1 - "
            f"{weapon} name{player}\n")


def test_a_log_boiled_down(tmp_path):
    lines = ["start 30 1 levels\\test\\testmap\\testmap\n", "spawn 1 0\n", "spawn 1 1\n", "spawn 1 2\n"]
    for tick in range(1, 301):
        # (a person firing at an enemy 20 units off; a bot beside, left out)
        lines.append(_sample(tick, 0, "R", 0, (10.0, 10.0, 0.0), fire=tick % 2 == 0))
        lines.append(_sample(tick, 1, "R", 1, (30.0, 10.0, 0.0)))
        lines.append(_sample(tick, 2, "B", 1, (90.0, 90.0, 0.0), fire=True))
    # (the enemy steps onto a teleporter at 30, 10 and comes out at 60, 60; then picks up a rifle)
    lines.append(_sample(301, 1, "R", 1, (30.0, 10.0, 0.0)))
    lines.append(_sample(302, 1, "R", 1, (60.0, 60.0, 0.0)))
    lines.append(_sample(303, 1, "R", 1, (60.0, 60.0, 0.0)))
    lines.append(_sample(304, 1, "R", 1, (60.0, 61.0, 0.0), weapon="weapons\\assault rifle\\assault rifle"))
    lines.append(_sample(305, 1, "R", 1, (60.0, 61.0, 0.0), weapon="weapons\\assault rifle\\assault rifle"))
    log = tmp_path / "play.log"
    log.write_text("".join(lines))
    maps = {}
    bots_profile.read_log(str(log), "", maps, {"R", "L"})
    assert list(maps) == ["testmap"]
    text = bots_profile.map_profile("testmap", maps["testmap"])
    words = [line.split() for line in text.splitlines() if not line.startswith("#")]
    spots = {(float(w[1]), float(w[2])): (int(w[4]), int(w[5])) for w in words if w[0] == "spot"}
    assert (8.0, 8.0) in spots and (32.0, 8.0) in spots, spots
    assert spots[(8.0, 8.0)][1] == 50, "the share fired in"
    assert all(abs(x - 90) > 4 for x, _ in spots), "a bot's place taken for a person's"
    teleporters = [w for w in words if w[0] == "teleporter"]
    assert not teleporters, "one use is not a teleporter people take"
    pickups = [w for w in words if w[0] == "pickup"]
    assert not pickups, "one pickup is not a place people go for"
    ranges = {w[1]: float(w[2]) for w in words if w[0] == "range"}
    assert ranges == {"pistol": 20.0}, ranges
    general = bots_profile.general_profile(maps)
    assert "range pistol 20.0" in general and "spot" not in general
