"""The desktop's links on Linux: the game registers openh1:// alone, and drops a registration of halo:// (OpenCE's
scheme, which older builds registered) only where it runs this executable (port/linux/src/p2p.c's
register_link_schemes, posix_net.c's posix_register_url_scheme and posix_forget_url_scheme: url_schemes.c). Windows'
registry has the same two (port/windows/src/win32_p2p.c); the Mac's and iOS's apps declare theirs
(tools/test_macos_app.py, tools/test_ios_app.py), as Android's manifest does (test_android_links)."""

import re

import pytest

from tools.harness import ROOT, build, function, mutated, read, run

CASES = ["schemes", "register", "forget"]


def generated(fault=None):
    posix = read("port/linux/src/posix_net.c")
    code = f'#include "{ROOT}/port/linux/src/p2p.h"\n\n'
    code += "\n\n".join(function(posix, name) for name in (
        "desktop_entries", "posix_register_url_scheme", "posix_forget_url_scheme"))
    code += "\n\n" + function(read("port/linux/src/p2p.c"), "register_link_schemes")
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("url_schemes", generated()), case)
    assert status == 0, output


# a fault, and the case that must catch it
FAULTS = {
    "registers-halo": (('p2p_register_url_scheme(P2P_LINK_SCHEME, "GOAT H1 invite");',
                        'p2p_register_url_scheme(P2P_OLD_LINK_SCHEME, "GOAT H1 invite");'), "schemes"),
    "halo-kept": (("\tp2p_forget_url_scheme(P2P_OLD_LINK_SCHEME);\n", ""), "schemes"),
    "opence-dropped": (("\tif (!strstr(existing, line))\n\t\treturn 0;\n", ""), "forget"),
    "path-prefix-taken": (('"\\nExec=\\"%s\\" %%u\\n"', '"\\nExec=\\"%s"'), "forget"),
    "no-default-set": (("\t\trun_program(arguments);\n", ""), "register"),
}


@pytest.mark.parametrize("fault", sorted(FAULTS))
def test_negative_control(fault):
    change, case = FAULTS[fault]
    status, output = run(build("url_schemes", generated(change)), case)
    assert status == 1, f"{fault} was not caught by {case}: {output}"


def test_android_links():
    """the Android app's manifest takes openh1:// links (invites, parties'), and not halo:// ones, OpenCE's"""
    manifest = (ROOT / "port/android/app/src/main/AndroidManifest.xml").read_text()
    schemes = re.findall(r'android:scheme="([^"]*)"', manifest)
    assert schemes and set(schemes) == {"openh1"}
    assert set(re.findall(r'android:scheme="openh1" android:host="(\w+)"', manifest)) == {"join", "party"}
