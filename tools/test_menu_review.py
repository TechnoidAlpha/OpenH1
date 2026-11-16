"""tools/menu_review.py's parts that need no browser: a click on a picture as the menus' widget under it, the
web's notes in the studio's format, their status changes, the login, the files it serves."""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import menu_notes  # noqa: E402
import menu_review  # noqa: E402

SCREEN = "main_menu/multiplayer_type_select/lobby/lobby_screen"
TAG = "pc\\main_menu\\multiplayer_type_select\\lobby\\lobby_screen"


def layout():
    widget = lambda id, parent, name, box: {"kind": "widget", "id": id, "parent": parent, "type": 0,
                                            "focused": False, "name": "pc\\main_menu\\lobby\\" + name, "box": box,
                                            "clip": None}
    return {"frame": 1, "prompts": "keyboard", "touch_menus": False, "screen": [-106, 0, 746, 480],
            "screen_name": TAG, "items": [
                widget(0, -1, "lobby_screen", [0, 0, 640, 480]),
                widget(1, 0, "backdrop", [-106, 0, 746, 480]),
                widget(2, 1, "button_bar", [0, 410, 640, 440]),
                widget(3, 2, "prompt_add", [500, 415, 600, 439]),
                {"kind": "text", "widget": 3, "text": " ADD PLAYER", "box": [500, 415, 600, 439],
                 "clip": [-106, 0, 852, 480], "extent": [510, 418, 600, 435], "scale": 1.0},
                {"kind": "text", "widget": 2, "text": "LEAVE", "box": [10, 415, 100, 439],
                 "clip": [-106, 0, 852, 480], "extent": [20, 418, 60, 435], "scale": 1.0}]}


SIZE = (1920, 1080)


def test_a_click_is_the_widget_under_it():
    shown = layout()
    # (the menus' 550,427 on a 1920x1080 picture of 852 units: 2.25 pixels a unit, from 106 left of 0)
    x, y = menu_review.picture_point(shown, SIZE, 550, 427)
    assert (round(x), round(y)) == (1478, 961)
    assert menu_review.menu_point(shown, SIZE, x, y) == (550.0, 427.0)
    assert menu_review.widget_at(shown, 550, 427)["name"].endswith("prompt_add")
    assert menu_review.widget_at(shown, 50, 420)["name"].endswith("button_bar")
    # (the backdrop over the whole screen is the screen's own, as the root is)
    assert menu_review.widget_at(shown, 300, 100) is None


def test_a_widgets_fields_as_the_studio_writes_them():
    shown = layout()
    fields = menu_review.widget_fields(shown, menu_review.widget_at(shown, 550, 427))
    assert fields["widget"].endswith("prompt_add") and fields["box"] == [500, 415, 600, 439]
    assert [name.rsplit("\\", 1)[-1] for name in fields["widget_path"]] == [
        "lobby_screen", "backdrop", "button_bar", "prompt_add"]
    assert fields["widget_text"] == "ADD PLAYER"
    assert menu_review.widget_fields(shown, None) == {"widget": None, "widget_path": [], "widget_text": "", "box": None}


def test_web_notes_on_a_widget_an_area_a_point_and_the_screen():
    shown = layout()
    x, y = menu_review.picture_point(shown, SIZE, 550, 427)
    note = menu_review.make_web_note(shown, SIZE, {"kind": "widget", "x": x, "y": y, "variant": "keyboard-16x9",
                                                   "note": " ADD PLAYER is cut off ", "author": "Paul"}, "latest")
    assert note["status"] == "open" and note["origin"] == "web" and note["author"] == "Paul"
    assert note["kind"] == "fix" and note["scope"] == "variant" and note["thread"] == []
    assert note["screen"] == SCREEN and note["screen_tag"] == TAG and note["note"] == "ADD PLAYER is cut off"
    assert note["widget"].endswith("prompt_add") and note["input"] == "keyboard" and note["shape"] == "16:9"
    assert note["image"] == {"run": "latest", "variant": "keyboard-16x9", "size": [1920, 1080], "x": 1478, "y": 961}
    x0, y0 = menu_review.picture_point(shown, SIZE, 0, 400)
    x1, y1 = menu_review.picture_point(shown, SIZE, 120, 450)
    area = menu_review.make_web_note(shown, SIZE, {"kind": "area", "x": x1, "y": y1, "x1": x0, "y1": y0,
                                                   "variant": "controller-16x10", "note": "crowded"}, "pr-1")
    assert area["widget"] is None and area["area"] == [0, 400, 120, 450] and area["target"] == "area"
    assert area["area_widgets"] == ["pc\\main_menu\\lobby\\button_bar"] and area["author"] == "reviewer"
    assert area["input"] == "controller" and area["shape"] == "16:10"
    point = menu_review.make_web_note(shown, SIZE, {"kind": "point", "x": x0, "y": y0, "variant": "touch-phone",
                                                    "note": "here"}, "latest")
    assert point["area"] == [0, 400, 0, 400] and point["target"] == "point"
    x, y = menu_review.picture_point(shown, SIZE, 300, 100)
    whole = menu_review.make_web_note(shown, SIZE, {"x": x, "y": y, "variant": "keyboard-16x9", "note": "dark"},
                                      "latest")
    assert whole["target"] == "screen" and whole["widget"] is None


def test_notes_written_read_and_their_status_changed(tmp_path):
    path = tmp_path / "menu_notes.jsonl"
    studio = {"id": "aaa", "status": "open", "note": "from F9", "screen": SCREEN, "input": "controller",
              "shape": "16:10"}
    menu_notes.append_note(path, studio)
    web = menu_review.make_web_note(layout(), SIZE, {"x": 1, "y": 1, "variant": "keyboard-16x9", "note": "web"},
                                    "latest")
    menu_notes.append_note(path, web)
    notes = menu_notes.read_notes(path)
    assert [note["note"] for note in notes] == ["from F9", "web"]
    assert menu_review.note_variant(notes[0]) == "controller-16x10"
    assert menu_review.note_variant(notes[1]) == "keyboard-16x9"
    menu_notes.set_status(path, "aaa", "in_progress", changed="now")
    menu_notes.set_status(path, web["id"], "done", commit="abc")
    notes = menu_notes.read_notes(path)
    assert notes[0]["status"] == "in_progress" and notes[0]["changed"] == "now"
    assert notes[1]["status"] == "done" and notes[1]["commit"] == "abc"
    assert "in progress" in menu_notes.list_notes(notes, False) and web["id"] not in menu_notes.list_notes(notes, False)
    menu_notes.set_status(path, web["id"], "closed")
    assert menu_notes.read_notes(path)[1]["status"] == "closed"
    for wrong in (("aaa", "repaired"), ("nope", "open")):
        try:
            menu_notes.set_status(path, *wrong)
        except (ValueError, KeyError):
            pass
        else:
            raise AssertionError(f"{wrong} was taken")


def test_the_login():
    token = menu_review.token_of("s3cret")
    assert menu_review.signed_in(f"other=1; menu_review={token}", "s3cret")
    assert not menu_review.signed_in("menu_review=" + menu_review.token_of("other"), "s3cret")
    assert not menu_review.signed_in("", "s3cret") and not menu_review.signed_in("menu_review=s3cret", "s3cret")


def test_only_the_galleries_files(tmp_path):
    run = tmp_path / "build" / "menu_gallery" / "after" / "keyboard-16x9"
    run.mkdir(parents=True)
    (run / "x.jpg").write_bytes(b"\xff\xd8")
    (tmp_path / "secret.txt").write_text("no")
    root = tmp_path / "build" / "menu_gallery"
    assert menu_review.safe_file("after/keyboard-16x9/x.jpg", [root]) == (run / "x.jpg").resolve()
    for bad in ("../../secret.txt", "/etc/passwd", "after/../../../secret.txt", "", "after/keyboard-16x9/x.jpg\x00"):
        assert menu_review.safe_file(bad, [root]) is None
    runs = menu_review.find_runs(tmp_path / "build")
    assert [(name, has) for name, _, has in runs] == [("latest", False)]
    compared = tmp_path / "build" / "menu_review" / "runs" / "pr-7"
    (compared / "after").mkdir(parents=True)
    (compared / "before").mkdir()
    assert ("pr-7", True) in [(name, has) for name, _, has in menu_review.find_runs(tmp_path / "build")]


def test_the_render_compares_with_the_earliest_fix_s_parent():
    assert menu_review.render_ref([{"status": "open"}]) == "HEAD~1"
    head = __import__("subprocess").run(["git", "-C", str(menu_review.ROOT), "rev-parse", "HEAD"],
                                        capture_output=True, text=True).stdout.strip()
    parent = __import__("subprocess").run(["git", "-C", str(menu_review.ROOT), "rev-parse", "HEAD~1"],
                                          capture_output=True, text=True).stdout.strip()
    notes = [{"status": "done", "commit": head}, {"status": "closed", "commit": parent},
             {"status": "done", "commit": "0" * 40}]
    assert menu_review.render_ref(notes) == f"{parent}~1"


def test_a_sign_in_link_goes_on_to_the_apps_own_pages_alone():
    assert menu_review.local_next("screen?run=latest&screen=a") == "screen?run=latest&screen=a"
    for elsewhere in ("", "//evil.example/x", "/notes", "https://evil.example", "\\\\evil", "javascript:alert(1)"):
        assert menu_review.local_next(elsewhere) == "./"


def test_a_note_names_its_widget_its_kind_scope_and_the_edits_proposed():
    shown = layout()
    note = menu_review.make_web_note(shown, SIZE, {
        "kind": "widget", "widget": "pc\\main_menu\\lobby\\prompt_add", "variant": "keyboard-16x9",
        "note": "Room for the key", "note_kind": "question", "scope": "everywhere",
        "edits": {"box": {"to": [490, 415, 600, 439]}, "text": {"old": "ADD PLAYER", "new": "ADD A PLAYER"}}}, "latest")
    assert note["widget"].endswith("prompt_add") and note["kind"] == "question" and note["scope"] == "everywhere"
    assert note["edits"]["box"] == {"from": [500, 415, 600, 439], "to": [490.0, 415.0, 600.0, 439.0],
                                    "delta": [-10.0, 0.0, 10.0, 0.0]}
    assert note["edits"]["text"] == {"old": "ADD PLAYER", "new": "ADD A PLAYER"}
    # (nothing proposed where the box and words are as they were; a box of no sense refused)
    same = menu_review.clean_edits({"box": {"to": [500, 415, 600, 439]}, "text": {"old": "A", "new": "A"}},
                                   menu_review.widget_named(shown, "pc\\main_menu\\lobby\\prompt_add"))
    assert same == {}
    assert menu_review.clean_box([1, 2, 0, 4]) is None and menu_review.clean_box("x") is None
    unknown = menu_review.make_web_note(shown, SIZE, {"kind": "widget", "widget": "nope", "variant": "keyboard-16x9",
                                                      "note": "n", "note_kind": "bogus", "scope": "bogus"}, "latest")
    assert unknown["widget"] is None and unknown["kind"] == "fix" and unknown["scope"] == "variant"
