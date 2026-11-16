"""tools/menu_notes.py and tools/menu_sources.py: the notes read, listed, batched, briefed (the source first),
replied to, ready for review only after a render and its checks; the style guide's rules; where widgets come
from; menu_gallery.py's page with notes."""
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import menu_gallery  # noqa: E402
import menu_notes  # noqa: E402
import menu_sources  # noqa: E402

SCREEN = "main_menu/settings_select/player_setup/player_profile_edit/audio_settings/audio_settings_screen"
WIDGET = "pc\\main_menu\\settings_select\\player_setup\\player_profile_edit\\audio_settings\\output_device_label"
BROWSER = "main_menu/multiplayer_type_select/join_game/join_game_screen"


def note(identifier, status="open", widget=WIDGET, text="Say SPEAKERS", screen=SCREEN, **more):
    made = {"id": identifier, "status": status, "time": "2026-11-07T20:00:00Z", "note": text, "screen": screen,
            "screen_tag": "pc\\" + screen.replace("/", "\\"), "widget": widget,
            "widget_path": ["pc\\" + screen.replace("/", "\\"), widget] if widget else [],
            "widget_text": "OUTPUT DEVICE" if widget else "", "box": [-68, 125, 226, 144] if widget else None,
            "input": "keyboard", "shape": "16:9", "screen_width": 852,
            "picture": "/tmp/menu_notes/2026-11-07_20-00-00.png"}
    made.update(more)
    return made


def write(path, notes):
    path.write_text("".join(json.dumps(item) + "\n" for item in notes) + "not json\n")


def test_list_reply_and_status(tmp_path):
    path = tmp_path / "menu_notes.jsonl"
    write(path, [note("a1"), note("b2", widget=None, text="Too crowded"), note("c3", status="fixed", commit="abc"),
                 note("d4", status="in_progress")])
    notes = menu_notes.read_notes(path)
    assert [item["id"] for item in notes] == ["a1", "b2", "c3", "d4"]
    listed = menu_notes.list_notes(notes, False)
    assert listed.splitlines()[0] == SCREEN and "a1" in listed and "c3" not in listed and "[in progress]" in listed
    assert "the screen as a whole: Too crowded" in listed
    assert "[ready for review abc" in menu_notes.list_notes(notes, True)
    assert menu_notes.main(["reply", "a", "Which word?", "--file", str(path)]) == 0
    assert menu_notes.main(["start", "b", "--file", str(path)]) == 0
    notes = menu_notes.read_notes(path)
    assert notes[0]["thread"][0]["text"] == "Which word?" and notes[0]["thread"][0]["author"] == "agent"
    assert notes[1]["status"] == "in_progress"
    assert menu_notes.main(["close", "c3", "--file", str(path)]) == 0
    assert menu_notes.read_notes(path)[2]["status"] == "closed"


def test_ids_by_their_first_characters():
    notes = [note("a1"), note("a2"), note("b3")]
    assert [item["id"] for item in menu_notes.pick(notes, ["b"])] == ["b3"]
    try:
        menu_notes.pick(notes, ["a"])
    except SystemExit as error:
        assert "more than one" in str(error)
    else:
        raise AssertionError("an ambiguous ID was taken")


def test_brief_puts_the_source_first_and_questions_apart():
    rules = [("everywhere", "Plain names."), ("main_menu/settings_select/*", "Short values."), ("*/lobby/*", "No.")]
    text = menu_notes.brief([
        note("a1", scope="screen", edits={"box": {"from": [0, 0, 10, 10], "to": [0, -2, 12, 10], "delta": [0, -2, 2, 2]},
                                          "text": {"old": "OUTPUT DEVICE", "new": "SPEAKERS"}}),
        note("q2", widget=None, text="Should this screen exist?", kind="question",
             thread=[{"author": "agent", "time": "t", "text": "It holds the audio settings."}])], rules)
    assert text.startswith("1 menu note to fix, 1 question to answer.")
    first = text.split("\n1. ")[1]
    assert first.startswith("[a1] FIX: Say SPEAKERS\n   source: tools/port_settings.py:")
    assert "_settings_text" in first.splitlines()[1]
    assert "scope: this screen, in every variant" in first
    assert "proposed box: [0, 0, 10, 10] -> [0, -2, 12, 10]" in first and "moved +0, -2; +2 wider" in first
    assert 'proposed words: "OUTPUT DEVICE" -> "SPEAKERS"' in first
    assert "\nStyle rules everywhere: Plain names.\n" in text
    assert "style rules for this screen too: Short values." in first and "Plain names" not in first
    second = text.split("\n2. ")[1]
    assert second.startswith("[q2] QUESTION (answer on its thread; change no code until the reviewer answers)")
    assert "reply from agent: It holds the audio settings." in second and "on the screen as a whole" in second


def test_batches_part_the_notes_by_file_and_area():
    notes = [note("a1"), note("a2", widget="pc\\main_menu\\settings_select\\player_setup\\player_profile_edit\\"
                                           "audio_settings\\voice_chat_label"),
             note("b1", widget="pc\\main_menu\\multiplayer_type_select\\join_game\\prompt_refresh", screen=BROWSER),
             note("q1", kind="question"), note("x1", status="fixed")]
    grouped = menu_notes.batches(notes)
    assert [(key, [item["id"] for item in group]) for key, group in grouped] == [
        (("tools/port_settings.py", "_settings_page"), ["a1", "a2"]),
        (("tools/port_settings.py", "_browser"), ["b1"])]
    assert "batch 2: tools/port_settings.py, _browser (1 note): b1" in menu_notes.batches_text(notes)


def test_rules_kept_in_the_style_guide(tmp_path):
    guide = tmp_path / "STYLE.md"
    guide.write_text("# Style\n\n## Rules\n\n- (everywhere) One.\n")
    menu_notes.add_rule("main_menu/settings_select/*", "  Values   fit. ", guide)
    rules = menu_notes.style_rules(guide)
    assert rules == [("everywhere", "One."), ("main_menu/settings_select/*", "Values fit.")]
    assert menu_notes.rules_for(SCREEN, rules) == ["One.", "Values fit."]
    assert menu_notes.rules_for(BROWSER, rules) == ["One."]
    assert menu_notes.style_rules()  # (the repository's own guide has rules)


def test_fixed_only_after_a_render_without_new_problems(tmp_path, monkeypatch):
    path = tmp_path / "menu_notes.jsonl"
    write(path, [note("a1")])
    monkeypatch.setattr(menu_notes, "RUNS", tmp_path / "runs")
    found = {"new": []}

    def run(command, **_):
        out = Path(command[command.index("--out") + 1])
        out.mkdir(parents=True, exist_ok=True)
        (out / "report.json").write_text(json.dumps({"new_problems": found["new"]}))
        assert command[command.index("--compare") + 1] == "abc123~1"
        return subprocess.CompletedProcess(command, 0, "", "")

    monkeypatch.setattr(menu_notes.subprocess, "run", run)
    found["new"] = [{"variant": "keyboard-16x9", "screen": SCREEN, "check": "cut", "message": "x"}]
    assert menu_notes.fixed(path, menu_notes.read_notes(path), "abc123") == 1
    assert menu_notes.read_notes(path)[0]["status"] == "open"
    found["new"] = []
    assert menu_notes.fixed(path, menu_notes.read_notes(path), "abc123") == 0
    fixed = menu_notes.read_notes(path)[0]
    assert fixed["status"] == "fixed" and fixed["commit"] == "abc123" and fixed["review"]["new_problems"] == 0


def test_sources_located():
    place = menu_sources.locate("tools/port_settings.py multiplayer_files/_browser/_prompts")
    assert place["calls"] == ["multiplayer_files", "_browser", "_prompts"]
    assert place["where"].startswith("tools/port_settings.py:") and place["where"].endswith(" _prompts")
    assert set(place["lines"]) == {"multiplayer_files", "_browser", "_prompts"}
    ce = menu_sources.locate(menu_sources.fallback_source("tools/ce_menus.py", "main_menu/main_menu"))
    assert ce["calls"] == ["widget_xml"] and ce["tag"] == "ui\\shell\\main_menu\\main_menu"
    assert "from the PC tag ui\\shell\\main_menu\\main_menu" in menu_sources.describe(
        "tools/ce_menus.py widget_xml ui\\shell\\main_menu\\main_menu")
    widgets = menu_sources.catalog()
    found = widgets["main_menu/multiplayer_type_select/join_game/prompt_refresh"]
    assert found["source"] == "tools/port_settings.py multiplayer_files/_browser/_prompts"
    layout = {"items": [{"kind": "widget", "name": "pc\\main_menu\\multiplayer_type_select\\join_game\\prompt_refresh"},
                        {"kind": "text", "widget": 0}]}
    assert menu_sources.enrich_layout(layout, widgets)["items"][0]["source"] == found["source"]


def test_note_screens(tmp_path):
    path = tmp_path / "menu_notes.jsonl"
    write(path, [note("a1"), note("b2")])
    assert menu_notes.note_screens(path) == [SCREEN]


def test_the_gallery_page_shows_a_screens_notes_unchanged_or_not(tmp_path):
    screens = [menu_gallery.Screen(SCREEN, "x.xml", "tools/port_settings.py")]
    shown = menu_gallery.capture.Picture(4, 4, [bytes(12)] * 4)
    result = {"picture": shown, "digest": "same", "opened": True, "layout": None}
    key = (SCREEN, "keyboard-16x9")
    page = menu_gallery.write_page(tmp_path, screens, ["keyboard-16x9"], {key: result}, {}, {key: dict(result)}, {},
                                   notes={SCREEN: [note("a1"), note("c3", status="done")]}).read_text()
    assert page.count('<section class="screen"') == 1 and "Say SPEAKERS" in page and "done" in page
