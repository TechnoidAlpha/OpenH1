"""The Spartan link's reticle on a teammate's feed (port/linux/game/spartan_link.c): where their aim lands on the feed
through its camera, whether it is red (a living enemy biped in their weapon's autoaim reach, as Halo's reticle), and
the line along the aim (past their own unit and what it rides; a collision test only). And where it is drawn: on the
link's feeds alone, after the link's own check that it is shown."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["place", "color", "ray"]
CONTROLS = [
    # the feed's up taken as down: the aim from behind lands above the middle
    ("place->y = (bounds->y0 + bounds->y1) * 0.5f -", "place->y = (bounds->y0 + bounds->y1) * 0.5f +", "place"),
    # the reticle kept when it falls off the feed's picture
    ("place->y - margin >= bounds->y0", "place->y >= bounds->y0", "place"),
    # an ally's body red
    ("game_team_is_enemy(team_index, object->object.owner_team_index)", "TRUE", "color"),
    # red at any range
    ("!(distance <= range)", "FALSE", "color"),
    # the line stopped by the teammate's own body
    ("spartan_link_root(result->object_index) != root_index)", "TRUE)", "ray"),
]


def generated(fault=None):
    code = read("port/linux/game/spartan_link.c")
    section = code[code.index("/* ---------- the teammate's aim */"):code.index("/* ---------- the aim as this machine")]
    if fault:
        section = mutated(section, *fault)
    return (("under_test.inc", section),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("spartan_link_aim", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("spartan_link_aim", generated((before, after))), case)
    assert status == CHECK_FAILED, output


def test_drawn_on_the_shown_link_only():
    code = read("port/linux/game/spartan_link.c")
    draw = function(code, "spartan_link_draw")
    # (nothing drawn while the link is hidden: the reticle after the link's check, on a feed's drawn picture)
    assert draw.index("if (!spartan_link_shown(local_player_index))\n\t\treturn;") < draw.index("spartan_link_feed_aim_place(")
    assert "if (halo_view_drawn(SPARTAN_LINK_VIEW(local_player_index, index)))" in draw
    # the aim read from the world only: a collision test, no seed, no effect
    aim = code[code.index("/* ---------- the teammate's aim */"):code.index("/* ---------- the drawing's pieces */")]
    aim = re.sub(r"/\*.*?\*/", "", aim, flags=re.S)
    called = set(re.findall(r"\b([a-z_][a-z0-9_]*)\s*\(", aim))
    touching = [name for name in called if re.search(r"random|seed|effect_new|^damage_|sound|_set_|_new$", name)]
    assert not touching, touching
    assert "collision_test_vector" in called
    # and none of it sent: the network's code never asks
    assert "spartan_link" not in read("port/linux/game/network_distributed.c")
