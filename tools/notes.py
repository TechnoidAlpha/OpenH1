#!/usr/bin/env python3
"""The review notes, every kind, and what an agent does with them: the recorded frames' (a note a client of
the replay service leaves on a draw of a recorded moment, build/notes.jsonl: docs/replay_protocol.md) and the
menus' (the menu studio's and the menu review app's, build/menu_notes.jsonl: tools/menu_notes.py, whose work
this tool hands them to).

    python tools/notes.py                         # the notes to act on
    python tools/notes.py batches                 # the notes to fix, grouped so agents do not collide
    python tools/notes.py brief [ID...] [--batch N]   # what to change, where, how to see it
    python tools/notes.py start ID...             # taken (in progress)
    python tools/notes.py reply ID "words"        # a reply on its thread (a question's answer)
    python tools/notes.py fixed ID... [--commit SHA] [--after PNG]   # ready for review
    python tools/notes.py reopen|close ID...      # (the reviewer's, normally done in the app)
    python tools/notes.py rule "SCOPE" "rule"     # a general preference (the menus' STYLE.md)
    python tools/notes.py --all                   # every note

A note is a JSON line: the menu notes' fields (tools/menu_notes.py: id, kind "fix" or "question", scope
"variant", "screen" or "everywhere", status "open", "in_progress", "fixed" (ready for review) or "closed",
the note, its thread) and, for a recorded frame's, an anchor: the moment and what drew the pixel it is on

    "anchor": {"recording": "<path>", "frame": n, "pixel": [x, y], "draw": {"i", "kind", "label", "id",
               "tags": {"object", "model", "shader", ...}, "source", "textures", "pipeline"},
               "camera": {...}, "map": "...", "tick": n, "backend": "metal", "build": "<sha>", "source_name": "..."}

and a picture of the frame with the draw outlined ("picture", under build/inspector/notes). For an
anchored note the scopes read: "variant" this moment, "screen" this thing (the object or tag wherever it
draws), "everywhere" every draw like it (its kind, its shader). A note on a menu widget (draw kind
menu_widget) carries the menu notes' screen, widget and source too, and is a menu note: its brief and
`fixed` are tools/menu_notes.py's (the gallery re-rendered against the commit's parent).

`fixed` on another note renders its frame again with this checkout's replay (build/macos/halo_replay
<recording> --serve, or --replay) as the "after" picture, which a client shows beside the
note's picture (the before); --after gives a picture instead. Look at both before marking it.
"""
import argparse
import base64
import json
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import menu_notes  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
NOTES = ROOT / "build" / "notes.jsonl"
MENU_NOTES = menu_notes.NOTES
AFTER = ROOT / "build" / "inspector" / "after"
REPLAY = ROOT / "build" / "macos" / "halo_replay"


def is_menu(note: dict) -> bool:
    """a menu note: the studio's and the review app's (a screen, no anchor), or an anchored one on a menu widget"""
    anchor = note.get("anchor") or {}
    if anchor:
        return (anchor.get("draw") or {}).get("kind") == "menu_widget" and bool(note.get("screen"))
    return True


def load(files: list) -> list:
    """(file, note) for every note of the files, in their order"""
    return [(path, note) for path in files for note in menu_notes.read_notes(path)]


def draw_of(note: dict) -> dict:
    return (note.get("anchor") or {}).get("draw") or {}


def describe(note: dict) -> str:
    if is_menu(note) and not note.get("anchor"):
        return menu_notes.describe(note)
    draw = draw_of(note)
    if not draw:
        return "the frame as a whole"
    thing = draw.get("label") or (draw.get("tags") or {}).get("object") or "?"
    return f"{draw.get('kind', '?')} {thing}"


def place(note: dict) -> str:
    anchor = note.get("anchor") or {}
    if not anchor:
        return note.get("screen", "?")
    return f"{Path(anchor.get('recording', '?')).name} frame {anchor.get('frame', '?')}"


def list_text(entries: list, everything: bool) -> str:
    shown = [(path, note) for path, note in entries if everything or note.get("status") in menu_notes.ACTIVE]
    if not shown:
        return "No notes." if everything else "No open notes."
    lines, seen = [], None
    for _, note in sorted(shown, key=lambda entry: place(entry[1])):
        if place(note) != seen:
            seen = place(note)
            lines.append(seen)
        question = " (question)" if menu_notes.kind_of(note) == "question" else ""
        lines.append(f"  {note.get('id')}{menu_notes.status_word(note)}{question}  {describe(note)}: {note.get('note')}")
    return "\n".join(lines)


def batch_key(note: dict) -> tuple:
    """a note to fix's batch: the menu notes' (their generator's file and area), else the draw's kind and
    what it is made of (its shader, else its object), so two agents with two batches touch different code"""
    if is_menu(note):
        return menu_notes.batch_key(note)
    draw = draw_of(note)
    tags = draw.get("tags") or {}
    return draw.get("kind", "frame"), tags.get("shader") or tags.get("object") or draw.get("label") or "?"


def batches(entries: list) -> list:
    grouped = {}
    for path, note in entries:
        if note.get("status") == "open" and menu_notes.kind_of(note) == "fix":
            grouped.setdefault(batch_key(note), []).append((path, note))
    return sorted(grouped.items(), key=lambda item: (-len(item[1]), item[0]))


def anchor_brief(number: int, note: dict) -> list:
    anchor = note.get("anchor") or {}
    draw = draw_of(note)
    heading = "QUESTION (answer on its thread; change no code until the reviewer answers)" \
        if menu_notes.kind_of(note) == "question" else "FIX"
    scope = {"variant": "this moment only", "screen": "this thing wherever it draws (its object or tag)",
             "everywhere": "every draw like it (its kind, its shader)"}[menu_notes.scope_of(note)]
    out = [f"{number}. [{note.get('id')}] {heading}: {note.get('note')}"]
    if is_menu(note):
        # a menu widget: its generator's source first, as the menu notes' briefs have it
        out.append(f"   source: {menu_notes.menu_sources.describe(note.get('source') or '')}")
        out.append(f"   screen: {note.get('screen')}, widget: {note.get('widget')}")
        scope = menu_notes.SCOPES[menu_notes.scope_of(note)]
    out.append(f"   scope: {scope}")
    if draw:
        out.append(f"   draw: {draw.get('kind')} \"{draw.get('label', '')}\" (index {draw.get('i')}, id {draw.get('id')})")
        for name, value in (draw.get("tags") or {}).items():
            out.append(f"   {name}: {value}")
        if draw.get("textures"):
            out.append("   textures: " + ", ".join(draw["textures"]))
        if draw.get("pipeline"):
            out.append(f"   pipeline: {draw['pipeline']}")
        if draw.get("source"):
            out.append(f"   source: {draw['source']}")
    else:
        out.append("   on the frame as a whole")
    recording = anchor.get("recording", "?")
    out.append(f"   moment: {recording} frame {anchor.get('frame')}" + (f", pixel {anchor['pixel']}" if anchor.get("pixel") else ""))
    details = [f"map {anchor['map']}" if anchor.get("map") else "", f"tick {anchor['tick']}" if anchor.get("tick") is not None else "",
               f"{anchor.get('backend')} build {anchor.get('build')}" if anchor.get("backend") else ""]
    if any(details):
        out.append("   " + ", ".join(detail for detail in details if detail))
    camera = anchor.get("camera") or {}
    if camera:
        out.append("   camera: " + ", ".join(f"{key} {value}" for key, value in camera.items()))
    for reply in note.get("thread", []):
        out.append(f"   reply from {reply.get('author')}: {reply.get('text')}")
    if note.get("picture"):
        out.append(f"   picture (the draw outlined): {note['picture']}")
    out.append(f"   see it: build/macos/halo_replay {recording} --shots <dir> --shot-frames {anchor.get('frame')}"
               f" (or open the recording in a client of the replay service: docs/replay_protocol.md)")
    out.append("")
    return out


def brief(entries: list) -> str:
    # (a note on a recorded frame is briefed with its moment, a menu widget's with its source first too)
    menus = [note for _, note in entries if is_menu(note) and not note.get("anchor")]
    others = [note for _, note in entries if not (is_menu(note) and not note.get("anchor"))]
    out = []
    if others:
        fixes = [note for note in others if menu_notes.kind_of(note) == "fix"]
        questions = [note for note in others if menu_notes.kind_of(note) == "question"]
        out += [f"{len(fixes)} recorded frame note{'s' if len(fixes) != 1 else ''} to fix, {len(questions)} question"
                f"{'s' if len(questions) != 1 else ''} to answer.",
                "Each is on a draw of a recorded frame: find what draws it from its tags (the object, shader, "
                "textures) and kind, change the code or the asset, rebuild the replay (ninja macos_replay), look at "
                "the frame again, commit, then `python tools/notes.py fixed <id> --commit <sha>` (it renders the "
                "frame again as the after picture). Questions: `python tools/notes.py reply <id> \"...\"`, no code "
                "changed until the reviewer answers.", ""]
        for number, note in enumerate(fixes + questions, 1):
            out += anchor_brief(number, note)
    if menus:
        out.append(menu_notes.brief(menus))
    return "\n".join(out).rstrip() + "\n" if out else "No open notes.\n"


def pick(entries: list, ids: list, default=menu_notes.ACTIVE) -> list:
    if not ids:
        return [(path, note) for path, note in entries if note.get("status") in default]
    chosen = []
    for prefix in ids:
        matches = [(path, note) for path, note in entries if str(note.get("id", "")).startswith(prefix)]
        if len(matches) != 1:
            raise SystemExit(f"notes: {prefix!r} is {'no note' if not matches else 'more than one note'}")
        chosen.append(matches[0])
    return chosen


# ---------- the after picture: the note's frame from the replay service

def render_after(note: dict, replay: Path, out: Path) -> Path:
    """the note's frame rendered by a replay (its --serve), with the draw at its pixel outlined if it still
    is there: the picture's path"""
    anchor = note.get("anchor") or {}
    recording = Path(anchor.get("recording", ""))
    if not recording.is_absolute():
        recording = ROOT / recording
    if not recording.is_file():
        raise SystemExit(f"notes: {note.get('id')}: no recording {recording} for the after picture (--after PNG)")
    if not replay.is_file():
        raise SystemExit(f"notes: no replay at {replay} (ninja macos_replay, or --replay, or --after PNG)")
    out.mkdir(parents=True, exist_ok=True)
    picture = out / f"{note['id']}-{time.strftime('%Y%m%d-%H%M%S')}.png"
    command = [str(replay), str(recording), "--serve"]
    if replay.suffix == ".py":
        command = [sys.executable] + command
    request = {"cmd": "frame", "n": int(anchor.get("frame", 0)), "png": str(picture)}
    result = subprocess.run(command, input=json.dumps(request) + "\n" + '{"cmd":"quit"}\n', capture_output=True,
                            text=True, timeout=600, env={**os.environ, "SDL_AUDIO_DRIVER": "dummy"})
    reply = json.loads(result.stdout.splitlines()[0]) if result.stdout.strip() else {}
    if not reply.get("ok") or not picture.is_file():
        raise SystemExit(f"notes: the replay did not render frame {request['n']}: {reply.get('error') or result.stderr[-800:]}")
    return picture


def fixed(chosen: list, commit: str, after: Path, replay: Path, force: bool) -> int:
    # (the gallery's check is for the menu studio's and review app's notes; one on a recorded frame is checked
    # on that frame again)
    menus = [(path, note) for path, note in chosen if is_menu(note) and not note.get("anchor")]
    status = 0
    by_file = {}
    for path, note in menus:
        by_file.setdefault(path, []).append(note)
    for path, notes in by_file.items():
        status |= menu_notes.fixed(path, notes, commit, force)
    for path, note in chosen:
        if is_menu(note) and not note.get("anchor"):
            continue
        picture = after if after else render_after(note, replay, AFTER)
        shown = picture.relative_to(ROOT) if picture.resolve().is_relative_to(ROOT) else picture
        menu_notes.set_status(path, note["id"], "fixed", commit=commit, changed=menu_notes.now(), after=str(shown))
        print(f"notes: {note['id']} ready for review ({commit[:9]}): before {note.get('picture', '?')}  after {shown}")
    return status


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="The review notes: the recorded frames' and the menus'.", allow_abbrev=False)
    parser.add_argument("action", nargs="?", default="list",
                        choices=("list", "brief", "batches", "start", "reply", "fixed", "done", "reopen", "close", "rule"))
    parser.add_argument("ids", nargs="*")
    parser.add_argument("--file", type=Path, action="append",
                        help="a notes file (default: build/notes.jsonl and build/menu_notes.jsonl)")
    parser.add_argument("--all", action="store_true", help="list every note")
    parser.add_argument("--batch", type=int, help="brief: a batch's notes (notes.py batches)")
    parser.add_argument("--commit", help="fixed: the commit that addressed them (HEAD's)")
    parser.add_argument("--after", type=Path, help="fixed: the after picture (else rendered by the replay)")
    parser.add_argument("--replay", type=Path, default=REPLAY, help="fixed: the replay that renders the after picture")
    parser.add_argument("--author", default="agent", help="reply: who replies")
    parser.add_argument("--force", action="store_true", help="fixed: mark menu notes though the checks find new problems")
    arguments = parser.parse_args(argv)
    files = arguments.file or [NOTES, MENU_NOTES]
    entries = load(files)
    action = arguments.action
    if action == "list":
        print(list_text(entries, arguments.all))
        return 0
    if action == "batches":
        grouped = batches(entries)
        if not grouped:
            print("No notes to fix.")
            return 0
        for number, (key, group) in enumerate(grouped, 1):
            print(f"batch {number}: {key[0]}, {key[1]} ({len(group)} note{'s' if len(group) != 1 else ''}):"
                  f" {' '.join(note.get('id') for _, note in group)}")
        print("Take one batch each: `python tools/notes.py brief --batch N`, then `start` its notes.")
        return 0
    if action == "brief":
        if arguments.batch:
            grouped = batches(entries)
            if not 1 <= arguments.batch <= len(grouped):
                parser.error(f"there are {len(grouped)} batches")
            chosen = grouped[arguments.batch - 1][1]
        else:
            chosen = pick(entries, arguments.ids)
        print(brief(chosen) if chosen else "No open notes.", end="")
        return 0
    if action == "rule":
        return menu_notes.main(["rule"] + arguments.ids)
    if action == "reply":
        if len(arguments.ids) != 2:
            parser.error("reply takes a note's ID and the words")
        path, note = pick(entries, arguments.ids[:1])[0]
        menu_notes.add_reply(path, note["id"], arguments.ids[1], arguments.author)
        print(f"notes: replied on {note['id']}")
        return 0
    if not arguments.ids:
        parser.error(f"{action} needs the notes' IDs")
    chosen = pick(entries, arguments.ids)
    if action in ("fixed", "done"):
        return fixed(chosen, arguments.commit or menu_notes.head_commit(), arguments.after, arguments.replay, arguments.force)
    status = {"start": "in_progress", "reopen": "open", "close": "closed"}[action]
    for path, note in chosen:
        menu_notes.set_status(path, note["id"], status, changed=menu_notes.now())
    print(f"notes: {len(chosen)} note{'s' if len(chosen) != 1 else ''} {status.replace('_', ' ')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
