"""tools/notes.py: the recorded frames' notes (an anchor on a draw of a recorded frame) and the menus' in one
tool: listed, batched, briefed, answered, taken, and ready for review with an after picture (given, or rendered
by the replay service: a made-up one, FAKE_SERVICE, stands in for halo_replay)."""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import notes  # noqa: E402

# a made-up replay service (docs/replay_protocol.md): info, and frame writing a 2x2 PNG
FAKE_SERVICE = r"""
import json, struct, sys, zlib
def png(path):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    raw = b"".join(b"\0" + b"\x80\x40\x20" * 2 for _ in range(2))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
for line in sys.stdin:
    request = json.loads(line)
    if request["cmd"] == "quit":
        break
    if request["cmd"] == "info":
        reply = {"ok": True, "protocol": 1, "frames": 100, "width": 2, "height": 2, "backend": "fake", "build": "0",
                 "marks": False, "records": 0}
    elif request["cmd"] == "frame":
        png(request["png"])
        reply = {"ok": True, "n": request["n"], "ms": 0.0, "width": 2, "height": 2}
    else:
        reply = {"ok": False, "error": "unknown command"}
    print(json.dumps(reply), flush=True)
"""


def anchored(identifier, kind="world_object", label="crate", shader="scenery\\shaders\\wood", status="open",
             note_kind="fix", recording="build/r.rec"):
    return {"id": identifier, "status": status, "time": "2026-11-08T00:00:00Z", "kind": note_kind, "scope": "screen",
            "note": f"note {identifier}", "author": "reviewer", "origin": "client",
            "anchor": {"recording": recording, "frame": 12, "pixel": [4, 5],
                       "draw": {"i": 3, "kind": kind, "label": label, "id": 65558,
                                "tags": {"object": "scenery\\crate", "shader": shader}, "textures": ["t"]},
                       "camera": {"fov": 70.0}, "map": "m", "tick": 7, "backend": "metal", "build": "abc"},
            "thread": [], "picture": "build/inspector/notes/x.png"}


def menu(identifier):
    return {"id": identifier, "status": "open", "time": "2026-11-08T00:00:00Z", "kind": "fix", "scope": "variant",
            "note": "wider", "screen": "main_menu/settings_select", "widget": "pc\\main_menu\\settings_select\\panel",
            "thread": []}


def write(path, items):
    path.write_text("".join(json.dumps(item) + "\n" for item in items))


def test_list_batches_and_brief(tmp_path, capsys):
    general, menus = tmp_path / "notes.jsonl", tmp_path / "menu_notes.jsonl"
    write(general, [anchored("a1"), anchored("a2"), anchored("b1", kind="hud", label="health", shader="ui\\meter"),
                    anchored("q1", note_kind="question"), anchored("z9", status="closed")])
    write(menus, [menu("m1")])
    files = ["--file", str(general), "--file", str(menus)]
    assert notes.main(files + ["list"]) == 0
    listed = capsys.readouterr().out
    assert "r.rec frame 12" in listed and "world_object crate: note a1" in listed and "z9" not in listed
    assert "m1" in listed
    assert notes.main(files + ["batches"]) == 0
    batches = capsys.readouterr().out
    first = batches.splitlines()[0]
    assert "world_object, scenery\\shaders\\wood (2 notes): a1 a2" in first
    assert notes.main(["--file", str(general), "brief", "a1", "q1"]) == 0
    brief = capsys.readouterr().out
    assert "1 recorded frame note to fix, 1 question to answer." in brief
    assert "shader: scenery\\shaders\\wood" in brief and "moment: build/r.rec frame 12, pixel [4, 5]" in brief
    assert "QUESTION" in brief and "this thing wherever it draws" in brief


def test_reply_start_and_fixed_with_a_picture(tmp_path, capsys):
    general = tmp_path / "notes.jsonl"
    write(general, [anchored("a1")])
    files = ["--file", str(general)]
    assert notes.main(files + ["start", "a1"]) == 0
    assert notes.main(files + ["reply", "a1", "brighter now"]) == 0
    after = tmp_path / "after.png"
    after.write_bytes(b"png")
    assert notes.main(files + ["fixed", "a1", "--commit", "abc123", "--after", str(after)]) == 0
    note = json.loads(general.read_text().splitlines()[0])
    assert note["status"] == "fixed" and note["commit"] == "abc123" and note["after"] == str(after)
    assert note["thread"][0] == {**note["thread"][0], "author": "agent", "text": "brighter now"}
    # fields the tool does not know are kept
    assert note["anchor"]["camera"] == {"fov": 70.0}


def test_fixed_renders_the_after_with_the_replay(tmp_path, monkeypatch, capsys):
    recording = tmp_path / "demo.rec"
    recording.write_bytes(b"x")
    general = tmp_path / "notes.jsonl"
    write(general, [anchored("a1", recording=str(recording))])
    monkeypatch.setattr(notes, "AFTER", tmp_path / "after")
    fake = tmp_path / "fake_replay.py"
    fake.write_text(FAKE_SERVICE)
    assert notes.main(["--file", str(general), "fixed", "a1", "--commit", "abc", "--replay", str(fake)]) == 0
    note = json.loads(general.read_text().splitlines()[0])
    picture = Path(note["after"])
    assert picture.is_file() and picture.read_bytes()[:4] == b"\x89PNG"


def test_an_anchored_note_on_a_menu_widget(tmp_path, capsys):
    note = anchored("w1", kind="menu_widget", label="join button")
    note.update(screen="main_menu/settings_select", widget="main_menu/settings_select/panel",
                source="tools/port_settings.py settings_files")
    general = tmp_path / "notes.jsonl"
    write(general, [note])
    assert notes.is_menu(note)
    assert notes.main(["--file", str(general), "brief", "w1"]) == 0
    brief = capsys.readouterr().out
    assert "main_menu/settings_select" in brief and "frame 12" in brief and "source: tools/port_settings.py" in brief
    after = tmp_path / "after.png"
    after.write_bytes(b"png")
    # its fixed is the frame's after picture, not the menu gallery's run
    assert notes.main(["--file", str(general), "fixed", "w1", "--commit", "abc", "--after", str(after)]) == 0
    assert json.loads(general.read_text())["after"] == str(after)
