#!/usr/bin/env python3
"""GOAT H1's own network numbers, given out when a branch merges, not when it
starts (port/linux/NETCODE.md, "Numbers on parallel branches").

Branches running side by side used to take the same next number (the
Standard protocol's version, a distributed message's id, a game record
bit) and the later one renumbered. Now a branch marks what it adds as
pending, takes whatever free number builds for its own tests, and says
what its change does in a file of port/linux/net_changes/; then, the last
thing before it merges, `tools/netver.py bump` gives out the numbers free
on master at that moment:

    _distributed_message_my_thing = 144, /* netver: pending */

    tools/netver.py change my-thing "what the machines now send, file.c's function"
    tools/netver.py check       # the numbers agree (CI runs it; --final on master)
    tools/netver.py bump        # after merging master in: the numbers given out
    tools/netver.py rebump      # master moved on after a bump: undo it, merge master, bump

Only GOAT H1's own numbers are given out: the messages from
FIRST_OPENH1_DISTRIBUTED_MESSAGE (120) up, the bits of the game record's
enum of GOAT H1's switches (from _network_game_hold_to_pick_up_bit's), and
HALO_PORT_STANDARD_NETWORK_VERSION. The classic (OpenCE) numbers, and
HALO_PORT_NETWORK_VERSION, come only from upstream: a pending mark on one is
an error.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import textwrap
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MARK = "/* netver: pending */"
CHANGES = "port/linux/net_changes"
LIMITS = "port/linux/include/halo_port_limits.h"
VERSION = "HALO_PORT_STANDARD_NETWORK_VERSION"
# the history's last sentence, which follows its numbered entries
HISTORY_END = "Legacy games, and what an OpenCE machine sees, are unchanged"
BUMP_SUBJECT = "Assign the network numbers"
PENDING_CLASH = " (pending: tools/netver.py bump gives it a free one)"


@dataclass(frozen=True)
class Kind:
    """a kind of number: the enum (the one with `anchor` in `path`), its
    entries' names, the first of GOAT H1's own (`first`, a number or the
    value of an entry's name) and the largest it may have"""
    title: str
    path: str
    anchor: str
    entry: str
    first: str
    largest: int


KINDS = (
    Kind("distributed message", "port/linux/game/network_distributed.h", "NUMBER_OF_DISTRIBUTED_MESSAGES",
         r"_distributed_message_\w+", "120", 255),
    Kind("game record bit", "source/networking/network_game_manager.h", "_network_game_hold_to_pick_up_bit",
         r"_network_game_\w+_bit", "_network_game_hold_to_pick_up_bit", 7),
)


@dataclass
class Entry:
    name: str
    value: int
    pending: bool
    line: int  # its index in the file's lines


class NetverError(Exception):
    pass


def enum_entries(kind: Kind, lines: list[str]) -> list[Entry]:
    """the entries `name = value,` of the enum that holds kind.anchor"""
    at = next((index for index, line in enumerate(lines) if re.search(rf"\b{kind.anchor}\b", line)), None)
    if at is None:
        raise NetverError(f"{kind.path}: no {kind.anchor}")
    start = next(index for index in range(at, -1, -1) if re.match(r"\s*enum\b", lines[index]))
    end = next(index for index in range(at, len(lines)) if re.match(r"\s*}", lines[index]))
    pattern = re.compile(rf"^\s*({kind.entry})\s*=\s*(\d+)\s*,")
    entries = []
    for index in range(start, end):
        match = pattern.match(lines[index])
        if match:
            entries.append(Entry(match.group(1), int(match.group(2)), MARK in lines[index], index))
    return entries


def first_own(kind: Kind, entries: list[Entry]) -> int:
    if kind.first.isdigit():
        return int(kind.first)
    for entry in entries:
        if entry.name == kind.first:
            return entry.value
    raise NetverError(f"{kind.path}: no {kind.first}")


def changes(root: Path) -> list[Path]:
    folder = root / CHANGES
    return sorted(path for path in folder.glob("*.txt")) if folder.is_dir() else []


def standard_version(text: str) -> int:
    match = re.search(rf"^#define {VERSION} (\d+)\s*$", text, re.M)
    if not match:
        raise NetverError(f"{LIMITS}: no #define {VERSION} <number>")
    return int(match.group(1))


def history(text: str) -> str:
    """the comment of the version's history, which follows its #define"""
    start = re.search(rf"^#define {VERSION} \d+\s*$", text, re.M).end()
    end = text.index("*/", start)
    return text[start:end]


def check(root: Path, final: bool = False) -> list[str]:
    """what is wrong with the numbers (nothing: an empty list). final: master's
    check, where nothing may be pending"""
    problems = []
    pending_count = 0
    for kind in KINDS:
        lines = (root / kind.path).read_text().splitlines()
        entries = enum_entries(kind, lines)
        first = first_own(kind, entries)
        seen: dict[int, Entry] = {}
        for entry in entries:
            if entry.pending:
                pending_count += 1
                if entry.value < first:
                    problems.append(f"{kind.path}:{entry.line + 1}: {entry.name} is marked pending, but {entry.value}"
                                    f" is a classic (OpenCE) number, below {first}: those come only from upstream")
                if final:
                    problems.append(f"{kind.path}:{entry.line + 1}: {entry.name} is still pending: run "
                                    "tools/netver.py bump before merging")
            if entry.value > kind.largest:
                problems.append(f"{kind.path}:{entry.line + 1}: {entry.name} = {entry.value}: a {kind.title} is at"
                                f" most {kind.largest}")
            other = seen.get(entry.value)
            if other:
                problems.append(f"{kind.path}:{entry.line + 1}: {entry.name} has {entry.value}, as {other.name} does"
                                + (PENDING_CLASH if entry.pending or other.pending else ""))
            seen.setdefault(entry.value, entry)
    found = changes(root)
    if final and found:
        problems += [f"{path.relative_to(root)}: a network change not given its version yet: run tools/netver.py bump"
                     for path in found]
    if pending_count and not found:
        problems.append(f"pending numbers but no file in {CHANGES}/ saying what changes on the wire: "
                        "tools/netver.py change <topic> \"<what>\"")
    for path in found:
        if not " ".join(path.read_text().split()):
            problems.append(f"{path.relative_to(root)}: empty: say what the machines now send")
    text = (root / LIMITS).read_text()
    version = standard_version(text)
    if not re.search(rf"(^|[(;\s]){version}: ", history(text)):
        problems.append(f"{LIMITS}: {VERSION} is {version}, but its history (the comment after it) has no "
                        f"\"{version}: \"")
    legacy = re.search(r"^#define HALO_PORT_NETWORK_VERSION (\d+)", text, re.M)
    base = re.search(r"^#define HALO_PORT_STANDARD_BASE_VERSION (\d+)", text, re.M)
    if legacy and base and legacy.group(1) != base.group(1) and not found:
        problems.append(f"{LIMITS}: HALO_PORT_NETWORK_VERSION is {legacy.group(1)} (upstream raised it), the Standard "
                        f"version's base {base.group(1)}: tools/netver.py change <topic> \"network version "
                        f"{legacy.group(1)}, OpenCE's (...)\", then tools/netver.py bump")
    return problems


def add_history(text: str, version: int, entry: str) -> str:
    """the version's history with `version: entry` added after its last"""
    start = re.search(rf"^#define {VERSION} \d+\s*$", text, re.M).end()
    end = text.index("*/", start)
    comment = text[start:end]
    at = comment.find(HISTORY_END)
    if at < 0:
        raise NetverError(f"{LIMITS}: the history after {VERSION} does not end with \"{HISTORY_END}\"")
    before = comment[:at].rstrip()
    if not before.endswith("."):
        raise NetverError(f"{LIMITS}: the history's last entry does not end with \".\"")
    last_line_start = before.rfind("\n") + 1
    joined = before[last_line_start:-1] + f"; {version}: {' '.join(entry.split()).rstrip('.')}."
    wrapped = "\n".join(textwrap.wrap(joined, width=78, break_long_words=False, break_on_hyphens=False))
    comment = before[:last_line_start] + wrapped + "\n" + comment[at:]
    return text[:start] + comment + text[end:]


def bump(root: Path) -> list[str]:
    """gives every pending number the next free one, and the network changes
    the next version; returns what it did"""
    # (a pending number taken by another is what bump is for)
    problems = [problem for problem in check(root) if not problem.endswith(PENDING_CLASH)]
    if problems:
        raise NetverError("\n".join(problems))
    done = []
    for kind in KINDS:
        path = root / kind.path
        lines = path.read_text().splitlines(keepends=True)
        entries = enum_entries(kind, lines)
        first = first_own(kind, entries)
        given = [entry.value for entry in entries if not entry.pending and entry.value >= first]
        next_free = max(given, default=first - 1) + 1
        for entry in entries:
            if not entry.pending:
                continue
            if next_free > kind.largest:
                raise NetverError(f"{kind.path}: no {kind.title} free for {entry.name}: they end at {kind.largest}")
            line = lines[entry.line]
            line = re.sub(rf"(\b{entry.name}\s*=\s*)\d+", rf"\g<1>{next_free}", line, count=1)
            line = line.replace(" " + MARK, "").replace(MARK, "")
            lines[entry.line] = line
            done.append(f"{entry.name} = {next_free} (was {entry.value})")
            next_free += 1
        path.write_text("".join(lines))
    found = changes(root)
    if found:
        limits = root / LIMITS
        text = limits.read_text()
        version = standard_version(text) + 1
        entry = "; ".join(" ".join(path.read_text().split()).rstrip(".") for path in found)
        text = re.sub(rf"^(#define {VERSION} )\d+", rf"\g<1>{version}", text, count=1, flags=re.M)
        text = add_history(text, version, entry)
        # (the Legacy version upstream raised, which the Standard version follows)
        legacy = re.search(r"^#define HALO_PORT_NETWORK_VERSION (\d+)", text, re.M)
        if legacy:
            text = re.sub(r"^(#define HALO_PORT_STANDARD_BASE_VERSION )\d+", rf"\g<1>{legacy.group(1)}", text,
                          count=1, flags=re.M)
        limits.write_text(text)
        for path in found:
            path.unlink()
        done.append(f"{VERSION} = {version}: {entry}")
    return done


def git(*args: str, check_result: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", str(ROOT), *args], check=check_result, text=True, capture_output=True)


def commit_bump(done: list[str]) -> None:
    paths = [kind.path for kind in KINDS] + [LIMITS, CHANGES]
    git("add", "-A", "--", *paths)
    git("commit", "-q", "-m", BUMP_SUBJECT + "\n\n" + "\n".join(done))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    check_parser = commands.add_parser("check", help="whether the numbers agree")
    check_parser.add_argument("--final", action="store_true", help="master's check: nothing may be pending")
    bump_parser = commands.add_parser("bump", help="give out the pending numbers and the next version")
    bump_parser.add_argument("--no-commit", action="store_true", help="leave the changes uncommitted")
    rebump_parser = commands.add_parser("rebump", help="undo the branch's bump, merge master, bump again")
    rebump_parser.add_argument("--base", default="origin/master", help="the branch merged into (origin/master)")
    change_parser = commands.add_parser("change", help=f"add a network change's file to {CHANGES}/")
    change_parser.add_argument("topic", help="a short name: the file's")
    change_parser.add_argument("text", help="what the machines now send, as the history says it")
    args = parser.parse_args()
    try:
        if args.command == "check":
            problems = check(ROOT, args.final)
            for problem in problems:
                print(problem, file=sys.stderr)
            if not problems:
                print("The network numbers agree.")
            return 1 if problems else 0
        if args.command == "change":
            path = ROOT / CHANGES / (re.sub(r"[^a-z0-9_-]+", "-", args.topic.lower()).strip("-") + ".txt")
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("\n".join(textwrap.wrap(" ".join(args.text.split()), width=78)) + "\n")
            print(f"Wrote {path.relative_to(ROOT)}")
            return 0
        if args.command == "rebump":
            log = git("log", "--format=%H %s", f"{args.base}..HEAD").stdout.splitlines()
            bumps = [line.split()[0] for line in log if line.split(" ", 1)[1].startswith(BUMP_SUBJECT)]
            if bumps:
                git("revert", "--no-edit", bumps[0])
                print(f"Reverted {bumps[0][:12]}")
            merged = git("merge", "--no-edit", args.base, check_result=False)
            if merged.returncode:
                print(merged.stdout + merged.stderr, file=sys.stderr)
                print("Resolve the merge, commit, then run tools/netver.py bump", file=sys.stderr)
                return 1
        done = bump(ROOT)
        if not done:
            print("Nothing pending.")
            return 0
        if args.command == "rebump" or not args.no_commit:
            commit_bump(done)
        print("\n".join(done))
        return 0
    except NetverError as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
