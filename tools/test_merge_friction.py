"""The generated menus' merges (tools/menus_regen.sh, tools/git_setup.sh,
.gitattributes): the files marked merge=menus-generated are the ones the
script writes again, a merge with the driver set up conflicts in none of
them, and --resolve refuses while a source is still conflicted"""

import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MENUS = "port/assets/menus"


def generated_in_script():
    text = (ROOT / "tools/menus_regen.sh").read_text()
    return set(re.search(r"^GENERATED=\(([^)]*)\)", text, re.M).group(1).split())


def generated_in_attributes():
    marked = set()
    for line in (ROOT / ".gitattributes").read_text().splitlines():
        if "merge=menus-generated" in line:
            path = line.split()[0]
            assert path.startswith(MENUS + "/")
            marked.add(path[len(MENUS) + 1:].removesuffix("/**"))
    return marked


def test_attributes_mark_what_the_script_writes():
    assert generated_in_script() == generated_in_attributes()
    # (and the hand-made ones are not marked: the redraws, the hand-written
    # screens, the stills, the documentation)
    for made_by_hand in ("svg", "screens", "stills", "README.md", "STYLE.md"):
        assert made_by_hand not in generated_in_script()
        assert (ROOT / MENUS / made_by_hand).exists()


def git(repo, *args, check=True):
    return subprocess.run(["git", "-C", str(repo), "-c", "user.name=t", "-c", "user.email=t@invalid", *args],
                          check=check, capture_output=True, text=True)


def two_branches(tmp_path, source_clash):
    """a repository whose branches a and b both changed a generated file (and,
    if source_clash, the same line of a source)"""
    repo = tmp_path / "repo"
    (repo / "tools").mkdir(parents=True)
    (repo / MENUS / "ce").mkdir(parents=True)
    shutil.copy(ROOT / "tools/menus_regen.sh", repo / "tools")
    shutil.copy(ROOT / "tools/git_setup.sh", repo / "tools")
    shutil.copy(ROOT / ".gitattributes", repo)
    (repo / MENUS / "ce/strings.xml").write_text("one\ntwo\n")
    (repo / "tools/port_settings.py").write_text("ROW = 'base'\n")
    git(repo, "init", "-q", "-b", "base")
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", "base")
    for branch, text in (("a", "one A\ntwo\n"), ("b", "one B\ntwo\n")):
        git(repo, "checkout", "-q", "-b", branch, "base")
        (repo / MENUS / "ce/strings.xml").write_text(text)
        if source_clash:
            (repo / "tools/port_settings.py").write_text(f"ROW = '{branch}'\n")
        git(repo, "commit", "-q", "-am", branch)
    return repo


def test_the_driver_takes_our_side_without_a_conflict(tmp_path):
    repo = two_branches(tmp_path, source_clash=False)
    # (the driver alone: the hook, which writes the menus again, needs the
    # PC tags; tools/menus_regen.sh --check in CI is its safety net)
    git(repo, "config", "merge.menus-generated.driver", "true")
    merged = git(repo, "merge", "--no-edit", "--no-verify", "a", check=False)
    assert merged.returncode == 0, merged.stdout + merged.stderr
    assert (repo / MENUS / "ce/strings.xml").read_text() == "one B\ntwo\n"


def test_without_the_driver_git_conflicts_as_before(tmp_path):
    """negative control: the attribute without git_setup.sh's driver merges
    as any file does"""
    repo = two_branches(tmp_path, source_clash=False)
    merged = git(repo, "merge", "--no-edit", "a", check=False)
    assert merged.returncode != 0
    assert "strings.xml" in git(repo, "diff", "--name-only", "--diff-filter=U").stdout


def test_resolve_waits_for_the_sources(tmp_path):
    repo = two_branches(tmp_path, source_clash=True)
    git(repo, "merge", "--no-edit", "a", check=False)
    resolved = subprocess.run([str(repo / "tools/menus_regen.sh"), "--resolve"], capture_output=True, text=True,
                              env={"PATH": "/usr/bin:/bin:/usr/local/bin:/opt/homebrew/bin",
                                   "MENU_INPUTS": str(tmp_path / "no-inputs")})
    assert resolved.returncode == 1
    assert "tools/port_settings.py" in resolved.stderr
    assert "strings.xml" not in resolved.stderr
    assert not (tmp_path / "no-inputs").exists()  # (nothing fetched)


def test_git_setup_installs_the_driver_and_hook_once(tmp_path):
    repo = two_branches(tmp_path, source_clash=False)
    for _ in range(2):
        subprocess.run([str(repo / "tools/git_setup.sh")], check=True, capture_output=True)
    assert git(repo, "config", "merge.menus-generated.driver").stdout.strip() == "true"
    hook = (repo / ".git/hooks/post-merge").read_text()
    assert hook.count("--after-merge") == 2  # (one line: its grep and its run)
