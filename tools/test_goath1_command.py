"""goath1, the command players and scripts type (goath1 bots status), on every desktop (port/linux/src/cli_install.c):
the Linux release's bin/goath1 runs the bin/openh1 beside it, whether it is run by its path or through a link in a
folder on the PATH (as halo --install-cli or a package may make), with every argument as it was; Windows's goath1.cmd
runs the halo.exe beside it; halo --install-cli's script (cli_install_write) runs the game, replaces one it wrote and
leaves another program's goath1 alone."""

import os
import stat
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT))

import linux_package  # noqa: E402
from harness import function, read  # noqa: E402

GAME = "#!/bin/sh\nprintf '%s\\n' \"ran $0\" \"$@\" > \"$(dirname \"$0\")/../calls.txt\"\n"

pytestmark = pytest.mark.skipif(os.name == "nt", reason="the Unix launchers run under sh")


def tree_with(tmp_path, launcher_text):
    """a release's bin/: the launcher (its text) and a stand-in openh1 that notes its arguments"""
    bin_folder = tmp_path / "release" / "bin"
    bin_folder.mkdir(parents=True)
    (bin_folder / "openh1").write_text(GAME)
    (bin_folder / "goath1").write_text(launcher_text)
    for name in ("openh1", "goath1"):
        (bin_folder / name).chmod(0o755)
    return bin_folder


def calls(bin_folder):
    return (bin_folder.parent / "calls.txt").read_text().splitlines()


def through_a_link(tmp_path, bin_folder):
    """goath1 run as a folder on the PATH has it: a link to the release's"""
    path_folder = tmp_path / "on-path"
    path_folder.mkdir()
    (path_folder / "goath1").symlink_to(bin_folder / "goath1")
    return subprocess.run(["goath1", "bots", "order red defend", "red flag"], capture_output=True, text=True,
                          env={**os.environ, "PATH": f"{path_folder}{os.pathsep}{os.environ['PATH']}"})


def test_release_has_goath1_beside_openh1(tmp_path):
    build = tmp_path / "build"
    build.mkdir()
    for name in ("halo", "openh1.assets", "libSDL3.so.0"):
        (build / name).write_text(name)
    tree = tmp_path / "tree"
    linux_package.stage_tree(tree, build / "halo", build / "libSDL3.so.0", [], installer=False)
    assert (tree / "bin/goath1").read_bytes() == linux_package.COMMAND.read_bytes()
    assert (tree / "bin/openh1").read_text() == "halo"
    assert linux_package.mode("bin/goath1") == 0o755


def test_goath1_runs_openh1_with_its_arguments(tmp_path):
    bin_folder = tree_with(tmp_path, linux_package.COMMAND.read_text())
    result = subprocess.run([str(bin_folder / "goath1"), "bots", "status", "--json"], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert calls(bin_folder) == [f"ran {bin_folder / 'openh1'}", "bots", "status", "--json"]


def test_goath1_through_a_link_on_the_path(tmp_path):
    bin_folder = tree_with(tmp_path, linux_package.COMMAND.read_text())
    result = through_a_link(tmp_path, bin_folder)
    assert result.returncode == 0, result.stderr
    assert calls(bin_folder)[1:] == ["bots", "order red defend", "red flag"]


def test_goath1_through_a_link_negative_control(tmp_path):
    """(a launcher that takes its folder from $0 without following the link finds no openh1 beside the link)"""
    faulty = linux_package.COMMAND.read_text().replace('"$(readlink -f "$0")"', '"$0"')
    assert faulty != linux_package.COMMAND.read_text()
    bin_folder = tree_with(tmp_path, faulty)
    result = through_a_link(tmp_path, bin_folder)
    assert result.returncode != 0
    assert not (bin_folder.parent / "calls.txt").exists()


def test_windows_goath1_cmd():
    text = (ROOT / "port/assets/windows/goath1.cmd").read_bytes()
    assert b"\r\n" in text and b"\n" not in text.replace(b"\r\n", b"")
    assert b'"%~dp0halo.exe" %*' in text
    assert b"WINDOWS_COMMAND" in (ROOT / "tools/ci_build.py").read_bytes()


def install_writer(tmp_path, fault=None):
    """cli_install.c's cli_install_write, compiled alone"""
    code = function(read("port/linux/src/cli_install.c"), "cli_install_write")
    if fault:
        assert code.count(fault[0]) == 1
        code = code.replace(*fault)
    source = tmp_path / "writer.c"
    source.write_text("#include <errno.h>\n#include <stdio.h>\n#include <string.h>\n#include <sys/stat.h>\n"
                      '#define CLI_INSTALL_MARK "# goath1: GOAT H1\'s command line (halo --install-cli)"\n'
                      + code + "\nint main(int argc, char **argv)\n{\n\tchar path[1024], why[1024];\n"
                      "\tint ok = cli_install_write(argv[1], argv[2], path, sizeof(path), why, sizeof(why));\n"
                      "\tputs(ok ? path : why);\n\treturn ok ? 0 : 1;\n}\n")
    executable = tmp_path / "writer"
    subprocess.run([os.environ.get("CC", "cc"), "-Wall", "-Werror", str(source), "-o", str(executable)], check=True)
    return executable


def test_install_cli_writes_a_goath1_that_runs_the_game(tmp_path):
    writer = install_writer(tmp_path)
    bin_folder = tree_with(tmp_path, "")
    folder = tmp_path / "local-bin"
    folder.mkdir()
    result = subprocess.run([str(writer), str(folder), str(bin_folder / "openh1")], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout
    script = folder / "goath1"
    assert stat.S_IMODE(script.stat().st_mode) == 0o755
    subprocess.run([str(script), "bots", "help"], check=True)
    assert calls(bin_folder)[1:] == ["bots", "help"]
    # (again: its own, replaced)
    assert subprocess.run([str(writer), str(folder), str(bin_folder / "openh1")]).returncode == 0


def test_install_cli_leaves_another_goath1_alone(tmp_path):
    writer = install_writer(tmp_path)
    folder = tmp_path / "local-bin"
    folder.mkdir()
    (folder / "goath1").write_text("#!/bin/sh\necho someone else's\n")
    result = subprocess.run([str(writer), str(folder), "/opt/game/openh1"], capture_output=True, text=True)
    assert result.returncode == 1 and "not GOAT H1's" in result.stdout
    assert (folder / "goath1").read_text() == "#!/bin/sh\necho someone else's\n"
    assert subprocess.run([str(writer), str(folder), '/opt/a"b/openh1'], capture_output=True).returncode == 1


def test_install_cli_negative_control(tmp_path):
    """(a writer that does not look for its mark replaces another program's goath1)"""
    writer = install_writer(tmp_path, ("if (!strstr(existing, CLI_INSTALL_MARK))", "if (0)"))
    folder = tmp_path / "local-bin"
    folder.mkdir()
    (folder / "goath1").write_text("#!/bin/sh\necho someone else's\n")
    assert subprocess.run([str(writer), str(folder), "/opt/game/openh1"], capture_output=True).returncode == 0
    assert "someone else's" not in (folder / "goath1").read_text()
