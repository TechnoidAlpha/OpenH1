#!/usr/bin/env python3
"""A release's SHA256SUMS, and its signature by OpenH1's release key.

    python3 tools/release_sign.py sums release/              # release/SHA256SUMS: each file's SHA-256
    OPENH1_RELEASE_KEY=<seed> python3 tools/release_sign.py sign release/SHA256SUMS
    python3 tools/release_sign.py sign --key-file <seed file> release/SHA256SUMS
    python3 tools/release_sign.py verify release/SHA256SUMS  # exit 0: the signature is the release key's
    python3 tools/release_sign.py keygen <seed file>         # a new key: its seed to the file (mode 600)
    python3 tools/release_sign.py public --key-file <seed file>   # the seed's public key

The scheme is OpenCE's (its tools/release_sign.sh, deploy/release-key.pub),
with OpenH1's own key:

- SHA256SUMS: a line for each of the release's files, "<sha256 hex>  <name>",
  as sha256sum writes it and `sha256sum -c` reads it, in name order (the sums
  file and its signature left out).
- SHA256SUMS.sig: the Ed25519 signature (RFC 8032) of SHA256SUMS's exact
  bytes, as 128 lowercase hex digits and a newline.
- The release key's seed (its private key) is 64 hex digits: the workflow's
  OPENH1_RELEASE_KEY secret, or a file of them (keygen's). The public key,
  64 hex digits, is port/assets/release-key.pub, which the self-updaters
  carry (port/linux/src/release_signature.c) and verify checks with
  (or --public-key's).

sign checks the signature it made against the public key before it keeps
it: a seed that is not the release key's is refused, rather than published
for every updater to refuse. The seed only ever goes to a file of a private
temporary folder (for OpenSSL), removed at once, and is never printed.

Needs OpenSSL 3 (`openssl pkeyutl -rawin`) on the PATH, or OPENSSL naming it.
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PUBLIC_KEY = ROOT / "port" / "assets" / "release-key.pub"
SUMS = "SHA256SUMS"
SIGNATURE = "SHA256SUMS.sig"
KEY_VARIABLE = "OPENH1_RELEASE_KEY"
# RFC 8410's DER prefixes: an Ed25519 private key's PKCS#8 (then its seed),
# and a public key's SubjectPublicKeyInfo (then its 32 bytes)
PRIVATE_PREFIX = bytes.fromhex("302e020100300506032b657004220420")
PUBLIC_PREFIX = bytes.fromhex("302a300506032b6570032100")


class SignError(Exception):
    pass


def hex_bytes(text: str, size: int, what: str) -> bytes:
    text = text.strip()
    if not re.fullmatch(r"[0-9a-fA-F]{%d}" % (2 * size), text):
        raise SignError(f"{what} is not {2 * size} hex digits")
    return bytes.fromhex(text)


def openssl() -> str:
    program = os.environ.get("OPENSSL") or shutil.which("openssl")
    if not program:
        raise SignError("no openssl on the PATH (OpenSSL 3 is needed)")
    version = subprocess.run([program, "version"], capture_output=True, text=True).stdout
    match = re.match(r"OpenSSL (\d+)\.", version)
    if not match or int(match.group(1)) < 3:
        raise SignError(f"{program} is not OpenSSL 3 or later ({version.strip() or 'no version'}); "
                        "name one with OPENSSL")
    return program


def run_openssl(arguments) -> subprocess.CompletedProcess:
    return subprocess.run([openssl(), *arguments], capture_output=True)


def write_sums(folder: Path) -> Path:
    """folder/SHA256SUMS: each file's SHA-256 and name, in name order"""
    lines = []
    for path in sorted(folder.iterdir(), key=lambda item: item.name):
        if not path.is_file() or path.name in (SUMS, SIGNATURE):
            continue
        if "\n" in path.name or "\r" in path.name:
            raise SignError(f"{path.name!r}: a name sha256sum cannot list")
        digest = hashlib.sha256()
        with path.open("rb") as file:
            for chunk in iter(lambda: file.read(1 << 20), b""):
                digest.update(chunk)
        lines.append(f"{digest.hexdigest()}  {path.name}\n")
    if not lines:
        raise SignError(f"{folder}: no files to sum")
    sums = folder / SUMS
    sums.write_text("".join(lines), encoding="utf-8", newline="\n")
    print(f"{sums}: {len(lines)} files", flush=True)
    return sums


def read_seed(key_file) -> bytes:
    if key_file:
        text = Path(key_file).read_text(encoding="ascii", errors="replace")
    else:
        text = os.environ.get(KEY_VARIABLE, "")
        if not text.strip():
            raise SignError(f"no key: set {KEY_VARIABLE} or give --key-file")
    return hex_bytes(text, 32, "the release key's seed")


def read_public(public_key) -> bytes:
    if public_key:
        text = public_key if re.fullmatch(r"\s*[0-9a-fA-F]{64}\s*", public_key) else Path(public_key).read_text()
    else:
        text = PUBLIC_KEY.read_text()
    return hex_bytes(text.splitlines()[0] if text.strip() else "", 32, "the public key")


def public_of(seed: bytes) -> bytes:
    with tempfile.TemporaryDirectory() as work:
        key = Path(work) / "key.der"
        descriptor = os.open(key, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as file:
            file.write(PRIVATE_PREFIX + seed)
        result = run_openssl(["pkey", "-inform", "DER", "-in", str(key), "-pubout", "-outform", "DER"])
        if result.returncode or len(result.stdout) != len(PUBLIC_PREFIX) + 32 or \
                not result.stdout.startswith(PUBLIC_PREFIX):
            raise SignError("openssl could not make the seed's public key")
        return result.stdout[len(PUBLIC_PREFIX):]


def signature_of(seed: bytes, message: Path) -> bytes:
    with tempfile.TemporaryDirectory() as work:
        key = Path(work) / "key.der"
        out = Path(work) / "signature"
        descriptor = os.open(key, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as file:
            file.write(PRIVATE_PREFIX + seed)
        result = run_openssl(["pkeyutl", "-sign", "-rawin", "-inkey", str(key), "-keyform", "DER",
                              "-in", str(message), "-out", str(out)])
        signature = out.read_bytes() if out.exists() else b""
        if result.returncode or len(signature) != 64:
            raise SignError("openssl did not make a signature of 64 bytes")
        return signature


def verify(message: Path, signature_text: str, public: bytes) -> bool:
    """whether the signature (SHA256SUMS.sig's text) is the public key's, of the file's bytes"""
    line = signature_text.split("\n")[0].rstrip("\r")
    if not re.fullmatch(r"[0-9a-fA-F]{128}", line):
        return False
    with tempfile.TemporaryDirectory() as work:
        key = Path(work) / "public.der"
        signature = Path(work) / "signature"
        key.write_bytes(PUBLIC_PREFIX + public)
        signature.write_bytes(bytes.fromhex(line))
        result = run_openssl(["pkeyutl", "-verify", "-pubin", "-inkey", str(key), "-keyform", "DER", "-rawin",
                              "-in", str(message), "-sigfile", str(signature)])
        return result.returncode == 0


def sign(message: Path, seed: bytes, public: bytes) -> Path:
    if public_of(seed) != public:
        raise SignError("the key is not the release key of the public key "
                        f"({PUBLIC_KEY.relative_to(ROOT)} or --public-key): nothing signed")
    text = signature_of(seed, message).hex() + "\n"
    if not verify(message, text, public):
        raise SignError("the signature made does not verify: nothing signed")
    out = message.with_name(message.name + ".sig")
    out.write_text(text, encoding="ascii", newline="\n")
    print(f"{out} written", flush=True)
    return out


def keygen(path: Path) -> bytes:
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    seed = os.urandom(32)
    public = public_of(seed)
    try:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError:
        raise SignError(f"{path} exists: not replaced") from None
    with os.fdopen(descriptor, "w") as file:
        file.write(seed.hex() + "\n")
    os.chmod(path, 0o600)
    return public


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    sums = commands.add_parser("sums", help="write <folder>/SHA256SUMS")
    sums.add_argument("folder", type=Path)
    for name in ("sign", "verify"):
        command = commands.add_parser(name, help=f"{name} a SHA256SUMS (its signature: <file>.sig)")
        command.add_argument("file", type=Path)
        command.add_argument("--public-key", help="the public key (64 hex digits, or a file of them); "
                             "default port/assets/release-key.pub")
        if name == "sign":
            command.add_argument("--key-file", help=f"the seed's file (default: ${KEY_VARIABLE})")
    generate = commands.add_parser("keygen", help="a new release key: its seed to the file, its public key printed")
    generate.add_argument("key_file", type=Path)
    generate.add_argument("--public-out", type=Path, help="also write the public key to this file")
    public = commands.add_parser("public", help="the public key of a seed")
    public.add_argument("--key-file", help=f"the seed's file (default: ${KEY_VARIABLE})")
    arguments = parser.parse_args()
    try:
        if arguments.command == "sums":
            write_sums(arguments.folder)
        elif arguments.command == "sign":
            if not arguments.file.is_file():
                raise SignError(f"no file {arguments.file}")
            sign(arguments.file, read_seed(arguments.key_file), read_public(arguments.public_key))
        elif arguments.command == "verify":
            if not arguments.file.is_file():
                raise SignError(f"no file {arguments.file}")
            signature = arguments.file.with_name(arguments.file.name + ".sig")
            if not signature.is_file():
                print(f"{signature}: no signature", file=sys.stderr)
                return 1
            if not verify(arguments.file, signature.read_text(encoding="ascii", errors="replace"),
                          read_public(arguments.public_key)):
                print(f"{arguments.file}: the signature is not the release key's", file=sys.stderr)
                return 1
            print(f"{arguments.file}: the signature is good")
        elif arguments.command == "keygen":
            key = keygen(arguments.key_file)
            if arguments.public_out:
                arguments.public_out.write_text(key.hex() + "\n", encoding="ascii")
            print(key.hex())
        elif arguments.command == "public":
            print(public_of(read_seed(arguments.key_file)).hex())
    except SignError as error:
        print(f"release_sign: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
