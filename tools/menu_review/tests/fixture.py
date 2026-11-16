"""A gallery made up for the browser tests (tests/review.spec.ts): two screens of the menu files' names in two
variants, their pictures plain rectangles (none of the game's), their layouts as debug.menu_layout writes them,
and a --compare run with a before; an empty notes file.

    python3 tests/fixture.py <folder>
"""
import json
import shutil
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(TOOLS))
import capture  # noqa: E402

SCREENS = {
    "main_menu/settings_select/player_setup/player_profile_edit/audio_settings/audio_settings_screen": "audio",
    "main_menu/main_menu": "main",
}
# the variants: (their screen in the menus' units, the picture's size)
VARIANTS = {"keyboard-16x9": ([-106, 0, 746, 480], (1704, 960)), "controller-16x10": ([-64, 0, 704, 480], (1536, 960))}


def widgets(base: str) -> list:
    """the made-up screen: a panel of two labels, and a button"""
    def widget(id, parent, name, box):
        return {"kind": "widget", "id": id, "parent": parent, "type": 1 if name.startswith("label") or name.startswith("button") else 0,
                "focused": False, "name": f"{base}\\{name}", "box": box, "clip": None}

    def text(widget, words, box):
        return {"kind": "text", "widget": widget, "scale": 1.0, "text": words, "box": box, "clip": [-106, 0, 852, 480],
                "extent": [box[0] + 3, box[1], box[0] + 3 + 8 * len(words), box[3] - 2], "whole": None, "wide": None}

    return [widget(0, -1, "screen", [0, 0, 640, 480]), widget(1, 0, "panel", [100, 100, 400, 300]),
            {**widget(2, 1, "label_a", [120, 120, 300, 140]),
             "source": "tools/port_settings.py settings_files/_settings_page/_settings_text"},
            widget(3, 1, "label_b", [120, 160, 300, 180]),
            widget(4, 0, "button_ok", [450, 400, 578, 424]),
            text(2, "SOUND", [120, 120, 300, 140]), text(3, "MUSIC", [120, 160, 300, 180]), text(4, "OK", [450, 400, 578, 424])]


def picture(screen, size, items, shade) -> capture.Picture:
    width, height = size
    rows = [bytearray(bytes((20, 24, 32 + shade)) * width) for _ in range(height)]
    scale_x = width / (screen[2] - screen[0])
    scale_y = height / (screen[3] - screen[1])
    colours = {1: (40, 80, 160), 2: (200, 200, 220), 3: (200, 200, 220), 4: (60, 160, 90)}
    for item in items:
        if item["kind"] != "widget" or item["id"] not in colours:
            continue
        x0, y0, x1, y1 = item["box"]
        left, right = int((x0 - screen[0]) * scale_x), int((x1 - screen[0]) * scale_x)
        top, bottom = int((y0 - screen[1]) * scale_y), int((y1 - screen[1]) * scale_y)
        for row in rows[max(top, 0):min(bottom, height)]:
            row[left * 3:right * 3] = bytes(colours[item["id"]]) * (right - left)
    return capture.Picture(width, height, [bytes(row) for row in rows])


def make(folder: Path):
    if folder.exists():
        shutil.rmtree(folder)
    for run, sides in (("menu_gallery", ("after",)), ("menu_review/runs/pr-1", ("before", "after"))):
        for side in sides:
            for variant, (screen, size) in VARIANTS.items():
                out = folder / "build" / run / side / variant
                out.mkdir(parents=True, exist_ok=True)
                for name, short in SCREENS.items():
                    slug = name.replace("/", "-")
                    base = "pc\\" + name.replace("/", "\\").rsplit("\\", 1)[0]
                    items = widgets(base)
                    layout = {"frame": 1, "prompts": "keyboard", "touch_menus": False, "screen": screen,
                              "screen_name": "pc\\" + name.replace("/", "\\"), "items": items}
                    (out / f"{slug}.json").write_text(json.dumps(layout))
                    shown = picture(screen, size, items, 30 if side == "before" else 0)
                    capture.write_png(shown, out / f"{slug}.png")
                    capture.write_png(shown, out / f"{slug}.thumb.png")
    # (a note fixed, ready for review: its before | after is the --compare run's)
    audio = next(name for name in SCREENS if SCREENS[name] == "audio")
    ready = {"id": "6aef00000001", "status": "fixed", "kind": "fix", "scope": "variant", "time": "2026-11-07T20:00:00Z",
             "note": "Close the gap under SOUND", "screen": audio, "variant": "keyboard-16x9", "author": "Paul",
             "widget": "pc\\" + audio.replace("/", "\\").rsplit("\\", 1)[0] + "\\label_b", "commit": "abc1234",
             "review": {"run": "pr-1", "new_problems": 0}, "thread": []}
    (folder / "notes.jsonl").write_text(json.dumps(ready) + "\n")


if __name__ == "__main__":
    make(Path(sys.argv[1]))
