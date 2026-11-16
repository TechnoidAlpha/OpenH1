#!/usr/bin/env python3
"""The menus reviewed on the gallery's pictures, in a browser: a design tool of
this machine's for the maintainer's phone and desktop, and the agents who act
on its notes.

    python tools/menu_review.py serve [--host 0.0.0.0] [--port 8765]
    python tools/menu_review.py render [--ref REF]      # the noted screens again, before | after
    python tools/menu_review.py secret                  # print the login's secret

This file serves data, pictures and notes as a JSON API; the client is
tools/menu_review/ (TypeScript, its build committed in static/: an inspector
in the manner of a design tool's: select, drill down, layers, inspect, zoom
and pan, proposed edits with snapping, comments pinned to widgets, before |
after). It serves the galleries in build/ (tools/menu_gallery.py's:
build/menu_gallery, "latest", and the runs in build/menu_review/runs: a
--compare run's before and after, a fix's), behind a shared secret (--secret,
MENU_REVIEW_SECRET, or one made once in build/menu_review/secret; typed at
/login, or a link /login?key=<secret> that signs a phone in once): the
pictures are the game's, so the app is never open, and nothing leaves this
machine. Bind it to the LAN, or to a Tailscale or Fabric address, with --host.

Notes go to the studio's notes file (build/menu_notes.jsonl,
tools/menu_notes.py's format, out of git) with "source": "web", the author, the
run and where on the picture, the widget's source (tools/menu_sources.py), the
kind (fix or question), the scope, the edits proposed and a copy of the picture
with the widget or area outlined (build/menu_notes/). Render (the client's
button, or `render`) shows the noted screens again before | after
(tools/menu_gallery.py --compare <ref> --notes). --watch-brief (off by default)
writes tools/menu_notes.py's brief as notes arrive, for an agent to pick up.
"""
import argparse
import hashlib
import hmac
import json
import mimetypes
import os
import re
import secrets
import shutil
import subprocess
import sys
import threading
import time
import urllib.parse
from http import cookies
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import menu_gallery  # noqa: E402
import menu_notes  # noqa: E402
import menu_sources  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
REVIEW = BUILD / "menu_review"
STATIC = Path(__file__).resolve().parent / "menu_review" / "static"
COOKIE = "menu_review"
NOTES_WATCH_SECONDS = 2.0

# the studio's inputs and shapes as the gallery's variants
STUDIO_VARIANTS = {("keyboard", "16:9"): "keyboard-16x9", ("controller", "16:10"): "controller-16x10",
                   ("touch", "phone 20:9"): "touch-phone", ("keyboard", "4:3"): "keyboard-4x3",
                   ("keyboard", "21:9"): "keyboard-21x9"}
INPUT_VARIANTS = {"keyboard": "keyboard-16x9", "controller": "controller-16x10", "touch": "touch-phone"}


# ---------- the login

def secret_of(argument: str = None, review: Path = REVIEW) -> str:
    """the login's secret: given, MENU_REVIEW_SECRET's, or the one made once in build/menu_review/secret"""
    if argument:
        return argument
    if os.environ.get("MENU_REVIEW_SECRET"):
        return os.environ["MENU_REVIEW_SECRET"]
    path = review / "secret"
    if path.is_file():
        return path.read_text().strip()
    review.mkdir(parents=True, exist_ok=True)
    made = secrets.token_urlsafe(9)
    path.write_text(made + "\n")
    os.chmod(path, 0o600)
    return made


def token_of(secret: str) -> str:
    """the cookie a login gives: the secret's HMAC (the secret itself never stored in the browser)"""
    return hmac.new(secret.encode(), b"menu-review", hashlib.sha256).hexdigest()


def signed_in(cookie_header: str, secret: str) -> bool:
    jar = cookies.SimpleCookie()
    try:
        jar.load(cookie_header or "")
    except cookies.CookieError:
        return False
    given = jar.get(COOKIE)
    return bool(given) and hmac.compare_digest(given.value, token_of(secret))


def local_next(where: str) -> str:
    """where a sign-in link goes on to: a page of the app's own (a relative path), else the app"""
    if not where or where.startswith(("/", "\\")) or ":" in where.split("?", 1)[0] or "//" in where:
        return "./"
    return where


# ---------- the galleries

def find_runs(build: Path = BUILD) -> list:
    """the galleries there are: build/menu_gallery ("latest") and build/menu_review/runs/<name>, newest first,
    each (name, folder, has a before)"""
    runs = []
    latest = build / "menu_gallery"
    if (latest / "after").is_dir():
        runs.append(("latest", latest))
    folder = build / "menu_review" / "runs"
    if folder.is_dir():
        for run in sorted(folder.iterdir(), key=lambda path: path.stat().st_mtime, reverse=True):
            if (run / "after").is_dir():
                runs.append((run.name, run))
    return [(name, path, (path / "before").is_dir()) for name, path in runs]


def run_folder(name: str, build: Path = BUILD):
    for run, folder, _ in find_runs(build):
        if run == name:
            return folder
    return None


def picture_of(folder: Path, side: str, variant: str, slug: str):
    """a gallery's picture of a screen in a variant (JPEG, else PNG), or None"""
    for suffix in (".jpg", ".png"):
        path = folder / side / variant / (slug + suffix)
        if path.is_file():
            return path
    return None


def thumb_of(folder: Path, variant: str, slug: str):
    for name in (f"{slug}.thumb.jpg", f"{slug}.thumb.png", f"{slug}.jpg", f"{slug}.png"):
        if (folder / "after" / variant / name).is_file():
            return f"after/{variant}/{name}"
    return None


def ordered_variants(names) -> list:
    names = list(names)
    return [name for name in menu_gallery.VARIANTS if name in names] + \
        sorted(name for name in names if name not in menu_gallery.VARIANTS)


def run_screens(folder: Path) -> list:
    """the screens a gallery has pictures of, in the menu files' order, with its variants: [(screen, [variant])]"""
    variants = ordered_variants(path.name for path in (folder / "after").iterdir() if path.is_dir())
    found = []
    for screen in menu_gallery.enumerate_screens(ROOT / menu_gallery.MENUS, True):
        shown = [variant for variant in variants if picture_of(folder, "after", variant, screen.slug)]
        if shown:
            found.append((screen, shown))
    return found


def menu_of(screen) -> str:
    """the menu a screen is in, for the gallery's groups: its path's first part after main_menu"""
    parts = screen.name.split("/")
    if parts[0] != "main_menu" or len(parts) < 3:
        return "main menu" if parts[0] == "main_menu" else parts[0]
    return parts[1].replace("_select", "").replace("_", " ")


def safe_file(relative: str, roots: list):
    """a file under one of the roots, by a path relative to the first that has it; None for anything else (no
    .., no other place)"""
    if not relative or "\x00" in relative:
        return None
    for root in roots:
        if root is None:
            continue
        root = root.resolve()
        path = (root / relative).resolve()
        if path.is_relative_to(root) and path.is_file():
            return path
    return None


def picture_size(path: Path):
    """a picture's size: PNG's and JPEG's headers read (PIL's where it is there)"""
    try:
        from PIL import Image
        with Image.open(path) as image:
            return image.size
    except ImportError:
        pass
    data = path.read_bytes()
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return int.from_bytes(data[16:20], "big"), int.from_bytes(data[20:24], "big")
    index = 2
    while index < len(data):
        marker, length = data[index + 1], int.from_bytes(data[index + 2:index + 4], "big")
        if marker in (0xC0, 0xC2):
            return int.from_bytes(data[index + 7:index + 9], "big"), int.from_bytes(data[index + 5:index + 7], "big")
        index += 2 + length
    raise ValueError(f"no size in {path}")


# ---------- a place on a picture, as the menus' widget there

def menu_point(layout: dict, size, x: float, y: float):
    """a point of a picture (its pixels, of its size) in the menus' units, as the layout has them"""
    x0, y0, x1, y1 = layout["screen"]
    width, height = size
    return x0 + x * (x1 - x0) / width, y0 + y * (y1 - y0) / height


def picture_point(layout: dict, size, x: float, y: float):
    """a point in the menus' units on a picture of that size"""
    x0, y0, x1, y1 = layout["screen"]
    width, height = size
    return (x - x0) * width / (x1 - x0), (y - y0) * height / (y1 - y0)


def widget_at(layout: dict, x: float, y: float):
    """the widget a point (the menus' units) is on: the last drawn that holds it (the deepest, on top), those
    over the whole screen passed over as the screen's own (menu_layout.c's menu_layout_widget_at); None"""
    widgets = [item for item in layout.get("items", []) if item["kind"] == "widget"]
    for widget in reversed(widgets):
        x0, y0, x1, y1 = widget["box"]
        if x0 <= x < x1 and y0 <= y < y1 and (x1 - x0 < 640 or y1 - y0 < 480):
            return widget
    return None


def widget_named(layout: dict, name: str):
    """a layout's widget of a tag name (the last drawn of it), or None"""
    found = None
    for item in layout.get("items", []):
        if item["kind"] == "widget" and item["name"] == name:
            found = item
    return found


def widget_fields(layout: dict, widget) -> dict:
    """a widget as a note has it (the studio's menu_layout_widget_json): its name, its path down the screen,
    the words it and its children draw, its box, its source"""
    if widget is None:
        return {"widget": None, "widget_path": [], "widget_text": "", "box": None}
    widgets = {item["id"]: item for item in layout.get("items", []) if item["kind"] == "widget"}
    path, step = [], widget["id"]
    while step in widgets and len(path) < 32:
        path.append(widgets[step]["name"])
        step = widgets[step]["parent"]
    words = []
    for item in layout.get("items", []):
        if item["kind"] != "text":
            continue
        owner = item.get("widget")
        while owner in widgets and owner != widget["id"]:
            owner = widgets[owner]["parent"]
        if owner == widget["id"]:
            words.append(" ".join(item.get("text", "").split()))
    fields = {"widget": widget["name"], "widget_path": list(reversed(path)), "widget_text": " ".join(words),
              "box": list(widget["box"])}
    if widget.get("source"):
        fields["source"] = widget["source"]
    return fields


def widgets_in(layout: dict, area) -> list:
    """the widgets with words wholly in an area (the menus' units): their names, for an area's note"""
    x0, y0, x1, y1 = area
    widgets = {item["id"]: item for item in layout.get("items", []) if item["kind"] == "widget"}
    found = []
    for item in layout.get("items", []):
        if item["kind"] != "text" or not item.get("extent") or item.get("widget") not in widgets:
            continue
        ex0, ey0, ex1, ey1 = item["extent"]
        name = widgets[item["widget"]]["name"]
        if ex0 >= x0 and ey0 >= y0 and ex1 <= x1 and ey1 <= y1 and name not in found:
            found.append(name)
    return found[:12]


def variant_input_shape(variant: str):
    """the studio's input and shape names of a gallery's variant"""
    for (input_name, shape), name in STUDIO_VARIANTS.items():
        if name == variant:
            return input_name, shape
    return "keyboard", variant


def note_variant(note: dict) -> str:
    """the gallery's variant a note was seen in: the web's own, else the studio's input and shape's"""
    if note.get("variant"):
        return note["variant"]
    return STUDIO_VARIANTS.get((note.get("input"), note.get("shape")),
                               INPUT_VARIANTS.get(note.get("input"), "keyboard-16x9"))


def clean_box(value):
    """a box from a request ([x0, y0, x1, y1] of numbers, x0 <= x1), rounded; None for anything else"""
    if not isinstance(value, (list, tuple)) or len(value) != 4:
        return None
    try:
        x0, y0, x1, y1 = (round(float(number), 1) for number in value)
    except (TypeError, ValueError):
        return None
    if x1 < x0 or y1 < y0 or max(abs(x0), abs(y0), abs(x1), abs(y1)) > 10000:
        return None
    return [x0, y0, x1, y1]


def clean_edits(edits, widget) -> dict:
    """the edits a request proposes for its widget: a box moved or resized (from the widget's box: to, and the
    delta as moved x, moved y, wider, taller, in the menus' units), the label's words (old, new)"""
    cleaned = {}
    if not isinstance(edits, dict) or widget is None:
        return cleaned
    to = clean_box((edits.get("box") or {}).get("to") if isinstance(edits.get("box"), dict) else None)
    if to:
        before = list(widget["box"])
        delta = [round(to[0] - before[0], 1), round(to[1] - before[1], 1),
                 round((to[2] - to[0]) - (before[2] - before[0]), 1), round((to[3] - to[1]) - (before[3] - before[1]), 1)]
        if any(delta):
            cleaned["box"] = {"from": before, "to": to, "delta": delta}
    text = edits.get("text") if isinstance(edits.get("text"), dict) else None
    if text and str(text.get("new", "")) != str(text.get("old", "")):
        cleaned["text"] = {"old": str(text.get("old", ""))[:500], "new": str(text.get("new", ""))[:500]}
    return cleaned


def make_web_note(layout: dict, size, request: dict, run: str, picture: str = None) -> dict:
    """a note from the app: on a widget (named, or at a point of the picture), an area (a drag), a point (a
    pin) or the screen, in the studio's notes' format with the web's fields: its kind, scope, edits"""
    kind = request.get("kind", "widget")
    image = {"run": run, "variant": request["variant"], "size": list(size)}
    widget = None
    area = None
    if kind == "widget":
        if request.get("widget"):
            widget = widget_named(layout, str(request["widget"]))
        else:
            x, y = menu_point(layout, size, float(request["x"]), float(request["y"]))
            widget = widget_at(layout, x, y)
        if request.get("x") is not None:
            image.update(x=round(float(request["x"])), y=round(float(request["y"])))
    elif kind in ("area", "point"):
        px0, py0 = float(request["x"]), float(request["y"])
        px1, py1 = (float(request.get("x1", px0)), float(request.get("y1", py0))) if kind == "area" else (px0, py0)
        px0, px1 = sorted((px0, px1))
        py0, py1 = sorted((py0, py1))
        mx0, my0 = menu_point(layout, size, px0, py0)
        mx1, my1 = menu_point(layout, size, px1, py1)
        area = [round(mx0), round(my0), round(mx1), round(my1)]
        image.update(x=round(px0), y=round(py0))
        if kind == "area":
            image.update(x1=round(px1), y1=round(py1))
    input_name, shape = variant_input_shape(request["variant"])
    tag = layout.get("screen_name", "")
    note = {"id": menu_notes.new_id(), "status": "open", "time": menu_notes.now(),
            "kind": request.get("note_kind") if request.get("note_kind") in menu_notes.KINDS else "fix",
            "scope": request.get("scope") if request.get("scope") in menu_notes.SCOPES else "variant",
            "note": str(request.get("note", ""))[:menu_notes.NOTE_LENGTH].strip(),
            "screen": menu_notes.menu_name(tag) or request.get("screen", ""), "screen_tag": tag,
            **widget_fields(layout, widget),
            "input": input_name, "shape": shape, "variant": request["variant"],
            "screen_width": layout["screen"][2] - layout["screen"][0],
            "source": None, "author": str(request.get("author", "")).strip()[:60] or "reviewer",
            "origin": "web", "target": kind if widget is not None or kind != "widget" else "screen", "image": image,
            "thread": [], "picture": picture}
    note["source"] = widget.get("source") if widget is not None and widget.get("source") else \
        menu_notes.source_of({**note, "source": None})
    edits = clean_edits(request.get("edits"), widget)
    if edits:
        note["edits"] = edits
    if area is not None:
        note["area"] = area
        note["area_widgets"] = widgets_in(layout, area)
    return note


def outline_picture(source: Path, destination: Path, layout: dict, note: dict) -> Path:
    """the picture with what the note is about outlined, and the box proposed (PIL's, where it is there; else
    as it is)"""
    destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        shutil.copy2(source, destination.with_suffix(source.suffix))
        return destination.with_suffix(source.suffix)
    with Image.open(source) as opened:
        image = opened.convert("RGB")
    draw = ImageDraw.Draw(image)
    width = max(2, image.width // 400)

    def rectangle(box, colour):
        x0, y0 = picture_point(layout, image.size, box[0], box[1])
        x1, y1 = picture_point(layout, image.size, box[2], box[3])
        if note.get("target") == "point":
            x0, y0, x1, y1 = x0 - 14, y0 - 14, x0 + 14, y0 + 14
        draw.rectangle([x0, y0, x1, y1], outline=colour, width=width)

    box = note.get("box") or note.get("area")
    if box:
        rectangle(box, (255, 255, 0))
    proposed = (note.get("edits") or {}).get("box")
    if proposed:
        rectangle(proposed["to"], (255, 64, 255))
    image.save(destination, "PNG")
    return destination


# ---------- before | after of a note fixed

def note_before_after(note: dict, build: Path = BUILD):
    """the run that shows a note's screen before and after in its variant: the one its fix recorded
    (menu_notes.py fixed), else the newest with it: (run, before, after relative paths) or None"""
    screen = next((screen for screen in menu_gallery.enumerate_screens(ROOT / menu_gallery.MENUS, True)
                   if screen.name == note.get("screen")), None)
    if screen is None:
        return None
    variant = note_variant(note)
    runs = find_runs(build)
    recorded = (note.get("review") or {}).get("run")
    runs.sort(key=lambda run: run[0] != recorded)
    for name, folder, compared in runs:
        if not compared:
            continue
        before = picture_of(folder, "before", variant, screen.slug)
        after = picture_of(folder, "after", variant, screen.slug)
        if before and after:
            return name, before.relative_to(folder).as_posix(), after.relative_to(folder).as_posix()
    return None


def render_ref(notes: list) -> str:
    """the ref a fix batch is compared with: the parent of the earliest commit the fixed notes name (HEAD~1
    when none does)"""
    commits = [note["commit"] for note in notes
               if note.get("status") in menu_notes.READY + ("closed",) and note.get("commit")]
    earliest = None
    for commit in commits:
        if subprocess.run(["git", "-C", str(ROOT), "cat-file", "-e", f"{commit}^{{commit}}"],
                          capture_output=True).returncode:
            continue
        if earliest is None or subprocess.run(["git", "-C", str(ROOT), "merge-base", "--is-ancestor", commit,
                                               earliest], capture_output=True).returncode == 0:
            earliest = commit
    return f"{earliest}~1" if earliest else "HEAD~1"


def render(notes_path: Path, ref: str = None, extra: list = ()) -> tuple:
    """the noted screens again, before | after (menu_gallery.py --compare REF --notes), into a run of their own:
    (the run's folder, the exit status, its output's tail)"""
    ref = ref or render_ref(menu_notes.read_notes(notes_path))
    out = REVIEW / "runs" / f"fixes-{time.strftime('%Y%m%d-%H%M%S')}"
    command = [sys.executable, str(ROOT / "tools" / "menu_gallery.py"), "--compare", ref, "--notes", str(notes_path),
               "--out", str(out), "--no-fail", *extra]
    result = subprocess.run(command, capture_output=True, text=True)
    return out, result.returncode, (result.stdout + result.stderr)[-2000:]


# ---------- the API

class Review:
    """the app's state: its paths and secret, a lock for the notes file, the render's progress"""

    def __init__(self, secret: str, notes: Path, build: Path = BUILD):
        self.secret = secret
        self.notes = notes
        self.build = build
        self.lock = threading.Lock()
        self.rendering = None
        self.rendered = ""

    def runs(self) -> dict:
        return {"runs": [{"name": name, "compared": compared} for name, _, compared in find_runs(self.build)],
                "render": self.rendering or self.rendered, "variants": list(menu_gallery.VARIANTS),
                "scopes": menu_notes.SCOPES, "kinds": list(menu_notes.KINDS)}

    def screens(self, run: str) -> dict:
        folder = run_folder(run, self.build)
        if folder is None:
            return {"error": "no such run"}
        counts = {}
        for note in menu_notes.read_notes(self.notes):
            if note.get("status") in menu_notes.ACTIVE:
                counts[note.get("screen")] = counts.get(note.get("screen"), 0) + 1
        found = []
        for screen, variants in run_screens(folder):
            found.append({"name": screen.name, "label": screen.label, "menu": menu_of(screen), "file": screen.file,
                          "generator": screen.generator, "variants": variants, "open": counts.get(screen.name, 0),
                          "thumbs": {variant: f"file/{urllib.parse.quote(run)}/{thumb_of(folder, variant, screen.slug)}"
                                     for variant in variants}})
        return {"run": run, "compared": (folder / "before").is_dir(), "screens": found}

    def screen(self, run: str, name: str) -> dict:
        folder = run_folder(run, self.build)
        screen = next((screen for screen in menu_gallery.enumerate_screens(ROOT / menu_gallery.MENUS, True)
                       if screen.name == name), None)
        if folder is None or screen is None:
            return {"error": "no such screen"}
        widgets = menu_sources.catalog()
        variants = {}
        names = set()
        for variant in ordered_variants(path.name for path in (folder / "after").iterdir() if path.is_dir()):
            after = picture_of(folder, "after", variant, screen.slug)
            layout_path = folder / "after" / variant / f"{screen.slug}.json"
            if after is None:
                continue
            layout = json.loads(layout_path.read_text()) if layout_path.is_file() else None
            if layout is not None:
                menu_sources.enrich_layout(layout, widgets)
                names.update(menu_sources.menu_name(item["name"]) for item in layout["items"]
                             if item["kind"] == "widget")
            before = picture_of(folder, "before", variant, screen.slug)
            before_layout = folder / "before" / variant / f"{screen.slug}.json"
            variants[variant] = {
                "image": f"file/{urllib.parse.quote(run)}/{after.relative_to(folder).as_posix()}",
                "size": list(picture_size(after)), "layout": layout,
                "before": f"file/{urllib.parse.quote(run)}/{before.relative_to(folder).as_posix()}" if before else None,
                "before_layout": json.loads(before_layout.read_text()) if before and before_layout.is_file() else None}
        attributes = {}
        for widget in names:
            if widget in widgets:
                found = widgets[widget]
                attributes[widget] = {**found, "place": menu_sources.locate(found["source"])}
        return {"name": screen.name, "label": screen.label, "file": screen.file, "generator": screen.generator,
                "run": run, "variants": variants, "widgets": attributes,
                "rules": menu_notes.rules_for(screen.name, menu_notes.style_rules())}

    def note_list(self) -> list:
        notes = menu_notes.read_notes(self.notes)
        for note in notes:
            note["variant"] = note_variant(note)
            if note.get("status") in menu_notes.READY + ("closed",):
                pair = note_before_after(note, self.build)
                if pair:
                    run, before, after = pair
                    note["before_after"] = {"run": run, "before": f"file/{urllib.parse.quote(run)}/{before}",
                                            "after": f"file/{urllib.parse.quote(run)}/{after}"}
            if note.get("picture"):
                note["picture_url"] = f"notefile/{urllib.parse.quote(Path(note['picture']).name)}"
        return notes

    def add_note(self, request: dict) -> dict:
        run, variant, screen_name = request.get("run", ""), request.get("variant", ""), request.get("screen", "")
        folder = run_folder(run, self.build)
        screen = next((screen for screen in menu_gallery.enumerate_screens(ROOT / menu_gallery.MENUS, True)
                       if screen.name == screen_name), None)
        if folder is None or screen is None or variant not in menu_gallery.VARIANTS:
            return {"error": "no such screen"}
        if not str(request.get("note", "")).strip() and not request.get("edits"):
            return {"error": "an empty note"}
        side = "before" if request.get("side") == "before" else "after"
        layout_path = folder / side / variant / f"{screen.slug}.json"
        picture = picture_of(folder, side, variant, screen.slug)
        if not layout_path.is_file() or picture is None:
            return {"error": "that picture has no layout"}
        layout = menu_sources.enrich_layout(json.loads(layout_path.read_text()))
        note = make_web_note(layout, picture_size(picture), request, run)
        if not note["note"]:
            note["note"] = "(the edits proposed)"
        note["screen"] = screen_name
        out = self.notes.parent / "menu_notes" / f"web-{note['id']}.png"
        note["picture"] = str(outline_picture(picture, out, layout, note))
        with self.lock:
            menu_notes.append_note(self.notes, note)
        return note

    def change(self, identifier: str, request: dict) -> dict:
        """a note's reply or status: {"reply": words, "author"}, {"status": ...}"""
        with self.lock:
            if request.get("reply"):
                note = menu_notes.add_reply(self.notes, identifier, request["reply"], str(request.get("author", ""))[:60])
            if request.get("status"):
                note = menu_notes.set_status(self.notes, identifier, request["status"], changed=menu_notes.now(),
                                             changed_by=str(request.get("author", ""))[:60] or None)
            if not request.get("reply") and not request.get("status"):
                raise ValueError("nothing to change")
        return note

    def start_render(self):
        if self.rendering:
            return

        def work():
            out, status, tail = render(self.notes)
            self.rendered = f"Rendered {out.name}" if status == 0 else f"The render failed: {tail[-300:]}"
            self.rendering = None

        self.rendering = "Rendering the noted screens..."
        threading.Thread(target=work, daemon=True).start()


LOGIN_PAGE = """<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>Menu review</title>
<style>body{margin:0;min-height:100vh;display:grid;place-items:center;background:#0e1116;color:#e6edf3;
font:15px system-ui,sans-serif}form{display:grid;gap:10px;width:min(320px,90vw)}input,button{font:inherit;padding:10px;
border-radius:8px;border:1px solid #30363d;background:#161b22;color:inherit}button{background:#2f81f7;border-color:#2f81f7;
color:#fff}p{color:#f85149;margin:0}</style></head><body><form method="post" action="login"><h1>Menu review</h1>%s
<input type="password" name="secret" placeholder="Secret" autocomplete="current-password" autofocus>
<button>Sign in</button></form></body></html>"""


def make_handler(review: Review):
    class Handler(BaseHTTPRequestHandler):
        server_version = "MenuReview"

        def log_message(self, format, *args):
            pass

        def send(self, status: int, body: bytes, kind: str = "application/json", headers=(), cache="no-store"):
            self.send_response(status)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", cache)
            self.send_header("X-Frame-Options", "DENY")
            self.send_header("Referrer-Policy", "no-referrer")
            self.send_header("X-Content-Type-Options", "nosniff")
            for name, value in headers:
                self.send_header(name, value)
            self.end_headers()
            self.wfile.write(body)

        def json(self, value, status=200):
            self.send(status, json.dumps(value).encode())

        def redirect(self, where: str, headers=()):
            self.send(303, b"", "text/plain", headers=[("Location", where), *headers])

        def authorized(self) -> bool:
            return signed_in(self.headers.get("Cookie", ""), review.secret)

        def body(self):
            length = int(self.headers.get("Content-Length", "0") or 0)
            return self.rfile.read(min(length, 262144))

        def sign_in(self, where="./"):
            cookie = f"{COOKIE}={token_of(review.secret)}; Path=/; HttpOnly; SameSite=Lax; Max-Age=2592000"
            return self.redirect(local_next(where), [("Set-Cookie", cookie)])

        def do_GET(self):
            url = urllib.parse.urlsplit(self.path)
            query = {key: values[-1] for key, values in urllib.parse.parse_qs(url.query).items()}
            path = url.path.lstrip("/")
            if path == "login":
                # (a sign-in link, login?key=<secret>, for a phone: the cookie set, the key gone from the address)
                given = query.get("key", "")
                if given and hmac.compare_digest(given.encode(), review.secret.encode()):
                    return self.sign_in(query.get("next", ""))
                return self.send(200, (LOGIN_PAGE % "").encode(), "text/html; charset=utf-8")
            if not self.authorized():
                if path.startswith("api/"):
                    return self.json({"error": "sign in"}, 401)
                return self.redirect("login")
            if path in ("", "index.html"):
                return self.static("index.html")
            if path.startswith("static/"):
                return self.static(path[7:])
            if path == "api/runs":
                return self.json(review.runs())
            if path == "api/screens":
                return self.json(review.screens(query.get("run", "latest")))
            if path == "api/screen":
                return self.json(review.screen(query.get("run", "latest"), query.get("screen", "")))
            if path == "api/notes":
                return self.json(review.note_list())
            if path.startswith("file/"):
                run, _, relative = urllib.parse.unquote(path[5:]).partition("/")
                folder = run_folder(run, review.build)
                return self.file(safe_file(relative, [folder]) if folder else None)
            if path.startswith("notefile/"):
                return self.file(safe_file(urllib.parse.unquote(path[9:]), [review.notes.parent / "menu_notes"]))
            self.send(404, b"not found", "text/plain")

        def static(self, name: str):
            found = safe_file(name, [STATIC])
            if not found:
                return self.send(404, b"not found", "text/plain")
            kind = mimetypes.guess_type(found.name)[0] or "application/octet-stream"
            if kind.startswith("text/") or kind.endswith("javascript"):
                kind += "; charset=utf-8"
            self.send(200, found.read_bytes(), kind, cache="no-cache")

        def file(self, found):
            if not found or found.suffix.lower() not in (".jpg", ".png", ".json"):
                return self.send(404, b"not found", "text/plain")
            kind = mimetypes.guess_type(found.name)[0] or "application/octet-stream"
            self.send(200, found.read_bytes(), kind, cache="private, max-age=300")

        def do_POST(self):
            path = urllib.parse.urlsplit(self.path).path.lstrip("/")
            if path == "login":
                form = urllib.parse.parse_qs(self.body().decode(errors="replace"))
                if hmac.compare_digest(form.get("secret", [""])[-1].encode(), review.secret.encode()):
                    return self.sign_in()
                time.sleep(1.0)
                return self.send(401, (LOGIN_PAGE % "<p>That is not the secret.</p>").encode(),
                                 "text/html; charset=utf-8")
            if not self.authorized():
                return self.json({"error": "sign in"}, 401)
            try:
                request = json.loads(self.body() or b"{}")
            except json.JSONDecodeError:
                return self.json({"error": "not JSON"}, 400)
            if path == "api/notes":
                try:
                    return self.json(review.add_note(request))
                except (ValueError, KeyError, TypeError) as error:
                    return self.json({"error": f"not saved: {error}"}, 400)
            match = re.fullmatch(r"api/notes/([0-9a-f]+)", path)
            if match:
                try:
                    return self.json(review.change(match.group(1), request))
                except (KeyError, ValueError) as error:
                    return self.json({"error": f"not changed: {error}"}, 400)
            if path == "api/render":
                review.start_render()
                return self.json({"render": review.rendering})
            self.send(404, b"not found", "text/plain")

    return Handler


def watch_brief(review: Review, out: Path):
    """when notes arrive, tools/menu_notes.py's brief of the open ones written for an agent"""
    seen = None
    while True:
        try:
            stamp = review.notes.stat().st_mtime_ns if review.notes.exists() else 0
        except OSError:
            stamp = 0
        if stamp != seen:
            seen = stamp
            active = [note for note in menu_notes.read_notes(review.notes) if note.get("status") in menu_notes.ACTIVE]
            text = menu_notes.brief(active) if active else "No open notes.\n"
            part = out.with_suffix(".part")
            part.write_text(text)
            part.replace(out)
        time.sleep(NOTES_WATCH_SECONDS)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="The menus reviewed on the gallery's pictures.", allow_abbrev=False)
    parser.add_argument("action", choices=("serve", "render", "secret"))
    parser.add_argument("--host", default="0.0.0.0", help="the address to listen on (a LAN's, Tailscale's...)")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--secret", help="the login's secret (MENU_REVIEW_SECRET; else build/menu_review/secret)")
    parser.add_argument("--notes", type=Path, default=menu_notes.NOTES, help="the notes file")
    parser.add_argument("--build", type=Path, default=BUILD, help="the folder of the galleries (build/)")
    parser.add_argument("--ref", help="render: the ref to compare with (the fixed notes' earliest commit's parent)")
    parser.add_argument("--watch-brief", nargs="?", const=BUILD / "menu_notes_brief.txt", type=Path, metavar="FILE",
                        help="serve: write the open notes' brief there as they arrive (build/menu_notes_brief.txt)")
    arguments = parser.parse_args(argv)
    secret = secret_of(arguments.secret)
    if arguments.action == "secret":
        print(secret)
        return 0
    if arguments.action == "render":
        out, status, tail = render(arguments.notes, arguments.ref)
        print(tail, file=sys.stderr)
        print(out / "index.html")
        return status
    review = Review(secret, arguments.notes, arguments.build)
    if arguments.watch_brief:
        threading.Thread(target=watch_brief, args=(review, arguments.watch_brief), daemon=True).start()
    server = ThreadingHTTPServer((arguments.host, arguments.port), make_handler(review))
    print(f"menu_review: http://{arguments.host}:{arguments.port}/ (the secret: tools/menu_review.py secret)",
          file=sys.stderr, flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
