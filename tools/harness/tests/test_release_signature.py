"""The self-updater's check of a release (port/linux/src/release_signature.c, the whole file): its SHA256SUMS signed by
the release key, a bad signature, a missing one while signatures are optional and once they are required, another
key's, and the download's line; on the real SHA-256 and Ed25519 (p2p_crypto.c, Monocypher). Also: the key built in
is port/assets/release-key.pub's, and tools/release_sign.py (OpenSSL) signs and verifies as the game does."""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, ROOT, build, constant, mutated, read, run  # noqa: E402

sys.path.insert(0, str(ROOT / "tools"))
import release_sign  # noqa: E402

CASES = ["good", "bad", "malformed", "missing-optional", "missing-required", "wrong-key", "listing", "openssl"]

# the test's seed (the SHA-256 of "openh1 release-signature test key"), its public key and OpenSSL's signature of the
# C test's vector_sums by it (tools/release_sign.py; test_tool_signs_the_vector makes it again)
VECTOR_SEED = "8a543fbaacb9aaa064eeb749f9cb3c3fc4c0591525d068c0d96f7472708f1c01"
VECTOR_PUBLIC_KEY = "8c07dbdf6ba7a37f17d36dc2fcfffec8c69d9dbcf334c626d1898ecf9c7c254e"
VECTOR_SIGNATURE = ("d527c0b8a1b9a06828a9ed8b3921b4d16827fb82ae1d9b36520a3f465c199fe0"
                    "0221125d74114f4af8de0059466cb3fdc12688f9322d16922e262e200ae7420a")
VECTOR_SUMS = ("0000000000000000000000000000000000000000000000000000000000000000  OpenH1-Android.apk\n"
               "1111111111111111111111111111111111111111111111111111111111111111  OpenH1-Linux.zip\n")

# a fault in the code and the case that must catch it
NEGATIVE_CONTROLS = {
    "signature-unchecked": (("if (p2p_ed25519_verify(keys[index], sums, sums_size, bytes))", "if (1)"), "bad"),
    "signs-less": (("p2p_ed25519_verify(keys[index], sums, sums_size, bytes)",
                    "p2p_ed25519_verify(keys[index], sums, sums_size - 1, bytes)"), "good"),
    "unsigned-when-required": (("\telse if (required)\n", "\telse if (0)\n"), "missing-required"),
    "unsigned-refused-optional": (("\telse if (required)\n", "\telse if (1)\n"), "missing-optional"),
    "last-key-only": (("for (index = 0; index < key_count; index++)",
                       "for (index = key_count - 1; index < key_count; index++)"), "good"),
    "any-key": (("if (index == key_count)\n", "if (0)\n"), "wrong-key"),
    "digest-unchecked": (("if (!p2p_equal(listed_digest, digest, RELEASE_DIGEST_SIZE))\n\t\t\treturn -1;", ""), "bad"),
    "unsigned-digest-unchecked": (("\tmatch = release_sums_match(sums, sums_size, asset, digest);",
                                   "\tmatch = 1;"), "missing-optional"),
    "name-by-prefix": (("length != RELEASE_DIGEST_SIZE * 2 + 2 + asset_length ||",
                        "length < RELEASE_DIGEST_SIZE * 2 + 2 + asset_length ||"), "listing"),
    "first-line-wins": (("\t\t\treturn -1;\n\t\tlisted = 1;", "\t\t\treturn listed ? 1 : -1;\n\t\treturn 1;"),
                        "listing"),
    "signature-trailing-junk": (("\t\tif (text[index] != '\\n' && text[index] != '\\r' && text[index] != ' ' && "
                                 "text[index] != '\\t')\n\t\t\treturn 0;", ""), "malformed"),
    "signature-without-sums": (("\t\tif (!sums)\n\t\t{\n\t\t\trelease_error(error, error_size, \"it has a signature",
                                "\t\tif (0)\n\t\t{\n\t\t\trelease_error(error, error_size, \"it has a signature"),
                               "malformed"),
    "test-key-ignored": (("\tif (test_key && strlen(test_key)", "\tif (0 && strlen(test_key)"), "wrong-key"),
}


def crypto_includes():
    """p2p_crypto.c and Monocypher as whole files, with the parts of p2p_internal.h and posix.h they need."""
    internal = read("port/linux/src/p2p_internal.h")
    section = internal[internal.index("/* ---------- p2p_crypto.c */"):]
    section = section[:section.index("/* ----------", 10)]
    posix = read("port/linux/src/posix.h")
    posix_stub = "typedef unsigned long posix_ulong;\n" + \
        re.search(r"^void posix_random_bytes\([^;]*\);", posix, re.M).group(0) + "\n"
    internal_stub = ("#ifndef TEST_P2P_INTERNAL\n#define TEST_P2P_INTERNAL\n#include <stdint.h>\n"
                     f"enum {{ P2P_TOKEN_SIZE = {constant(internal, 'P2P_TOKEN_SIZE')} }};\n"
                     "void p2p_hex(const unsigned char *bytes, int size, char *text);\n" + section + "#endif\n")
    return (
        ("posix.h", posix_stub), ("p2p_internal.h", internal_stub),
        ("monocypher.h", read("port/third_party/monocypher/monocypher.h")),
        ("monocypher-ed25519.h", read("port/third_party/monocypher/monocypher-ed25519.h")),
        ("monocypher.inc", read("port/third_party/monocypher/monocypher.c")),
        ("monocypher_ed25519.inc", read("port/third_party/monocypher/monocypher-ed25519.c")),
        ("p2p_crypto.inc", read("port/linux/src/p2p_crypto.c")),
        ("release_signature.h", read("port/linux/src/release_signature.h")),
    )


def generated(fault=None):
    code = read("port/linux/src/release_signature.c")
    if fault:
        code = mutated(code, *fault)
    config = f'#define VECTOR_PUBLIC_KEY "{VECTOR_PUBLIC_KEY}"\n#define VECTOR_SIGNATURE "{VECTOR_SIGNATURE}"\n'
    return crypto_includes() + (("under_test.inc", code), ("config.inc", config))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("release_signature", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("release_signature", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"


def test_built_in_key_is_the_published_one():
    """The key the updaters carry is port/assets/release-key.pub's, which tools/release_sign.py checks with."""
    public = (ROOT / "port/assets/release-key.pub").read_text().strip()
    assert re.fullmatch(r"[0-9a-f]{64}", public), "port/assets/release-key.pub is not 64 lowercase hex digits"
    source = read("port/linux/src/release_signature.c")
    keys = re.search(r"release_keys\[\] =\s*\{(.*?)\};", source, re.S)
    assert keys, "release_signature.c's release_keys not found"
    assert public in re.findall(r'"([0-9a-f]{64})"', keys.group(1)), \
        "release_signature.c does not carry port/assets/release-key.pub's key"


def test_required():
    """Every release is signed, so an unsigned one is refused (release_signature.h)."""
    assert constant(read("port/linux/src/release_signature.h"), "RELEASE_SIGNATURE_REQUIRED") == 1


def test_updater_checks_each_download():
    """Every path of updater.c that downloads a release's file checks it before it is unpacked: the background's
    and the Frame's each after their download, and updater_install (the desktop's window and a dedicated server's)
    once after either of its own."""
    updater = read("port/linux/src/updater.c")
    downloads = updater.count("updater_fetch(url, zip_path") + updater.count("updater_download_zip(zip_path")
    assert downloads == 4, "updater.c downloads a release in another way now: check it is verified"
    assert updater.count("updater_verify(zip_path") == 3
    install = updater[updater.index("static int updater_install("):]
    install = install[:install.index("\n}\n")]
    assert install.count("updater_download_zip(zip_path") == 1 and install.count("updater_fetch(url, zip_path") == 1
    assert install.index("updater_verify(zip_path") > max(install.index("updater_download_zip(zip_path"),
                                                          install.index("updater_fetch(url, zip_path"))
    assert install.index("updater_verify(zip_path") < install.index("updater_unpack(zip_path")


def test_test_key_only_for_a_test_server():
    """debug.update_key is trusted only while debug.update_url is set: never for GitHub's releases."""
    updater = read("port/linux/src/updater.c")
    assert 'config_string("debug.update_url")[0] ? config_string("debug.update_key") : NULL' in updater


def openssl_or_skip():
    try:
        release_sign.openssl()
    except release_sign.SignError as error:
        pytest.skip(str(error))


def test_tool_signs_the_vector():
    """tools/release_sign.py (OpenSSL) makes the signature the C test checks with Monocypher (Ed25519 is
    deterministic)."""
    openssl_or_skip()
    seed = bytes.fromhex(VECTOR_SEED)
    assert release_sign.public_of(seed).hex() == VECTOR_PUBLIC_KEY
    with tempfile.TemporaryDirectory() as work:
        sums = Path(work) / "SHA256SUMS"
        sums.write_text(VECTOR_SUMS, newline="\n")
        assert release_sign.signature_of(seed, sums).hex() == VECTOR_SIGNATURE


def tool(*arguments, environment=None):
    return subprocess.run([sys.executable, str(ROOT / "tools/release_sign.py"), *arguments], capture_output=True,
                          text=True, env=environment)


def test_tool_round_trip():
    """keygen, sums, sign and verify: a good signature, a changed file, a missing signature, another key, and a
    seed that is not the public key's refused before anything is signed."""
    openssl_or_skip()
    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        release = work / "release"
        release.mkdir()
        (release / "OpenH1-Linux.zip").write_bytes(b"a build")
        (release / "OpenH1-Android.apk").write_bytes(b"another")
        key, public = work / "key", work / "key.pub"
        result = tool("keygen", str(key), "--public-out", str(public))
        assert result.returncode == 0, result.stderr
        assert key.stat().st_mode & 0o777 == 0o600
        assert key.read_text().strip() not in result.stdout, "keygen printed the seed"
        assert tool("keygen", str(key)).returncode == 2, "keygen replaced a key"
        assert tool("sums", str(release)).returncode == 0
        sums = release / "SHA256SUMS"
        assert sums.read_text().splitlines()[1].endswith("  OpenH1-Linux.zip")
        assert tool("verify", str(sums), "--public-key", str(public)).returncode == 1, "no signature, verified"
        result = tool("sign", str(sums), "--key-file", str(key), "--public-key", str(public))
        assert result.returncode == 0, result.stderr
        assert key.read_text().strip() not in result.stdout + result.stderr, "sign printed the seed"
        assert re.fullmatch(r"[0-9a-f]{128}\n", (release / "SHA256SUMS.sig").read_text())
        assert tool("verify", str(sums), "--public-key", str(public)).returncode == 0
        # (the release key's own, by default: not this key's signature)
        assert tool("verify", str(sums)).returncode == 1
        # (the seed by the environment, as the workflow passes it)
        environment = {"PATH": __import__("os").environ.get("PATH", ""), "OPENH1_RELEASE_KEY": key.read_text()}
        assert tool("sign", str(sums), "--public-key", str(public), environment=environment).returncode == 0
        # a seed that is not the public key's: nothing signed
        (release / "SHA256SUMS.sig").unlink()
        assert tool("sign", str(sums), "--key-file", str(key)).returncode == 2
        assert not (release / "SHA256SUMS.sig").exists()
        # a changed file
        assert tool("sign", str(sums), "--key-file", str(key), "--public-key", str(public)).returncode == 0
        sums.write_text(sums.read_text().replace("0", "1", 1))
        assert tool("verify", str(sums), "--public-key", str(public)).returncode == 1
