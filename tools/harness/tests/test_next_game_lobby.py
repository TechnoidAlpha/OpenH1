"""A network game's end and the next game (next_game_lobby.c): after the host's PICK GAME on the scores every
machine loads the main menu's map for the next game's lobby, behind the loading screen; the lobby's clock runs, so it
is drawn and the loading screen ends (else it stayed over the lobby for good, on the host and on every OpenH1 client),
no tick of the ended game is left open, and the next game plays at the rate agreed for it. For the host and a client,
of an OpenH1 game at 30, 60 and 120 and of a Legacy one; and of a mixed game at 60 and 120 (network.allow_opence),
whose OpenCE machine is still known as one in the next game, its player stepped once a tick and sent no rate, while
the OpenH1 client takes the next game's rate."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = [f"{role}:{rate}" for role in ("host", "client") for rate in ("30", "60", "120", "legacy")]
CASES += [f"mixed-{role}:{rate}" for role in ("host", "client") for rate in ("60", "120")]

LOBBY_CLOCK = ("\tif (!main_menu_is_loaded())\n\t\tgame_time_end();", "\tgame_time_end();")

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # (the lobby's clock stopped, as it was: the loading screen stayed over the lobby)
    "lobby-clock-stopped": (LOBBY_CLOCK, "client:120"),
    "host-lobby-clock-stopped": (LOBBY_CLOCK, "host:60"),
    "mixed-lobby-clock-stopped": (LOBBY_CLOCK, "mixed-host:120"),
    "ended-game-clock-runs": ((LOBBY_CLOCK[0], ""), "host:30"),
    "tick-left-open": (("\tsim_rate_tick_drop();\n\tcsmemset(&game_time_substeps", "\tcsmemset(&game_time_substeps"),
                       "client:120"),
    "rate-of-the-last-game": (("\tif (in_lobby)\n\t\tnetwork_standard_globals.hosting_rate", "\tif (FALSE)\n\t\t"
                               "network_standard_globals.hosting_rate"), "host:60"),
    "opence-players-of-the-last-game": (("\tnetwork_standard_globals.opence_players_time = NONE;\n", ""),
                                        "mixed-host:120"),
    "client-hears-the-last-game": (("\tcsmemset(network_standard_globals.opence_players_heard, 0, "
                                    "sizeof(network_standard_globals.opence_players_heard));\n", ""),
                                   "mixed-client:60"),
    "machine-kinds-forgotten": (("\tnetwork_standard_globals.opence_players_time = NONE;\n",
                                 "\tnetwork_standard_globals.opence_players_time = NONE;\n"
                                 "\tcsmemset(network_standard_globals.machine_kinds, 0, "
                                 "sizeof(network_standard_globals.machine_kinds));\n"), "mixed-host:60"),
    "opence-sent-the-rate": (("\tlegacy_game.standard_rate = 0;\n", ""), "mixed-host:120"),
}

# network_standard.c's, of the rate and the mixed games' machines and players
STANDARD_FUNCTIONS = (
    "network_standard_server_setting", "network_standard_rate_setting", "network_standard_server_created",
    "network_standard_hosting_mixed",
    "network_standard_join_kind", "network_standard_machine_joined", "network_standard_machine_kind",
    "network_standard_machine_is_opence", "network_standard_machine_is_openh1", "network_standard_game_for_machine",
    "network_standard_note_opence_players", "network_standard_player_is_opence", "network_standard_new_game",
    "network_standard_handle_message", "network_standard_server_rate", "network_standard_stamp_game",
    "network_standard_join_token", "network_standard_client_joining", "network_standard_agreed_rate",
)


def tail(source, marker):
    """the source from a marker on: a file's globals and its code"""
    return source[source.index(marker):]


def generated(fault=None):
    sim_header = read("port/linux/game/sim_rate.h")
    standard = read("port/linux/game/network_standard.c")
    distributed = read("port/linux/game/network_distributed.h")
    limits = read("port/linux/include/halo_port_limits.h")
    config = enum_with(read("source/game/game.h"), "_game_connection_network_client") + "\n"
    config += enum_with(sim_header, "SIM_RATE_CLASSIC") + "\n" + enum_with(sim_header, "NUMBER_OF_SIM_SUBSTEP_SLOTS") + "\n"
    start = sim_header.index("typedef void (*sim_substep_procedure)")
    config += sim_header[start:sim_header.index(";", start) + 1] + "\n"
    for name in ("HALO_PORT_STANDARD_NETWORK_VERSION", "HALO_PORT_MAXIMUM_NETWORK_MACHINES",
                 "HALO_PORT_MAXIMUM_NETWORK_PLAYERS"):
        config += "#define %s %d\n" % (name, constant(limits, name))
    config += "#define TICKS_PER_SECOND 30\n#define MAXIMUM_TRACKED_PLAYERS HALO_PORT_MAXIMUM_NETWORK_PLAYERS\n"
    config += "#define _distributed_message_opence_players %d\n" % constant(distributed,
                                                                           "_distributed_message_opence_players")
    standard_header = read("port/linux/game/network_standard.h")
    config += enum_with(standard_header, "_network_standard_join_legacy") + "\n"
    config += "struct network_game;\n" + standard_header[standard_header.index("/* ---------- prototypes */"):
                                                        standard_header.rindex("#endif")]
    config += enum_with(standard, "MAXIMUM_STANDARD_MACHINES") + "\n"
    config += structure(distributed, "distributed_message_header") + "\n"
    config += structure(standard, "distributed_opence_players") + "\n"

    code = tail(read("port/linux/game/sim_rate.c"), "/* ---------- globals */")
    clock = read("source/game/game_time.c")
    code += "\n" + structure(clock, "game_time_globals_struct")
    code += "\nstatic struct game_time_globals_struct *game_time_globals;\n"
    end = clock.index("} game_time_substeps;") + len("} game_time_substeps;")
    code += clock[clock.rindex("static struct", 0, end):end] + "\n"
    for name in ("game_time_port_drop_substeps", "game_time_initialize_for_new_map", "game_time_dispose_from_old_map",
                 "game_time_end", "game_time_start", "game_in_progress", "game_time_get"):
        code += "\n" + function(clock, name)
    code += "\n" + standard[standard.index("/* ---------- globals */"):standard.index("/* ---------- private code */")]
    for name in STANDARD_FUNCTIONS:
        code += "\n" + function(standard, name)
    main = read("source/main/main.c")
    for name in ("main_menu_is_loaded", "main_new_map", "main_load_ui_scenario"):
        code += "\n" + function(main, name)
    code += "\n" + function(read("source/networking/network_game_manager.c"), "network_game_reset_for_next_round")
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("next_game_lobby", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("next_game_lobby", generated(fault)), case)
    assert status == CHECK_FAILED, f"{control} was not caught: {output}"
