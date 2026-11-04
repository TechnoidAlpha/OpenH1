"""Internet play's relays (port/linux/src/p2p_relay.c): the relay pairs two machines only by the token of a session
both hold, a token for each relay, and only after a cookie shows each bind came from its address (return
routability); it tells the host's side alone where the joiner is (the address a host bans a relayed player by),
forwards only tunnel packets between the pair's two addresses, within its limits and buckets (which keep the
thousandths of a short refill), and survives anything sent to it; the machines' attempts are timed from their last
demotion; offers are tagged and taken a few at most, a second apart; a peer's path fails over from a silent relay
to another, and goes direct only once the direct path has lasted."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = [
    "pairing", "return-routability", "token-per-relay", "verified-address", "offers", "path", "demoted", "buckets",
    "game-mode", "limits", "malformed", "front-path",
]

# a fault in p2p_relay.c and the case that must catch it
NEGATIVE_CONTROLS = {
    # a bind taken without its address's cookie (spoofed binds)
    "no-cookie-check": (("\tif (!cookie_good(address, port, token, cookie, now))\n",
                         "\tif (0 && !cookie_good(address, port, token, cookie, now))\n"), "return-routability"),
    # a cookie good from any address
    "cookie-of-any-address": (("\tmemcpy(data + 4, &address, 4);\n\tmemcpy(data + 8, &port, 2);\n",
                               "\tmemset(data + 4, 0, 6);\n"), "return-routability"),
    # one token for every relay (#309's)
    "one-token-for-all": (("p2p_hmac_sha256(secret, P2P_SHA256_SIZE, data, (int)sizeof(data), token);",
                           "p2p_hmac_sha256(secret, P2P_SHA256_SIZE, data, (int)sizeof(label) - 1, token);"),
                          "token-per-relay"),
    # a release from any address with the token
    "release-from-anyone": (("\tif (side->bound && side->address == address && side->port == port)\n"
                             "\t\tpair_free(pair, \"released\");",
                             "\tif (side->bound)\n\t\tpair_free(pair, \"released\");"), "token-per-relay"),
    # the joiner told its host's address
    "joiner-told-host": (("\t\tside_index == 0 ? other : NULL);", "\t\tother);"), "verified-address"),
    # the joiner believes an address its relay tells
    "joiner-believes-relay": (("\t\tif (attempt->side == 0)\n\t\t{\n\t\t\tmemcpy(&attempt->peer_address.address",
                               "\t\tif (1)\n\t\t{\n\t\t\tmemcpy(&attempt->peer_address.address"),
                              "verified-address"),
    # offers from a peer taken without end
    "offers-unlimited": (("\tif (heard->count >= OFFERS_PER_SESSION || (heard->count && "
                          "!relay_elapsed(now, heard->time, OFFER_INTERVAL)))",
                          "\tif (0)"), "offers"),
    # an offer's tag not checked (anyone with the invite could offer)
    "offer-untagged": (("\tif (!p2p_equal(tag, body + size - P2P_RELAY_OFFER_TAG_SIZE, P2P_RELAY_OFFER_TAG_SIZE))\n"
                        "\t\treturn -1;\n", ""), "offers"),
    # a direct packet takes over at once (#309's)
    "direct-at-once": (("\t\tif (!silent && !relay_elapsed(now, path->direct_first, DIRECT_SWITCH_TIME))\n",
                        "\t\tif (0)\n"), "path"),
    # another relay takes over from a live carrier
    "relay-over-live-path": (("\tif (!silent)\n\t\treturn 0;\n\tpath->carrier = via;",
                              "\tpath->carrier = via;"), "path"),
    # a demoted attempt timed from its creation (#309's)
    "demoted-from-creation": (("\tattempt->state = state;\n\tattempt->state_time = now;\n",
                               "\tattempt->state = state;\n"), "demoted"),
    # a demotion not told (no failover)
    "demotion-untold": (("\tattempt->state_time = now;\n\tif (was_paired)\n\t\tp2p_relay_lost(",
                         "\tattempt->state_time = now;\n\tif (0)\n\t\tp2p_relay_lost("), "demoted"),
    # a refill rounded down to whole units (#309's)
    "bucket-rounding": (("\tbucket->left += (long long)rate * (long long)gone;",
                         "\tbucket->left += (long long)rate * (long long)gone / 1000 * 1000;"), "buckets"),
    # a dedicated server's game relays strangers
    "strangers-relayed": (("\tif (relay.limits.mode == _p2p_relay_game && !p2p_relay_peer_address(address, port))\n"
                           "\t{", "\tif (0)\n\t{"), "game-mode"),
    # no limit of relays for one peer
    "peer-attempts-unlimited": (("if (!free_attempt || peer_attempts >= P2P_RELAY_PEER_RELAYS)\n\t\treturn -1;\n"
                                 "\tmemset(free_attempt, 0, sizeof(*free_attempt));\n\tfree_attempt->used = 1;\n"
                                 "\tmemcpy",
                                 "if (!free_attempt || peer_attempts >= MAXIMUM_ATTEMPTS)\n\t\treturn -1;\n"
                                 "\tmemset(free_attempt, 0, sizeof(*free_attempt));\n\tfree_attempt->used = 1;\n"
                                 "\tmemcpy"), "limits"),
    # a front's path released at the front as a relay's pair (a RELEASE to it)
    "front-path-released": (("\t\tif (!attempt->front && (attempt->state == _attempt_waiting || "
                             "attempt->state == _attempt_paired))",
                             "\t\tif (attempt->state == _attempt_waiting || attempt->state == _attempt_paired)"),
                            "front-path"),
    # one client's path takes another peer's packets
    "front-path-any-peer": (("\t\t\tif (memcmp(attempt->peer, peer, P2P_IDENTIFIER_SIZE) || "
                             "attempt->relay.address != front->address ||",
                             "\t\t\tif (attempt->relay.address != front->address ||"), "front-path"),
    # a front's path answered by a relay's BOUND (its zero nonce)
    "front-path-answered": (("\t\tif (attempt->used && !attempt->front && attempt->relay.address == address && "
                             "attempt->relay.port == port &&",
                             "\t\tif (attempt->used && attempt->relay.address == address && "
                             "attempt->relay.port == port &&"), "front-path"),
    # a front's paths for one peer unlimited
    "front-paths-unlimited": (("\tif (!free_attempt || peer_attempts >= P2P_RELAY_PEER_RELAYS)\n\t\treturn -1;\n"
                               "\tmemset(free_attempt, 0, sizeof(*free_attempt));\n\tfree_attempt->used = 1;\n"
                               "\tfree_attempt->front = 1;",
                               "\tif (!free_attempt || peer_attempts >= MAXIMUM_ATTEMPTS)\n\t\treturn -1;\n"
                               "\tmemset(free_attempt, 0, sizeof(*free_attempt));\n\tfree_attempt->used = 1;\n"
                               "\tfree_attempt->front = 1;"), "front-path"),
}

STUB_HEADERS = (
    ("platform.h", "/* (the test's) */\n"),
    ("posix.h", "void posix_random_bytes(void *buffer, unsigned long size);\n"),
)


def crypto():
    """p2p_crypto.c's SHA-256, HMAC and constant-time comparison, as they are"""
    source = read("port/linux/src/p2p_crypto.c")
    start = source.index("static const unsigned int sha256_constants")
    hmac = function(source, "p2p_hmac_sha256")
    end = source.index(hmac) + len(hmac)
    return source[start:end] + "\n" + function(source, "p2p_equal") + "\n"


def generated(fault=None):
    relay = read("port/linux/src/p2p_relay.c")
    if fault:
        relay = mutated(relay, *fault)
    files = tuple((name, read(f"port/linux/src/{name}")) for name in ("p2p.h", "p2p_internal.h"))
    return files + STUB_HEADERS + (("under_test.inc", crypto() + relay),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("p2p_relay", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("p2p_relay", generated(fault)), case)
    assert status == CHECK_FAILED, f"p2p_relay.c with a fault ({control}) passed '{case}': the test cannot see it"
