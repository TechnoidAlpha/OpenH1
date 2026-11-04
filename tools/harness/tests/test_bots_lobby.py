"""The real bots of a hosted game (network_server_manager.c's): as many as bots.count asks for join a game's lobby (any
gametype but Race), local or network, as far as it has room, on the teams bots.teams asks for, follow the players' team
changes and leave when fewer are asked for; none in Race, nor before a player is in. A network game's bots make room
for its players (bots.make_room, bots.fill_to): a player who joins a full game takes a bot's place at once, in the lobby
and in the game in progress (the bot of the team with more players, the lowest scoring), and a bot comes back a while
after a player leaves; with make room off the game takes its most players of players besides its bots. The listing
counts the players apart from the bots, and the other machines are told only by the game's own messages."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["fills", "room", "fewer", "even", "against", "follows-players", "network-lobby", "not-race",
         "not-once-started", "no-player-no-bots", "names-and-machines", "make-room-lobby", "fill-to", "fixed-bots",
         "listing-counts", "make-room-ingame", "leaver", "host-removes-a-bot", "party-held"]
GAME_MANAGER_FUNCTIONS = ("network_game_invalidate_player", "network_player_is_valid", "network_game_player_is_valid",
                          "network_game_add_player", "network_game_remove_player", "network_game_has_free_player_slot")
BOTS_FUNCTIONS = ("bots_machine_is_bot", "bots_network_player", "bots_play_game_engine")
# network_server_manager.c's, in order
SERVER_FUNCTIONS = ("network_game_server_port_bots_census", "network_game_server_port_bots_human_maximum",
                    "network_game_server_port_bots_room", "network_game_server_port_bots_wanted",
                    "network_game_server_port_bots_team", "network_game_server_port_bots_leaver",
                    "network_game_server_port_bots_leave", "network_game_server_port_bots",
                    "network_game_server_port_bots_ingame", "network_game_server_port_bots_make_room",
                    "network_game_server_port_bots_remove",
                    "network_game_server_port_bots_humans", "network_game_server_has_room_for_player",
                    "network_game_server_add_player_to_game")

# a fault in a function, and the case that must catch it
NEGATIVE_CONTROLS = {
    "bots-in-any-gametype": ("bots_play_game_engine", ("return game_engine_index == game_engine_slayer ||",
                                                       "return 1 ||"), "not-race"),
    "bots-local-only": ("network_game_server_port_bots_wanted", ("|| !census->humans)",
                        "|| !census->humans || !network_game_is_splitscreen_local())"), "network-lobby"),
    "bots-once-started": ("network_game_server_port_bots",
                          ("server->state != _network_game_server_state_pregame || server->sent_start_game_message",
                           "server->state != _network_game_server_state_pregame"), "not-once-started"),
    "teams-not-evened": ("network_game_server_port_bots_team", ("team_counts[1] < team_counts[0] ? 1 : 0", "1"),
                         "even"),
    "against-the-players": ("network_game_server_port_bots_team", ("human_team == 0 ? 1 : 0", "human_team"),
                            "against"),
    "one-too-many-stays": ("network_game_server_port_bots", ("census.bots > wanted;", "census.bots > wanted + 1;"),
                           "fewer"),
    "removed-bot-stays": ("network_game_server_port_bots_remove",
                          ("if (slot == NONE || !network_game_server_port_bots_leave(server, slot))",
                           "if (slot == NONE)"), "host-removes-a-bot"),
    "removed-bot-replaced": ("network_game_server_port_bots",
                             ("missing = wanted - census.bots;", "missing = BOTS_MAXIMUM;"), "host-removes-a-bot"),
    "bots-alone": ("network_game_server_port_bots_wanted", (" || !census->humans)", ")"),
                   "no-player-no-bots"),
    "teams-not-followed": ("network_game_server_port_bots",
                           ("else if (server->game.players[census.slot_of_bot[bot_index]].team_index != team_index)",
                            "else if (0)"), "follows-players"),
    "no-room-made": ("network_game_server_add_player_to_game",
                     ("network_game_server_port_bots_make_room(server);", ";"), "make-room-lobby"),
    "fill-ignored": ("network_game_server_port_bots_wanted",
                     ("wanted = MIN(wanted, MAX(fill - census->humans - network_party_held_slots(), 0));", ";"),
                     "fill-to"),
    "bots-take-held-slots": ("network_game_server_port_bots_wanted",
                             ("fill - census->humans - network_party_held_slots()", "fill - census->humans"),
                             "party-held"),
    "no-fill-in-local-games": ("network_game_server_port_bots_wanted",
                               ("if (bots_make_room())", "if (bots_make_room() && !network_game_is_splitscreen_local())"),
                               "fill-to"),
    "fixed-bots-take-players-room": ("network_game_server_port_bots_room",
                                     ("bots_make_room() ? 0 : bots;", "bots_make_room() ? 0 : 0;"), "fixed-bots"),
    "listing-counts-bots": ("network_game_server_port_bots_humans", ("return census.humans;",
                                                                    "return census.humans + census.bots;"),
                            "listing-counts"),
    "removal-untold": ("network_game_server_port_bots_leave",
                       ("if (!message || !network_game_server_send_message_to_all_machines(server, message))",
                        "if (!message)"), "make-room-ingame"),
    "comes-back-at-once": ("network_game_server_port_bots_ingame",
                           (" ||\n\t\tsystem_milliseconds() - network_game_server_bots.changed_time < "
                            "BOTS_RETURN_MILLISECONDS)", ")"), "make-room-ingame"),
    "comes-back-as-a-player-leaves": ("network_game_server_port_bots_ingame",
                                      ("\t\tnetwork_game_server_bots.changed_time = system_milliseconds();\n"
                                       "\tnetwork_game_server_bots.humans",
                                       "\t\t;\n\tnetwork_game_server_bots.humans"), "make-room-ingame"),
    "comes-back-over-a-joiner": ("network_game_server_port_bots_ingame",
                                 (" || server->waiting_player_count > 0", ""), "make-room-ingame"),
    "highest-scoring-leaves": ("network_game_server_port_bots_leaver", ("score < best_score", "score > best_score"),
                               "leaver"),
    "smaller-team-leaves": ("network_game_server_port_bots_leaver",
                            ("team_size > best_team_size ||", "team_size < best_team_size ||"), "leaver"),
}


def generated(fault=None):
    server = read("source/networking/network_server_manager.c")
    manager = read("source/networking/network_game_manager.c")
    bots_header = read("port/linux/game/bots.h")
    bots = read("port/linux/game/bots.c")
    limits = read("port/linux/include/halo_port_limits.h")
    config = ""
    for name in ("HALO_PORT_MAXIMUM_NETWORK_MACHINES", "HALO_PORT_MAXIMUM_NETWORK_PLAYERS"):
        config += f"#define {name} {constant(limits, name)}\n"
    config += f"#define MAXIMUM_LOCAL_PLAYERS {constant(read('source/game/players.h'), 'MAXIMUM_LOCAL_PLAYERS')}\n"
    config += f"#define NUMBER_OF_MULTIPLAYER_TEAMS {constant(server, 'NUMBER_OF_MULTIPLAYER_TEAMS')}\n"
    config += f"#define NETWORK_PLAYER_NAME_LENGTH {constant(server, 'NETWORK_PLAYER_NAME_LENGTH')}\n"
    config += f"#define NETWORK_GAME_PLAYER_QUIT_DELAY {constant(server, 'NETWORK_GAME_PLAYER_QUIT_DELAY')}\n"
    config += f"#define BOTS_MAXIMUM {constant(bots_header, 'BOTS_MAXIMUM')}\n"
    match = re.search(r"^#define BOTS_FIRST_MACHINE .*$", bots_header, re.M)
    assert match, "BOTS_FIRST_MACHINE not found in bots.h"
    config += match.group(0) + "\n"
    config += enum_with(bots_header, "_bots_teams_against") + "\n"
    config += enum_with(server, "_network_game_server_state_pregame") + "\n"
    config += enum_with(read("source/game/game_engine.h"), "game_engine_slayer") + "\n"
    config += enum_with(read("source/networking/network_messages.h"), "_message_server_remove_player_ingame") + "\n"
    config += structure(read("source/game/players.h"), "network_player") + "\n"
    config += structure(server, "message_server_remove_player_ingame") + "\n"
    code = ""
    for name in GAME_MANAGER_FUNCTIONS:
        code += function(manager, name) + "\n"
    for name in BOTS_FUNCTIONS:
        text = function(bots, name)
        if fault and fault[0] == name:
            text = mutated(text, *fault[1])
        code += text + "\n"
    # (the bots' constants, census and state, as the server has them)
    code += re.search(r"^enum\n\{\n\t/\* \(a game in progress: the time from a bot's.*?^\};\n", server,
                      re.M | re.S).group(0) + "\n"
    code += structure(server, "network_game_server_bots_census") + "\n"
    code += re.search(r"^static struct\n\{\n\tstruct network_game_server \*server;.*?^\} network_game_server_bots = "
                      r"[^;]*;\n", server, re.M | re.S).group(0) + "\n"
    for name in SERVER_FUNCTIONS:
        text = function(server, name)
        if fault and fault[0] == name:
            text = mutated(text, *fault[1])
        code += text + "\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("bots_lobby", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    name, fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("bots_lobby", generated((name, fault))), case)
    assert status == CHECK_FAILED, f"the bots with a fault ({control}) passed '{case}': the test cannot see it"
