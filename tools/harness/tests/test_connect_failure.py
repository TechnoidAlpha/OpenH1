"""What a player is told when a network game cannot be joined (port/linux/game/menu_functions.c's
connect_failure_text, the connecting box's and the server browser's; source/networking/network_client_manager.c's
network_game_client_rejection_text, a host's refusal): the advice for the cause the game knows (this network blocking
picks the network's advice, a phone's Wi-Fi), no technical terms, and every text fits its box in the menus' fonts."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, ROOT, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["cause", "words", "rejection"]

NEGATIVE_CONTROLS = {
    # (this network's block given the general advice)
    "network-blocked-not-told": (("\telse if (status->nat_strict)\n", "\telse if (FALSE)\n"), "cause"),
    # (a phone told what a computer is)
    "phone-not-told-wifi": (('Settings, Online." : on_phone ?', 'Settings, Online." : FALSE ?'), "cause"),
    # (the setting named whether or not it is on)
    "router-setting-always": (('!config_boolean("network.allow_upnp") ?', 'TRUE ?'), "cause"),
    # (a full game told what any refusal is)
    "full-as-any-refusal": (("\tcase _rejection_code_game_is_full:\n", "\tcase 99:\n"), "rejection"),
    # (a technical term back in the words)
    "jargon": (('L"Your network is blocking it."', 'L"This network\'s NAT is strict."'), "words"),
}


def generated(fault=None):
    menus = read("port/linux/game/menu_functions.c")
    p2p = read("port/linux/src/p2p.h")
    protocol = read("source/networking/network_game_protocol.h")
    client = read("source/networking/network_client_manager.c")
    config = "\n\n".join([
        enum_with(p2p, "_p2p_join_idle"), structure(p2p, "p2p_join_status"),
        enum_with(protocol, "_rejection_code_version_too_old"),
        "\n".join(re.findall(r"^#define CONNECT_(?:HEADLINE|DETAIL|LINE)_LENGTH \d+$", menus, re.M))]) + "\n"
    code = "\n\n".join([function(menus, "connect_failure_text"),
                        function(client, "network_game_client_rejection_text")])
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("connect_failure", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("connect_failure", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"


def text_width(words, font):
    """as test_touch_screens.py's: the menus' units, the tag's font (fonts.json) as the game draws it, with 7% to
    spare"""
    from PIL import ImageFont
    import json
    tag, capitals = ("ui\\large_ui", 13) if "large" in font else ("ui\\small_ui", 10)
    fonts = json.loads((ROOT / "port/assets/fonts/fonts.json").read_text())["fonts"]
    face = next(entry["file"] for entry in fonts if entry["tag"] == tag)
    probe = ImageFont.truetype(str(ROOT / "port/assets/fonts" / face), 1000)
    _, top, _, bottom = probe.getbbox("H")
    return probe.getlength(words) * capitals / (bottom - top) * 0.98 * 1.07


def wrapped(words, width):
    """the lines words take, broken at spaces as menu_text_box_wrap breaks them"""
    lines, line = [], ""
    for word in words.split(" "):
        longer = f"{line} {word}".strip()
        if line and text_width(longer, "small") > width:
            lines.append(line)
            line = word
        else:
            line = longer
    return lines + [line]


def test_texts_fit():
    """Each headline fits the connecting box's header (270 wide, large_ui), each of a detail's lines the box (280,
    centred: 268 of it), four lines at most above its buttons; the server browser's sentence ("Couldn't join.", the
    game the panel shows) fits its panel's three lines (148 wide, small_ui) at 640x480."""
    pytest.importorskip("PIL")
    menus = read("port/linux/game/menu_functions.c")
    assert 'L"Couldn\'t join. %s"' in function(menus, "lobby_browser_update")
    status, output = run(build("connect_failure", generated()), "print")
    assert status == 0, output
    for line in output.splitlines():
        kind, words = line[0], line[2:]
        if kind == "H":
            assert text_width(words, "large") <= 270, f"headline {words!r}"
        elif kind == "D":
            assert text_width(words, "small") <= 268, f"detail line {words!r}"
        elif kind == "L":
            assert len(wrapped(f"Couldn't join. {words}", 148)) <= 3, f"browser line {words!r}"
    for block in re.split(r"^H .*$", output, flags=re.M):
        assert sum(line.startswith("D ") for line in block.splitlines()) <= 4, block
