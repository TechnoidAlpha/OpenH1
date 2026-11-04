"""Tests for the Linux port's tooling (port/linux, the build scripts), its
settings, menus and maps' checks."""

import math
import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest

from tools import build_common, linux_msvc_semantics, x86_game_abi
from tools.port_settings import MENU_PLATFORMS

# ---------- MSVC semantics header


def test_strip_cplusplus_keeps_c_branches_only():
    text = "\n".join([
        "a",
        "#ifdef __cplusplus", "cpp1", "#else", "c1", "#endif",
        "#if FOO", "b", "#else", "c", "#endif",
        "#ifndef __cplusplus", "c2", "#else", "cpp2", "#endif",
        "#if defined(__cplusplus)", "cpp3", "#if X", "cpp4", "#endif", "#endif",
        "#ifdef _WIN64", "win64", "#endif",
        "end",
    ])
    assert linux_msvc_semantics.strip_cplusplus(text).split() == ["a", "c1", "b", "c", "c2", "end"]


def write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="latin-1")
    return path


def test_scan_finds_prototype_scope_tags_and_inline_functions(tmp_path):
    header = write(tmp_path / "a.h", """
        struct location;
        void f(struct scenario *s, union color *c);
        /* struct commented_out */
        __inline long prototyped(long x) { return x; }
        __inline long private_helper(long x) { return x; }
        long prototyped(long x);
        #ifdef __cplusplus
        __inline long cpp_only(long x) { return x; }
        #endif
    """)
    files = [header]
    tags = linux_msvc_semantics.scan_tags(files)
    assert tags == {"location": {"struct"}, "scenario": {"struct"}, "color": {"union"}}
    assert linux_msvc_semantics.scan_inline_functions(files, all_inlines=False) == {"prototyped"}
    assert linux_msvc_semantics.scan_inline_functions(files, all_inlines=True) == {
        "prototyped", "private_helper",
    }


def test_render_skips_tags_used_as_both_struct_and_union():
    text = linux_msvc_semantics.render(
        {"a": {"struct"}, "b": {"union"}, "mixed": {"struct", "union"}}, {"f"}
    )
    assert "struct a;" in text
    assert "union b;" in text
    assert "mixed" not in text
    assert "#pragma weak f" in text


# ---------- Xbox SDK declarations (port/include/xdk)


XDK_INCLUDE = Path(__file__).resolve().parent.parent / "port" / "include" / "xdk"


def test_xdk_headers_use_the_sdk_spellings():
    # each port gives the SDK's keywords and names its own meaning (its
    # prefix header), so none of one port's must be written into them
    for header in sorted(XDK_INCLUDE.glob("*.h")):
        text = re.sub(r"/\*.*?\*/", "", header.read_text(encoding="utf-8"), flags=re.S)
        assert "__attribute__" not in text, header.name
        assert not re.findall(r"\bhalo_\w+", text), header.name


def x86_abi(tmp_path):
    """skips unless this computer builds the game's 32-bit x86 ABI
    (tools/x86_game_abi.py: clang and the C library's 32-bit files)"""
    reason = x86_game_abi.can_build(tmp_path / "x86-probe")
    if reason:
        pytest.skip(reason)


def test_xdk_headers_compile_for_the_game(tmp_path):
    x86_abi(tmp_path)
    root = XDK_INCLUDE.parent.parent.parent
    source = write(tmp_path / "unit.c", "".join(
        f"#include <{name}>\n" for name in ("xtl.h", "xbdm.h", "xkbd.h", "d3d8perf.h")
    ))
    flags = [flag for flag in x86_game_abi.GAME_ABI_FLAGS + x86_game_abi.GAME_FLAGS if flag != "-Wno-everything"]
    for defines in ([], ["-DDEBUG_KEYBOARD"], ["-DNOD3D", "-DNODSOUND"]):
        subprocess.run(
            ["clang", *flags, *defines, "-Werror", "-fsyntax-only",
             "-include", str(root / "port/linux/include/halo_linux_prefix.h"),
             "-I", str(root / "port/linux/include"), "-idirafter", str(XDK_INCLUDE), str(source)],
            check=True,
        )


def pdb_writer():
    """An xdk_headers Writer over an empty type table: basic types only."""
    from tools import pdb200_types, xdk_headers
    pdb = object.__new__(pdb200_types.Pdb)
    pdb.types, pdb.aggregates = {}, {}
    return xdk_headers, xdk_headers.Writer(pdb)


def test_xdk_headers_regroups_anonymous_members():
    # the PDB lists an anonymous structure's or union's members in their
    # container, at their offsets
    xdk_headers, writer = pdb_writer()
    member = xdk_headers.Member
    ulong, int64 = 0x22, 0x13
    union = writer.render(writer.group(
        [member("LowPart", ulong, 0), member("HighPart", ulong, 4), member("QuadPart", int64, 0)], True), "", "u")
    assert union == ["struct {", "    unsigned long LowPart;", "    unsigned long HighPart;", "};",
                     "__int64 QuadPart;"]
    structure = writer.render(writer.group(
        [member("Tag", ulong, 0), member("Low", ulong, 4), member("High", ulong, 8),
         member("Whole", int64, 4), member("After", ulong, 12)], False), "", "s")
    assert structure == ["unsigned long Tag;", "union {", "    struct {", "        unsigned long Low;",
                         "        unsigned long High;", "    };", "    __int64 Whole;", "};",
                         "unsigned long After;"]


# ---------- game sources


def test_game_sources_leave_out_the_excluded(tmp_path, monkeypatch):
    write(tmp_path / "source" / "a.c", "")
    write(tmp_path / "source" / "zlib" / "b.c", "")
    write(tmp_path / "source" / "zlib" / "example.c", "")
    write(tmp_path / "source" / "tiff" / "c.c", "")
    write(tmp_path / "source" / "tiff" / "deep" / "d.c", "")
    write(tmp_path / "source" / "tiffx.c", "")
    write(tmp_path / "source" / "a.h", "")
    monkeypatch.chdir(tmp_path)
    # (a folder is a name ending in "/": not a file that only starts like it)
    config = {"game": {"root": "source", "exclude": ["source/zlib/example.c", "source/tiff/"],
                       "defines": ["DEBUG"], "include_dirs": ["source", "source/saved games"]}}
    assert build_common.game_sources(config) == [Path("source/a.c"), Path("source/tiffx.c"), Path("source/zlib/b.c")]
    assert build_common.game_defines_and_includes(config) == '-DDEBUG -Isource -I"source/saved games"'


# ---------- menus (port/assets/menus)


MENUS = Path(__file__).resolve().parent.parent / "port/assets/menus"
MENU_ATTRIBUTES = {
    "menus": {"root"},
    "bitmap": {"name", "width", "height", "frames", "platform"},
    "frame": {"png", "map", "index", "width", "height", "x", "y", "platform"},
    "strings": {"name", "map", "platform"},
    "string": {"text", "map", "platform"},
    "widget": {"name", "type", "controller", "flags", "bitmap", "text", "strings", "values", "setting", "font",
               "color", "align", "description", "string_list", "list_flags", "text_flags", "header_bitmap",
               "footer_bitmap", "header_bounds", "footer_bounds", "child_controller", "x", "y", "left", "top",
               "width", "height", "text_x", "text_y", "auto_close", "auto_close_fade", "string_index", "platform",
               "wide"},
    "child": {"widget", "controller", "x", "y", "platform"},
    "on": {"event", "run", "script", "open", "replace", "close", "widget", "focus", "reload", "sound", "otherwise",
           "label", "back", "branch", "platform"},
    "data": {"input", "platform"},
    "conditional": {"widget", "if_failed", "platform"},
    "replace": {"search", "function", "platform"},
}


def c_strings(path: Path, start: str, end: str) -> list:
    """the string literals of a C file between two markers"""
    text = path.read_text(encoding="latin-1")
    text = text[text.index(start):]
    return re.findall(r'"([^"\\]*)"', text[:text.index(end)])


# menu_files.c's attributes read as whole numbers (its tables' { name, NULL, &long })
MENU_INTEGER_ATTRIBUTES = {"auto_close", "auto_close_fade", "height", "index", "left", "string_index", "text_x",
                           "text_y", "top", "width", "x", "y"}


def test_menus_are_well_formed():
    """What port/linux/src/menu_files.c and port/linux/game/menu_tags.c check
    when the game loads them, but the map's own names (paths with backslashes),
    which only the map has."""
    import json
    import xml.etree.ElementTree as ElementTree

    root = MENUS.parent.parent.parent
    tags = root / "port/linux/game/menu_tags.c"
    events = c_strings(tags, "event_names[] =", "};")
    flags = c_strings(tags, "widget_flag_names[] =", "};")
    functions = set(c_strings(root / "source/interface/ui_widget_event_handler_functions.c", '\t{\n\t\t"NULL",', "}"))
    functions |= set(c_strings(tags, "port_function_names[] =", "};"))
    inputs = set(c_strings(tags, "game_data_input_names[] =", "};"))
    inputs |= set(c_strings(tags, "port_game_data_input_names[] =", "};"))
    listed = json.loads((MENUS / "menus.json").read_text())["files"]
    files = sorted(MENUS.rglob("*.xml"))
    assert sorted(path.relative_to(MENUS).as_posix() for path in files) == \
        sorted(name for name in listed if name.endswith(".xml"))
    widgets, bitmaps, strings, references, roots = {}, {}, {}, [], []
    for path in files:
        tree = ElementTree.parse(path)
        assert tree.getroot().tag == "menus", path
        if tree.getroot().get("root"):
            roots.append(tree.getroot().get("root"))
        for element in tree.getroot().iter():
            where = f"{path.name}: <{element.tag} {element.get('name', '')}>"
            assert element.tag in MENU_ATTRIBUTES, where
            assert set(element.attrib) <= MENU_ATTRIBUTES[element.tag], where
            # (the platforms that show it: menu_files.c's MENU_PLATFORM)
            assert element.get("platform") is None or \
                set(element.get("platform").split()) <= set(MENU_PLATFORMS), where
            # (menu_files.c's whole numbers, which the tags keep in shorts,
            # its true/false attributes, and no text but whitespace outside
            # <string>s)
            for attribute in MENU_INTEGER_ATTRIBUTES & set(element.attrib):
                value = element.get(attribute)
                assert re.fullmatch(r"-?[0-9]+", value) and -32768 <= int(value) <= 32767, f"{where} {attribute}"
            for attribute in ("back", "branch", "if_failed"):
                assert element.get(attribute) in (None, "true", "false"), f"{where} {attribute}"
            # (menu_tags.c's wide layout: "dx dw", each within the extra width 16:9 has)
            if element.get("wide") is not None:
                assert re.fullmatch(r"-?[0-9]+ -?[0-9]+", element.get("wide")) and \
                    all(abs(int(value)) <= 212 for value in element.get("wide").split()), f"{where} wide"
            assert not (element.text or "").strip() and not (element.tail or "").strip(), where
            if element.tag == "bitmap":
                bitmaps[element.get("name")] = element
                for frame in element.iter("frame"):
                    assert (frame.get("png") is None) != (frame.get("map") is None), where
                    if frame.get("map"):
                        assert "\\" in frame.get("map") and int(frame.get("index")) >= 0, where
                        continue
                    assert frame.get("png") in listed, where
                    with (MENUS / frame.get("png")).open("rb") as png:
                        header = png.read(24)
                    width, height = int.from_bytes(header[16:20], "big"), int.from_bytes(header[20:24], "big")
                    logical = int(frame.get("width")), int(frame.get("height"))
                    assert width % logical[0] == 0 and width // logical[0] == height // logical[1], where
            elif element.tag == "strings":
                strings[element.get("name")] = element
                # (a string's map= is its number in the list's map list, menu_tags.c)
                for string in element.findall("string"):
                    assert string.get("map") is None or (element.get("map") and string.get("map").isdigit()), where
            elif element.tag == "widget":
                name = element.get("name")
                assert name and name not in widgets, where
                widgets[name] = element
                assert element.get("type", "container") in ("container", "text", "spinner", "column_list"), where
                assert set(element.get("flags", "").split()) <= set(flags), where
                children = element.findall("widget") + element.findall("child")
                if element.get("type") == "spinner":
                    assert len(children) in (0, 1, 3), where
                if element.get("strings") or "items_from_strings" in element.get("list_flags", ""):
                    assert element.get("type") == "spinner" and not children, where
                if element.get("setting"):
                    assert len(element.get("strings").split("|")) == len(element.get("values").split("|")), where
                for attribute in ("bitmap", "header_bitmap", "footer_bitmap"):
                    references.append((where, bitmaps, element.get(attribute)))
                references.append((where, strings, element.get("string_list")))
                references.append((where, widgets, element.get("description")))
            elif element.tag == "child" or element.tag == "conditional":
                references.append((where, widgets, element.get("widget")))
            elif element.tag == "on":
                assert set(element.get("event").split()) <= set(events), where
                run = element.get("run")
                assert run is None or run in functions or run.startswith("unwired "), where
                assert element.get("otherwise") is None or run, where
                for attribute in ("open", "replace", "focus", "widget", "otherwise"):
                    references.append((where, widgets, element.get(attribute)))
            elif element.tag == "data":
                assert element.get("input") in inputs or element.get("input").startswith("unwired "), where
    assert all(root_name in widgets for root_name in roots)
    for where, names, name in references:
        if name and "\\" not in name:
            assert name in names, f"{where} {name}"


def test_menu_settings_exist():
    """Every setting a menu's spinner sets is one of config.toml's
    (port/linux/src/port_config.c) or of the profile, and
    the keyboard's controls are the same in the settings, in the controls'
    screen and in the input code."""
    root = MENUS.parent.parent.parent
    config = (root / "port/linux/src/port_config.c").read_text()
    functions = (root / "port/linux/game/menu_functions.c").read_text()
    known = set(re.findall(r'^\t\{ "([a-z_]+\.[a-z_]+)", _config_', config, re.M))
    profile = set(re.findall(r'\{ "(profile\.[a-z_]+)", \d', functions))
    for path in (MENUS / "ce").glob("*.xml"):
        for setting in re.findall(r'setting="([^"]+)"', path.read_text()):
            assert setting in known | profile, f"{path.name}: {setting}"
    controls = set(re.findall(r'"(controls\.[a-z_]+)"', (root / "port/linux/src/xinput_sdl.c").read_text()))
    assert controls == {name for name in known if name.startswith("controls.")}
    assert controls == set(re.findall(r'\{ "(controls\.[a-z_]+)", L"', functions))


def config_check_build(tmp_path, defines):
    """port_config.c built on its own with the build's defines
    (tools/config_check.c): run(command...) in a folder of config.toml"""
    root = Path(__file__).resolve().parent.parent
    compiler = shutil.which("clang") or shutil.which("cc")
    if not compiler:
        pytest.skip("needs a C compiler")
    stubs = tmp_path / "stubs"
    (stubs / "SDL3").mkdir(parents=True)
    (stubs / "SDL3" / "SDL.h").write_text(
        "const char *SDL_GetBasePath(void);\n"
        "typedef struct SDL_PathInfo { int type; unsigned long long size; long long create_time, modify_time,"
        " access_time; } SDL_PathInfo;\n"
        "_Bool SDL_GetPathInfo(const char *path, SDL_PathInfo *info);\n")
    (stubs / "platform.h").write_text("void platform_log(const char *format, ...);\n")
    # (the command line's and a dedicated server's: tools/config_check.c's stand-ins)
    (stubs / "posix.h").write_text("int posix_command_line_argument(int index, char *buffer, unsigned long size);\n"
                                   "typedef unsigned long posix_ulong;\n")
    (stubs / "posix_server.h").write_text("int posix_server_private_file(const char *path);\n")
    for name in ("port_config.c", "port_config.h", "install_layout.h"):
        shutil.copy(root / "port/linux/src" / name, stubs / name)
    program = tmp_path / "config_check"
    subprocess.run([compiler, *defines, f"-I{stubs}",
                    f"-I{root}/port/third_party/tomlc17", "-o", str(program), str(stubs / "port_config.c"),
                    str(root / "tools/config_check.c"), str(root / "port/third_party/tomlc17/tomlc17.c"),
                    "-lpthread"], check=True, capture_output=True)
    data = tmp_path / "data"
    data.mkdir()

    def run(*command):
        result = subprocess.run([str(program), str(data), *command], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        return result.stdout.strip()
    run.config = data / "config.toml"
    return run


@pytest.fixture
def config_check(tmp_path):
    """port_config.c with the VR build's settings (config_check_build)"""
    return config_check_build(tmp_path, ["-DHALO_VR=1", "-DHALO_GUEST=1"])


@pytest.fixture
def config_check_mac(tmp_path):
    """port_config.c with the macOS build's settings (config_check_build)"""
    return config_check_build(tmp_path, ["-DHALO_MACOS=1", "-DHALO_GUEST=1"])


def test_config_writes_defaults_commented_out(config_check):
    """a new file has every setting commented out at its default, once; a
    setting written replaces its commented default"""
    assert config_check("get", "vr.hands") == "arms"
    text = config_check.config.read_text()
    assert "is at its default" in text
    assert '\n# hands = "arms"\n' in text and "\nhands =" not in text
    config_check("get", "vr.hands")
    assert config_check.config.read_text() == text
    config_check("write", "vr.hands", "floating")
    written = config_check.config.read_text()
    assert '\nhands = "floating"\n' in written and '# hands = "arms"' not in written
    assert len(re.findall(r"^(# )?hands = ", written, re.M)) == 1
    assert config_check("get", "vr.hands") == "floating"
    assert config_check.config.read_text() == written


def test_config_keeps_an_older_file(config_check):
    """an older version's file keeps its values, and gains the note and the
    settings it lacks commented out; its vr.hands = "floating", the older
    versions' default, becomes the default, once"""
    old = ('# Halo settings\n#\n# The game writes this file with the defaults when it is missing.\n\n'
           '[display]\n\n# Wait for the display.\nvsync = false\n\n[vr]\n\nhands = "floating"\nturn = "smooth"\n')
    config_check.config.write_text(old)
    assert config_check("get", "vr.hands") == "arms"
    assert config_check("get", "display.vsync") == "false"
    assert config_check("get", "vr.turn") == "smooth"
    text = config_check.config.read_text()
    assert text.startswith("# Halo settings\n#\n# The game writes this file with the defaults when it is missing.\n#\n# A")
    assert "\nvsync = false\n" in text and '\nturn = "smooth"\n' in text
    assert '\n# hands = "arms"\n' in text and '"floating"' not in text
    assert "\n# smooth_turn_speed = 150.0\n" in text
    config_check("get", "vr.hands")
    assert config_check.config.read_text() == text
    # (chosen again, it stays)
    config_check("write", "vr.hands", "floating")
    assert config_check("get", "vr.hands") == "floating"
    # (any other value of an older file stays)
    config_check.config.write_text(old.replace('"floating"', '"game"'))
    assert config_check("get", "vr.hands") == "game"


MOUSE_FROM_OLD = 0.0022 * 180 / math.pi / 0.022  # the older scale's radians a count over CS2's degrees


def test_config_mouse_sensitivity_in_cs2_units(config_check_mac):
    """input.mouse_sensitivity set on the older scale (0.0022 radians a
    count) becomes CS2's (0.022 degrees a count) for the same turn, and the
    vertical one, on that scale too (0 the same), a multiplier of it; once:
    the file then has the setting's new comment, and a value written in CS2's
    units stays. A file that never set them keeps the defaults, 5.73 being
    the old default's turn"""
    run = config_check_mac
    assert run("get", "input.mouse_sensitivity") == "5.73"
    assert run("get", "input.mouse_vertical_sensitivity") == "1"
    assert run("get", "input.zoom_sensitivity") == "cs2"
    assert abs(5.73 / MOUSE_FROM_OLD - 1) < 1e-3
    new = run.config.read_text()
    assert "Counter-Strike 2's" in new
    run("write", "input.mouse_sensitivity", "1.7")
    assert run("get", "input.mouse_sensitivity") == "1.7"

    # an older version's, with the note (commented defaults) and its comment
    old = ("# Halo settings\n#\n# A setting commented out (\"# key = value\") is at its default, which a\n#\n\n"
           "[input]\n\n# How far the view turns for the mouse's movement.\n"
           "# (for one run: HALO_MOUSE_SENSITIVITY=<value>)\nmouse_sensitivity = 0.5\n\n"
           "# Moving the mouse forward looks down.\n# (for one run: HALO_MOUSE_INVERT=1 makes it true)\n"
           "invert_mouse = true\n\n"
           "# How far the view turns up and down for the mouse's movement; 0 for the\n"
           "# same as input.mouse_sensitivity.\n# (for one run: HALO_MOUSE_VERTICAL_SENSITIVITY=<value>)\n"
           "mouse_vertical_sensitivity = 0.25\n")
    run.config.write_text(old)
    assert float(run("get", "input.mouse_sensitivity")) == pytest.approx(0.5 * MOUSE_FROM_OLD, abs=1e-4)
    assert float(run("get", "input.mouse_vertical_sensitivity")) == pytest.approx(0.5)
    assert run("get", "input.invert_mouse") == "true"
    text = run.config.read_text()
    assert "Counter-Strike 2's" in text and "0 for the" not in text
    assert len(re.findall(r"^(# )?mouse_sensitivity = ", text, re.M)) == 1
    assert "\ninvert_mouse = true\n" in text
    run("get", "input.mouse_sensitivity")
    assert run.config.read_text() == text

    # (not set: the defaults; "0", the same, is the multiplier 1)
    run.config.write_text(old.replace("mouse_sensitivity = 0.5", "# mouse_sensitivity = 1.0")
                          .replace("mouse_vertical_sensitivity = 0.25", "mouse_vertical_sensitivity = 0.0"))
    assert run("get", "input.mouse_sensitivity") == "5.73"
    assert run("get", "input.mouse_vertical_sensitivity") == "1"
    assert "\n# mouse_sensitivity = 5.73\n" in run.config.read_text()


# the settings display.graphics chooses, and their values in Classic and in
# Enhanced (the same on every build)
GRAPHICS_SETTINGS = ["anti_aliasing", "anisotropic_filtering", "shadow_resolution", "per_pixel_lighting",
                     "model_detail", "dynamic_lights"]
GRAPHICS_CLASSIC = ["off", "0", "128", "false", "auto", "classic"]
GRAPHICS_ENHANCED = ["smaa", "16", "512", "true", "native", "enhanced"]


def graphics_values(run):
    return [run("get", f"display.{name}") for name in GRAPHICS_SETTINGS]


def test_graphics_preset_chooses_the_settings_left_to_it(config_check_mac):
    """display.graphics gives the settings it covers their values where the
    player has not set them; Classic and Enhanced written choose them all,
    leaving the file's lines of them commented out"""
    run = config_check_mac
    assert run("get", "display.graphics") == "enhanced"
    assert graphics_values(run) == GRAPHICS_ENHANCED
    text = run.config.read_text()
    assert '\n# graphics = "enhanced"\n' in text and "\n# anisotropic_filtering = 16\n" in text
    run("write", "display.graphics", "classic")
    assert graphics_values(run) == GRAPHICS_CLASSIC
    text = run.config.read_text()
    assert '\ngraphics = "classic"\n' in text
    assert not re.search(r"^(anti_aliasing|anisotropic_filtering|shadow_resolution) =", text, re.M)
    run("write", "display.graphics", "enhanced")
    assert graphics_values(run) == GRAPHICS_ENHANCED


def test_graphics_preset_keeps_a_setting_set(config_check_mac, monkeypatch):
    """a setting it covers set in the file, by its variable or in Settings
    wins over display.graphics; written at the preset's value, it is left
    to the preset again, and so is one set at the value of a preset chosen"""
    run = config_check_mac
    run("write", "display.anisotropic_filtering", "8")
    assert "\nanisotropic_filtering = 8\n" in run.config.read_text()
    run("write", "display.graphics", "classic")
    assert graphics_values(run) == ["off", "8", "128", "false", "auto", "classic"]
    # (at the preset's value: commented out, the preset's again)
    run("write", "display.anisotropic_filtering", "0")
    assert "\n# anisotropic_filtering = 0\n" in run.config.read_text()
    run("write", "display.graphics", "enhanced")
    assert run("get", "display.anisotropic_filtering") == "16"
    # (one set by hand at the value of the preset chosen)
    run.config.write_text(run.config.read_text().replace("# shadow_resolution = 512", "shadow_resolution = 128"))
    assert run("get", "display.shadow_resolution") == "128"
    run("write", "display.graphics", "classic")
    assert "\n# shadow_resolution = 128\n" in run.config.read_text()
    run("write", "display.graphics", "enhanced")
    assert run("get", "display.shadow_resolution") == "512"
    # (and by their variables, for one run)
    monkeypatch.setenv("HALO_GRAPHICS", "classic")
    monkeypatch.setenv("HALO_MODEL_DETAIL", "highest")
    assert graphics_values(run) == ["off", "0", "128", "false", "highest", "classic"]


def test_battery_saver_reads_the_classic_values(config_check_mac):
    """with battery saver on (display.battery_saver), the game reads the
    settings display.graphics chooses at their Classic values, but those the
    player has set; Settings still shows (and writes from) the player's own"""
    run = config_check_mac
    assert run("get", "display.battery_saver") == "off"
    assert [run("saving", f"display.{name}") for name in GRAPHICS_SETTINGS] == \
        [f"{classic} {enhanced}" for classic, enhanced in zip(GRAPHICS_CLASSIC, GRAPHICS_ENHANCED)]
    run("write", "display.anti_aliasing", "msaa4x")
    assert run("saving", "display.anti_aliasing") == "msaa4x msaa4x"
    assert run("saving", "display.anisotropic_filtering") == "0 16"
    # (the other settings are as they are)
    assert run("saving", "display.vsync") == "false false"
    assert run("saving", "display.fov") == "0 0"


def test_vsync_default_by_build(tmp_path, monkeypatch, config_check_mac):
    """display.vsync: off on the Linux builds, Windows and the Mac; on under
    gamescope (the Steam Deck's Game Mode) where the player has not set it,
    paced by display.low_latency, and as set where they have"""
    for name in ("GAMESCOPE_WAYLAND_DISPLAY", "XDG_CURRENT_DESKTOP", "SteamDeck", "SteamGamepadUI", "HALO_NO_VSYNC"):
        monkeypatch.delenv(name, raising=False)
    assert config_check_mac("get", "display.vsync") == "false"
    run = config_check_build(tmp_path / "linux", ["-DHALO_VULKAN=1", "-DHALO_GUEST=1"])
    assert run("get", "display.vsync") == "false"
    assert "\n# vsync = false\n" in run.config.read_text()
    monkeypatch.setenv("SteamDeck", "1")
    monkeypatch.setenv("SteamGamepadUI", "1")
    assert run("get", "display.vsync") == "true"
    run("write", "display.vsync", "false")
    assert run("get", "display.vsync") == "false"
    # (gamescope's own variable alone)
    monkeypatch.delenv("SteamDeck")
    monkeypatch.delenv("SteamGamepadUI")
    monkeypatch.setenv("GAMESCOPE_WAYLAND_DISPLAY", "gamescope-0")
    gamescope = config_check_build(tmp_path / "gamescope", ["-DHALO_VULKAN=1", "-DHALO_GUEST=1"])
    assert gamescope("get", "display.vsync") == "true"
    assert "\n# vsync = true\n" in gamescope.config.read_text()


def test_graphics_preset_on_a_headset(config_check):
    """Enhanced and Classic in the VR build: as on the desktop and the Mac"""
    assert graphics_values(config_check) == GRAPHICS_ENHANCED
    config_check("write", "display.graphics", "classic")
    assert graphics_values(config_check) == GRAPHICS_CLASSIC


def test_default_brokers_are_brokers_txt():
    """The brokers a game uses with no brokers.txt beside its config.toml
    (p2p_signal.c's DEFAULT_BROKERS) are port/assets/network/brokers.txt's."""
    root = MENUS.parent.parent.parent
    listed = [line.split("#", 1)[0].strip()
              for line in (root / "port/assets/network/brokers.txt").read_text().splitlines()]
    default = re.search(r'^#define DEFAULT_BROKERS "([^"]*)"',
                        (root / "port/linux/src/p2p_signal.c").read_text(), re.M).group(1)
    assert default.split(",") == [line for line in listed if line]


def test_p2p_signatures_and_listings(tmp_path):
    """internet play's Ed25519 (RFC 8032), the X25519 key of a seed, and the
    server browser's listings from host to browser (tools/p2p_lobby_check.c),
    built with the platform layer's flags in the game's 32-bit x86 ABI
    (tools/x86_game_abi.py), as the game's data has them"""
    x86_abi(tmp_path)
    flags = x86_game_abi.platform_cflags(x86_game_abi.semantics_headers(tmp_path / "semantics"))
    program = tmp_path / "p2p_lobby_check"
    built = subprocess.run(["clang", *flags, "-O1", "-no-pie", "-Wl,--unresolved-symbols=ignore-all", "-o",
                            str(program), "tools/p2p_lobby_check.c", "port/linux/src/p2p_crypto.c",
                            "port/linux/src/p2p_lobby.c", "port/third_party/monocypher/monocypher.c",
                            "port/third_party/monocypher/monocypher-ed25519.c"],
                           capture_output=True, text=True)
    assert built.returncode == 0, built.stderr[-4000:]
    result = subprocess.run([str(program)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout
    assert "PASS" in result.stdout


# ---------- parties (port/linux/src/p2p_party.c): several machines in one
# process (tools/p2p_party_check.c), each case by its name

PARTY_CASES = ["make", "join", "follow", "busy", "local", "leave", "handoff", "timeout", "regroup", "disband",
               "full", "joinless", "format", "version", "keys", "host", "spare"]
PARTY_SOURCES = ["tools/p2p_party_check.c", "port/linux/src/p2p_crypto.c", "port/third_party/monocypher/monocypher.c",
                 "port/third_party/monocypher/monocypher-ed25519.c"]


def party_check_build(folder: Path, source=None) -> Path:
    """tools/p2p_party_check.c with p2p_party.c (or a copy of it: source),
    built for this computer: the party's messages are written byte by byte,
    so the game's ABI changes nothing of them"""
    compiler = shutil.which("clang") or shutil.which("cc")
    if not compiler:
        pytest.skip("needs a C compiler")
    root = Path(__file__).resolve().parent.parent
    folder.mkdir(parents=True, exist_ok=True)
    program = folder / "p2p_party_check"
    # (warnings are errors but in a mutated copy, whose fault may leave a
    # variable unused)
    defines = [f'-DP2P_PARTY_SOURCE="{source}"'] if source else ["-Werror"]
    flags = ["-std=gnu99", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Iport/linux/src",
             "-Iport/third_party/monocypher", "-idirafter", "port/linux/include"]
    built = subprocess.run([compiler, *flags, *defines, "-O1", "-o", str(program), *PARTY_SOURCES, "-lpthread"],
                           cwd=root, capture_output=True, text=True)
    assert built.returncode == 0, built.stderr[-4000:]
    return program


def party_run(program: Path, case: str) -> subprocess.CompletedProcess:
    return subprocess.run([str(program), case], capture_output=True, text=True, timeout=120)


@pytest.fixture(scope="module")
def party_check(tmp_path_factory):
    return party_check_build(tmp_path_factory.mktemp("party"))


@pytest.mark.parametrize("case", PARTY_CASES)
def test_party(party_check, case):
    """a party's machines, made, joined, leading, following the leader into
    its games, leaving, regrouping (tools/p2p_party_check.c's cases), and
    its messages' format"""
    result = party_run(party_check, case)
    assert result.returncode == 0 and "PASS" in result.stdout, result.stdout


# each fault put in p2p_party.c, and the case that must then fail: a check
# that cannot fail proves nothing
PARTY_MUTATIONS = [
    ("an older term's leader is followed", "return term > party.term;", "return term < party.term;", "timeout"),
    ("a joiner is never listed", "memcpy(party.roster[party.roster_count++], member->id, MEMBER_ID_SIZE);\n"
     "\tparty.send_wanted = 1;", "party.send_wanted = 1;", "join"),
    ("a seventeenth is listed", "if (party.roster_count >= P2P_PARTY_MAXIMUM_MEMBERS)",
     "if (party.roster_count > P2P_PARTY_MAXIMUM_MEMBERS)", "full"),
    ("a message sent again is taken", "if (member && sequence <= member->sequence)",
     "if (member && sequence < member->sequence)", "format"),
    ("signatures are not checked",
     "if (!p2p_ed25519_verify(message + 3, data, P2P_SHA256_SIZE + size, message + size))", "if (0)", "format"),
    ("another format is read", "if (message[1] != PARTY_VERSION)", "if (0)", "format"),
    ("a follow answered is asked again", "\t\tparty.handled_order = order;\n", "", "follow"),
    ("busy is an answer", "if (result != _p2p_party_follow_busy && result != _p2p_party_follow_none)",
     "if (result != _p2p_party_follow_none)", "busy"),
    ("a leader's leave is a member's", "\twas_leader = !memcmp(member->id, party.leader, MEMBER_ID_SIZE);\n"
     "\tif (party.listed)", "\twas_leader = 0;\n\tif (party.listed)", "handoff"),
    ("a silent leader is never passed over", "\t\tleader_lost();\n\t}\n\tif ((party.send_wanted",
     "\t}\n\tif ((party.send_wanted", "timeout"),
    ("no one is in the leader's game", "return place == _p2p_party_place_game && !memcmp(host, party.target_invite, "
     "P2P_KEY_HASH_SIZE);", "return 0;", "follow"),
    ("a member ends the party", "void p2p_party_disband(void)\n{\n\tpthread_mutex_lock(&p2p_lock);\n\tif (leading())",
     "void p2p_party_disband(void)\n{\n\tpthread_mutex_lock(&p2p_lock);\n\tif (party.active)", "disband"),
    ("a link no one answers is waited on for ever", "\t\tif (elapsed(party.joined_time, JOIN_WAIT))",
     "\t\tif (0)", "joinless"),
    ("the menus are not told who could not follow",
     "notice(_p2p_party_notice_follow_failed, member->name, member->result);", "(void)0;", "follow"),
    ("a friend Discord gave the spare's link to is not answered",
     "if (!party.active && spare_heard(payload, size))", "if (0)", "spare"),
    ("the spare's topic is not listened on", "\t\tp2p_signal_party(spare_listening() ? party.spare.topic : \"\");",
     "\t\t(void)0;",
     "spare"),
    ("MAKE PARTY makes a party of its own, not the spare's", "\tif (party.spare.made)\n\t\tmemcpy(token",
     "\tif (0)\n\t\tmemcpy(token", "spare"),
    ("a member is named by its party key, not its tunnel key", "\t\t\tp2p_key_hash(x25519, member->key_hash);",
     "\t\t\tp2p_key_hash(member->key, member->key_hash);", "keys"),
    ("a member of another version is named", " || member->network_version != HALO_PORT_NETWORK_VERSION)\n"
     "\t\t\tcontinue;\n\t\t/* (once each", ")\n\t\t\tcontinue;\n\t\t/* (once each", "keys"),
    ("a member names the others", "leading() && index < party.roster_count && count < maximum_count",
     "index < party.roster_count && count < maximum_count", "keys"),
    ("the lead passed to a member no better", "\t\t\tmember->host_quality <= quality)",
     "\t\t\tmember->host_quality < 0)", "host"),
    ("the last of the best, not the first", "\t\tbest = member;\n\t\tquality = member->host_quality;",
     "\t\tbest = member;", "host"),
    ("a member that cannot host takes the lead", "\t\t\tparty.host_quality > _p2p_party_host_unable)",
     "\t\t\t1)", "host"),
    ("an ask to host from anyone taken", "\t\tif (member && !memcmp(id, party.leader, MEMBER_ID_SIZE) && "
     "body_size == MEMBER_ID_SIZE + 1 &&\n\t\t\tis_self(body) && party.listed && !leading()",
     "\t\tif (member && body_size == MEMBER_ID_SIZE + 1 &&\n\t\t\tis_self(body) && party.listed && !leading()",
     "host"),
    ("the host's ask taken again and again", "\tparty.host_request = 0;\n", "", "host"),
    ("a member's host byte not read", "\t\tmember->host_quality = offset < size && body[offset] <= "
     "_p2p_party_host_open ? body[offset] :\n\t\t\t_p2p_party_host_unable;",
     "\t\tmember->host_quality = _p2p_party_host_unable;", "host"),
]


@pytest.mark.parametrize("what,original,mutated,case", PARTY_MUTATIONS, ids=[m[0] for m in PARTY_MUTATIONS])
def test_party_negative_controls(tmp_path, what, original, mutated, case):
    root = Path(__file__).resolve().parent.parent
    text = (root / "port/linux/src/p2p_party.c").read_text()
    assert text.count(original) == 1, f"the mutation's text ({what}) is no longer in p2p_party.c once"
    copy = write(tmp_path / "p2p_party.c", text.replace(original, mutated))
    program = party_check_build(tmp_path, source=copy)
    result = party_run(program, case)
    # (a fault that overruns the roster crashes the check: failing too)
    assert result.returncode != 0 and ("FAIL" in result.stdout or result.returncode < 0), \
        f"{what}: the {case} case still passes"


# ---------- friends (port/linux/src/p2p_friends.c): several machines in one
# process (tools/p2p_friends_check.c), each case by its name

FRIENDS_CASES = ["code", "file", "request", "ignore", "mutual", "offline", "presence", "hidden", "remove", "block",
                 "invite", "join", "recent", "forged"]
FRIENDS_SOURCES = ["tools/p2p_friends_check.c", "port/linux/src/p2p_crypto.c",
                   "port/third_party/monocypher/monocypher.c", "port/third_party/monocypher/monocypher-ed25519.c"]


def friends_check_build(folder: Path, source=None) -> Path:
    """tools/p2p_friends_check.c with p2p_friends.c (or a copy of it:
    source), built for this computer, as the party's check is"""
    compiler = shutil.which("clang") or shutil.which("cc")
    if not compiler:
        pytest.skip("needs a C compiler")
    root = Path(__file__).resolve().parent.parent
    folder.mkdir(parents=True, exist_ok=True)
    program = folder / "p2p_friends_check"
    defines = [f'-DP2P_FRIENDS_SOURCE="{source}"'] if source else ["-Werror"]
    flags = ["-std=gnu99", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Iport/linux/src",
             "-Iport/third_party/monocypher", "-idirafter", "port/linux/include"]
    built = subprocess.run([compiler, *flags, *defines, "-O1", "-o", str(program), *FRIENDS_SOURCES, "-lpthread"],
                           cwd=root, capture_output=True, text=True)
    assert built.returncode == 0, built.stderr[-4000:]
    return program


def friends_run(program: Path, case: str, folder: Path) -> subprocess.CompletedProcess:
    folder.mkdir(parents=True, exist_ok=True)
    return subprocess.run([str(program), case, str(folder)], capture_output=True, text=True, timeout=120)


@pytest.fixture(scope="module")
def friends_check(tmp_path_factory):
    return friends_check_build(tmp_path_factory.mktemp("friends"))


@pytest.mark.parametrize("case", FRIENDS_CASES)
def test_friends(friends_check, tmp_path, case):
    """friends' codes, their file, requests (accepted, ignored, mutual, while
    away), presence (only friends read it; appearing offline), removing,
    blocking, invites, joining, recent players, and what is turned away
    (tools/p2p_friends_check.c's cases)"""
    result = friends_run(friends_check, case, tmp_path / "profiles")
    assert result.returncode == 0 and "PASS" in result.stdout, result.stdout


# each fault put in p2p_friends.c, and the case that must then fail
FRIENDS_MUTATIONS = [
    ("signatures are not checked", "\treturn p2p_ed25519_verify(message + 3, data, P2P_SHA256_SIZE + size, "
     "message + size);", "\treturn 1;", "forged"),
    ("another format is read", "if (size < 3 || message[0] != FRIENDS_MAGIC || message[1] != FRIENDS_VERSION ||",
     "if (size < 3 || message[0] != FRIENDS_MAGIC ||", "forged"),
    ("a request sent again is taken", "\t\tif (time > request->last_time)", "\t\tif (1)", "forged"),
    ("the checks' budget is not spent", "\tfriends.checks_left--;\n", "", "forged"),
    ("a code's check character is not checked", "\tif (code_check(values) != values[11])\n\t\treturn -1;\n", "",
     "code"),
    ("a check that misses swaps", "\t\tinterim = code_double(interim) ^ values[index];",
     "\t\tinterim = interim ^ values[index];", "code"),
    ("an ignored code's request is shown", "\tif (find_blocked(code) || ignored(code))\n\t\treturn;",
     "\tif (find_blocked(code))\n\t\treturn;", "ignore"),
    ("a blocked code's request is shown", "\tif (find_blocked(code) || ignored(code))\n\t\treturn;",
     "\tif (ignored(code))\n\t\treturn;", "block"),
    ("asking each other is not accepting", "\tif (find_sent(code))\n\t{\n\t\t/* (asked for: friends now.",
     "\tif (0)\n\t{\n\t\t/* (asked for: friends now.", "mutual"),
    ("a request sent is never sent again", "(!sent->sent || elapsed(sent->sent_time, ADD_INTERVAL))",
     "!sent->sent", "offline"),
    ("the presence shared while hidden", "\tsharing = friends.share_setting;\n", "\tsharing = 1;\n", "hidden"),
    ("a friend never goes offline", "\treturn record->heard && !(record->presence.flags & _presence_offline) &&\n"
     "\t\t!elapsed(record->heard_time, PRESENCE_TIMEOUT);",
     "\treturn record->heard && !(record->presence.flags & _presence_offline);", "presence"),
    ("a friend's FIRST is not answered", "((!presence_same(&presence, &friends.published) || (friends.presence_wanted &&",
     "((!presence_same(&presence, &friends.published) || (0 &&", "presence"),
    ("the presence key kept on removal", "\tfriends.topics_wanted = 1;\n\tpresence_rotate();\n",
     "\tfriends.topics_wanted = 1;\n", "remove"),
    ("invites taken at once again", "(record->invited && !elapsed(record->invite_time, INVITE_INTERVAL))", "(0)",
     "invite"),
    ("an offline friend is joined", "if (record && !joined && online(record) &&", "if (record && !joined &&", "join"),
    ("a player of no code kept beside its code",
     "\t\t\tmemmove(recent, recent + 1, sizeof(*recent) * (size_t)(P2P_FRIENDS_RECENT - 1 - index));\n",
     "\t\t\tbreak;\n", "recent"),
    ("a friend listed again as a recent player",
     "\t\tif (!recent->used || (recent->has_code && find_friend(recent->code)))",
     "\t\tif (!recent->used)", "request"),
    ("no ADD back when both asked at once", "\t\tnotice(_p2p_friends_notice_accepted, name);\n\t\tsend_add(code);\n",
     "\t\tnotice(_p2p_friends_notice_accepted, name);\n", "mutual"),
    ("its own code a recent player", "(code && !memcmp(code, friends.code, P2P_FRIEND_CODE_BYTES)) || !cleaned[0])",
     "!cleaned[0])", "recent"),
    ("the list's order turned", "\t\treturn first->kind < second->kind ? -1 : 1;",
     "\t\treturn first->kind < second->kind ? 1 : -1;", "recent"),
    ("its own key a friend in the file", " || (friends.has_identity && !memcmp(record.key, friends.key, "
     "P2P_KEY_SIZE)))", ")", "file"),
]


@pytest.mark.parametrize("what,original,mutated,case", FRIENDS_MUTATIONS, ids=[m[0] for m in FRIENDS_MUTATIONS])
def test_friends_negative_controls(tmp_path, what, original, mutated, case):
    root = Path(__file__).resolve().parent.parent
    text = (root / "port/linux/src/p2p_friends.c").read_text()
    assert text.count(original) == 1, f"the mutation's text ({what}) is no longer in p2p_friends.c once"
    copy = write(tmp_path / "p2p_friends.c", text.replace(original, mutated))
    program = friends_check_build(tmp_path, source=copy)
    result = friends_run(program, case, tmp_path / "profiles")
    assert result.returncode != 0 and ("FAIL" in result.stdout or result.returncode < 0), \
        f"{what}: the {case} case still passes"



# port_config.c on its own, with the app's file paths (HALO_DATA_ROOT) and C
# library calls: "get <setting>" prints it, "write <setting> <value>" sets it
CONFIG_PROGRAM = r"""
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "port_config.h"

void platform_log(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
}

int main(int argc, char **argv)
{
    char text[256];

    if (argc == 3 && !strcmp(argv[1], "get") && config_text(argv[2], text, sizeof(text)))
        return puts(text) < 0;
    if (argc == 4 && !strcmp(argv[1], "write"))
        return !config_write(argv[2], argv[3]);
    return 1;
}
"""


def test_config_has_each_section_once(tmp_path):
    """config.toml names each section once, as TOML asks: the defaults the
    game writes, and a file an older version wrote with [display] twice
    (which went unread), joined"""
    compiler = shutil.which("clang") or shutil.which("cc")
    if not compiler:
        pytest.skip("needs a C compiler")
    root = Path(__file__).resolve().parent.parent
    stubs = tmp_path / "stubs"
    (stubs / "SDL3").mkdir(parents=True)
    (stubs / "SDL3" / "SDL.h").write_text("")
    (stubs / "platform.h").write_text("void platform_log(const char *format, ...);\n")
    # (beside the stubs, which it then includes instead of the platform layer's)
    for name in ("port_config.c", "port_config.h", "install_layout.h"):
        shutil.copy(root / "port/linux/src" / name, stubs / name)
    (tmp_path / "main.c").write_text(CONFIG_PROGRAM)
    program = tmp_path / "config_program"
    subprocess.run([compiler, "-DHALO_ANDROID=1", "-DHALO_GUEST=1", f"-I{stubs}",
                    f"-I{root}/port/third_party/tomlc17", "-o", str(program),
                    str(tmp_path / "main.c"), str(stubs / "port_config.c"),
                    str(root / "port/third_party/tomlc17/tomlc17.c"), "-lpthread"], check=True)
    data = tmp_path / "data"
    data.mkdir()
    config = data / "config.toml"

    def run(*command):
        result = subprocess.run([str(program), *command], capture_output=True, text=True,
                                env={**os.environ, "HALO_DATA_ROOT": str(data)})
        assert result.returncode == 0, result.stderr
        assert "using the defaults" not in result.stderr, result.stderr
        return result.stdout.strip(), result.stderr

    def sections():
        return re.findall(r"^\[(\w+)\]$", config.read_text(), re.M)

    # the defaults
    assert run("get", "display.first_person_legs")[0] == "false"
    assert len(sections()) == len(set(sections())), sections()
    run("write", "display.first_person_legs", "true")
    assert run("get", "display.first_person_legs")[0] == "true"

    # an older version's
    config.write_text("[display]\nvsync = false\n\n[game]\nconsole_log = \"all\"\n\n"
                      "[display]\nfirst_person_legs = true\n")
    value, log = run("get", "display.first_person_legs")
    assert value == "true" and "joined" in log
    assert sections().count("display") == 1
    assert run("get", "display.vsync")[0] == "false"
    assert run("get", "game.console_log")[0] == "all"


# ---------- system link's stand-in machines (tools/system_link_bots.py)


def test_system_link_bots_messages_are_the_games():
    """the stand-in machines number their messages as network_messages.h does,
    and send each at the size network_messages.c's fields decode (pad 0,
    bytes 1, shorts 2, longs 4; an array a count byte and one element)"""
    import struct
    import time
    import types
    from tools import system_link_bots as bots

    root = Path(__file__).resolve().parents[1]
    header = (root / "source/networking/network_messages.h").read_text()
    enum = header[header.index("enum network_game_message_type"):]
    names = re.findall(r"\t_message_(\w+)", enum[:enum.index("};")])
    for name, value in vars(bots).items():
        if name.lower() in names:
            assert names.index(name.lower()) == value, name
    sizes = {}
    source = (root / "source/networking/network_messages.c").read_text()
    for fields, name in re.findall(r"\t\{\n((?:\t\t.*\n)*?)\t\},\n\tNETWORK_GAME_MESSAGE_DEFINITION\((\w+),", source):
        sizes[name] = sum(int(count) * {"pad": 0, "bytes": 1, "shorts": 2, "longs": 4}.get(kind, 0) + (kind == "array")
                          for kind, count in re.findall(r"_data_packet_field_(\w+), (\d+)", fields))

    sent = []
    machine = bots.Machine(1, "127.0.0.2", "127.0.0.1", print)
    machine.udp.close()
    machine.send = sent.append
    machine.udp = types.SimpleNamespace(sendto=lambda data, address: sent.append(data))
    machine.joined()
    machine.handle(bots.SERVER_MACHINE_ACCEPTED, struct.pack(">ih", 0, 1))
    settings = bytes(0x24) + b"levels\\test\\bloodgulch\\bloodgulch".ljust(0x80, b"\0")
    machine.handle(bots.SERVER_GAME_SETTINGS_UPDATE, struct.pack(">HHH", len(settings), 0, len(settings)) + settings)
    machine.tick(time.monotonic())
    machine.handle(bots.SERVER_BEGIN_GAME, b"")
    machine.tick(time.monotonic() + 60)
    machine.handle(bots.SERVER_GAME_UPDATE, struct.pack(">I", 1))
    machine.tick(time.monotonic())
    assert len(sent) == 6
    for data in sent:
        name = names[data[-1]]
        assert len(data) - 4 == sizes[name], name


# ---------- the wire format (tools/network_format.py)


def test_network_format_is_the_recorded_one():
    """what the machines send each other is what port/linux/network_format.txt
    records for HALO_PORT_NETWORK_VERSION, and NETCODE.md says what that
    version changed"""
    from tools import network_format

    problems = network_format.check()
    assert not problems, "\n".join(problems)


# ---------- the tag validator (port/linux/game/tag_validate.c)

RETAIL_MAPS = Path("assets/maps")
# the tag validator alone on map files (tools/map_validate.c), built in the
# game's 32-bit x86 ABI, for the tests (tools/x86_game_abi.py)
MAP_VALIDATE_FOLDER = Path("build/map_validate")
MAP_VALIDATE = MAP_VALIDATE_FOLDER / "map_validate"


def map_validate(tmp_path):
    """build/map_validate/map_validate, built if it is missing or older than
    its sources; skips where the game's 32-bit x86 ABI cannot be built"""
    x86_abi(tmp_path)
    return x86_game_abi.build_map_validate(MAP_VALIDATE_FOLDER)


def test_map_validate_builds(tmp_path):
    """the tag validator's program builds, maps its tag caches where the
    game reads its maps, and refuses a file that is no map"""
    program = map_validate(tmp_path)
    result = subprocess.run([str(program)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    junk = tmp_path / "junk.map"
    junk.write_bytes(bytes(4096))
    result = subprocess.run([str(program), str(junk)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 1 and f"{junk}: refused" in result.stdout, result.stdout + result.stderr


def _retail_maps(tmp_path):
    maps = sorted(RETAIL_MAPS.glob("*.map"))
    if not maps:
        pytest.skip("needs the retail maps in assets/maps")
    map_validate(tmp_path)
    return maps


def test_retail_maps_need_no_corrections(tmp_path):
    """every retail map in assets/maps passes the tag validator, each of its
    structure bsps too, with no correction: the schemas (tag_schema_*.c)
    say what the game's own maps hold"""
    maps = _retail_maps(tmp_path)
    result = subprocess.run([str(MAP_VALIDATE), "--strict", *map(str, maps)], capture_output=True, text=True,
                            timeout=1800)
    assert result.returncode == 0, result.stdout[-6000:]


def test_tag_validator_survives_damaged_maps(tmp_path):
    """retail maps with a few of their tags' words changed at random: the
    validator never crashes or hangs, and a map it lets through is clean
    when checked again"""
    maps = _retail_maps(tmp_path)
    chosen = [path for path in maps if path.stem in ("bloodgulch", "a10", "ui")] or maps[:1]
    result = subprocess.run([str(MAP_VALIDATE), "--fuzz", "300", "--seed", "1", *map(str, chosen)],
                            capture_output=True, text=True, timeout=1800)
    assert result.returncode == 0, (result.stdout + result.stderr)[-6000:]


# ---------- Custom Edition maps (port/linux/game/cache_file_formats.c, custom_edition_cache.c)

def _custom_edition_maps(tmp_path):
    """the Custom Edition maps (HALO_CUSTOM_EDITION_MAPS, or the gitignored
    assets/custom_edition), with the bitmaps.map, sounds.map and loc.map
    they need beside them"""
    configured = os.environ.get("HALO_CUSTOM_EDITION_MAPS")
    folder = Path(configured) if configured else Path("assets/custom_edition")
    maps = [path for path in sorted(folder.glob("*.map"))
            if path.stem.lower() not in ("bitmaps", "sounds", "loc")]
    if not maps or not (folder / "bitmaps.map").is_file():
        pytest.skip("needs Custom Edition maps (HALO_CUSTOM_EDITION_MAPS or assets/custom_edition)")
    map_validate(tmp_path)
    return maps


def test_custom_edition_maps_are_loaded_and_checked(tmp_path):
    """every Custom Edition map is loaded, converted and passed by the tag
    validator, each of its structure bsps too: none is refused (a community
    map's own mistakes are corrected and logged)"""
    maps = _custom_edition_maps(tmp_path)
    result = subprocess.run([str(MAP_VALIDATE), *map(str, maps)], capture_output=True, text=True, timeout=3600)
    assert result.returncode == 0, result.stdout[-6000:]
    assert "refused" not in result.stdout, result.stdout[-6000:]


def test_custom_edition_loader_and_validator_survive_damaged_maps(tmp_path):
    """Custom Edition maps with a few of their file's words changed at random:
    the loader, the conversion and the validator never crash or hang, and a
    map they let through is clean when checked again"""
    maps = _custom_edition_maps(tmp_path)
    chosen = [path for path in maps if path.stem.lower() in ("bloodgulch", "hugeass", "ui")] or maps[:2]
    result = subprocess.run([str(MAP_VALIDATE), "--fuzz", "100", "--seed", "1", *map(str, chosen)],
                            capture_output=True, text=True, timeout=3600)
    assert result.returncode == 0, (result.stdout + result.stderr)[-6000:]
