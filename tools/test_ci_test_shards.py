"""tools/ci_test_shards.py: the workflow's tests job's split, every file in
exactly one shard, balanced by the recorded times, the same every time."""

from tools import ci_test_shards


def test_every_file_in_one_shard():
    files = ci_test_shards.test_files(["tools/harness", "tools/test_ci_test_shards.py"])
    assert "tools/test_ci_test_shards.py" in files and any(file.startswith("tools/harness/") for file in files)
    times = {file: float(len(file) % 7) for file in files}
    split, _ = ci_test_shards.shards(files, 4, times)
    assert sorted(file for shard in split for file in shard) == files
    assert split == ci_test_shards.shards(list(reversed(files)), 4, times)[0]


def test_balanced_by_time():
    times = {"a": 10.0, "b": 6.0, "c": 5.0, "d": 4.0, "e": 1.0}
    split, loads = ci_test_shards.shards(list(times), 2, times)
    assert split == [["a", "d"], ["b", "c", "e"]] and loads == [14.0, 12.0]
    # (without times: by count alone)
    split, loads = ci_test_shards.shards(list(times), 2, {})
    assert split == [["a", "c", "e"], ["b", "d"]] and loads == [3.0, 2.0]
    # (a file without a time counts as the mean of those with one)
    split, loads = ci_test_shards.shards(["a", "new"], 2, {"a": 10.0})
    assert loads == [10.0, 10.0]
