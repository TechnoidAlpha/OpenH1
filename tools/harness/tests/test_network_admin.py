"""A dedicated server's admins in the game (port/linux/game/network_admin.c): whether a proven key is an admin's
(admins.txt's and server.admins' keys and key ids), the server's command each request of an admin's runs, the
requests refused (another player in the slot, out of range, a text of more than one word where one is taken), and
how many requests a machine may make a second. The key's proof itself is test_player_link.py's admin-proof."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["listed", "request-lines", "requests-refused", "rate"]

FUNCTIONS = ("admin_hex_digit", "admin_lower", "admin_word_character", "network_admin_listed", "admin_text_clean",
             "network_admin_request_line", "network_admin_rate_take")

# a fault in network_admin.c, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "any-key-prefix": (("match = length == 64 ? key_hex : length == 16 ? key_id : NULL;",
                        "match = length >= 16 ? key_id : NULL;"), "network_admin_listed", "listed"),
    "comments-read": (("if (*cursor == '#')", "if (0)"), "network_admin_listed", "listed"),
    "slot-not-checked": (("if (strcmp(name, slot_name))", "if (0)"), "network_admin_request_line",
                         "requests-refused"),
    "entries-of-words": (("if (word && character == ' ')", "if (0)"), "admin_text_clean", "requests-refused"),
    "ban-no-limit": (("|| request->number > NETWORK_ADMIN_BAN_MOST_SECONDS", ""), "network_admin_request_line",
                     "requests-refused"),
    "no-rate": (("if (rate->tokens <= 0)\n\t\treturn FALSE;", ""), "network_admin_rate_take", "rate"),
    "burst-unbounded": (("rate->tokens += (long)earned;", "rate->tokens += (long)earned + 1;"),
                        "network_admin_rate_take", "rate"),
}


def generated(fault=None, faulty_function=None):
    source = read("port/linux/game/network_admin.c")
    text = ""
    for name in FUNCTIONS:
        code = function(source, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("network_admin.h", read("port/linux/game/network_admin.h")), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("network_admin", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("network_admin", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"network_admin.c with a fault ({control}) passed '{case}': the test cannot see it"
