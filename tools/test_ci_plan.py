"""tools/ci_plan.py: what each run of the Build workflow does (the quick check's tests and platforms, the full
suite, a release), and the workflow wired to it."""

import json
import re
from pathlib import Path

import pytest

from tools import ci_plan

ROOT = Path(__file__).resolve().parent.parent
WORKFLOW = (ROOT / ".github/workflows/build.yml").read_text()


def selected(plan):
    return set(plan["tests"].split())


def test_a_harness_source_brings_its_tests_and_the_core():
    plan = ci_plan.quick(["port/linux/src/xinput_sdl.c"], "")
    tests = selected(plan)
    assert "tools/harness/tests/test_back_pause.py" in tests  # (it reads xinput_sdl.c)
    assert set(ci_plan.CORE_TESTS) <= tests
    assert len(tests) < len(ci_plan.test_files(ci_plan.NO_BUILD_TESTS)) / 2
    assert plan["scorecard"] == "true" and plan["windows"] in ("true", "false")


def test_a_changed_test_and_its_case_run():
    tests = selected(ci_plan.quick(["tools/harness/tests/back_pause.c"], ""))
    assert "tools/harness/tests/test_back_pause.py" in tests
    assert "tools/harness/tests/test_back_pause.py" in selected(
        ci_plan.quick(["tools/harness/tests/test_back_pause.py"], ""))


def test_a_python_tool_brings_the_tests_that_import_it():
    tests = selected(ci_plan.quick(["tools/linux_package.py"], ""))
    assert "tools/test_linux_package.py" in tests
    assert "tools/test_install_layout.py" in tests or "tools/test_linux_package.py" in tests


def test_infrastructure_runs_everything():
    for file in (".github/workflows/build.yml", "configure.py", "tools/build_common.py", "tools/harness/__init__.py",
                 "tools/ci_test_shards.py"):
        plan = ci_plan.quick([file], "")
        assert plan["tests"] == " ".join(ci_plan.NO_BUILD_TESTS), file
        assert json.loads(plan["shards"]) == [1, 2, 3, 4]
        assert all(plan[name] == "true" for name in ci_plan.PLATFORMS), file


def test_platforms_only_for_their_files():
    plan = ci_plan.quick(["tools/harness/tests/test_back_pause.py"], "")
    assert not any(plan[name] == "true" for name in ci_plan.PLATFORMS)
    assert plan["scorecard"] == "false"
    assert ci_plan.quick(["port/windows/host/host_crash_win.c"], "")["windows"] == "true"
    assert ci_plan.quick(["port/android/app/build.gradle"], "")["android"] == "true"
    assert ci_plan.quick(["port/macos/presenter.m"], "")["macos"] == "true"
    # (a commit asks for one)
    assert ci_plan.quick(["tools/harness/tests/test_back_pause.py"], "Fix it [mac]")["macos"] == "true"


def test_documentation_alone_is_small():
    plan = ci_plan.quick(["README.md"], "")
    assert selected(plan) == set(ci_plan.CORE_TESTS) and plan["shards"] == "[1]"


@pytest.mark.parametrize("event, ref, version, dry, mode, publish", [
    ("push", "refs/tags/build-3000", "", False, "release", "true"),
    ("workflow_dispatch", "refs/heads/master", "3000", False, "release", "true"),
    ("workflow_dispatch", "refs/heads/master", "3000", True, "release", "false"),
    ("workflow_dispatch", "refs/heads/ci-branch", "3000", False, "release", "false"),
    ("workflow_dispatch", "refs/heads/ci-branch", "", False, "full", "false"),
    ("push", "refs/heads/master", "", False, "full", "false"),
])
def test_modes(event, ref, version, dry, mode, publish):
    args = type("Args", (), dict(event=event, ref=ref, sha="HEAD", base="", version=version, dry_run=dry,
                                  message="", latest=2534))
    plan = ci_plan.plan(args)
    assert plan["mode"] == mode and plan["publish"] == publish
    assert plan["number"] == ("3000" if mode == "release" else "")
    assert plan["save"] == ("true" if mode == "full" else "false")
    assert plan["release"] == ("true" if mode == "release" else "false")
    assert plan["dry_run"] == ("true" if mode == "release" and publish == "false" else "false")


def test_an_older_number_is_refused():
    args = type("Args", (), dict(event="push", ref="refs/tags/build-2000", sha="HEAD", base="", version="",
                                  dry_run=False, message="", latest=2534))
    with pytest.raises(SystemExit):
        ci_plan.plan(args)


def test_the_workflow_runs_the_plans_lists():
    """the workflow takes its tests from the plan, which lists every test file the old lists had"""
    assert "tools/ci_plan.py" in WORKFLOW
    assert "needs.changes.outputs.tests" in WORKFLOW and "needs.changes.outputs.build_tests" in WORKFLOW
    for test in ci_plan.BUILD_TESTS:
        assert (ROOT / test.split("::")[0]).exists(), test
    for test in ci_plan.NO_BUILD_TESTS + ci_plan.CORE_TESTS:
        assert (ROOT / test).exists(), test
    # (a pull request's run is cancelled by a newer one; master's and a tag's never)
    concurrency = WORKFLOW[WORKFLOW.index("\nconcurrency:"):WORKFLOW.index("\npermissions:")]
    assert "cancel-in-progress: ${{ github.event_name == 'pull_request' }}" in concurrency
    assert "github.run_id" in concurrency
    # (only a release run publishes, and only when not a dry run)
    publish = WORKFLOW[WORKFLOW.index("      - name: Publish\n"):]
    assert "PUBLISH: ${{ needs.changes.outputs.publish }}" in publish[:600]
    create = publish.index("gh release create")
    assert 'if [ "$PUBLISH" != true ]; then' in publish[:create] and "exit 0" in publish[:create]
    for step in ("      - name: Upload Windows symbols to Sentry\n", "      - name: Keep the last five\n"):
        assert "if: needs.changes.outputs.publish == 'true'" in WORKFLOW[WORKFLOW.index(step):][:200], step
