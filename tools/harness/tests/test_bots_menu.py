"""The bots in the menus: every hosted game's lobby but co-op's has the host's bots' rows (BOTS: NONE, ADD n, FILL TO
n; BOT SKILL; BOT TEAMS), which tools/port_settings.py writes and port/linux/game/lobby_menu.inc shows and writes;
Server Setup has none (a game of this machine alone, Create Game's OFFLINE, hides the network's rows and says what it
is in their place: menu_functions.c's server_settings_local); the menus' spinners of more than two values stop at
their ends (ui_widget.c's ui_widget_port_spinner_stops: the bots' count once went round from the most to none), and a
game the bots left one team alone in goes on (game_engine.c's game_engine_should_end_game)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, ROOT, build, constant, function, mutated, read, run, structure  # noqa: E402

CASES = ["local", "spinner-ends", "lobby-bots", "bots-left"]

NEGATIVE_CONTROLS = {
    "network-rows-kept": (("visible_set(named(list, server_settings_network_rows[index], 0), !local);", ""), "local"),
    "no-line": (('visible_set(named(screen_of(list), "local_note", 0), local);', ""), "local"),
    "spinner-wraps": (("return forward ? widget->parameters.list.selected_index >= count - 1 :",
                       "return FALSE && forward ? widget->parameters.list.selected_index >= count - 1 :"),
                      "spinner-ends"),
    "toggles-stop": (("|| count <= 2)", ")"), "spinner-ends"),
    "xbox-spinners-stop": (("|| !pc_menu_tag(widget->definition_tag_index)", ""), "spinner-ends"),
    "bots-load-writes": (("\tlobby_board.bots_shown = NONE;\n\tlobby_board_bots_choice(spinner, TRUE);",
                          "\tlobby_board_bots_choice(spinner, FALSE);"), "lobby-bots"),
    "bots-never-written": (("if (!locked && lobby_board.bots_shown != NONE", "if (0"), "lobby-bots"),
    "bots-locked-written": (("if (!locked && lobby_board.bots_shown", "if (lobby_board.bots_shown"), "lobby-bots"),
    "bots-left-ends-game": (("!multiple_teams_alive() && !game_engine_port_bot_left()", "!multiple_teams_alive()"),
                            "bots-left"),
    "players-left-goes-on": (("!multiple_teams_alive() && !game_engine_port_bot_left()",
                              "!multiple_teams_alive() && 0"), "bots-left"),
}

MT = "main_menu/multiplayer_type_select"
CE = ROOT / "port/assets/menus/ce"


def generated(fault=None):
    menus = read("port/linux/game/menu_functions.c")
    lobby = read("port/linux/game/lobby_menu.inc")
    widgets = read("source/interface/ui_widget.c")
    engine = read("source/game/game_engine.c")
    rows = menus[menus.index("static char const *const server_settings_network_rows"):]
    rows = rows[:rows.index("};") + 2]
    code = "\n\n".join([rows, function(menus, "server_settings_local"),
                        function(widgets, "ui_widget_port_spinner_stops"),
                        function(lobby, "lobby_board_bots_choice"), function(lobby, "lobby_board_bots_load"),
                        function(engine, "game_engine_port_bot_left"), function(engine, "game_engine_should_end_game")])
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("bots_menu", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("bots_menu", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"


def _port_settings():
    sys.path.insert(0, str(ROOT / "tools"))
    import port_settings

    return port_settings


def test_lobby_has_the_bots_rows():
    """the lobby's bots' rows, the computer's and the touchscreen's, as tools/port_settings.py writes them: BOTS's
    spinner the choices (network_host_controls.c's, as many: bots.h's BOTS_MAXIMUM) loaded by "port lobby bots
    load", BOT SKILL's and BOT TEAMS' their settings' with the values port_config.c takes"""
    port_settings = _port_settings()
    assert port_settings.BOTS_MAXIMUM == int(constant(read("port/linux/game/bots.h"), "BOTS_MAXIMUM"))
    choices = port_settings.BOT_CHOICES
    assert choices[0] == "NONE" and choices[1] == "ADD 1" and "ADD 16" in choices and "ADD 20" in choices
    assert choices[-1] == f"FILL TO {port_settings.BOTS_MAXIMUM}" and "FILL TO 2" in choices
    assert len(choices) == 1 + (16 + (port_settings.BOTS_MAXIMUM - 16) // 4) * 2 - 1
    config = read("port/linux/src/port_config.c")
    for name in ("lobby", "lobby.touch"):
        file_name = f"{MT}.{name}".replace("/", ".") + ".xml"
        text = (CE / file_name).read_text()
        assert text == "\n".join(port_settings.multiplayer_files()[file_name]), "run python3 tools/port_settings.py"
        folder = f"{MT}/lobby" + ("/touch" if name == "lobby.touch" else "")
        count = re.search(rf'<widget name="{folder}/board_bots_count_spinner"[^>]*>(.*?)</widget>', text, re.S)
        assert f'strings="{"|".join(choices)}"' in count.group(0) and "setting=" not in count.group(0)
        assert 'run="port lobby bots load"' in count.group(1)
        for setting, values in (("bots.skill", ["easy", "normal", "heroic", "legendary"]),
                                ("bots.teams", ["even", "against"])):
            key = setting.replace(".", "_")
            spinner = re.search(rf'<widget name="{folder}/board_{key}_spinner"[^>]*>', text).group(0)
            assert f'setting="{setting}"' in spinner and f'values="{"|".join(values)}"' in spinner, spinner
            assert f'{{ "{setting}", _config_' in config
    assert '"port lobby bots load"' in read("port/linux/game/menu_tags.c")
    assert '"port lobby bots load"' in read("port/linux/game/menu_functions.c")


def test_server_setup_has_no_bots():
    """Server Setup has no bots' rows (the lobby has them), and a game of this machine alone's line where the
    network's rows are"""
    port_settings = _port_settings()
    name = f"{MT}/server_settings".replace("/", ".") + ".xml"
    text = (CE / name).read_text()
    assert text == "\n".join(port_settings.multiplayer_files()[name]), "run python3 tools/port_settings.py"
    assert not re.search(r"name=\"[^\"]*bot", text), "a bots row in Server Setup"
    assert f'<child widget="{MT}/server_settings/local_note"/>' in text
    note_top = int(re.search(rf'<widget name="{MT}/server_settings/local_note"[^>]*top="(\d+)"', text).group(1))
    game_y = int(re.search(rf'<child widget="{MT}/server_settings/op_game_mode" x="54" y="(\d+)"/>', text).group(1))
    assert abs(note_top - game_y) <= 8, (note_top, game_y)


def test_multiplayer_has_offline():
    """Multiplayer's HOST has no PLAY WITH BOTS: CREATE GAME's page has OFFLINE (a game of this machine alone), the
    computer's and the touchscreen's, which menu_functions.c's multiplayer_mode_set hosts so"""
    for name in ("hub", "hub.touch"):
        hub = (CE / f"main_menu.multiplayer_type_select.{name}.xml").read_text()
        assert "hub_create_bots" not in hub
        assert re.search(r'<widget name="[^"]*hub_create_offline"', hub), name
    menus = read("port/linux/game/menu_functions.c")
    assert 'strstr(name, "create_offline")' in function(menus, "multiplayer_mode_set")
    assert '{ "hub_create_offline", _multiplayer_item_offline }' in menus
