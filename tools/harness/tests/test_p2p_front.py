"""Internet play's fronts (port/linux/src/p2p_front.c): a server registers only once a cookie showed its address,
and only with an operator's key the front allows and its signature; the front passes a client's tunnel packets
to the server with its number and address, and the server's answers to that client alone, from the server's own
address and session; a registration keeps a few clients per address, those never answered giving way first, and
forgets them as they fall silent; a registration that lapses keeps its port for its key; the server's side renews
and gives up its registration; the front's rates hold; and nothing a front sends to an address it has not checked
is larger than what that address sent it."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = [
    "registration", "return-routability", "unknown-key", "forwarding", "clients", "expiry", "limits",
    "no-amplification", "malformed", "operators-file",
]

# a fault in p2p_front.c and the case that must catch it
NEGATIVE_CONTROLS = {
    # a REGISTER taken without its address's cookie (forged sources)
    "no-cookie-check": (("\tif (!cookie_good(address, port, key, cookie, now))\n",
                         "\tif (0 && !cookie_good(address, port, key, cookie, now))\n"), "return-routability"),
    # a cookie good from any address
    "cookie-of-any-address": (("\tmemcpy(data + 4, &address, 4);\n\tmemcpy(data + 8, &port, 2);\n",
                               "\tmemset(data + 4, 0, 6);\n"), "return-routability"),
    # any key registers
    "any-operator": (("\t\tif (!operator_allowed(key))\n", "\t\tif (0)\n"), "unknown-key"),
    # the signature not checked
    "signature-unchecked": (("\t\tif (!p2p_ed25519_verify(key, message, message_size, signature))\n",
                             "\t\tif (!message_size && !p2p_ed25519_verify(key, message, message_size, signature))\n"),
                            "unknown-key"),
    # a TO taken from any address with the session
    "to-from-anyone": (("\t\tif (registration->used && registration->address == address && "
                        "registration->port == port &&\n\t\t\tp2p_equal(registration->session, packet + 2, "
                        "SESSION_SIZE))",
                        "\t\tif (registration->used)"), "forwarding"),
    # a FROM taken from any address
    "from-from-anyone": (("\t\taddress != front.front.address || port != front.front.port ||\n"
                          "\t\t!p2p_equal(packet + 2, front.session, SESSION_SIZE))",
                          "\t\t0)"), "forwarding"),
    # anything passed on from the public port (a reflector)
    "public-port-any-packet": (("\tif (!registration->used || size < P2P_TUNNEL_MINIMUM_PACKET || "
                                "size > P2P_TUNNEL_MAXIMUM_PACKET ||\n\t\tpacket[0] != P2P_TUNNEL_MAGIC || "
                                "!address || !port)",
                                "\tif (!registration->used || size < 1 || size > P2P_TUNNEL_MAXIMUM_PACKET)"),
                               "forwarding"),
    # no limit of clients from one address
    "clients-per-address-unlimited": (("\tif (from_address >= CLIENTS_PER_ADDRESS)\n",
                                       "\tif (from_address >= CLIENTS)\n"), "clients"),
    # those never answered do not give way (a forged flood fills the table)
    "unanswered-kept": (("\tif (!free_client)\n\t\tfree_client = unanswered;\n", ""), "clients"),
    # a client's bytes unlimited
    "client-unlimited": (("\tif (!bucket_spend(&client->bytes, CLIENT_UPLOAD, size, now) ||\n",
                          "\tif (0 ||\n"), "limits"),
    # the challenge as large as a REGISTER (an amplifier at the cookie's step)
    "challenge-large": (("\t\tp2p_front_sendto(-1, address, port, challenge, CHALLENGE_SIZE);",
                         "\t\t{ unsigned char big[REGISTER_SIZE + 64] = { 0 }; memcpy(big, challenge, "
                         "CHALLENGE_SIZE); p2p_front_sendto(-1, address, port, big, sizeof(big)); }"),
                        "no-amplification"),
    # a lapsed registration's port not kept for its key
    "port-not-kept": (("\t\tif (registration->has_key && p2p_equal(registration->key, key, P2P_FRONT_KEY_SIZE))\n"
                       "\t\t\treturn registration;\n", ""), "expiry"),
    # the server never gives up a silent front
    "server-never-lapses": (("\tif (front.state == _session_registered && front_elapsed(now, front.heard_time, "
                             "REGISTERED_SILENCE))", "\tif (0)"), "expiry"),
}

STUB_HEADERS = (
    ("platform.h", "/* (the test's) */\n"),
    ("posix.h", "void posix_random_bytes(void *buffer, unsigned long size);\n"),
    ("log_address.h", "enum { LOG_ADDRESS_SIZE = 56 };\n"
                      "const char *log_address_ipv4(unsigned long address, unsigned short port, char *text, int size);\n"),
)


def crypto():
    """p2p_crypto.c's SHA-256, HMAC and constant-time comparison, as they are"""
    source = read("port/linux/src/p2p_crypto.c")
    start = source.index("static const unsigned int sha256_constants")
    hmac = function(source, "p2p_hmac_sha256")
    end = source.index(hmac) + len(hmac)
    return source[start:end] + "\n" + function(source, "p2p_equal") + "\n"


def generated(fault=None):
    front = read("port/linux/src/p2p_front.c")
    if fault:
        front = mutated(front, *fault)
    files = tuple((name, read(f"port/linux/src/{name}")) for name in ("p2p.h", "p2p_internal.h"))
    return files + STUB_HEADERS + (("under_test.inc", crypto() + front),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("p2p_front", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("p2p_front", generated(fault)), case)
    assert status == CHECK_FAILED, f"p2p_front.c with a fault ({control}) passed '{case}': the test cannot see it"
