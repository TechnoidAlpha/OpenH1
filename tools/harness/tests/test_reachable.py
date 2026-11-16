"""Every game function a harness test compiles from the sources (`function`, `inline`) is still called by the game:
a test of a function nothing calls any more proves nothing about the game. This stands in for the per-test checks
that a call's text is where it was ("wired"), which broke on every rename and merge: the names come from the tests
themselves, so a rename that the tests follow is followed here too."""
import ast
import re
import sys
from functools import lru_cache
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import ROOT

TESTS = Path(__file__).resolve().parent
SUFFIXES = {".c", ".h", ".inc", ".inl", ".cpp", ".hpp", ".m", ".mm"}
COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
IDENTIFIER = re.compile(r"[A-Za-z_]\w*")
DEFINED = re.compile(r"^(?:[A-Za-z_][^;{=(]*?[\s*])?([A-Za-z_]\w*)\s*\(")  # a declaration's or definition's head

# (functions a test takes that the game did not call when this test was written: each to be wired or deleted with
# its test; an entry is dropped once the game calls it, which the test below asks for)
NOT_YET_CALLED = {}


def taken_names():
    """{name: [test files taking it]} for every function a harness test takes from the sources by name"""
    names = {}
    for test in sorted(TESTS.glob("test_*.py")):
        for node in ast.walk(ast.parse(test.read_text())):
            if isinstance(node, ast.Call) and len(node.args) >= 2:
                called = node.func.id if isinstance(node.func, ast.Name) else getattr(node.func, "attr", None)
                name = node.args[1]
                if called in ("function", "inline") and isinstance(name, ast.Constant) and isinstance(name.value, str):
                    names.setdefault(name.value, []).append(test.name)
    return names


@lru_cache(maxsize=None)
def game_sources():
    """{path: text} of the game's C sources"""
    return {path: path.read_text(errors="replace")
            for folder in ("port", "source") for path in sorted((ROOT / folder).rglob("*"))
            if path.suffix in SUFFIXES and path.is_file()}


def uses(texts):
    """(defined, referenced): the names each declared or defined at a line's start, and the counts of every other
    mention of a name (calls, function pointers, tables)"""
    defined, referenced = set(), {}
    for text in texts:
        for line in COMMENT.sub(" ", text).split("\n"):
            head = DEFINED.match(line)
            skip = None
            if head:
                defined.add(head.group(1))
                skip = head.start(1)
            for match in IDENTIFIER.finditer(line):
                if match.start() != skip:
                    referenced[match.group()] = referenced.get(match.group(), 0) + 1
    return defined, referenced


def uncalled(names, texts):
    """the names the game defines but never mentions otherwise"""
    defined, referenced = uses(texts)
    return sorted(name for name in names if name in defined and not referenced.get(name))


def test_every_tested_function_is_called():
    names = taken_names()
    assert len(names) > 100, "the tests' functions were not found"
    found = uncalled(names, game_sources().values())
    called = sorted(set(NOT_YET_CALLED) - set(found))
    assert not called, f"called now, so drop from NOT_YET_CALLED: {called}"
    missing = [name for name in found if name not in NOT_YET_CALLED]
    assert not missing, "\n".join(f"{name} (tested by {', '.join(names[name])}) is called nowhere in the game"
                                  for name in missing)


@pytest.mark.parametrize("where", ["call", "pointer"])
def test_negative_control(where):
    """a function whose only caller is removed is caught, whether it was called or taken as a pointer"""
    lines = ["static int under_test(int value)", "{", "\treturn value;", "}"]
    caller = "\treturn under_test(1);" if where == "call" else "\tconst handler_t handler = under_test;"
    whole = "\n".join(lines + ["int caller(void)", "{", caller, "}"])
    assert uncalled(["under_test"], [whole]) == []
    assert uncalled(["under_test"], [whole.replace(caller, "\treturn 0;")]) == ["under_test"]
    assert uncalled(["under_test"], ["/* under_test(1); */\n" + "\n".join(lines)]) == ["under_test"]
