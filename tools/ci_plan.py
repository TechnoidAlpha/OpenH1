#!/usr/bin/env python3
"""What a run of the Build workflow (.github/workflows/build.yml) does: its
changes job runs this and passes the answer to every other job.

Three modes (docs/releasing.md):

- quick, a pull request's check: Linux x86-64's debug build, the tests the
  changed files touch (always a small core set; every test when the build
  or the test infrastructure changed), the scorecard when C changed, and
  another platform's debug build only when the pull request touches that
  platform's files (or its newest commit asks: [mac], [ios], [windows],
  [android], [arm64], [all]);
- full, a push to a main branch (master; dev too until it goes) or a run
  started by hand without a version: every platform and every test, the
  release configurations too, no release;
- release, a pushed tag build-<N> or a run started by hand with a version:
  the full suite, signed and notarized, published as release build-<N>
  (unless a dry run: then packaged, uploaded as an artifact, and nothing
  published).

    ci_plan.py [--event E] [--ref R] [--sha S] [--base B] [--version N] [--dry-run]
               [--message M] [--latest N]
    ci_plan.py --select FILE...      the quick check's tests for these changed files

Prints key=value lines (GITHUB_OUTPUT's form). A push of documentation
alone (Markdown, website/) builds nothing.
"""

import argparse
import json
import math
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ci_test_shards  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MAIN_BRANCHES = ("refs/heads/master", "refs/heads/dev")

# The tests that need no build, the tests job's (in shards, tools/ci_test_shards.py)
NO_BUILD_TESTS = [
    "tools/test_host_units.py", "tools/test_host_pages.py", "tools/test_android_elf_alignment.py",
    "tools/test_guest_image_check.py", "tools/test_check_host_imports.py", "tools/test_host_syscall.py",
    "tools/test_guest_layout.py", "tools/test_linux_guest_build.py", "tools/test_build_errors.py",
    "tools/test_guest_toolchain.py", "tools/test_linux_port.py", "tools/test_join_page.py",
    "tools/test_linux_package.py", "tools/test_install_layout.py", "tools/test_android_asm_convert.py",
    "tools/test_guest_asm_cache.py", "tools/test_guest_branch_relax.py", "tools/test_host_watch_hash.py",
    "tools/test_touch_menu.py", "tools/harness", "tools/test_controller_remap.py",
    "tools/test_controller_remap_context.py", "tools/test_net_compare.py", "tools/test_loading_ring.py",
    "tools/test_preparation.py", "tools/test_progress_ownership.py", "tools/test_ci_test_shards.py",
    "tools/test_supersampling.py", "tools/test_android_resolution.py", "tools/test_screen_passes_samples.py",
    "tools/test_notes.py", "tools/test_menu_look.py", "tools/test_mcc_local_package.py", "tools/test_mcc_server.py",
    "tools/test_mcc_share_menu.py", "tools/test_mcc_resource_selection.py", "tools/test_mcc_identity_candidates.py",
    "tools/test_mcc_cache_format.py", "tools/test_mcc_projectiles.py", "tools/test_mcc_tag_validation.py",
    "tools/test_mcc_share.py", "tools/test_mcc_share_rules.py", "tools/test_mcc_share_stage.py",
    "tools/test_mcc_share_storage.py", "tools/test_mcc_share_metadata.py", "tools/test_mcc_load_network.py",
    "tools/test_checkpoint_epoch.py", "tools/test_coop_checkpoint.py", "tools/test_coop_checkpoint_recovery.py",
    "tools/test_mcc_checkpoint.py", "tools/test_mcc_keyboard_glyphs.py", "tools/test_controller_buttons.py",
    "tools/test_hud_button_style.py", "tools/test_hud_corrections.py", "tools/test_ci_plan.py",
]

# The tests that read linux-x86_64's debug build, that job's (its Start the build's tests step)
BUILD_TESTS = [
    "tools/test_profile.py", "tools/test_host_units.py::test_host_screenshot", "tools/test_light_storage.py",
    "tools/test_vulkan_passes.py", "tools/test_host_vr_vulkan.py", "tools/test_vr_vehicles.py",
    "tools/test_vr_effects.py", "tools/test_host_texture_idle.py", "tools/test_hdr.py", "tools/test_hdr_backends.py",
    "tools/test_screen_passes_samples.py", "tools/test_ambient_occlusion_light.py",
]

# always run by the quick check: fast, and across the engine, the host and the CI's own tools
CORE_TESTS = [
    "tools/test_ci_plan.py", "tools/test_ci_test_shards.py", "tools/test_host_units.py",
    "tools/test_check_host_imports.py", "tools/test_guest_layout.py",
    "tools/harness/tests/test_client_build.py", "tools/harness/tests/test_config_write.py",
    "tools/harness/tests/test_release_signature.py", "tools/harness/tests/test_update_names.py",
    "tools/harness/tests/test_sim_rate.py", "tools/harness/tests/test_game_rules.py",
]
CORE_BUILD_TESTS = ["tools/test_host_units.py::test_host_screenshot"]

# what changes everything: the build system and the test infrastructure (every test, every platform)
INFRA = re.compile(r"^(\.github/|configure\.py$|tools/(build_common|ci_build|ci_plan|ci_test_shards|guest_toolchain"
                   r"|android_build|linux_guest_build|x86_game_abi)\.py$|tools/harness/(?!tests/)|(.*/)?conftest\.py$"
                   r"|(pytest|setup)\.(ini|cfg)$|pyproject\.toml$|port/third_party/)")
DOCS = re.compile(r"(\.md$|^website/)")
CODE = re.compile(r"\.(c|h|inc|cpp|m|mm)$")

# a platform's own files, and the macros that mark a shared C file's part for it
PLATFORMS = {
    "windows": (re.compile(r"^(port/windows/|tools/(windows_|wine_|test_windows))"),
                re.compile(r"\b(HALO_WINDOWS|_WIN32)\b")),
    "android": (re.compile(r"^(port/android/|tools/(android_|touch_|test_android|test_touch|guest_asm_cache"
                           r"|guest_branch_relax|guest_base_register))"),
                re.compile(r"\b(HALO_ANDROID|__ANDROID__)\b")),
    "linux_arm64": (re.compile(r"^(port/linux/frame/|tools/(frame_|test_frame|guest_asm_cache|android_asm_convert"
                               r"|guest_branch_relax|guest_base_register))"),
                    re.compile(r"\b(__aarch64__|HALO_ARM64)\b")),
    # (macOS: what its app builds beyond the arm64_32 guest, which linux-arm64 and Android build: the Apple and
    # Metal files, the renderer's stream that the Metal backend reads)
    "macos": (re.compile(r"^(port/(macos|ios)/|tools/(macos_|ios_|metal_|test_macos|test_metal))|\.(m|mm|metal)$"
                         r"|[Mm]etal|xgpu"),
              re.compile(r"\b(HALO_MACOS|__APPLE__)\b")),
    # (iOS: what is iOS's or Apple's alone, and the touch controls)
    "ios": (re.compile(r"^(port/(ios|macos)/|tools/(ios_|macos_|test_ios))|[Tt]ouch"),
            re.compile(r"\b(HALO_IOS|TARGET_OS_IOS)\b")),
}
OPT_IN = {"windows": "[windows]", "android": "[android]", "linux_arm64": "[arm64]", "macos": "[mac]",
          "ios": "[ios]"}

SHARD_SECONDS = 180  # the most a quick check's test shard is given, by the recorded times (one processor's:
# a shard runs its files on the runner's four, about a minute)
SHARDS = 4


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, check=True).stdout


def test_files(paths):
    return ci_test_shards.test_files(paths)


def times():
    path = ROOT / "tools/ci_test_times.json"
    return json.loads(path.read_text()) if path.exists() else {}


def read(path):
    try:
        return (ROOT / path).read_text(errors="replace")
    except OSError:
        return ""


TOKEN = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_./-]*")


def tokens(text):
    """the words of a text as a test names files: each path, and each of its parts (a file's name, a module's
    stem)"""
    found = set()
    for token in TOKEN.findall(text):
        token = token.rstrip("./-")
        found.add(token)
        for part in token.split("/"):
            found.add(part)
            found.add(part.split(".")[0])
    return found


def keys(changed):
    """the words a test that reads or exercises the changed file has: its path, its name, a Python module's or a
    harness case's stem (a harness test builds its case's C by name: build("back_pause", ...))"""
    path = Path(changed)
    found = {changed, path.name}
    if path.suffix == ".py" or changed.startswith("tools/harness/tests/"):
        found.add(path.stem)
    return found


_INCLUDES = None


def includers(header):
    """the C files that include the header (one level)"""
    global _INCLUDES
    if _INCLUDES is None:
        _INCLUDES = {}
        pattern = re.compile(r'#\s*include\s*[<"](?:[^">]*/)?([^">/]+)[">]')
        for folder in ("port", "source"):
            for file in (ROOT / folder).rglob("*"):
                if file.suffix in (".c", ".h", ".inc", ".m", ".mm", ".cpp") and "third_party" not in file.parts:
                    name = file.relative_to(ROOT).as_posix()
                    for included in set(pattern.findall(read(name))):
                        _INCLUDES.setdefault(included, []).append(name)
    return _INCLUDES.get(Path(header).name, [])


def select(changed, candidates, core):
    """the candidates (test files, or file::test) the changed files touch, and the core set: a candidate that is
    itself changed, or that names a changed file (keys), or a file that includes a changed header; a code file no
    candidate names brings in the candidates that name its folder's other files (erring toward running more)"""
    words = {candidate: tokens(read(candidate.split("::")[0])) for candidate in candidates}
    chosen = set(core) & set(candidates)

    def naming(names):
        return {candidate for candidate, found in words.items() if found & names}

    for file in changed:
        if DOCS.search(file):
            continue
        hits = {candidate for candidate in candidates if candidate.split("::")[0] == file}
        names = keys(file)
        if file.endswith(".h"):
            for includer in includers(file):
                names |= keys(includer)
        hits |= naming(names)
        if not hits and (CODE.search(file) or file.endswith(".py")):
            folder = ROOT / Path(file).parent
            neighbours = set()
            if folder.is_dir():
                for path in folder.iterdir():
                    if path.is_file() and not DOCS.search(path.name):
                        neighbours |= keys(path.relative_to(ROOT).as_posix())
            neighbours.discard(Path(file).name)
            hits |= naming(neighbours)
        chosen |= hits
    return sorted(chosen)


def platform_wanted(name, changed, message):
    files, macro = PLATFORMS[name]
    if OPT_IN[name] in message or "[all]" in message:
        return True
    for file in changed:
        if files.search(file):
            return True
        if CODE.search(file) and macro.search(read(file)):
            return True
    return False


def shard_plan(files, everything):
    """how many shards the tests get: four for every test, else enough for SHARD_SECONDS each by the recorded
    times"""
    if everything:
        return SHARDS
    recorded = times()
    known = list(recorded.values())
    default = sum(known) / len(known) if known else 1.0
    total = sum(recorded.get(file, default) for file in files)
    return max(1, min(SHARDS, math.ceil(total / SHARD_SECONDS)))


def quick(changed, message):
    """the quick check's plan for the changed files"""
    plan = {}
    code = [file for file in changed if not DOCS.search(file)]
    everything = any(INFRA.search(file) for file in code)
    if everything:
        print("the build system or the test infrastructure changed: every test, every platform", file=sys.stderr)
        tests = " ".join(NO_BUILD_TESTS)
        build_tests = " ".join(BUILD_TESTS)
        files = None
    else:
        candidates = test_files(NO_BUILD_TESTS)
        files = select(code, candidates, CORE_TESTS)
        tests = " ".join(files)
        build_tests = " ".join(select(code, BUILD_TESTS, CORE_BUILD_TESTS))
    shards = shard_plan(files or [], everything)
    plan["tests"] = tests
    plan["shards"] = json.dumps(list(range(1, shards + 1)))
    plan["shard_count"] = str(shards)
    plan["build_tests"] = build_tests
    plan["scorecard"] = str(everything or any(CODE.search(file) or file.startswith(("tools/scorecard/",
                                                                                     "tools/stagekit/"))
                                              for file in code)).lower()
    for name in PLATFORMS:
        plan[name] = str(everything or platform_wanted(name, code, message)).lower()
    return plan


def full():
    plan = {"tests": " ".join(NO_BUILD_TESTS), "shards": json.dumps(list(range(1, SHARDS + 1))),
            "shard_count": str(SHARDS), "build_tests": " ".join(BUILD_TESTS), "scorecard": "true"}
    plan.update({name: "true" for name in PLATFORMS})
    return plan


def changed_files(base, head):
    try:
        if base and not set(base) <= {"0"}:
            git("cat-file", "-e", f"{base}^{{commit}}")
            text = git("-c", "core.quotePath=false", "diff", "--name-only", "--no-renames", base, head)
            return [line for line in text.splitlines() if line]
    except subprocess.CalledProcessError:
        pass
    return None


def plan(args):
    event, ref = args.event, args.ref
    result = {"mode": "full", "build": "true", "number": "", "publish": "false", "dry_run": "false",
              "save": "false", "full": "true", "release": "false"}
    tag = re.fullmatch(r"refs/tags/build-([0-9]+)", ref or "")
    if event == "pull_request":
        result["mode"] = "quick"
        result["full"] = "false"
        base = git("merge-base", args.base, args.sha).strip() if args.base else ""
        changed = changed_files(base, args.sha)
        if changed is None:
            print("no base to compare with: the whole check", file=sys.stderr)
            result.update(full())
            result["mode"] = "quick"
            result["full"] = "false"
        else:
            print(f"changed from {base[:12]}:", *(f"  {file}" for file in changed), sep="\n", file=sys.stderr)
            result.update(quick(changed, args.message or ""))
            if not any(not DOCS.search(file) for file in changed):
                result["build"] = "false"
        return result

    result.update(full())
    if tag or (event == "workflow_dispatch" and args.version):
        result["mode"] = "release"
        result["release"] = "true"
        number = tag.group(1) if tag else args.version
        if not number.isdigit() or int(number) <= 0:
            sys.exit(f"::error::{number!r} is not a build number")
        result["number"] = str(int(number))
        latest = args.latest or 0
        publish = bool(tag) or (event == "workflow_dispatch" and ref in MAIN_BRANCHES and not args.dry_run)
        if publish and int(number) <= latest:
            sys.exit(f"::error::build-{number} is not newer than the latest release, build-{latest}: the "
                     "self-updaters would not take it")
        if not publish:
            print(f"dry run: build-{number} is built and packaged, and nothing is published", file=sys.stderr)
        result["publish"] = str(publish).lower()
        result["dry_run"] = str(not publish).lower()
        return result

    # a push to a main branch, or a run started by hand: the full suite, the compile caches saved for the
    # pull requests' checks
    result["save"] = "true"
    if event == "push" and ref in MAIN_BRANCHES:
        changed = changed_files(args.base, args.sha)
        if changed is not None:
            print(f"changed from {args.base[:12]}:", *(f"  {file}" for file in changed), sep="\n", file=sys.stderr)
            if not any(not DOCS.search(file) for file in changed):
                result["build"] = "false"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--event", default=os.environ.get("GITHUB_EVENT_NAME", ""))
    parser.add_argument("--ref", default=os.environ.get("GITHUB_REF", ""))
    parser.add_argument("--sha", default=os.environ.get("GITHUB_SHA", "HEAD"))
    parser.add_argument("--base", default="", help="a pull request's base, or a push's before")
    parser.add_argument("--version", default="", help="a run started by hand: the release's number")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--message", default="", help="the newest commit's message ([mac], [windows]...)")
    parser.add_argument("--latest", type=int, default=0, help="the latest release's number")
    parser.add_argument("--select", nargs="+", metavar="FILE", help="the quick check's plan for these files")
    parser.add_argument("--all-tests", action="store_true", help="every test that needs no build, one line")
    parser.add_argument("--all-build-tests", action="store_true", help="every test of the build, one line")
    args = parser.parse_args()
    if args.all_tests or args.all_build_tests:
        print(" ".join(NO_BUILD_TESTS if args.all_tests else BUILD_TESTS))
        return
    result = quick(args.select, args.message) if args.select else plan(args)
    for key, value in result.items():
        print(f"{key}={value}")


if __name__ == "__main__":
    main()
