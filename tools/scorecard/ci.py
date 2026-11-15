#!/usr/bin/env python3
"""The scorecard's CI step (README.md, .github/workflows/build.yml's linux-x86_64 job), on the debug build:

1. StageKit's tests (tools/stagekit/tests);
2. the scorecard's comparisons themselves: every scenario played twice at 30 Hz must measure the same;
3. the scorecard at 30:60, 30:120 and 60:120: reported, and failing CI only for the tags in enforced.txt (the
   coordinator's: a unit's tags once it has landed), so that the rates the swarm has not converted yet do not stop
   every branch; holdouts too, when the SCORECARD_HOLDOUTS_JSON secret has them (their measures not shown);
4. the known bugs, each of which must be caught;
5. two runs of this build alike, tick for tick (identity.py --repeat), and an unseeded draw caught (its negative
   control);
6. the 30 Hz gate: on a main branch's full run (CI_SCORECARD_RECORD=1), this build's determinism logs recorded as
   the baseline (BASELINE, which the workflow keeps in its cache); elsewhere, this build against the latest one
   (none yet: a note).

    python tools/scorecard/ci.py

Exit status: 0 when every hard check passed, 1 otherwise.
"""

import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BASELINE = ROOT / ".scorecard-baseline"


def step(title, command, hard=True, expect=0):
    """a check run; expect, the exit status that passes it (a negative control's: 1, the check failing)"""
    print(f"\n=== {title}\n$ {' '.join(command)}", flush=True)
    started = time.monotonic()
    status = subprocess.run(command, cwd=ROOT).returncode
    print(f"--- {title}: {'passed' if status == expect else 'FAILED' if hard else 'failed (reported only)'} "
          f"({time.monotonic() - started:.1f} s)", flush=True)
    return status == expect or not hard


def main():
    python = sys.executable
    scorecard = [python, str(HERE / "scorecard.py"), "--no-retail"]
    enforced = [line.split("#")[0].strip() for line in (HERE / "enforced.txt").read_text().splitlines()]
    enforced = [tag for tag in enforced if tag]
    holdouts = []
    if os.environ.get("SCORECARD_HOLDOUTS_JSON"):
        folder = Path(tempfile.mkdtemp(prefix="holdouts-"))
        (folder / "holdouts.json").write_text(os.environ["SCORECARD_HOLDOUTS_JSON"])
        holdouts = ["--holdouts", str(folder)]
    results = [
        step("StageKit", [python, "-m", "pytest", "-q", "-rs", "tools/stagekit/tests"]),
        step("scorecard, 30 against 30", scorecard + holdouts + ["--pairs", "30:30"]),
        step("scorecard, 30, 60 and 120", scorecard + holdouts, hard=False),
    ]
    if enforced:
        results.append(step(f"scorecard, enforced ({', '.join(enforced)})",
                            scorecard + holdouts + ["--tags", ",".join(enforced)]))
    results.append(step("known bugs caught", scorecard + ["--mutation-checks"]))
    identity = [python, str(HERE / "identity.py")]
    results.append(step("determinism: two runs alike", identity + ["--repeat"]))
    results.append(step("determinism: an unseeded draw caught", identity + ["--repeat", "--mutation", "unseeded"],
                        expect=1))
    if os.environ.get("CI_SCORECARD_RECORD") == "1":
        results.append(step("30 Hz gate: master's baseline recorded", identity + ["--record", str(BASELINE)]))
    else:
        results.append(step("30 Hz gate: bit-identical to master", identity + ["--baseline", str(BASELINE),
                                                                              "--missing-ok"]))
    # (b30's AI against the original code's seeds: on a runner with the retail maps, HALO_RETAIL_MAPS; skipped
    # without them, as on GitHub's)
    results.append(step("campaign 30 Hz gate: b30's AI as the original's",
                        [python, str(HERE / "campaign_identity.py")]))
    print(f"\nscorecard CI: {'passed' if all(results) else 'FAILED'}")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
