"""A hosted internet game's listing in the server browser, as the menus set it (port/linux/game/menu_functions.c's
Create Game, Server Setup and START GAME, host_controls_menu.inc's LISTING): a public game stays listed through
Server Setup, the lobby's GAME SETTINGS and START GAME again, and Host Controls; Host Controls' LISTING turns it
private and public again, which Server Setup keeps after; a private game is listed only once made public; a local
network game never is, nor has the row."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["public", "toggle", "private", "lan"]

# a fault in the menus' code, and the case that must catch it
NEGATIVE_CONTROLS = {
    "setup-unlists": (("p2p_set_hosting_public(multiplayer.mode == _multiplayer_mode_host_internet && "
                       "!server_settings_private());", "p2p_set_hosting_public(FALSE);"), "public"),
    "setup-lists-private": (("p2p_set_hosting_public(multiplayer.mode == _multiplayer_mode_host_internet && "
                             "!server_settings_private());", "p2p_set_hosting_public(TRUE);"), "toggle"),
    "setup-shows-public": (("spinner->parameters.list.selected_index = server_settings_private() ? 1 : 0;",
                            "spinner->parameters.list.selected_index = 0;"), "private"),
    "made-private": (("multiplayer.game_private = !config_boolean(\"network.host_public\");",
                      "multiplayer.game_private = TRUE;"), "public"),
    "lan-listed": (("p2p_set_hosting_public(multiplayer.mode == _multiplayer_mode_host_internet && "
                    "!multiplayer.game_private);", "p2p_set_hosting_public(!multiplayer.game_private);"), "lan"),
    "no-password": (("\t\tmultiplayer.game_password : NULL);", "\t\tNULL : NULL);"), "public"),
    "toggle-not-listed": (("\t\tp2p_set_hosting_public(!server_settings_private());\n", ""), "toggle"),
    "toggle-not-kept": (("\t\tserver_settings_private_set(!server_settings_private());\n", ""), "toggle"),
    "row-says-public": (("server_settings_private() ? L\"PRIVATE\" : L\"PUBLIC\"", "L\"PUBLIC\""), "toggle"),
    "row-in-lan-game": (("shown = multiplayer.mode == _multiplayer_mode_host_internet && "
                         "config_boolean(\"network.online\") &&", "shown = config_boolean(\"network.online\") &&"),
                        "lan"),
}


def generated(fault=None):
    menus = read("port/linux/game/menu_functions.c")
    host = read("port/linux/game/host_controls_menu.inc")
    config = enum_with(menus, "_multiplayer_mode_host_internet,") + "\n"
    config += enum_with(host, "_host_game_listing,") + "\n"
    config += enum_with(host, "_host_tab_maps,") + "\n"
    config += enum_with(read("port/linux/game/network_host_game.h"), "_host_game_shuffle,") + ";\n"
    admin = read("port/linux/game/network_admin.h")
    config += enum_with(admin, "_network_admin_lists,") + "\n" + enum_with(admin, "_network_admin_flag_admin,") + "\n"
    config += re.search(r"static long const host_score_steps\[\] = \{[^}]*\};", host).group(0) + "\n"
    config += re.search(r"static short const host_time_steps\[\] = \{[^}]*\};", host).group(0) + "\n"
    config += re.search(r"static short const maximum_players\[\] = \{[^}]*\};", menus).group(0) + "\n"
    config += re.search(r"static short const cooperative_friendly_fire_modes\[\] =\s*\{[^}]*\};", menus).group(0)
    config += "\n"
    code = "\n\n".join([function(menus, name) for name in (
        "multiplayer_mode_set", "multiplayer_host", "server_settings_private", "server_settings_private_set",
        "server_settings_initialize", "ui_widget_port_cooperative_settings_apply",
        "server_start")] + [function(host, name) for name in (
        "host_notice", "host_confirming", "host_confirmed", "host_game", "host_game_teams", "host_game_rows_read",
        "host_game_limits", "host_game_row_text", "host_step_value", "host_password_done", "host_announce_done",
        "host_teams_arrange", "host_game_row_change")])
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("hosted_listing", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("hosted_listing", generated(fault)), case)
    assert status == CHECK_FAILED, f"the menus' code with a fault ({control}) passed '{case}': the test cannot see it"
