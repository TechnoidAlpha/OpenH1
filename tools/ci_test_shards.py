#!/usr/bin/env python3
"""The tests that need no build, split into the shards of the workflow's
tests job (.github/workflows/build.yml), which run on runners of their own
beside the builds: every test file of the paths given (a folder's
test_*.py and *_test.py, as pytest collects them) in exactly one shard,
the shards balanced by each file's recorded time (ci_test_times.json; a
file without one counts as the recorded files' mean), the longest first
into the shard with the least so far. The same paths and times always
give the same split.

    ci_test_shards.py SHARD COUNT PATH...   the files of shard SHARD (1 to COUNT), one per line
    ci_test_shards.py --record JUNIT...     ci_test_times.json again, from the shards' JUnit XML

(the workflow's tests-junit-* artifacts, written with -o junit_family=xunit1,
whose test cases name their file)
"""

import argparse
import json
import sys
import xml.etree.ElementTree as ElementTree
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TIMES = Path(__file__).resolve().parent / "ci_test_times.json"


def test_files(paths):
    """the test files of the paths, relative to the repository, sorted"""
    files = set()
    for name in paths:
        path = ROOT / name
        if path.is_dir():
            found = [file for pattern in ("test_*.py", "*_test.py") for file in path.rglob(pattern)
                     if "__pycache__" not in file.parts]
            if not found:
                sys.exit(f"{name}: no test files")
            files.update(found)
        elif path.is_file():
            files.add(path)
        else:
            sys.exit(f"{name}: no such file or folder")
    return sorted(file.relative_to(ROOT).as_posix() for file in files)


def shards(files, count, times):
    """the files in count shards, each a sorted list"""
    known = [times[file] for file in files if file in times]
    default = sum(known) / len(known) if known else 1.0
    loads = [0.0] * count
    split = [[] for _ in range(count)]
    for file in sorted(files, key=lambda file: (-times.get(file, default), file)):
        shard = min(range(count), key=lambda index: (loads[index], index))
        loads[shard] += times.get(file, default)
        split[shard].append(file)
    return [sorted(files) for files in split], loads


def record(reports):
    times = defaultdict(float)
    for report in reports:
        for case in ElementTree.parse(report).iter("testcase"):
            if case.get("file"):
                times[Path(case.get("file")).as_posix()] += float(case.get("time") or 0)
    if not times:
        sys.exit("no test cases with their files (-o junit_family=xunit1)")
    TIMES.write_text(json.dumps({file: round(time, 2) for file, time in sorted(times.items())}, indent=1) + "\n")
    print(f"{TIMES.name}: {len(times)} files, {sum(times.values()):.0f} s")


def main():
    if sys.argv[1:2] == ["--record"]:
        record(sys.argv[2:])
        return
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("shard", type=int)
    parser.add_argument("count", type=int)
    parser.add_argument("paths", nargs="+")
    args = parser.parse_args()
    if not 1 <= args.shard <= args.count:
        parser.error("SHARD is from 1 to COUNT")
    times = json.loads(TIMES.read_text()) if TIMES.exists() else {}
    split, loads = shards(test_files(args.paths), args.count, times)
    print(f"shard {args.shard} of {args.count}: {len(split[args.shard - 1])} files, "
          f"about {loads[args.shard - 1]:.0f} s of tests", file=sys.stderr)
    print("\n".join(split[args.shard - 1]))


if __name__ == "__main__":
    main()
