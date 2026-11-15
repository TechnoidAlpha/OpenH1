"""The latency meter's real arithmetic (display.latency_meter): a round trip's colour and bars (latency_meter.c), and
from network_distributed.c the round trip timed by when its tick was sent, its smoothing, and the two seconds of
silence that make a connection problem."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run  # noqa: E402
from harness import skip_without_32_bit_abi  # noqa: E402

CASES = ["colors", "since-tick", "since-tick-bounds", "smoothing", "connection-problem", "machine-silent"]
METER = ("latency_meter_color",)
NETCODE = ("distributed_milliseconds_since_tick", "distributed_smooth_ping", "distributed_host_silence",
           "distributed_connection_problem", "distributed_machine_silent")

# a fault in the code, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "green-at-80": (("milliseconds < LATENCY_GOOD_MILLISECONDS", "milliseconds <= LATENCY_GOOD_MILLISECONDS"),
                    "latency_meter_color", "colors"),
    "yellow-to-250": (("milliseconds < LATENCY_FAIR_MILLISECONDS", "milliseconds < LATENCY_POOR_MILLISECONDS"),
                      "latency_meter_color", "colors"),
    "stale-tick-timed": (("|| distributed_tick_sent_times[time & (TICK_SENT_TIMES - 1)].time != time", ""),
                         "distributed_milliseconds_since_tick", "since-tick"),
    "no-ceiling": (("elapsed > (unsigned long)MAXIMUM_PING_MILLISECONDS ? MAXIMUM_PING_MILLISECONDS : ", ""),
                   "distributed_milliseconds_since_tick", "since-tick-bounds"),
    "quarter-smoothing": (("- *milliseconds) / 8.0f", "- *milliseconds) / 4.0f"), "distributed_smooth_ping",
                          "smoothing"),
    "problem-at-two-seconds": (("silence > CONNECTION_PROBLEM_MILLISECONDS", "silence >= CONNECTION_PROBLEM_MILLISECONDS"),
                               "distributed_connection_problem", "connection-problem"),
    "problem-after-the-game": (("== _game_connection_network_client && game_engine_in_play()",
                                "== _game_connection_network_client"), "distributed_connection_problem",
                               "connection-problem"),
    "silent-never-heard": (("distributed_round_trips[machine_index].heard &&\n", "\n"), "distributed_machine_silent",
                           "machine-silent"),
}


def generated(fault=None, faulty_function=None):
    meter = read("port/linux/game/latency_meter.c")
    netcode = read("port/linux/game/network_distributed.c")
    config = ""
    for name in ("LATENCY_GOOD_MILLISECONDS", "LATENCY_FAIR_MILLISECONDS", "LATENCY_POOR_MILLISECONDS"):
        config += f"#define {name} {constant(meter, name)}\n"
    for name in ("TICK_SENT_TIMES", "MAXIMUM_PING_MILLISECONDS", "CONNECTION_PROBLEM_MILLISECONDS"):
        config += f"#define {name} {constant(netcode, name)}\n"
    config += "#define HALO_PORT_MAXIMUM_NETWORK_MACHINES %d\n" % constant(
        read("port/linux/include/halo_port_limits.h"), "HALO_PORT_MAXIMUM_NETWORK_MACHINES")
    text = ""
    for source, names in ((meter, METER), (netcode, NETCODE)):
        for name in names:
            code = function(source, name)
            if fault and name == faulty_function:
                code = mutated(code, *fault)
            text += code + "\n"
    return (("config.inc", config), ("under_test.inc", text))


# what only the game's 32-bit ABI shows (harness.NATIVE_64_BIT)
ABI_DEPENDENT = {
    "since-tick-bounds": "the millisecond clock is the game's 32-bit unsigned long, which wraps",
}


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    skip_without_32_bit_abi(ABI_DEPENDENT, case)
    status, output = run(build("latency_meter", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    skip_without_32_bit_abi(ABI_DEPENDENT, control, case)
    status, output = run(build("latency_meter", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
