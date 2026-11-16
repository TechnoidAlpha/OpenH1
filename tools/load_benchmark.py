#!/usr/bin/env python3
"""Measure offline map loading with explicit file, game and shader cache states.

The default offscreen driver measures work and submission, not physical display
cadence. HALO_LOAD_TRACE instrumentation is required. Private game caches live
only for this invocation; logs and JSON survive, run folders and BMPs do not.
Linux cold runs evict only the tested map/cache files and verify their residency.
No system-wide caches are dropped.
"""
import argparse
import ctypes
import hashlib
import json
import math
import mmap
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

from render_enumerate_sweep import STOCK_MAPS


MARKER = re.compile(r"\b(LOAD|LOAD_IO) ")
FIELDS = re.compile(r"(\w+)=(.*?)(?= \w+=|$)")
PRESENT_PHASES = ("gl.swap.return", "loading.swap.return", "vulkan.present.return", "metal.presented", "loading.presented", "xr.submit.end")
SAFE_SETTINGS = {"HALO_VR", "HALO_PREPARE_SHADERS", "HALO_AMBIENT_OCCLUSION", "HALO_RANDOM_SEED", "HALO_NO_AUDIO",
                 "HALO_SKIP_CINEMATICS", "HALO_GPU_STATS", "HALO_VULKAN_STATS", "HALO_VULKAN_VALIDATION",
                 "HALO_NO_VSYNC", "HALO_RESOLUTION", "HALO_RESOLUTION_SCALING", "HALO_VR_TIMING",
                 "HALO_VR_GPU_TIME", "HALO_VR_FOVEATION", "HALO_VR_RESOLUTION"}


def parse_trace(text):
    events, operations = [], []
    for number, line in enumerate(text.splitlines(), 1):
        marker = MARKER.search(line)
        if not marker:
            continue
        if MARKER.search(line, marker.end()):
            raise ValueError(f"interleaved load records at line {number}")
        kind = marker[1]
        payload = line[marker.end():]
        # detail and path are terminal free text, and may themselves contain '='.
        tail = "detail" if kind == "LOAD" else "path"
        head, separator, value = payload.partition(" " + tail + "=")
        pairs = FIELDS.findall(head)
        fields = dict(pairs)
        try:
            if not separator or len(fields) != len(pairs):
                raise ValueError("missing terminal field or duplicate field")
            fields[tail] = value
            required = ({"phase", "wall", "thread", "process", "detail"} if kind == "LOAD" else
                        {"op", "wall", "elapsed", "requested", "actual", "offset", "path"})
            if not required <= fields.keys():
                raise ValueError("missing required field")
            for key in ("wall", "thread", "process", "elapsed", "syscall"):
                if key in fields:
                    fields[key] = float(fields[key])
                    if not math.isfinite(fields[key]) or (fields[key] < 0 and
                            not (key in ("thread", "process") and fields[key] == -1)):
                        raise ValueError("invalid clock or duration")
            for key in ("requested", "actual", "offset", "bounce"):
                if key in fields:
                    fields[key] = int(fields[key])
                    if fields[key] < 0:
                        raise ValueError("negative byte count or offset")
            if kind == "LOAD":
                if not fields["phase"] or any(c.isspace() for c in fields["phase"]):
                    raise ValueError("invalid phase")
                events.append(fields)
            else:
                if fields["op"] not in ("read", "write") or fields["actual"] > fields["requested"]:
                    raise ValueError("invalid I/O operation or byte count")
                operations.append(fields)
        except (ValueError, TypeError) as error:
            raise ValueError(f"malformed {kind} at line {number}: {error}") from error
    return events, operations


# Milestones whose end is deliberately not named <phase>.end.
MILESTONES = {
    "enumerate.draws": ("enumerate.begin", "enumerate.draws.end"),
    "enumerate.total": ("enumerate.begin", "enumerate.prepared.end"),
    "enumerate.wait": ("enumerate.draws.end", "enumerate.prepared.end"),
    "tags.validate": ("tags.read.end", "tags.validate.end"),
    "bsp.validate": ("bsp.read.end", "bsp.validate.end"),
    "bsp.register": ("bsp.validate.end", "bsp.end"),
    "objects_and_remaining_initialize": ("objects.begin", "initialize.end"),
    "prepare.wait": ("prepare.wait.begin", "prepare.end"),
    "main_new_map.activation": ("main_new_map.begin", "main_new_map.initial_pulse.end"),
    "first_game_frame": ("first_game_frame.begin", "first_game_frame.submitted"),
}


def span_between(phase, begin, end):
    same_owner = begin.get("owner") is not None and begin.get("owner") == end.get("owner")
    span = dict(phase=phase, detail=begin["detail"], owner=begin.get("owner"),
                begin_wall=begin["wall"], end_wall=end["wall"])
    for key in ("wall", "thread", "process"):
        valid = end[key] >= begin[key] >= 0 and (key != "thread" or same_owner)
        span[key + "_ms"] = 1000 * (end[key] - begin[key]) if valid else None
    return span


def loading_boundaries(items, begin, end, epoch, stream):
    """Match actual presentations; readiness is not itself a presentation."""
    def positive(fields, name):
        try:
            value = float(fields[name])
            return value if math.isfinite(value) and value > 0 else None
        except (KeyError, ValueError):
            return None
    def phase(name):
        return [(event, fields) for event, fields in items if event["phase"] == name]
    def submitted_before(marker, presentations):
        candidates = [event for event, fields in presentations
                      if event.get("owner") == marker.get("owner") and event["wall"] <= marker["wall"]]
        return max(candidates, key=lambda event: event["wall"], default=None)

    ready = [event for event, fields in phase("loading.ready")
             if fields.get("epoch") == epoch and begin["wall"] <= event["wall"] <= end["wall"]]
    next_begin = min((event["wall"] for event, fields in phase("loading.begin")
                      if event["wall"] > begin["wall"]), default=math.inf)
    loading, presentations, normal = [], [], []
    complete = True
    clock = "host_monotonic_submission"
    if stream == "xr":
        presentations = phase("xr.submit.end")
        waits = phase("xr.wait.end")
        for event, fields in presentations:
            predictions = [f for e, f in waits if f.get("predicted") == fields.get("predicted")
                           and e["wall"] <= event["wall"]]
            if fields.get("predicted") and positive(fields, "layers") and float(fields["layers"]) >= 2 and predictions and predictions[-1].get("render") == "1":
                normal.append(event["wall"])
        for marker, fields in phase("loading.submitted"):
            if fields.get("epoch") != epoch:
                continue
            poses = [(e, f) for e, f in phase("loading.pose")
                     if f.get("epoch") == epoch and f.get("serial") == fields.get("serial")]
            matches = [(e, f) for e, f in presentations if len(poses) == 1
                       and poses[0][1].get("predicted")
                       and e.get("owner") == poses[0][0].get("owner")
                       and poses[0][0]["wall"] <= e["wall"] <= marker["wall"]
                       and f.get("predicted") == poses[0][1].get("predicted")]
            states = [f for e, f in waits if len(matches) == 1
                      and f.get("predicted") == matches[0][1].get("predicted")
                      and e["wall"] <= matches[0][0]["wall"]]
            if len(matches) == 1 and poses[0][1].get("valid") == "1" and positive(matches[0][1], "layers") and states and states[-1].get("render") == "1":
                loading.append(matches[0][0]["wall"])
            else:
                complete = False
        presentations = [e["wall"] for e, f in presentations]
    elif stream == "vulkan":
        presents = phase("vulkan.present.return")
        all_loading = set()
        for marker, fields in phase("loading.submitted"):
            matched = submitted_before(marker, presents)
            valid = matched is not None and any(e is matched and f.get("result") in ("0", "1000001003")
                                                for e, f in presents)
            if matched:
                all_loading.add(matched["wall"])
            if fields.get("epoch") == epoch:
                if valid and matched["wall"] >= begin["wall"]:
                    loading.append(matched["wall"])
                else:
                    complete = False
        presentations = [e["wall"] for e, f in presents]
        normal = [e["wall"] for e, f in presents if e["wall"] not in all_loading
                  and f.get("result") in ("0", "1000001003") and f.get("image") == "1"]
    elif stream == "gl":
        loading = [e["wall"] for e, f in phase("loading.swap.return") if f.get("epoch") == epoch]
        normal = [e["wall"] for e, f in phase("gl.swap.return")]
        presentations = normal + [e["wall"] for e, f in phase("loading.swap.return")]
    elif stream == "metal":
        clock = "drawable_presented_time"
        # Callbacks can arrive after loading.end or out of display order.
        # Match their epoch and compare drawable times, never callback wall.
        markers = phase("loading.presented")
        all_loading = {positive(f, "display_time") for e, f in markers}
        for event, fields in markers:
            if fields.get("epoch") == epoch:
                value = positive(fields, "display_time")
                if value is None:
                    complete = False
                else:
                    loading.append(value)
        for event, fields in phase("metal.presented"):
            value = positive(fields, "display_time")
            if value is not None:
                presentations.append(value)
                if value not in all_loading and fields.get("image") == "1" and positive(fields, "input"):
                    normal.append(value)
        presentations += loading
    loading.sort()
    complete = complete and bool(loading) and len(set(loading)) == len(loading)
    first, last = (loading[0], loading[-1]) if complete else (None, None)
    prior = max((value for value in presentations if first is not None and value < first), default=None)
    if prior not in normal:
        prior = None
    game = None
    if complete and ready:
        game = min((value for value in normal if value > last and
                    (stream == "metal" or ready[0]["wall"] <= value < next_begin)), default=None)
    request = max((e["wall"] for e, f in phase("loading.request") if e["wall"] <= begin["wall"]), default=None)
    def gap(after, before):
        return 1000 * (after - before) if after is not None and before is not None and after >= before else None
    entry, handoff = gap(first, prior), gap(game, last)
    internal = 1000 * max((b - a for a, b in zip(loading, loading[1:])), default=0) if complete else None
    return dict(clock=clock, loading_samples_complete=complete,
                prior_game_time=prior, first_loading_time=first, last_loading_time=last, first_game_time=game,
                entry_gap_ms=entry, internal_max_gap_ms=internal, first_game_handoff_ms=handoff,
                request_to_first_loading_ms=gap(first, request) if stream != "metal" else None,
                maximum_transition_gap_ms=max(entry, internal, handoff)
                if all(value is not None for value in (entry, internal, handoff)) else None)


def loading_epochs(events):
    """Keep presentation proof distinct from game submission and simulation ticks."""
    result, starts = [], {}
    items = sorted(((event, dict(FIELDS.findall(event.get("detail", "")))) for event in events),
                   key=lambda item: item[0]["wall"])
    for event in events:
        fields = dict(FIELDS.findall(event.get("detail", "")))
        if event["phase"] == "loading.begin":
            starts[(event.get("owner"), fields.get("epoch"))] = event
        elif event["phase"] == "loading.end":
            epoch = fields.get("epoch")
            begin = starts.pop((event.get("owner"), epoch), None)
            if begin is None:
                continue
            first, last = begin["wall"], event["wall"]
            scoped = [(e, dict(FIELDS.findall(e.get("detail", "")))) for e in events
                      if first <= e["wall"] <= last]
            frames = [(e, f) for e, f in items if f.get("epoch") == epoch
                      and (e["phase"] == "loading.presented" or
                           first <= e["wall"] <= last and e["phase"] in ("loading.submitted", "loading.swap.return"))]
            if frames and all(e["phase"] == "loading.presented" for e, f in frames):
                try:
                    frames.sort(key=lambda item: float(item[1]["display_time"]))
                except (KeyError, ValueError):
                    pass
            serials = [int(f["serial"]) for e, f in frames if "serial" in f]
            clocks = [int(f["elapsed_ns"]) for e, f in frames if "elapsed_ns" in f]
            poses = [f for e, f in scoped if e["phase"] == "loading.pose" and f.get("epoch") == epoch]
            predictions = [int(f["predicted"]) for f in poses if "predicted" in f]
            def vector(fields, key, size):
                try:
                    value = tuple(float(item) for item in fields[key].split(","))
                    return value if len(value) == size and all(math.isfinite(item) for item in value) else None
                except (KeyError, ValueError):
                    return None
            orientations = [vector(fields, "orientation", 4) for fields in poses]
            orientations = [value for value in orientations if value and sum(v * v for v in value) > 0]
            angles = []
            if orientations:
                initial = orientations[0]
                for value in orientations:
                    cosine = abs(sum(a * b for a, b in zip(initial, value))) / math.sqrt(
                        sum(a * a for a in initial) * sum(b * b for b in value))
                    angles.append(math.degrees(2 * math.acos(min(1.0, cosine))))
            anchors = [(vector(f, "anchor_position", 3), vector(f, "anchor_orientation", 4)) for e, f in frames]
            anchors_complete = bool(anchors) and all(position and orientation for position, orientation in anchors)
            spaces_complete = bool(frames) and all("world_space" in f for e, f in frames)
            streams = {}
            for name, phases in (("gl", ("gl.swap.return", "loading.swap.return")),
                                 ("vulkan", ("vulkan.present.return",)),
                                 ("metal", ("metal.presented", "loading.presented")), ("xr", ("xr.submit.end",))):
                times = sorted(e["wall"] for e in events if e["phase"] in phases)
                if not times:
                    continue
                before = [t for t in times if t <= first]
                after = [t for t in times if t >= last]
                within = [t for t in times if first < t < last]
                clipped = [first, *within, last]
                bracketed = ([before[-1]] if before else []) + within + ([after[0]] if after else [])
                streams[name] = dict(count=len(within), boundary_samples_complete=bool(before and after),
                    begin_to_first_ms=1000 * ((within[0] if within else last) - first),
                    last_to_end_ms=1000 * (last - (within[-1] if within else first)),
                    maximum_clipped_gap_ms=1000 * max(b - a for a, b in zip(clipped, clipped[1:])),
                    maximum_bracketing_gap_ms=1000 * max(b - a for a, b in zip(bracketed, bracketed[1:]))
                    if len(bracketed) > 1 and before and after else None,
                    actual_boundaries=loading_boundaries(items, begin, event, epoch, name))
            result.append(dict(epoch=epoch, owner=event.get("owner"), begin=first, end=last, completed=True,
                loading_frame_count=len(frames), serials=serials, elapsed_ns=clocks,
                # Service attempts can skip IDs when no runtime ticket is
                # ready. Only submitted events count as loading frames.
                serials_increasing=bool(serials) and len(serials) == len(frames)
                    and all(b > a for a, b in zip(serials, serials[1:])),
                serials_contiguous=bool(serials) and len(serials) == len(frames)
                    and all(b == a + 1 for a, b in zip(serials, serials[1:])),
                animation_clock_increasing=bool(clocks) and len(clocks) == len(frames)
                    and all(b > a for a, b in zip(clocks, clocks[1:])),
                pose_count=len(poses), pose_valid=bool(poses) and all(f.get("valid") == "1" for f in poses),
                head_orientation_samples=len(orientations),
                head_orientation_unique=len(set(orientations)),
                head_orientation_max_delta_degrees=max(angles) if angles else None,
                world_space_each_frame=all(f["world_space"] == "1" for e, f in frames) if spaces_complete else None,
                world_anchor_stable=len(set(anchors)) == 1 if anchors_complete else None,
                world_anchor_unique=len(set(anchors)) if anchors_complete else None,
                pose_each_frame=bool(frames) and [f.get("serial") for f in poses] == [f.get("serial") for e, f in frames],
                predicted_time_increasing=bool(predictions) and len(predictions) == len(poses)
                    and all(b > a for a, b in zip(predictions, predictions[1:])),
                presentations=streams))
    for (owner, epoch), begin in starts.items():
        result.append(dict(epoch=epoch, owner=owner, begin=begin["wall"], end=None, completed=False))
    return result


def summarize(events, operations, scenario):
    # Owner IDs prevent interleaved threads from pairing each other's phases.
    # Legacy traces retain wall/process spans but cannot prove thread CPU time.
    starts, spans, milestones = {}, [], {}
    for event in events:
        phase, owner = event["phase"], event.get("owner")
        if phase.endswith(".begin"):
            starts.setdefault((phase[:-6], owner), []).append(event)
        elif phase.endswith(".end") and starts.get((phase[:-4], owner)):
            begin = starts[(phase[:-4], owner)].pop()
            spans.append(span_between(phase[:-4], begin, event))
        for name, (first, last) in MILESTONES.items():
            if phase == first:
                milestones.setdefault((name, owner), []).append(event)
            elif phase == last and milestones.get((name, owner)):
                begin = milestones[(name, owner)].pop()
                spans.append(span_between(name, begin, event))
    presentations = {}
    for phase in PRESENT_PHASES:
        times = sorted(e["wall"] for e in events if e["phase"] == phase)
        if times:
            gaps = [1000 * (b - a) for a, b in zip(times, times[1:])]
            presentations[phase] = dict(count=len(times), maximum_gap_ms=max(gaps) if gaps else None,
                                         first_wall=times[0], last_wall=times[-1])
    totals = {}
    for op in ("read", "write"):
        selected = [record for record in operations if record.get("op") == op]
        totals[op] = dict(calls=len(selected), actual_bytes=sum(r.get("actual", 0) for r in selected),
                         elapsed_ms=1000 * sum(r.get("elapsed", 0) for r in selected),
                         syscall_ms=1000 * sum(r["syscall"] for r in selected)
                         if selected and all("syscall" in r for r in selected) else None)
    return dict(scenario=scenario, spans=spans, presentations=presentations, io=totals,
                loading_epochs=loading_epochs(events),
                events=events, operations=operations)


def completed_load(events, scenario):
    """Require the requested map's ordered activation and first rendered frame."""
    scenario = r"levels\ui\ui" if scenario == "ui" else scenario
    phase, owner, last_wall = 0, None, -1
    for event in events:
        name = event["phase"]
        if name == "main_new_map.begin":
            phase = 1 if event["detail"] == scenario else 0
            owner, last_wall = event.get("owner"), event["wall"]
            continue
        if not phase or event.get("owner") != owner:
            continue
        if event["wall"] < last_wall:
            return False
        last_wall = event["wall"]
        if phase == 1 and name == "initialize.end":
            phase = 2
        elif phase == 2 and name == "main_new_map.initial_pulse.end" and event["detail"] == scenario:
            phase = 3
        elif phase == 3 and name == "first_game_frame.begin" and event["detail"] in ("", scenario):
            phase = 4
        elif phase == 4 and name == "first_game_frame.submitted" and event["detail"] in ("", scenario):
            return True
    return False


def residency(path):
    """Count resident file pages without faulting them into memory (Linux)."""
    if sys.platform != "linux":
        raise RuntimeError("file-cache verification requires Linux mincore and posix_fadvise")
    size = path.stat().st_size
    if not size:
        return dict(bytes=0, pages=0, resident_pages=0)
    pages = (size + mmap.PAGESIZE - 1) // mmap.PAGESIZE
    library = ctypes.CDLL(None, use_errno=True)
    library.mincore.argtypes = (ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p)
    library.mincore.restype = ctypes.c_int
    with path.open("rb") as file, mmap.mmap(file.fileno(), size, access=mmap.ACCESS_COPY) as mapped:
        address = ctypes.addressof(ctypes.c_char.from_buffer(mapped))
        vector = (ctypes.c_ubyte * pages)()
        if library.mincore(address, size, vector):
            error = ctypes.get_errno()
            raise OSError(error, os.strerror(error), str(path))
        resident = sum(bool(value & 1) for value in vector)
    return dict(bytes=size, pages=pages, resident_pages=resident)


def prepare_files(paths, state):
    if state not in ("cold", "warm"):
        raise ValueError("unknown file-cache state: " + state)
    proof = []
    for path in sorted(set(paths)):
        if not path.is_file():
            raise FileNotFoundError(path)
        before = residency(path)
        with path.open("rb") as file:
            if state == "cold":
                os.fsync(file.fileno())
                os.posix_fadvise(file.fileno(), 0, 0, os.POSIX_FADV_DONTNEED)
            else:
                while file.read(1024 * 1024):
                    pass
        after = residency(path)
        proof.append(dict(path=str(path), before=before, after_preparation=after, after=after))
    # Later reads can reclaim an earlier file's pages on a busy machine.
    # Record a final sweep rather than treating observations minutes apart as
    # proof of the state at launch. This does not fault any pages into memory.
    for item in proof:
        item["after"] = residency(Path(item["path"]))
    if state == "warm":
        for item in proof:
            item["warm_reads"] = 1
            item["warm_checks"] = [item["after"]]
        # Reclamation during preparation can undo an earlier read. Retry only
        # incomplete files, with at most three passes before the measured run.
        for unused in range(2):
            missing = [item for item in proof
                       if item["after"]["resident_pages"] != item["after"]["pages"]]
            if not missing:
                break
            for item in missing:
                with Path(item["path"]).open("rb") as file:
                    while file.read(1024 * 1024):
                        pass
                item["warm_reads"] += 1
            for item in proof:
                item["after"] = residency(Path(item["path"]))
                item["warm_checks"].append(item["after"])
    # mincore can conservatively report pages resident without permission to
    # inspect their cache state. Such a result is not proof of a cold run.
    if not proof:
        raise RuntimeError("no files supplied for cache-state verification")
    if state == "cold" and any(item["after"]["resident_pages"] for item in proof):
        raise RuntimeError("file-cache eviction not verified: " + json.dumps(proof))
    if state == "warm" and any(item["after"]["resident_pages"] != item["after"]["pages"] for item in proof):
        raise RuntimeError("warm file-cache residency not verified: " + json.dumps(proof))
    return proof


def scenario_name(value):
    if value == "ui":
        return value
    if "\\" in value:
        return value
    campaign = {"a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40"}
    return rf"levels\{value}\{value}" if value in campaign else rf"levels\test\{value}\{value}"


def run_game(args, scenario, work, saves, shaders, destination, cache_proof=None):
    if any(key not in SAFE_SETTINGS for key, value in args.env):
        raise ValueError("--env includes settings that can change the scenario or isolation")
    destination.mkdir(parents=True, exist_ok=False)
    if cache_proof is not None:
        (destination / "cache-proof.json").write_text(json.dumps(cache_proof, indent=2) + "\n")
    with tempfile.TemporaryDirectory(prefix="run-", dir=work) as temporary:
        run = Path(temporary)
        # Desktop settings are located from the executable, not HALO_DATA_ROOT.
        # A symlink still resolves to the original installation on Linux.
        executable = run / args.halo.name
        shutil.copy2(args.halo, executable)
        sdl = args.halo.parent / "libSDL3.so.0"
        if sdl.exists():
            shutil.copy2(sdl, run / sdl.name)
        # (and the files the game reads beside it: its assets' file, embedded_assets.h, and the shipped records)
        for name in ("openh1.assets", "prepared.records"):
            if (args.halo.parent / name).exists():
                shutil.copy2(args.halo.parent / name, run / name)
        (run / "maps").symlink_to(args.data / "maps")
        if scenario != "ui":
            (run / "init.txt").write_text(f"map_name {scenario}\n")
        environment = dict(os.environ)
        # Existing task-specific settings must not accidentally select a lobby,
        # screenshots, or another run's paths.
        for key in list(environment):
            if key.startswith(("HALO_", "MESA_SHADER_CACHE", "__GL_SHADER_DISK_CACHE")):
                del environment[key]
        environment.update(dict(args.env))
        environment.update(HALO_NET_ONLINE="false", HALO_NO_CLIPBOARD="1", HALO_DATA_ROOT=str(run), HALO_SAVE_ROOT=str(saves),
                           HALO_EXIT_AFTER=str(args.seconds), HALO_LOAD_TRACE="1",
                           HALO_UPDATE_ANSWER="no", HALO_SCREENSHOT_EVERY="0", HALO_SCREENSHOT_DIR="",
                           XDG_CACHE_HOME=str(shaders), __GL_SHADER_DISK_CACHE_PATH=str(shaders / "nvidia"),
                           MESA_SHADER_CACHE_DIR=str(shaders / "mesa"), MESA_SHADER_CACHE_DISABLE="false",
                           __GL_SHADER_DISK_CACHE="1",
                           HALO_VULKAN_PIPELINE_CACHE=str(shaders / "vulkan.bin"))
        environment["SDL_VIDEO_DRIVER"] = getattr(args, "video_driver", "offscreen")
        environment["SDL_AUDIO_DRIVER"] = "dummy"
        environment.pop("SDL_VIDEODRIVER", None)
        environment.pop("SDL_AUDIODRIVER", None)
        started = time.monotonic()
        with (destination / "stdout.txt").open("w") as output:
            try:
                status = subprocess.run([str(executable)], cwd=run, env=environment, stdout=output,
                                        stderr=subprocess.STDOUT, timeout=args.seconds + 120).returncode
            except subprocess.TimeoutExpired:
                status = 124
            except OSError as error:
                status = 127
                output.write(f"launch failed: {error}\n")
        elapsed = time.monotonic() - started
        for name in ("debug.txt", "config.toml"):
            if (run / name).exists():
                shutil.copy2(run / name, destination / name)
        text = (destination / "stdout.txt").read_text(errors="replace")
        debug = destination / "debug.txt"
        debug_text = debug.read_text(errors="replace") if debug.exists() else ""
        errors = [line for line in (text + "\n" + debug_text).splitlines()
                  if "EXCEPTION" in line or "vulkan validation:" in line]
        pipeline_misses = [line for line in (text + "\n" + debug_text).splitlines()
                           if "pipeline missed map preparation" in line]
        try:
            events, operations = parse_trace(text)
        except ValueError as error:
            errors.append(str(error))
            events, operations = [], []
        result = summarize(events, operations, scenario)
        result.update(exit_status=status, elapsed_seconds=elapsed, errors=errors,
                      passed=status == 0 and not errors and not pipeline_misses and completed_load(events, scenario),
                      pipeline_misses=pipeline_misses, load_completed=completed_load(events, scenario),
                      settings={k: v for k, v in environment.items()
                                if k.startswith(("HALO_", "SDL_", "XDG_CACHE", "__GL_SHADER", "MESA_SHADER"))})
        (destination / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        if not result["passed"]:
            raise RuntimeError(f"run failed: {destination} (exit {status}; errors {errors[:3]}; trace {len(events)})")
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--halo", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True, help="folder containing maps/")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--maps", nargs="+", default=["bloodgulch", "a10", "b30"])
    parser.add_argument("--all", action="store_true", help="all 24 stock maps, including ui")
    parser.add_argument("--seconds", type=float, default=15)
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--keep-going", action="store_true",
                        help="retain failed game runs and attempt remaining cache states; still exit nonzero")
    parser.add_argument("--os-cache", nargs="+", choices=("cold", "warm"), default=["cold", "warm"])
    parser.add_argument("--game-cache", choices=("fresh", "reused"), default="reused")
    parser.add_argument("--shader-cache", choices=("fresh", "reused"), default="reused")
    parser.add_argument("--env", action="append", default=[], metavar="NAME=VALUE")
    parser.add_argument("--video-driver", choices=("offscreen", "x11"), default="offscreen",
                        help="x11 uses DISPLAY, which must name a private virtual display")
    args = parser.parse_args()
    args.env = [value.split("=", 1) for value in args.env]
    if any(len(value) != 2 for value in args.env):
        parser.error("--env must be NAME=VALUE")
    if any(key not in SAFE_SETTINGS for key, value in args.env):
        parser.error("--env permits only documented rendering/timing settings: " + ", ".join(sorted(SAFE_SETTINGS)))
    if args.repeat < 1 or not math.isfinite(args.seconds) or args.seconds <= 0:
        parser.error("--repeat and --seconds must be positive")
    if sys.platform != "linux":
        parser.error("cache-state benchmarks require Linux")
    scenarios = STOCK_MAPS if args.all else [scenario_name(value) for value in args.maps]
    if any(scenario not in STOCK_MAPS for scenario in scenarios):
        parser.error("--maps must name stock maps")
    if len(set(scenarios)) != len(scenarios) or len(set(args.os_cache)) != len(args.os_cache):
        parser.error("duplicate maps or OS cache states; use --repeat for repetitions")
    args.halo, args.data, args.out = (path.resolve() for path in (args.halo, args.data, args.out))
    args.out.mkdir(parents=True, exist_ok=False)
    manifest = dict(binary=str(args.halo), sha256=hashlib.sha256(args.halo.read_bytes()).hexdigest(),
                    kernel=os.uname().release, machine=os.uname().machine, game_cache=args.game_cache,
                    shader_cache=args.shader_cache, video_driver=args.video_driver,
                    presentation="virtual display" if args.video_driver == "x11" else
                        "offscreen; submissions are not display proof",
                    file_cache_scope="requested source map, ui.map, and private decompressed map files only",
                    shader_cache_scope="private disk artifacts; every run starts a new process",
                    passed=False, runs=[])
    guest = args.halo.parent / "halo_guest.elf"
    if guest.exists():
        manifest["guest_sha256"] = hashlib.sha256(guest.read_bytes()).hexdigest()
    def save_manifest():
        (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    def recorded_run(scenario, work, saves, shaders, destination, **metadata):
        record = dict(map=scenario, result=str(destination / "result.json"), passed=False, **metadata)
        manifest["runs"].append(record)
        save_manifest()
        try:
            result = run_game(args, scenario, work, saves, shaders, destination,
                              cache_proof=metadata.get("cache_proof"))
            record["passed"] = result["passed"]
            return result
        except Exception as error:
            record["error"] = str(error)
            if args.keep_going and (destination / "result.json").exists():
                result = json.loads((destination / "result.json").read_text())
                if not result["passed"]:
                    print(f"FAIL {destination}: {error}", file=sys.stderr, flush=True)
                    return result
            raise
        finally:
            save_manifest()

    save_manifest()
    try:
        # Keep decompressed maps on the evidence filesystem: /tmp is often
        # tmpfs, whose pages cannot be evicted for a cold disk-cache run.
        with tempfile.TemporaryDirectory(prefix=".work-", dir=args.out) as temporary:
            work = Path(temporary)
            for index, scenario in enumerate(scenarios):
                name = scenario.rsplit("\\", 1)[-1]
                saves, shaders = work / f"save-{index}", work / f"shaders-{index}"
                saves.mkdir()
                shaders.mkdir()
                if "reused" in (args.game_cache, args.shader_cache):
                    recorded_run(scenario, work, saves, shaders, args.out / name / "prime", kind="prime")
                for repeat in range(args.repeat):
                    for state in args.os_cache:
                        for folder, mode in ((saves, args.game_cache), (shaders, args.shader_cache)):
                            if mode == "fresh":
                                shutil.rmtree(folder)
                                folder.mkdir()
                        destination = args.out / name / f"{repeat}-{state}"
                        paths = [args.data / "maps" / f"{name}.map", args.data / "maps" / "ui.map"]
                        map_caches = list(saves.rglob("*.map"))
                        paths += map_caches
                        if args.game_cache == "reused" and not map_caches:
                            raise RuntimeError("priming run produced no reusable map cache")
                        shader_files = [p for p in shaders.rglob("*") if p.is_file() and p.stat().st_size]
                        if args.shader_cache == "reused" and not shader_files:
                            raise RuntimeError("priming run produced no reusable shader cache artifacts")
                        shader_inventory = [dict(path=str(p.relative_to(shaders)), bytes=p.stat().st_size)
                                            for p in sorted(shader_files)]
                        proof = prepare_files(paths, state)
                        result = recorded_run(scenario, work, saves, shaders, destination, kind="measured",
                                              repeat=repeat, os_cache=state, cache_proof=proof,
                                              shader_artifacts_before=shader_inventory)
                        print(f"{'PASS' if result['passed'] else 'FAIL'} {name} {state} repeat {repeat}: {len(result['events'])} trace events", flush=True)
        manifest["passed"] = all(record["passed"] for record in manifest["runs"])
    except Exception as error:
        manifest["error"] = str(error)
        raise
    finally:
        save_manifest()
    return 0 if manifest["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
