"""The join page (website/public/join/index.html, at p2p.h's P2P_JOIN_PAGE): the link it reads from an invite's
address (its part after "#", which browsers never send, or an older address's ?join=), run with Node when there is
one; and that it loads nothing from other sites."""

import json
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
PAGE = ROOT / "website/public/join/index.html"

PARTY = "fedcba9876543210fedcba9876543210"
GAME = "0123456789abcdef0123456789abcdef00112233445566778899aabbccddeeff"

# (the address's hash, its search) and the link it holds (None: none)
CASES = [
    (("#party=" + PARTY, ""), {"kind": "party", "link": "openh1://party/" + PARTY}),
    (("#join=" + GAME, ""), {"kind": "game", "link": "openh1://join/" + GAME}),
    (("#" + PARTY, ""), {"kind": "party", "link": "openh1://party/" + PARTY}),
    (("#" + GAME, ""), {"kind": "game", "link": "openh1://join/" + GAME}),
    (("#party=" + PARTY.upper(), ""), {"kind": "party", "link": "openh1://party/" + PARTY}),
    (("#utm=x&join=" + GAME, ""), {"kind": "game", "link": "openh1://join/" + GAME}),
    (("#party=" + PARTY + "&from=steam", ""), {"kind": "party", "link": "openh1://party/" + PARTY}),
    (("", "?join=" + GAME), {"kind": "game", "link": "openh1://join/" + GAME}),
    (("", "?x=1&party=" + PARTY), {"kind": "party", "link": "openh1://party/" + PARTY}),
    (("#party=" + GAME, ""), None),
    (("#join=" + PARTY, ""), None),
    (("#party=" + PARTY[:-1], ""), None),
    (("#join=" + GAME + "0", ""), None),
    (("#party=" + PARTY + '"><script>alert(1)</script>', ""), None),
    (("#party=" + PARTY[:-2] + "zz", ""), None),
    (("", ""), None),
    (("#", "?"), None),
]


def page_function() -> str:
    text = PAGE.read_text()
    match = re.search(r"// join-link:begin\n(.*?)// join-link:end", text, re.S)
    assert match, "the page's joinLink is marked"
    return match.group(1)


def test_page_loads_nothing_from_elsewhere():
    text = PAGE.read_text()
    loaded = re.findall(r'<(?:script|img|link)\b[^>]*\b(?:src|href)="([^"]+)"', text)
    assert loaded and all(url.startswith("/") for url in loaded), loaded
    assert "fetch(" not in text and "XMLHttpRequest" not in text and "sendBeacon" not in text


def test_join_link():
    node = shutil.which("node")
    if not node:
        pytest.skip("needs Node")
    program = page_function() + "\nconst cases = JSON.parse(process.argv[1]);\n" \
        "console.log(JSON.stringify(cases.map(([hash, search]) => joinLink(hash, search))));\n"
    result = subprocess.run([node, "-e", program, json.dumps([address for address, _ in CASES])],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    for (address, expected), got in zip(CASES, json.loads(result.stdout)):
        assert got == expected, address


def test_join_link_negative_control():
    """the check fails for a page that takes any length of token"""
    node = shutil.which("node")
    if not node:
        pytest.skip("needs Node")
    function = page_function()
    assert function.count("digits.length === 32 && ") == 1
    program = function.replace("digits.length === 32 && ", "digits.length > 0 && ") + \
        "\nconsole.log(JSON.stringify(joinLink('#party=" + PARTY[:-1] + "', '')));\n"
    result = subprocess.run([node, "-e", program], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0 and json.loads(result.stdout) is not None


def test_page_opens_the_link_on_every_system():
    """the page opens the openh1:// link at once on every system: the Mac's and the iPhone's apps are its handler too
    (tools/macos_app.py, tools/ios_app.py)"""
    script = PAGE.read_text().split("<script>")[1]
    assert re.search(r"\n  attempt\(\);\n  location\.href = found\.link;\n", script)
    assert not re.search(r"\bapple\b", script) and "iPhone|iPad|Mac" not in script
