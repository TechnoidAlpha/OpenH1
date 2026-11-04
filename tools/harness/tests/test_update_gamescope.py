"""The self-updater (updater.c, with posix_update.c's files and processes) in a Linux release's tree, updating to a
local fake release: under gamescope (the Steam Deck's Game Mode) it downloads in the background and asks in the game's
menus at the main menu, never with a system dialog; UPDATE puts the new files in place (the old ones as <name>.old)
and starts the new bin/openh1 in the same process, of the same parent (what Steam waits for), with none of the old
game's files; NOT NOW changes nothing, and the next start deletes the download. update.auto = false and
debug.update_answer still hold, and the desktop still asks with its dialog.

Compiled for this computer (update_gamescope.c), not the game's 32-bit ABI, and run on Linux only (/proc, exec)."""

import os
import platform
import subprocess
import sys
import zipfile
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import HARNESS, ROOT, function, mutated, read, structure  # noqa: E402

sys.path.insert(0, str(ROOT / "tools"))
import linux_package  # noqa: E402

pytestmark = pytest.mark.skipif(sys.platform != "linux", reason="the restart is Linux's (/proc, exec)")

BUILD = 7
ASSET = "OpenH1-Linux-ARM64.zip" if platform.machine() in ("aarch64", "arm64") else "OpenH1-Linux.zip"
ZLIB = ROOT / "port/third_party/zlib"
POSIX_FUNCTIONS = ["update_executable_path", "update_replace_file", "update_make_executable", "update_delete_file",
                   "update_make_directory", "update_launch", "update_restart"]
GAMESCOPE = ("GAMESCOPE_WAYLAND_DISPLAY", "XDG_CURRENT_DESKTOP", "SteamDeck", "SteamGamepadUI")

# the new version's bin/openh1: it notes the process it runs in, its parent and its files
NEW_GAME = """#!/bin/sh
files=$(for file in /proc/$$/fd/*; do readlink "$file"; done | tr '\\n' ' ')
echo "new: process $$ parent $PPID files $files" >> "$FAKE_TREE/started.txt"
"""
OLD = {"bin/openh1": "#!/bin/sh\necho old >> \"$FAKE_TREE/started.txt\"\n", "lib/openh1/libSDL3.so.0": "old SDL",
       "share/openh1/brokers.txt": "old brokers\n"}
NEW = {"bin/openh1": NEW_GAME, "lib/openh1/libSDL3.so.0": "new SDL", "share/openh1/brokers.txt": "new brokers\n",
       "share/openh1/added.txt": "a file the old version had not\n"}

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # no question under gamescope, as before
    "no-question-under-gamescope": ("updater", ("\tif (updater_in_game)\n\t{\n\t\tif (!pc_menu_update_question_open())",
                                           "\tif (0)\n\t{\n\t\tif (!pc_menu_update_question_open())"),
                               "test_not_now"),
    # the new version started as a process of its own, as on the desktop
    "restart-in-another-process": ("updater", ("\t\t\tupdate_restart(launch);", "\t\t\tupdate_launch(launch);"),
                                   "test_update"),
    # the old game's files left open in the new one
    "files-kept": ("posix", ("fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC);", "(void)flags;"), "test_update"),
}


def generated(fault=None):
    updater = read("port/linux/src/updater.c")
    posix = read("port/linux/src/posix_update.c")
    posix = "\n\n".join(function(posix, name) for name in POSIX_FUNCTIONS)
    if fault:
        where, change, _ = NEGATIVE_CONTROLS[fault]
        if where == "updater":
            updater = mutated(updater, *change)
        else:
            posix = mutated(posix, *change)
    return {
        "under_test.inc": posix + "\n\n" + updater,
        "update.h": read("port/linux/src/update.h"),
        "release_signature.h": read("port/linux/src/release_signature.h"),
        "p2p_internal.h": "#ifndef TEST_P2P_INTERNAL\n#define TEST_P2P_INTERNAL\n" +
        structure(read("port/linux/src/p2p_internal.h"), "p2p_sha256") + "\n"
        "void p2p_sha256_begin(struct p2p_sha256 *context);\n"
        "void p2p_sha256_add(struct p2p_sha256 *context, const void *data, int size);\n"
        "void p2p_sha256_end(struct p2p_sha256 *context, unsigned char *digest);\n#endif\n",
        # (update_gamescope.c's fakes are declared before under_test.inc)
        "SDL3/SDL.h": "", "platform.h": "", "install_layout.h": "", "port_config.h": "",
    }


_built = {}


def build(tmp_path_factory, fault=None):
    if fault in _built:
        return _built[fault]
    work = tmp_path_factory.mktemp(f"update_gamescope-{fault or 'as-is'}")
    for name, text in generated(fault).items():
        (work / name).parent.mkdir(parents=True, exist_ok=True)
        (work / name).write_text(text, encoding="latin-1")
    compiler = os.environ.get("CC", "clang")
    objects = []
    for source in ("adler32.c", "crc32.c", "inffast.c", "inflate.c", "inftrees.c", "zutil.c"):
        objects.append(work / f"{source}.o")
        subprocess.run([compiler, "-c", "-O1", "-w", "-DZ_PREFIX", "-I", str(ZLIB), str(ZLIB / source), "-o",
                        str(objects[-1])], check=True)
    executable = work / "update_gamescope"
    result = subprocess.run([compiler, "-std=gnu99", "-O1", "-Wall", "-Werror", "-Wno-unused-function",
                             f"-DHALO_BUILD_NUMBER={BUILD}", "-I", str(HARNESS / "include"), "-I", str(work), "-I",
                             str(ZLIB), str(HARNESS / "tests/update_gamescope.c"), *map(str, objects), "-o",
                             str(executable), "-lpthread"], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"update_gamescope.c does not compile:\n{result.stderr}")
    _built[fault] = executable
    return executable


def fake_release(folder):
    """the new release's zip, as tools/linux_package.py writes a release's"""
    tree = folder / "new-tree"
    for name, text in NEW.items():
        (tree / name).parent.mkdir(parents=True, exist_ok=True)
        (tree / name).write_text(text)
    (tree / "bin/openh1").chmod(0o755)
    archive = folder / ASSET
    linux_package.write_zip(tree, archive)
    with zipfile.ZipFile(archive) as opened:
        assert opened.getinfo("bin/openh1").compress_type == zipfile.ZIP_DEFLATED
    return archive


def installed_tree(folder):
    """the installed game (~/Applications/OpenH1, as install.sh puts it)"""
    tree = folder / "OpenH1"
    for name, text in OLD.items():
        (tree / name).parent.mkdir(parents=True, exist_ok=True)
        (tree / name).write_text(text)
    (tree / "bin/openh1").chmod(0o755)
    return tree


def start(executable, tree, release, gamescope=True, latest=BUILD + 1, **fakes):
    """the game started (as Steam starts it): (its process id, its output)"""
    environment = {key: value for key, value in os.environ.items() if key not in GAMESCOPE}
    if gamescope:
        environment["GAMESCOPE_WAYLAND_DISPLAY"] = "gamescope-0"
    environment.update(FAKE_TREE=str(tree), FAKE_RELEASE=str(release), FAKE_LATEST=str(latest),
                       **{f"FAKE_{key.upper()}": value for key, value in fakes.items()})
    process = subprocess.Popen([str(executable)], env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True)
    output, _ = process.communicate(timeout=60)
    assert process.returncode == 0, output
    return process.pid, output


def contents(tree):
    """the tree's files but the download and the file the test's game holds"""
    return {path.relative_to(tree).as_posix(): path.read_text() for path in sorted(tree.rglob("*")) if path.is_file()
            and path.name != "held-by-the-old-game" and path.relative_to(tree).parts[0] != "update.partial"}


def lines_in_order(output, *lines):
    at = 0
    for line in lines:
        found = output.find(line, at)
        assert found >= 0, f"no {line!r} after the line before it in:\n{output}"
        at = found + len(line)


def check_update(executable, tmp_path, **fakes):
    tree, release = installed_tree(tmp_path), fake_release(tmp_path)
    pid, output = start(executable, tree, release, **fakes)
    assert "dialog" not in output and "made a window" not in output, output
    # the new version, in the same process, of the same parent, with none of the old game's files
    started = (tree / "started.txt").read_text().splitlines()
    assert len(started) == 1 and started[0].startswith(f"new: process {pid} parent {os.getpid()} files "), \
        f"{started}\n{output}"
    assert "held-by-the-old-game" not in started[0], started[0]
    assert "test: the game goes on" not in output, output
    # the new files in place, the old ones as <name>.old, which the new version deletes
    files = contents(tree)
    for name, text in NEW.items():
        assert files.pop(name) == text, name
    for name, text in OLD.items():
        assert files.pop(f"{name}.old") == text, name
    assert sorted(files.pop("update-old.txt").splitlines()) == sorted(f"{name}.old" for name in NEW)
    assert set(files) == {"started.txt"}, files
    assert not (tree / "update.partial").exists()
    assert os.access(tree / "bin/openh1", os.X_OK)
    return output


def test_update(tmp_path, tmp_path_factory):
    """UPDATE at the main menu: the release downloaded in the background, then the question in the game's menus at
    the main menu (not before), then the new version put in place and started in the same process"""
    output = check_update(build(tmp_path_factory), tmp_path, player="update")
    lines_in_order(output, "test: started", "test: download https://api.github.com/repos/OpenH1/OpenH1/releases/latest",
                   f"test: download https://github.com/OpenH1/OpenH1/releases/download/build-{BUILD + 1}/{ASSET}",
                   "test: asked for SHA256SUMS", "test: asked for SHA256SUMS.sig",
                   f"test: release check of {ASSET}: sums 0, signature 0, required 1: unsigned",
                   f"update: build {BUILD + 1} is downloaded",
                   "test: the update question opened over the main menu",
                   f"update: asking about build {BUILD + 1} at the main menu", "test: UPDATE",
                   f"update: updating to build {BUILD + 1} (UPDATE)", f"update: starting build {BUILD + 1}")
    assert output.count("UPDATE)") == 1, output


def test_refused(tmp_path, tmp_path_factory):
    """a download its release's check refuses (release_signature.c): deleted, never asked about, the tree as it was"""
    tree, release = installed_tree(tmp_path), fake_release(tmp_path)
    _, output = start(build(tmp_path_factory), tree, release, player="update", verdict="refused")
    assert f"update: build {BUILD + 1} was refused: a test's refusal" in output, output
    assert "update question" not in output and "is downloaded" not in output, output
    assert contents(tree) == OLD and not (tree / "update.partial").exists()


def test_answer_yes(tmp_path, tmp_path_factory):
    """debug.update_answer = "yes" under gamescope: updated without the question, in the same process"""
    output = check_update(build(tmp_path_factory), tmp_path, update_answer="yes")
    assert "update question" not in output, output
    assert "update: answering yes (debug.update_answer)" in output


def test_not_now(tmp_path, tmp_path_factory):
    """NOT NOW: the game goes on as it was, with no dialog; the next start deletes the download, and asks again"""
    executable = build(tmp_path_factory)
    tree, release = installed_tree(tmp_path), fake_release(tmp_path)
    _, output = start(executable, tree, release, player="not now")
    lines_in_order(output, f"update: build {BUILD + 1} is downloaded", "test: the update question opened over the main menu",
                   "test: NOT NOW", "test: the game goes on: 1 question(s), 0 dialog(s), 0 window(s)")
    assert contents(tree) == OLD
    assert (tree / "update.partial" / ASSET).read_bytes() == release.read_bytes()
    _, output = start(executable, tree, release, latest=BUILD)
    assert f"update: this is the latest build ({BUILD})" in output, output
    assert contents(tree) == OLD and not (tree / "update.partial").exists()


def test_update_auto_false(tmp_path, tmp_path_factory):
    """update.auto = false (Settings > Network Setup > Check for Updates, the desktop's "Do not ask again"): nothing
    looked for, nothing downloaded"""
    tree, release = installed_tree(tmp_path), fake_release(tmp_path)
    _, output = start(build(tmp_path_factory), tree, release, update_auto="false", player="update")
    assert "update: not looked for: update.auto is false" in output and "test: download" not in output, output
    assert contents(tree) == OLD


def test_answer_never(tmp_path, tmp_path_factory):
    """debug.update_answer = "never" under gamescope: update.auto = false written, nothing changed"""
    tree, release = installed_tree(tmp_path), fake_release(tmp_path)
    _, output = start(build(tmp_path_factory), tree, release, update_answer="never")
    assert "test: config.toml: update.auto = false" in output and "update question" not in output, output
    assert contents(tree) == OLD


def test_desktop(tmp_path, tmp_path_factory):
    """not under gamescope: the desktop's dialog, as before (answered No here), and no download before it"""
    tree, release = installed_tree(tmp_path), fake_release(tmp_path)
    _, output = start(build(tmp_path_factory), tree, release, gamescope=False, player="update")
    assert "test: a dialog: Halo: new version" in output and "update question" not in output, output
    assert f"/releases/download/" not in output, output
    assert contents(tree) == OLD and not (tree / "update.partial").exists()


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control, tmp_path, tmp_path_factory):
    test = NEGATIVE_CONTROLS[control][2]
    executable = build(tmp_path_factory, control)
    with pytest.raises(AssertionError):
        if test == "test_update":
            check_update(executable, tmp_path, player="update")
        else:
            tree, release = installed_tree(tmp_path), fake_release(tmp_path)
            _, output = start(executable, tree, release, player="not now")
            assert "test: the game goes on: 1 question(s), 0 dialog(s), 0 window(s)" in output, output
