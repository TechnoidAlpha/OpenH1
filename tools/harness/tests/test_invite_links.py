"""COPY INVITE's message (port/linux/src/p2p.c's p2p_invite_message): a line with the join page's https address of
a party's or a game's link, which chat apps make clickable, and a line with the openh1:// link; and every form of
either link the game takes (p2p.c's parse_invite, p2p_party.c's p2p_party_parse_link, sdl_platform.c's look at the
clipboard), old and new, older builds' halo:// links among them (invite_links.c)."""

import pytest

from tools.harness import ROOT, build, function, mutated, read, run

CASES = ["party", "game", "forms", "none"]


def generated(fault=None):
    p2p = read("port/linux/src/p2p.c")
    code = f'#include "{ROOT}/port/linux/src/p2p_internal.h"\n\n'
    code += "\n\n".join(function(p2p, name) for name in ("p2p_hex", "hex_value", "parse_invite", "p2p_invite_message"))
    code += "\n\n" + function(read("port/linux/src/p2p_party.c"), "p2p_party_parse_link")
    code += "\n\n" + function(read("port/linux/src/sdl_platform.c"), "platform_text_has_invite_link")
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("invite_links", generated()), case)
    assert status == 0, output


# a fault, and the case that must catch it
FAULTS = {
    "page-hash-not-taken": ((', "&party=", "#party=" };', ', "&party=" };'), "forms"),
    "page-join-not-taken": ((', "&join=", "#join=" };', ', "&join=" };'), "forms"),
    "address-sent-to-server": (('"Join my GOAT H1 party: %s#party=%s\\n"', '"Join my GOAT H1 party: %s?party=%s\\n"'),
                               "party"),
    "no-link-to-paste": (("\"Or paste this into GOAT H1's Join by Link: \" P2P_PARTY_LINK_PREFIX \"%s\", "
                          "P2P_JOIN_PAGE, hex, hex);",
                          "\"\", P2P_JOIN_PAGE, hex);"), "party"),
    "game-as-party": (('"Join my GOAT H1 game: %s#join=%s\\n"', '"Join my GOAT H1 game: %s#party=%s\\n"'), "game"),
    "address-not-seen": ((', "openh1.com/join/#" };', " };"), "party"),
    "old-forms-dropped": (('"?join=", "&join=", "#join=" };', '"#join=" };'), "forms"),
    # (the game makes openh1:// links, takes older builds' halo:// ones, and sees either on the clipboard)
    "makes-halo-links": (("\"Or paste this into GOAT H1's Join by Link: \" P2P_JOIN_LINK_PREFIX \"%s\"",
                          "\"Or paste this into GOAT H1's Join by Link: \" P2P_OLD_JOIN_LINK_PREFIX \"%s\""), "game"),
    "makes-halo-party-links": (("\"Or paste this into GOAT H1's Join by Link: \" P2P_PARTY_LINK_PREFIX \"%s\"",
                                "\"Or paste this into GOAT H1's Join by Link: \" P2P_OLD_PARTY_LINK_PREFIX \"%s\""),
                               "party"),
    "halo-join-not-taken": (("{ P2P_JOIN_LINK_PREFIX, P2P_OLD_JOIN_LINK_PREFIX,", "{ P2P_JOIN_LINK_PREFIX,"), "forms"),
    "halo-party-not-taken": (("{ P2P_PARTY_LINK_PREFIX, P2P_OLD_PARTY_LINK_PREFIX,", "{ P2P_PARTY_LINK_PREFIX,"),
                             "forms"),
    "openh1-join-not-taken": (("{ P2P_JOIN_LINK_PREFIX, P2P_OLD_JOIN_LINK_PREFIX,", "{ P2P_OLD_JOIN_LINK_PREFIX,"),
                              "forms"),
    "openh1-party-not-taken": (("{ P2P_PARTY_LINK_PREFIX, P2P_OLD_PARTY_LINK_PREFIX,", "{ P2P_OLD_PARTY_LINK_PREFIX,"),
                               "forms"),
    "halo-not-seen": (("P2P_PARTY_LINK_PREFIX, P2P_OLD_JOIN_LINK_PREFIX,\n\t\tP2P_OLD_PARTY_LINK_PREFIX, ",
                       "P2P_PARTY_LINK_PREFIX, "), "forms"),
}


@pytest.mark.parametrize("fault", sorted(FAULTS))
def test_negative_control(fault):
    change, case = FAULTS[fault]
    status, output = run(build("invite_links", generated(change)), case)
    assert status == 1, f"{fault} was not caught by {case}: {output}"
