"""tools/netver.py: GOAT H1's network numbers given out at merge time, on
copies of the real headers"""

import re
import shutil
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import netver  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MESSAGES = netver.KINDS[0].path
BITS = netver.KINDS[1].path


@pytest.fixture
def tree(tmp_path):
    for path in [kind.path for kind in netver.KINDS] + [netver.LIMITS]:
        (tmp_path / path).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / path, tmp_path / path)
    return tmp_path


def entries(root, kind):
    return {entry.name: entry for entry in netver.enum_entries(kind, (root / kind.path).read_text().splitlines())}


def add_message(root, name, value, pending=True):
    """a message added at the end of the enum, as a branch adds one"""
    path = root / MESSAGES
    text = path.read_text()
    line = f"\t_distributed_message_{name} = {value},{' ' + netver.MARK if pending else ''}\n"
    path.write_text(text.replace("\n\tNUMBER_OF_DISTRIBUTED_MESSAGES", "\n" + line + "\n\tNUMBER_OF_DISTRIBUTED_MESSAGES", 1))


def largest_own(root, kind):
    values = [entry.value for entry in entries(root, kind).values()]
    return max(values)


def change(root, topic, text):
    (root / netver.CHANGES).mkdir(parents=True, exist_ok=True)
    (root / netver.CHANGES / f"{topic}.txt").write_text(text + "\n")


def test_real_tree_agrees():
    assert netver.check(ROOT) == []


def test_bump_gives_the_next_free_number_after_dev_took_the_branchs(tree):
    """two branches both took the next message; the first merged; the
    second's pending one moves past it, and the version is raised once"""
    top = largest_own(tree, netver.KINDS[0])
    version = netver.standard_version((tree / netver.LIMITS).read_text())
    add_message(tree, "first_branch", top + 1, pending=False)  # merged already
    add_message(tree, "second_branch", top + 1)
    add_message(tree, "second_branch_too", top + 2)
    change(tree, "second-branch", "the second branch's two messages (network_second.c)")
    clashes = netver.check(tree)
    assert any("second_branch has" in problem and problem.endswith(netver.PENDING_CLASH) for problem in clashes)

    done = netver.bump(tree)

    got = entries(tree, netver.KINDS[0])
    assert got["_distributed_message_first_branch"].value == top + 1
    assert got["_distributed_message_second_branch"].value == top + 2
    assert got["_distributed_message_second_branch_too"].value == top + 3
    assert not any(entry.pending for entry in got.values())
    assert netver.MARK not in (tree / MESSAGES).read_text()
    text = (tree / netver.LIMITS).read_text()
    assert netver.standard_version(text) == version + 1
    assert f"{version + 1}: the second branch's two messages (network_second.c)." in " ".join(
        netver.history(text).split())
    assert not list((tree / netver.CHANGES).glob("*.txt"))
    assert netver.check(tree, final=True) == []
    assert len(done) == 3


def test_classic_numbers_are_never_given_out(tree):
    """a pending mark on an OpenCE message (below 120) is refused, and bump
    leaves the classic numbers as they are"""
    path = tree / MESSAGES
    text = path.read_text()
    path.write_text(text.replace("_distributed_message_map_vote = 94,",
                                 "_distributed_message_map_vote = 94, " + netver.MARK, 1))
    change(tree, "vote", "a vote")
    problems = netver.check(tree)
    assert any("classic (OpenCE) number" in problem for problem in problems)
    with pytest.raises(netver.NetverError):
        netver.bump(tree)
    assert (tree / MESSAGES).read_text() == path.read_text()


def test_classic_entries_keep_their_numbers_through_a_bump(tree):
    before = {name: entry.value for name, entry in entries(tree, netver.KINDS[0]).items() if entry.value < 120}
    limits_before = (tree / netver.LIMITS).read_text()
    add_message(tree, "new", 200)
    change(tree, "new", "a new message")
    netver.bump(tree)
    after = {name: entry.value for name, entry in entries(tree, netver.KINDS[0]).items() if entry.value < 120}
    assert after == before
    # (and the Legacy games' version, OpenCE's, is not touched)
    legacy = re.compile(r"^#define HALO_PORT_NETWORK_VERSION .*$", re.M)
    assert legacy.search((tree / netver.LIMITS).read_text()).group() == legacy.search(limits_before).group()


def test_a_full_byte_of_bits_is_refused(tree):
    """a game record bit past 7 does not fit its byte"""
    kind = netver.KINDS[1]
    own = entries(tree, kind)
    first = netver.first_own(kind, list(own.values()))
    path = tree / BITS
    lines = path.read_text().splitlines(keepends=True)
    last = max(own.values(), key=lambda entry: entry.line)
    names = [f"_network_game_extra{index}_bit" for index in range(8)]
    lines[last.line + 1:last.line + 1] = [f"\t{name} = {first}, {netver.MARK}\n" for name in names]
    path.write_text("".join(lines))
    change(tree, "bits", "eight more switches")
    with pytest.raises(netver.NetverError, match="no game record bit free"):
        netver.bump(tree)


def test_dev_refuses_what_is_pending(tree):
    top = largest_own(tree, netver.KINDS[0])
    add_message(tree, "pending", top + 1)
    change(tree, "pending", "a message")
    assert netver.check(tree) == []
    final = netver.check(tree, final=True)
    assert any("still pending" in problem for problem in final)
    assert any("not given its version yet" in problem for problem in final)


def test_a_pending_number_needs_its_change_written(tree):
    top = largest_own(tree, netver.KINDS[0])
    add_message(tree, "unexplained", top + 1)
    assert any(netver.CHANGES in problem for problem in netver.check(tree))


def test_two_given_numbers_alike_are_an_error(tree):
    """negative control: two messages merged with one number (no marks) is
    not something bump may paper over"""
    top = largest_own(tree, netver.KINDS[0])
    add_message(tree, "one", top + 1, pending=False)
    add_message(tree, "two", top + 1, pending=False)
    problems = netver.check(tree)
    assert any("_distributed_message_two has" in problem and not problem.endswith(netver.PENDING_CLASH)
               for problem in problems)
    with pytest.raises(netver.NetverError):
        netver.bump(tree)


def test_an_upstream_raise_of_the_legacy_version_is_followed(tree):
    """a sync took OpenCE's new HALO_PORT_NETWORK_VERSION: check asks for a
    change, and bump raises the Standard version with its base"""
    path = tree / netver.LIMITS
    text = path.read_text()
    legacy = int(re.search(r"^#define HALO_PORT_NETWORK_VERSION (\d+)", text, re.M).group(1))
    path.write_text(re.sub(r"^(#define HALO_PORT_NETWORK_VERSION )\d+", rf"\g<1>{legacy + 1}", text, flags=re.M))
    assert any("upstream raised it" in problem for problem in netver.check(tree))
    change(tree, "sync", f"network version {legacy + 1}, OpenCE's")
    netver.bump(tree)
    text = path.read_text()
    assert re.search(rf"^#define HALO_PORT_STANDARD_BASE_VERSION {legacy + 1}$", text, re.M)
    assert netver.check(tree, final=True) == []
