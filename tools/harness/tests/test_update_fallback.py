"""The self-updater's downloads (updater.c, the whole file) against a fake GitHub, with no network: each tried again
while GitHub is in trouble (a 503, a 504, no answer); a release's file its link cannot give (GitHub's outage of its
release links, which answer 504) had from GitHub's API (/releases/assets/<id>, application/octet-stream), its id read
from the API's answer about the release; and the bytes checked as any are, by the real release_signature.c and
p2p_crypto.c: a signature by another key, or a zip SHA256SUMS does not list, refused through the API too. An outage
as the game starts asks the player nothing and shows no dialog, nor does one after the player's Yes; a refused
download is said in one.

Compiled for this computer (update_fallback.c), as test_update_gamescope.py's is, on Linux and macOS."""

import os
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, HARNESS, ROOT, mutated, read  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_release_signature import crypto_includes  # noqa: E402

BUILD = 7
ZLIB = ROOT / "port/third_party/zlib"
CASES = ["asset-ids", "redirects", "links", "retry", "fallback", "fallback-direct", "fallback-listed", "not-listed",
         "fallback-bad-signature", "fallback-bad-zip", "outage-at-start", "outage-after-yes", "refusal-after-yes"]

# a fault in updater.c (or the signature's check), and the case that must catch it
NEGATIVE_CONTROLS = {
    "no-retry": ("updater", ("\t\tif (result != _updater_fetch_unreachable)\n\t\t\tbreak;", "\t\tbreak;"), "retry"),
    "no-fallback": ("updater", ("\tif (!id)\n\t{\n\t\tsnprintf(error, error_size, \"%s could not be had: %s\", name, "
                                "first);", "\tif (1)\n\t{\n\t\tsnprintf(error, error_size, \"%s could not be had: %s\", "
                                "name, first);"), "fallback"),
    "no-octet-stream": ("updater", ("fallback = updater_get(url, UPDATE_ASSET_ACCEPT,", "fallback = updater_get(url, NULL,"),
                        "fallback"),
    "nested-ids": ("updater", ("\t\t\t\telse if (updater_json_string_is(member_key, member_key_end, \"id\"))",
                               "\t\t\t\telse if (updater_json_string_is(member_key, member_key_end, \"id\") && 0)"),
                   "asset-ids"),
    "any-redirect": ("updater", ("\t\t\t\treturn !strncmp(location, \"https://\", 8);", "\t\t\t\treturn 1;"),
                     "redirects"),
    "fallback-unchecked": ("signature", ("if (p2p_ed25519_verify(keys[index], sums, sums_size, bytes))", "if (1)"),
                           "fallback-bad-signature"),
    "asked-in-an-outage": ("updater", ("\t\t\tif (had < 0)\n\t\t\t{\n\t\t\t\tplatform_log(\"update: build %ld's "
                                       "signature", "\t\t\tif (0)\n\t\t\t{\n\t\t\t\tplatform_log(\"update: build %ld's "
                                       "signature"), "outage-at-start"),
    "dialog-in-an-outage": ("updater", ("\tif (updater_unreachable)\n\t{", "\tif (0)\n\t{"), "outage-after-yes"),
    "refusal-unsaid": ("updater", ("\tif (updater_unreachable)\n\t{", "\tif (1)\n\t{"), "refusal-after-yes"),
}


def generated(fault=None):
    updater = read("port/linux/src/updater.c")
    signature = read("port/linux/src/release_signature.c")
    if fault:
        where, change, _ = NEGATIVE_CONTROLS[fault]
        if where == "updater":
            updater = mutated(updater, *change)
        else:
            signature = mutated(signature, *change)
    return dict(crypto_includes()) | {
        "release_signature.inc": signature,
        "under_test.inc": updater,
        "update.h": read("port/linux/src/update.h"),
        # (update_fallback.c's fakes are declared before under_test.inc)
        "SDL3/SDL.h": "", "platform.h": "", "install_layout.h": "", "port_config.h": "", "report.h": "",
    }


_zlib = []
_built = {}


def zlib_objects(tmp_path_factory):
    if not _zlib:
        work = tmp_path_factory.mktemp("update_fallback-zlib")
        for source in ("adler32.c", "crc32.c", "inffast.c", "inflate.c", "inftrees.c", "zutil.c"):
            _zlib.append(work / f"{source}.o")
            subprocess.run([os.environ.get("CC", "clang"), "-c", "-O1", "-w", "-DZ_PREFIX", "-I", str(ZLIB),
                            str(ZLIB / source), "-o", str(_zlib[-1])], check=True)
    return _zlib


def build(tmp_path_factory, fault=None):
    if fault in _built:
        return _built[fault]
    work = tmp_path_factory.mktemp(f"update_fallback-{fault or 'as-is'}")
    for name, text in generated(fault).items():
        (work / name).parent.mkdir(parents=True, exist_ok=True)
        (work / name).write_text(text, encoding="latin-1")
    executable = work / "update_fallback"
    result = subprocess.run([os.environ.get("CC", "clang"), "-std=gnu99", "-O1", "-Wall", "-Werror",
                             "-Wno-unused-function", f"-DHALO_BUILD_NUMBER={BUILD}", "-I", str(HARNESS / "include"),
                             "-I", str(work), "-I", str(ZLIB), str(HARNESS / "tests/update_fallback.c"),
                             *map(str, zlib_objects(tmp_path_factory)), "-o", str(executable), "-lpthread"],
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"update_fallback.c does not compile:\n{result.stderr}")
    _built[fault] = executable
    return executable


def run(executable, case, folder):
    result = subprocess.run([str(executable), case, str(folder)], capture_output=True, text=True, errors="replace",
                            timeout=60)
    return result.returncode, (result.stdout + result.stderr).strip()


@pytest.mark.parametrize("case", CASES)
def test_case(case, tmp_path, tmp_path_factory):
    status, output = run(build(tmp_path_factory), case, tmp_path)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control, tmp_path, tmp_path_factory):
    case = NEGATIVE_CONTROLS[control][2]
    status, output = run(build(tmp_path_factory, control), case, tmp_path)
    assert status == CHECK_FAILED, f"updater.c with a fault ({control}) passed '{case}': the test cannot see it\n{output}"
