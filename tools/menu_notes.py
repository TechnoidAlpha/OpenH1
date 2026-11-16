#!/usr/bin/env python3
"""The menus' review notes: the menu studio's (F9: port/linux/game/menu_studio.c)
and the review app's (tools/menu_review.py), and what an agent does with them.

    python tools/menu_notes.py                       # the notes to act on, by screen
    python tools/menu_notes.py batches               # the notes to fix, grouped so agents do not collide
    python tools/menu_notes.py brief [ID...] [--batch N]   # what to change, where, the source first
    python tools/menu_notes.py start ID...           # taken (in progress)
    python tools/menu_notes.py reply ID "words"      # a reply on its thread (a question's answer)
    python tools/menu_notes.py fixed ID... [--commit SHA]  # re-rendered and checked, then ready for review
    python tools/menu_notes.py rule "SCOPE" "rule"   # a general preference, kept in port/assets/menus/STYLE.md
    python tools/menu_notes.py --all                 # every note

A note is a JSON line in the notes file (build/menu_notes.jsonl, out of git;
--file): its id; its kind ("fix", or "question": no code change until the
reviewer answers on its thread); its scope ("variant": this screen in this
variant, "screen": every variant of it, "everywhere": every widget like it,
of its kind or from the same generator function); its status ("open",
"in_progress", "fixed": ready for review, with the commit and the run that
shows it, and "closed" once the reviewer approves); the note; the screen; the
widget (its tag's name, path, words, box) and its source (the generator's
file and functions: tools/menu_sources.py); edits the reviewer proposed (a
box moved or resized: from, to and the delta in the menus' units; a label's
old and new words); its thread of replies; the input and variant; and a
picture of the screen with the widget outlined.

`fixed` re-renders the notes' screens in their variants against the commit's
parent (tools/menu_gallery.py --compare), runs the layout checks there, and
marks them ready for review only where nothing new is wrong; it prints the
before and after pictures, to be looked at first. The reviewer then approves
(closing the note) or reopens it with a reply in the app.
"""
import argparse
import fnmatch
import json
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import menu_gallery  # noqa: E402
import menu_sources  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
NOTES = ROOT / "build" / "menu_notes.jsonl"
STYLE = ROOT / "port" / "assets" / "menus" / "STYLE.md"
RUNS = ROOT / "build" / "menu_review" / "runs"

# a note's statuses: to act on (open, in_progress), ready for review (fixed, with its commit and run; "done",
# its earlier name), approved (closed)
STATUSES = ("open", "in_progress", "fixed", "closed", "done")
ACTIVE = ("open", "in_progress")
READY = ("fixed", "done")
KINDS = ("fix", "question")
SCOPES = {"variant": "this screen, in this variant only", "screen": "this screen, in every variant",
          "everywhere": "everywhere like this: every widget of its kind, or from the same generator function"}
# a note at most this long (the studio's field holds 200)
NOTE_LENGTH = 2000

menu_name = menu_sources.menu_name


def new_id() -> str:
    """a note's id as the studio makes them: the time's seconds and a part of a second, in hex"""
    moment = time.time()
    return f"{int(moment):08x}{int(moment * 1000) % 4096:03x}"


def now() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def read_notes(path: Path) -> list:
    """the notes of a file, in its order (a line that is not JSON skipped)"""
    notes = []
    if not path.is_file():
        return notes
    for line in path.read_text(errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            notes.append(json.loads(line))
        except json.JSONDecodeError:
            print(f"menu_notes: a line of {path} is not JSON: {line[:80]}", file=sys.stderr)
    return notes


def write_notes(path: Path, notes: list):
    """the file written again whole (beside it, then renamed over it)"""
    part = path.with_suffix(path.suffix + ".part")
    part.write_text("".join(json.dumps(note, ensure_ascii=False) + "\n" for note in notes))
    part.replace(path)


def append_note(path: Path, note: dict):
    """a note added to the file, as the studio adds its: a line at its end"""
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "a") as file:
        file.write(json.dumps(note, ensure_ascii=False) + "\n")


def update_note(path: Path, identifier: str, change) -> dict:
    """a note changed by a function of it, the file written again: the note"""
    notes = read_notes(path)
    for note in notes:
        if note.get("id") == identifier:
            change(note)
            write_notes(path, notes)
            return note
    raise KeyError(identifier)


def set_status(path: Path, identifier: str, status: str, **fields) -> dict:
    """a note's status changed (and fields with it: the commit, the run, who, when): the note"""
    if status not in STATUSES:
        raise ValueError(f"no status {status!r}")

    def change(note):
        note["status"] = status
        note.update({key: value for key, value in fields.items() if value is not None})

    return update_note(path, identifier, change)


def add_reply(path: Path, identifier: str, text: str, author: str) -> dict:
    """a reply on a note's thread"""
    text = str(text).strip()[:NOTE_LENGTH]
    if not text:
        raise ValueError("an empty reply")

    def change(note):
        note.setdefault("thread", []).append({"author": author or "reviewer", "time": now(), "text": text})

    return update_note(path, identifier, change)


# ---------- the notes' places

def kind_of(note: dict) -> str:
    return note.get("kind") if note.get("kind") in KINDS else "fix"


def scope_of(note: dict) -> str:
    return note.get("scope") if note.get("scope") in SCOPES else "variant"


def source_of(note: dict, widgets: dict = None) -> str:
    """the note's widget's source (or, on a screen as a whole, the screen's)"""
    # (a source is a generator's: the review app's first notes said "web" there)
    if str(note.get("source") or "").startswith("tools/"):
        return note["source"]
    widgets = widgets if widgets is not None else menu_sources.catalog()
    name = menu_name(note.get("widget")) if note.get("widget") else note.get("screen", "")
    found = widgets.get(name)
    return found["source"] if found else ""


def batch_key(note: dict, widgets: dict = None) -> tuple:
    """the batch a note to fix is in: its generator's file and the area of it (the function under the files'
    maker that made its widget, or the screen's menu), so that two agents with two batches do not edit the
    same code"""
    place = menu_sources.locate(source_of(note, widgets))
    calls = place["calls"]
    if place["file"].endswith("ce_menus.py") or not calls:
        parts = note.get("screen", "").split("/")
        area = parts[1] if len(parts) > 2 else parts[0]
    else:
        area = calls[1] if len(calls) > 1 and calls[0].endswith("_files") else calls[0]
    return place["file"] or "unknown", area


def batches(notes: list, widgets: dict = None) -> list:
    """the notes to fix (open, not questions), by batch: [((file, area), [note])], the largest first"""
    grouped = {}
    for note in notes:
        if note.get("status") == "open" and kind_of(note) == "fix":
            grouped.setdefault(batch_key(note, widgets), []).append(note)
    return sorted(grouped.items(), key=lambda item: (-len(item[1]), item[0]))


# ---------- the style guide

def style_rules(path: Path = STYLE) -> list:
    """the style guide's rules: [(scope, rule)], each a line "- (scope) rule" ("everywhere", or a pattern of
    screens' names: main_menu/settings_select/*)"""
    rules = []
    if not path.is_file():
        return rules
    for line in path.read_text().splitlines():
        match = re.match(r"^- \(([^)]+)\) (.+)$", line.strip())
        if match:
            rules.append((match.group(1).strip(), match.group(2).strip()))
    return rules


def rules_for(screen: str, rules: list) -> list:
    """the rules that apply to a screen"""
    return [rule for scope, rule in rules if scope == "everywhere" or fnmatch.fnmatch(screen, scope)]


def add_rule(scope: str, rule: str, path: Path = STYLE):
    """a rule added at the end of the style guide's rules"""
    rule = " ".join(rule.split())
    if not rule or not scope.strip() or ")" in scope:
        raise ValueError("a rule needs a scope and words")
    text = path.read_text()
    if not text.endswith("\n"):
        text += "\n"
    path.write_text(text + f"- ({scope.strip()}) {rule}\n")


# ---------- the commands' words

def pick(notes: list, ids: list, default=ACTIVE) -> list:
    """the notes of the IDs given (each by its first characters; those of the default statuses for none)"""
    if not ids:
        return [note for note in notes if note.get("status") in default]
    chosen = []
    for prefix in ids:
        matches = [note for note in notes if str(note.get("id", "")).startswith(prefix)]
        if len(matches) != 1:
            raise SystemExit(f"menu_notes: {prefix!r} is {'no note' if not matches else 'more than one note'}")
        chosen.append(matches[0])
    return chosen


def by_screen(notes: list) -> dict:
    grouped = {}
    for note in notes:
        grouped.setdefault(note.get("screen", "?"), []).append(note)
    return grouped


def describe(note: dict) -> str:
    widget = menu_name(note.get("widget")) if note.get("widget") else "the screen as a whole"
    words = " ".join((note.get("widget_text") or "").split())
    return f"{widget}" + (f' ("{words[:60]}")' if words else "")


def status_word(note: dict) -> str:
    status = note.get("status")
    if status == "open":
        return ""
    if status in READY:
        return f" [ready for review {str(note.get('commit', ''))[:9]}]"
    return f" [{str(status).replace('_', ' ')}]"


def list_notes(notes: list, everything: bool) -> str:
    shown = notes if everything else [note for note in notes if note.get("status") in ACTIVE]
    if not shown:
        return "No open notes." if not everything else "No notes."
    lines = []
    for screen, group in by_screen(shown).items():
        lines.append(f"{screen}")
        for note in group:
            question = " (question)" if kind_of(note) == "question" else ""
            lines.append(f"  {note.get('id')}{status_word(note)}{question}  {describe(note)}: {note.get('note')}")
    return "\n".join(lines)


def edits_lines(note: dict) -> list:
    edits = note.get("edits") or {}
    lines = []
    box = edits.get("box")
    if box:
        delta = box.get("delta", [0, 0, 0, 0])
        lines.append(f"   proposed box: {box.get('from')} -> {box.get('to')} (menu units: moved {delta[0]:+g},"
                     f" {delta[1]:+g}; {delta[2]:+g} wider, {delta[3]:+g} taller)")
    text = edits.get("text")
    if text:
        lines.append(f'   proposed words: "{text.get("old")}" -> "{text.get("new")}"')
    return lines


def brief(notes: list, rules: list = None) -> str:
    """what an agent needs for each note, its source first: the scope, the widget, the edits proposed, the
    thread, the picture, and the style guide's rules for its screen; questions after the fixes (answered on
    their threads, no code changed); and the way of working"""
    rules = style_rules() if rules is None else rules
    widgets = menu_sources.catalog()
    screens = {screen.name: screen for screen in menu_gallery.enumerate_screens(ROOT / menu_gallery.MENUS, True)}
    fixes = [note for note in notes if kind_of(note) == "fix"]
    questions = [note for note in notes if kind_of(note) == "question"]
    out = [f"{len(fixes)} menu note{'s' if len(fixes) != 1 else ''} to fix, {len(questions)} question"
           f"{'s' if len(questions) != 1 else ''} to answer.",
           "Change the generator at each note's source (the menu files in port/assets/menus/ce are generated: "
           "tools/port_settings.py runs alone, tools/ce_menus.py needs the PC tags), run it, check `git status` "
           "shows only the screens meant to change, commit, then `python tools/menu_notes.py fixed <id> --commit "
           "<sha>`: it re-renders the screens in their variants, checks them and prints the before | after, which "
           "you look at; then it is ready for review. A note that states a general preference becomes a rule: "
           "`python tools/menu_notes.py rule \"<scope>\" \"<rule>\"`. Questions: answer with "
           "`python tools/menu_notes.py reply <id> \"...\"` and change nothing until the reviewer answers.", ""]
    everywhere = [rule for scope, rule in rules if scope == "everywhere"]
    if everywhere:
        out[-1:] = ["Style rules everywhere: " + " | ".join(everywhere), ""]
    rules = [(scope, rule) for scope, rule in rules if scope != "everywhere"]
    for number, note in enumerate(fixes + questions, 1):
        screen = note.get("screen", "?")
        widget = menu_name(note.get("widget")) if note.get("widget") else ""
        found = widgets.get(widget or screen) or {}
        file = found.get("file") or (screens[screen].file if screen in screens else "?")
        heading = "QUESTION (answer on its thread; change no code until the reviewer answers)" \
            if kind_of(note) == "question" else "FIX"
        out.append(f"{number}. [{note.get('id')}] {heading}: {note.get('note')}")
        out.append(f"   source: {menu_sources.describe(source_of(note, widgets))}")
        out.append(f"   scope: {SCOPES[scope_of(note)]}")
        out.append(f"   screen: {screen} ({file})")
        if widget:
            out.append(f"   widget: {widget}")
            path = " > ".join(menu_name(part).rsplit("/", 1)[-1] for part in note.get("widget_path", []))
            if path:
                out.append(f"   its path: {path}")
            words = " ".join((note.get("widget_text") or "").split())
            if words:
                out.append(f'   its words: "{words}"')
            if note.get("box"):
                out.append(f"   its box (menu units, 640x480 centred): {note.get('box')}")
        elif note.get("area"):
            held = ", ".join(menu_name(name).rsplit("/", 1)[-1] for name in note.get("area_widgets", []))
            out.append(f"   an area of the screen (menu units): {note.get('area')}" + (f", holding {held}" if held else ""))
        else:
            out.append("   on the screen as a whole")
        out += edits_lines(note)
        for reply in note.get("thread", []):
            out.append(f"   reply from {reply.get('author')}: {reply.get('text')}")
        seen = note.get("variant") or "/".join(part for part in (note.get("input"), note.get("shape")) if part)
        out.append(f"   seen in: {seen or '?'}")
        if note.get("picture"):
            out.append(f"   picture: {note.get('picture')}")
        applying = rules_for(screen, rules)
        if applying:
            out.append("   style rules for this screen too: " + " | ".join(applying))
        out.append("")
    return "\n".join(out).rstrip() + "\n"


def batches_text(notes: list) -> str:
    grouped = batches(notes)
    if not grouped:
        return "No notes to fix."
    lines = []
    for number, ((file, area), group) in enumerate(grouped, 1):
        lines.append(f"batch {number}: {file}, {area} ({len(group)} note{'s' if len(group) != 1 else ''}):"
                     f" {' '.join(note.get('id') for note in group)}")
    lines.append("Take one batch each: `python tools/menu_notes.py brief --batch N`, then `start` its notes.")
    return "\n".join(lines)


# ---------- fixed: re-rendered, checked, looked at

def fixed(path: Path, chosen: list, commit: str, force: bool = False) -> int:
    """the notes' screens re-rendered against the commit's parent in their variants and checked; the notes
    ready for review where nothing new is wrong there: the exit status"""
    screens = sorted({note.get("screen", "") for note in chosen})
    variants = menu_gallery.parse_variants("default")
    out = RUNS / f"fixed-{commit[:9]}-{time.strftime('%H%M%S')}"
    pattern = "^(" + "|".join(re.escape(screen) for screen in screens) + ")$"
    command = [sys.executable, str(ROOT / "tools" / "menu_gallery.py"), "--compare", f"{commit}~1", "--screens",
               pattern, "--variants", ",".join(variants), "--out", str(out), "--no-fail"]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode or not (out / "report.json").is_file():
        print(result.stderr[-2000:], file=sys.stderr)
        return 1
    report = json.loads((out / "report.json").read_text())
    new = report.get("new_problems", [])
    for screen in screens:
        slug = re.sub(r"[^A-Za-z0-9_.-]+", "-", screen).strip("-")
        for variant in variants:
            print(f"{screen} [{variant}]: before {out}/before/{variant}/{slug}.jpg"
                  f"  after {out}/after/{variant}/{slug}.jpg")
    for problem in new:
        print(f"menu_notes: new: {problem['variant']}: {problem['screen']}: {problem['check']}: {problem['message']}")
    if new and not force:
        print("menu_notes: the fix brings new layout problems; not marked (--force marks it anyway)", file=sys.stderr)
        return 1
    for note in chosen:
        set_status(path, note["id"], "fixed", commit=commit, changed=now(),
                   review={"run": out.name, "new_problems": len(new)})
    print(f"menu_notes: {len(chosen)} note{'s' if len(chosen) != 1 else ''} ready for review ({commit[:9]},"
          f" run {out.name}). Look at the pictures above.")
    return 0


def note_screens(path: Path) -> list:
    """the screens notes are on (menu_gallery.py --notes's), in the file's order"""
    seen = []
    for note in read_notes(path):
        if note.get("screen") and note["screen"] not in seen:
            seen.append(note["screen"])
    return seen


def head_commit() -> str:
    return subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short=12", "HEAD"], capture_output=True,
                          text=True).stdout.strip()


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="The menus' review notes.", allow_abbrev=False)
    parser.add_argument("action", nargs="?", default="list",
                        choices=("list", "brief", "batches", "start", "reply", "fixed", "done", "reopen", "close",
                                 "rule"))
    parser.add_argument("ids", nargs="*", help="notes' IDs (their first characters); reply: an ID and the words; "
                                               "rule: a scope and the words")
    parser.add_argument("--file", type=Path, default=NOTES, help="the notes file (build/menu_notes.jsonl)")
    parser.add_argument("--all", action="store_true", help="list every note")
    parser.add_argument("--batch", type=int, help="brief: a batch's notes (menu_notes.py batches)")
    parser.add_argument("--commit", help="fixed: the commit that addressed them (HEAD's)")
    parser.add_argument("--author", default="agent", help="reply: who replies")
    parser.add_argument("--force", action="store_true", help="fixed: mark them though the checks find new problems")
    arguments = parser.parse_args(argv)
    notes = read_notes(arguments.file)
    action = arguments.action
    if action == "list":
        print(list_notes(notes, arguments.all))
        return 0
    if action == "batches":
        print(batches_text(notes))
        return 0
    if action == "brief":
        if arguments.batch:
            grouped = batches(notes)
            if not 1 <= arguments.batch <= len(grouped):
                parser.error(f"there are {len(grouped)} batches")
            chosen = grouped[arguments.batch - 1][1]
        else:
            chosen = pick(notes, arguments.ids)
        print(brief(chosen) if chosen else "No open notes.")
        return 0
    if action == "rule":
        if len(arguments.ids) != 2:
            parser.error('rule takes a scope ("everywhere" or a pattern of screens) and the rule')
        add_rule(*arguments.ids)
        print(f"menu_notes: added to {STYLE.relative_to(ROOT)}")
        return 0
    if action == "reply":
        if len(arguments.ids) != 2:
            parser.error("reply takes a note's ID and the words")
        note = pick(notes, arguments.ids[:1])[0]
        add_reply(arguments.file, note["id"], arguments.ids[1], arguments.author)
        print(f"menu_notes: replied on {note['id']}")
        return 0
    if not arguments.ids:
        parser.error(f"{action} needs the notes' IDs")
    chosen = pick(notes, arguments.ids)
    if action in ("fixed", "done"):
        return fixed(arguments.file, chosen, arguments.commit or head_commit(), arguments.force)
    status = {"start": "in_progress", "reopen": "open", "close": "closed"}[action]
    for note in chosen:
        set_status(arguments.file, note["id"], status, changed=now())
    print(f"menu_notes: {len(chosen)} note{'s' if len(chosen) != 1 else ''} {status.replace('_', ' ')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
