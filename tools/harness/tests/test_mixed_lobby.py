"""A mixed game's host (network_standard.c): an OpenH1 game OpenCE players join too. It advertises the Legacy game's
version with its rate and OpenH1 protocol beside it, takes both tokens and knows each machine's kind by its own,
sends an OpenCE machine a Legacy host's record and none of OpenH1's messages, and steps an OpenCE machine's players
at 30. A Legacy host and an OpenH1-only host stay as they were."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["legacy-host", "openh1-host", "mixed-host", "record", "messages", "players", "players-unmixed",
         "legacy-heard", "dedicated-server"]

# a fault in network_standard.c, and the case that must catch it
NEGATIVE_CONTROLS = {
    "mixed-advertised-as-openh1": (("network_standard_globals.hosting && !network_standard_globals.mixed ?",
                                    "network_standard_globals.hosting ?"), "mixed-host"),
    "legacy-host-writes-protocol": (("\tif (network_standard_hosting_mixed())\n\t\treserved[", "\tif (1)\n\t\treserved["),
                                    "legacy-host"),
    "openh1-only-takes-opence": (("if (network_standard_globals.mixed && !csmemcmp(request_token, game_token",
                                  "if (!csmemcmp(request_token, game_token"), "openh1-host"),
    "mixed-refuses-opence": (("\t\treturn _network_standard_join_opence;", "\t\treturn NONE;"), "mixed-host"),
    "record-keeps-rate": (("legacy_game.standard_rate = 0;", ";"), "record"),
    "record-keeps-pickup": (("\tSET_FLAG(legacy_game.cooperative_flags, _network_game_hold_to_pick_up_bit, FALSE);\n",
                             ""), "record"),
    "openh1-messages-to-opence": (("return type < FIRST_OPENH1_DISTRIBUTED_MESSAGE || "
                                   "!network_standard_machine_is_opence(machine_index);", "return TRUE;"), "messages"),
    "opence-players-unknown": (("network_standard_globals.opence_players[absolute_index] = TRUE;", ";"), "players"),
    "players-told-to-opence": (("if (network_standard_machine_is_openh1(machine_indices[index]))", "if (1)"),
                               "players"),
    "players-never-told-again": (("game_time_get() - network_standard_globals.opence_players_sent_time < "
                                  "OPENCE_PLAYERS_INTERVAL_TICKS", "1"), "players"),
    "client-takes-them-mid-tick": (("network_standard_globals.opence_players_heard[player_index] =",
                                    "network_standard_globals.opence_players[player_index] ="), "players"),
    "unmixed-players-at-30": (("\t\tif (!network_standard_hosting_mixed())\n\t\t\treturn FALSE;\n\t\tbreak;",
                               "\t\tbreak;"), "players-unmixed"),
    "server-reads-network-game-mode": (("halo_server_mode() ? network_standard_server_setting() != 0 :", ""),
                                       "dedicated-server"),
    "server-legacy-setting-changes-rate": (("return rate ? rate : network_standard_globals.hosting_rate;",
                                            "return rate ? rate : SIM_RATE_CLASSIC;"), "dedicated-server"),
    "legacy-record-keeps-old-rate": (("network_standard_globals.hosting ? (short)network_standard_server_rate(in_lobby) : 0",
                                      "network_standard_globals.hosting ? (short)network_standard_server_rate(in_lobby) : "
                                      "game->standard_rate"), "dedicated-server"),
}


def generated(fault=None):
    limits = read("port/linux/include/halo_port_limits.h")
    distributed = read("port/linux/game/network_distributed.h")
    config = "".join(f"#define {name} {constant(limits, name)}\n" for name in (
        "HALO_PORT_NETWORK_VERSION", "HALO_PORT_ADVERTISED_VERSION_OFFSET", "HALO_PORT_ADVERTISED_RATE_OFFSET",
        "HALO_PORT_ADVERTISED_OPENH1_VERSION_OFFSET", "HALO_PORT_STANDARD_NETWORK_VERSION",
        "HALO_PORT_STANDARD_VERSION_BIT", "HALO_PORT_MAXIMUM_NETWORK_MACHINES", "HALO_PORT_MAXIMUM_NETWORK_PLAYERS"))
    config += "#define HALO_PORT_STANDARD_ADVERTISED_VERSION " \
              "(HALO_PORT_STANDARD_VERSION_BIT | HALO_PORT_STANDARD_NETWORK_VERSION)\n"
    for name in ("_distributed_message_player_substeps", "_distributed_message_opence_players"):
        value = re.search(rf"\b{name} = (\d+)", distributed).group(1)
        config += f"#define {name} {value}\n"
    first = re.search(r"#define FIRST_OPENH1_DISTRIBUTED_MESSAGE (\w+)", distributed).group(1)
    config += f"#define FIRST_OPENH1_DISTRIBUTED_MESSAGE {first}\n"
    header = read("port/linux/game/network_standard.h")
    config += enum_with(header, "_network_standard_join_legacy") + "\n"
    # (the prototypes, which the code relies on: its functions call each other in any order)
    prototypes = header[header.index("/* ---------- prototypes */"):header.rindex("#endif")]
    source = read("port/linux/game/network_standard.c")
    # (the whole file from its constants on: its globals and functions)
    text = source[source.index("/* ---------- constants */"):]
    if fault:
        text = mutated(text, *fault)
    return (("config.inc", config), ("under_test.inc", prototypes + text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("mixed_lobby", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("mixed_lobby", generated(fault)), case)
    assert status == CHECK_FAILED, f"network_standard.c with a fault ({control}) passed '{case}': the test cannot see it"


def test_the_game_calls_it():
    """The host's join takes the token by network_standard_join_kind and records the machine's kind; its record goes
    to each machine as its kind has it; the distributed netcode's datagrams and reliable messages pass the guard; the
    lanes and the weapons' fast phase step an OpenCE player on the tick; a client joins a mixed game as OpenH1 only
    of its own protocol (debug.join_legacy never)."""
    handler = read("source/networking/network_server_message_handler.c")
    assert "join_kind = network_standard_join_kind(join_game_request.join_game_token, join_game_token," in handler
    assert "network_standard_machine_joined(machine_index, join_kind);" in handler
    assert "network_standard_advertise(advertisement.reserved, network_game_server_in_lobby(server));" in handler
    to_one = function(handler, "network_game_server_send_game_settings_to_client_machine")
    assert "network_standard_game_for_machine(" in to_one
    to_all = function(handler, "network_game_server_send_game_settings_to_all_machines")
    assert "network_game_server_send_message_to_machines_of_kind(server, encoded_message, kind)" in to_all
    of_kind = function(handler, "network_game_server_send_message_to_machines_of_kind")
    assert "network_standard_machine_is_opence(game_machine_index) != (opence == TRUE)" in of_kind
    distributed = read("port/linux/game/network_distributed.c")
    assert "!network_standard_message_goes_to(sender, header.type)" in function(distributed, "distributed_batch_add")
    assert "!network_standard_message_goes_to(machine_index, type)" in \
        function(distributed, "distributed_send_to_machine_reliably")
    assert "network_standard_host_tick();" in function(distributed, "network_distributed_tick")
    assert "network_standard_new_game();" in distributed
    vehicles = function(read("source/units/vehicles.c"), "vehicle_takes_substeps")
    assert "network_standard_player_is_opence(unit_get(driver_index)->unit.player_index)" in vehicles
    weapons = function(read("source/items/weapons.c"), "weapons_substep")
    assert "object_lane_moves_in_substeps(player->unit_index)" in weapons
    client = read("source/networking/network_client_manager.c")
    joins = function(client, "network_game_client_advertised_joins_as_openh1")
    assert '!config_boolean("debug.join_legacy")' in joins
    assert "== HALO_PORT_STANDARD_NETWORK_VERSION" in joins
