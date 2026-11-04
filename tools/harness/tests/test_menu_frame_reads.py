"""The menus' per-frame functions read no player files (port/linux/game/menu_functions.c and the screens it
includes).

pc_menu_game_data_function_invoke runs each frame for each widget with a game data input function ("... update"). A
player file read from there is read every frame: the saved game (ui_widget_port_saved_game reads and checks the whole
of savegame.bin, 20 MB) held the Campaign menu to 15 frames a second. These files are read when a screen opens, or
by a reader that reads at most every so often, and the frame's functions use what they read."""

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import function, mutated, read  # noqa: E402

FILES = ("menu_functions.c", "party_menu.inc", "matchmaking_menu.inc", "friends_menu.inc", "lobby_menu.inc",
         "campaign_menu.inc", "host_controls_menu.inc")

# what reads a player's file (the saved game, a profile)
FILE_READS = {"ui_widget_port_saved_game", "game_state_test_persistent_storage", "player_profile_get",
              "campaign_profile", "campaign_levels_read", "saved_games_read"}

# readers the frame may call, which read at most every so often or only on a change
PACED = {
    "profile_name_show",  # once a second
    "profile_list_read",  # when the profiles enumerated are not those read last
    "multiplayer_status_read",  # every MULTIPLAYER_STATUS_READ_TIME
    "pc_menu_profile_edit_begin",  # once, back from the profile manager
}

DEFINITION = re.compile(r"^(?:static |__inline )?[\w *]+?\b(\w+)\(\s*[^;{]*\)\s*\{", re.M)
CALL = re.compile(r"\b(\w+)\s*\(")


def sources():
    return {name: read(f"port/linux/game/{name}") for name in FILES}


def definitions(texts):
    found = {}
    for text in texts.values():
        for match in DEFINITION.finditer(text):
            found.setdefault(match.group(1), function(text, match.group(1)))
    return found


def frame_reads(texts):
    """each path from the per-frame dispatch to a player file's read"""
    functions = definitions(texts)
    dispatch = functions["pc_menu_game_data_function_invoke"]
    paths, seen = [], set()
    stack = [[name] for name in set(CALL.findall(dispatch)) if name in functions]
    while stack:
        path = stack.pop()
        name = path[-1]
        if name in seen or name in PACED:
            continue
        seen.add(name)
        for called in set(CALL.findall(functions[name].split("{", 1)[1])):
            if called in FILE_READS:
                paths.append(" > ".join(path + [called]))
            elif called in functions and called not in seen:
                stack.append(path + [called])
    return sorted(paths)


def test_no_file_read_each_frame():
    assert frame_reads(sources()) == []


def test_dispatch_reaches_the_campaign_screens():
    """the walk reaches the screens it is to check (else it would pass for nothing)"""
    functions = definitions(sources())
    assert "campaign_level_status" in functions and "multiplayer_status_update" in functions


def test_negative_control_saved_game_read_each_frame():
    """the Campaign menu's panel reading the saved game again, as it did"""
    texts = sources()
    texts["campaign_menu.inc"] = mutated(
        texts["campaign_menu.inc"],
        "\tif (level == campaign.saved_level && campaign.saved_difficulty != NONE)\n",
        "\tif (level == campaign.saved_level && ui_widget_port_saved_game(NULL, NULL, NULL))\n")
    assert any(path.endswith("campaign_level_status > ui_widget_port_saved_game")
               for path in frame_reads(texts)), frame_reads(texts)


def test_negative_control_profile_read_each_frame():
    """the multiplayer menu's NEXT LEVEL reading the profile each frame, as it did"""
    texts = sources()
    texts["menu_functions.c"] = mutated(
        texts["menu_functions.c"],
        "\t\tif (multiplayer_status.next_level != NONE)\n",
        "\t\tif (multiplayer_status_next_level() != NONE)\n")
    assert any(path.endswith("multiplayer_status_next_level > player_profile_get")
               for path in frame_reads(texts)), frame_reads(texts)
