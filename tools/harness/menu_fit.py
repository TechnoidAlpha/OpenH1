"""What the menu files' rows, buttons and prompts hold, where the engine draws it (test_menu_fit.py): the rows of a
list apart, a one-line text's capitals on the middle of the framed picture it is in or on, a prompt's words in its
picture, and Choose Mission's words in their boxes and its arrows by its cards. Each check returns the problems it
finds, so that a deliberate fault can be shown to be found."""

import re
from typing import NamedTuple

from harness import menu_layout

port_settings = menu_layout.port_settings

# the fonts' measures (port_settings.py's: draw_string puts the baseline the ascent below a text box's top and
# text_y, the capitals rising their height above it)
FONTS = {"ui\\small_ui": (port_settings.SMALL_UI_ASCENT, port_settings.SMALL_UI_CAPS),
         "ui\\large_ui": (port_settings.LARGE_UI_ASCENT, port_settings.LARGE_UI_CAPS)}
# the pictures of the port's parts that frame what is in them, their whole height (a section's picture is a line
# under its title, not a frame: not one of them)
FRAMES = re.compile(r"main_menu/parts/(item|outline|button|row|option_row|pill|player_row)_\d+x\d+$")
# how far off the middle capitals may be: half a unit, where whole units cannot centre them
SLACK = 0.5


class Placed(NamedTuple):
    """a widget placed in its parent: its name, attributes, and its box's left and top in the parent"""
    name: str
    attributes: dict
    x: int
    y: int


def port_names():
    """the names of the port's own screens' widgets (tools/port_settings.py's files)"""
    names = set()
    for name in [*port_settings.settings_files(), *port_settings.multiplayer_files(), *port_settings.campaign_files()]:
        names |= set(re.findall(r'<widget name="([^"]+)"', (menu_layout.CE / name).read_text()))
    return sorted(names)


def children(widgets, name):
    return [Placed(child, widgets[child].attributes, x, y) for child, x, y, _ in widgets[name].children]


def rows_overlapping(widgets, names):
    """the rows (op_*) of each list that overlap another of it at another place (those at one place are shown
    one at a time)"""
    problems = []
    for name in names:
        widget = widgets[name]
        if widget.attributes.get("type") != "column_list":
            continue
        rows = []
        for child, x, y, platform in widget.children:
            attributes = widgets[child].attributes
            if child.rsplit("/", 1)[-1].startswith("op_") and attributes.get("bitmap"):
                top = y + int(attributes.get("top", 0))
                rows.append((child, top, top + int(attributes.get("height", 0)), platform))
        for index, (row, top, bottom, platform) in enumerate(rows):
            for other, other_top, other_bottom, other_platform in rows[index + 1:]:
                if top == other_top or (platform and other_platform and
                                        not set(platform.split()) & set(other_platform.split())):
                    continue
                if top < other_bottom and other_top < bottom:
                    problems.append(f"{name}: {row} ({top} to {bottom}) over {other} ({other_top} to {other_bottom})")
    return problems


def capitals_middle(attributes, top):
    """where a text's (or a spinner's value's) capitals' middle is, from its parent's top; a spinner draws its
    value at its box's top, not text_y's"""
    ascent, caps = FONTS[attributes["font"]]
    text_y = 0 if attributes.get("type") == "spinner" else int(attributes.get("text_y", 0))
    return top + int(attributes.get("top", 0)) + text_y + ascent - caps / 2


def one_line(attributes):
    return attributes.get("font") in FONTS and "\\n" not in attributes.get("text", "") and \
        int(attributes.get("height", 0)) <= 32


def texts_off_middle(widgets, names):
    """the one-line texts whose capitals are off the middle of the framed part they are on (their own picture's)
    or in (their row's), and the spinners' values in such a row"""
    problems = []
    for name in names:
        widget = widgets[name]
        attributes = widget.attributes
        if not FRAMES.search(attributes.get("bitmap", "")):
            continue
        height = int(attributes.get("height", 0))
        if one_line(attributes) and attributes.get("type") == "text":
            middle = capitals_middle(attributes, -int(attributes.get("top", 0)))
            if abs(middle - height / 2) > SLACK:
                problems.append(f"{name}: its capitals' middle {middle:g}, its picture's {height / 2:g}")
        for child in children(widgets, name):
            if child.attributes.get("type") in ("text", "spinner") and one_line(child.attributes) and \
                    not child.attributes.get("bitmap"):
                middle = capitals_middle(child.attributes, child.y)
                if abs(middle - height / 2) > SLACK:
                    problems.append(f"{child.name}: its capitals' middle {middle:g}, its row's {height / 2:g}")
    return problems


def prompt_overflows(widgets, names):
    """the footers' prompts (prompt_*, with a part's picture) whose words end past their picture's frame: their
    key's cap and words as drawn (port_settings.prompt_words_end), in a server browser with its keys, in the lobby
    (which a browser opens) with the wider of its keys and the browser's"""
    problems = []
    for name in names:
        widget = widgets[name]
        attributes = widget.attributes
        if not name.rsplit("/", 1)[-1].startswith("prompt_") or not FRAMES.search(attributes.get("bitmap", "")) or \
                "text" not in attributes:
            continue
        browser = "/join_game/" in name
        end = int(attributes.get("text_x", 0)) + port_settings.prompt_words_end(attributes["text"], browser,
                                                                                 "/lobby/" in name)
        room = int(attributes["width"]) - port_settings.PROMPT_RIGHT_ROOM
        if end > room:
            problems.append(f"{name}: its words end at {end}, its picture's room at {room}")
    return problems


def stock_objectives():
    """the missions' objectives as the game shows them (campaign_menu.inc's campaign_string: the lines joined, a word
    broken at a line's end whole again)"""
    import xml.etree.ElementTree as ElementTree
    root = ElementTree.parse(menu_layout.CE / "strings.xml").getroot()
    strings = next(element for element in root.iter("strings")
                   if element.get("name") == "main_menu/solo_level_select/map_data")
    found = []
    for string in strings.iter("string"):
        # (the file's line breaks are "\\n", two characters)
        text = re.sub(r"(?<! )-\\n", "", string.get("text"))
        found.append(" ".join(text.replace("\\n", " ").split()))
    return found[:10]


def wrapped_lines(words, width):
    """how many lines words take wrapped to a width in small_ui (settings_text_wrap: at spaces)"""
    lines, line = 1, ""
    for word in words.split(" "):
        longer = f"{line} {word}" if line else word
        if line and port_settings.small_ui_width(longer) > width:
            lines, line = lines + 1, word
        else:
            line = longer
    return lines


def mission_overflows(widgets, names):
    """Choose Mission (issue 681): its arrows as far from the row of cards at every width (MISSION_ARROW_GAP, a unit
    either way: the row wider apart on a wide screen, campaign_menu.inc's mission_cards_place); NOT YET BEATEN within
    its box on either card; each mission's objective on no more lines than its box holds and the game wraps it to
    (MISSION_OBJECTIVE_LINES), at every width"""
    base = port_settings.CAMPAIGN
    if f"{base}/mission_screen" not in names:
        return []
    problems = []
    big, small = port_settings.MISSION_BIG[0], port_settings.MISSION_SMALL[0]
    gaps = set()
    for aspect, width in port_settings.WIDE_SCREENS.items():
        extra = port_settings.wide_extra_of(width)
        boxes = menu_layout.layout(f"{base}/mission_screen", width, every=widgets)
        row = big + 2 * small + 2 * (port_settings.MISSION_CARD_GAP + extra // 8)
        left, right = 320 - row // 2, 320 - row // 2 + row
        previous, following = menu_layout.one(boxes, "arrow_previous"), menu_layout.one(boxes, "arrow_next")
        gaps |= {(aspect, left - previous.x1), (aspect, following.x0 - right)}
        objective = menu_layout.one(boxes, "detail_objective")
        lines = port_settings.MISSION_OBJECTIVE_LINES
        holds = 1 + (objective.y1 - objective.y0 - port_settings.SMALL_UI_ASCENT - port_settings.SMALL_UI_DESCENT) // \
            port_settings.SMALL_UI_LINE
        if holds < lines:
            problems.append(f"{aspect}: the objective's box holds {holds} lines, {lines} are drawn")
        for words in stock_objectives():
            needs = wrapped_lines(words, objective.x1 - objective.x0)
            if needs > min(lines, holds):
                problems.append(f"{aspect}: \"{words}\" takes {needs} lines, {min(lines, holds)} are shown")
    for aspect, gap in sorted(gaps):
        if abs(gap - port_settings.MISSION_ARROW_GAP) > 1:
            problems.append(f"{aspect}: an arrow {gap} units from the cards, not {port_settings.MISSION_ARROW_GAP}")
    for kind in ("big", "small"):
        box = widgets[f"{base}/card_unbeaten_{kind}"].attributes
        need = port_settings.small_ui_width("NOT YET BEATEN") + port_settings.SMALL_UI_LEAD
        if int(box["width"]) < need:
            problems.append(f"card_unbeaten_{kind}: {box['width']} wide, NOT YET BEATEN {need}")
    return problems


def dialog_overflows(widgets, names):
    """The port's questions in the Quit box's look (issue 696): the message's box in the box's body, as far in from
    its sides (its middle the box's: the alert picture drawn DIALOG_WIDTH wide), its text fitted to it (text_flags
    wrap: a long mission's name wraps, ui_widget.c's ui_widget_port_text_fit); each button's pill (the button bars'
    picture: TEXT_BUTTON_PILL) at least BUTTON_ROOM from the box's edge and the footer's line, and from the button
    beside it, so that the ring the focus draws round it touches neither"""
    problems = []
    left, top = port_settings.DIALOG_LEFT, port_settings.DIALOG_TOP
    box_right = left + port_settings.DIALOG_BOX_WIDTH
    # (the alert picture's box: its edge 2 wide, its body from 29 to 130 and its footer from 131 to 157 down it)
    inner_left, inner_right, body_top, body_bottom = left + 2, box_right - 2, top + 29, top + 130
    footer_top, footer_bottom = top + 131, top + 157
    pill_x0, pill_y0, pill_x1, pill_y1 = port_settings.TEXT_BUTTON_PILL
    for screen in ("restart_screen", "update_screen"):
        name = next((name for name in names if name.endswith(f"/{screen}")), None)
        if not name:
            continue
        boxes = menu_layout.layout(name, 640, every=widgets)
        messages = [box for box in boxes if box.name.endswith("_message")]
        if len(messages) != 1:
            problems.append(f"{screen}: {len(messages)} messages")
            continue
        message = messages[0]
        attributes = widgets[next(child for child in widgets if child.endswith("/" + message.name))].attributes
        if "wrap" not in attributes.get("text_flags", "").split():
            problems.append(f"{message.name}: its text not fitted to it (text_flags wrap)")
        if message.x0 < inner_left or message.x1 > inner_right or message.y0 < body_top or message.y1 > body_bottom:
            problems.append(f"{message.name}: {message} past the box's body")
        if abs((message.x0 - inner_left) - (inner_right - message.x1)) > 1:
            problems.append(f"{message.name}: {message.x0 - inner_left} in from the box's left, "
                            f"{inner_right - message.x1} from its right")
        buttons = sorted((box for box in boxes if box.name.startswith(("restart_button_", "button_")) and
                          widgets.get(next((child for child in widgets if child.endswith("/" + box.name)), ""),
                                      menu_layout.Widget({}, [])).attributes.get("bitmap") == port_settings.TEXT_BUTTON),
                         key=lambda box: box.x0)
        pills = [(box.x0 + pill_x0, box.y0 + pill_y0, box.x0 + pill_x1, box.y0 + pill_y1, box.name) for box in buttons]
        if len(pills) != 2:
            problems.append(f"{screen}: {len(pills)} buttons")
        for x0, y0, x1, y1, button in pills:
            # (the picture drawn as wide as the alert picture is stretched: the PC's 256 drawn 280 wide)
            room = min(x0 - inner_left, inner_right - x1, y0 - footer_top, footer_bottom - y1)
            if room < BUTTON_ROOM:
                problems.append(f"{button}: its pill {room} from the box's edge or footer")
        for (_, _, right, _, first), (left_next, _, _, _, second) in zip(pills, pills[1:]):
            if left_next - right < BUTTON_ROOM:
                problems.append(f"{first} and {second}: {left_next - right} apart")
    return problems


# (the room a question's button keeps round it: the focus's ring is drawn in its pill, which this keeps clear of the
# box's edge, its footer's line and the button beside it)
BUTTON_ROOM = 2


def pause_box_misfits(widgets, names):
    """The pause boxes' pictures (pause/pausebox_*: issues 596 and 690): each frame as tall as its rows need at the
    campaign pause menu's spacing (pause_box_height), and its footer's band (below its line, above its edge) with its
    middle the legends' (PAUSE_BOX_LEGENDS_BOTTOM up from the box's bottom, the Xbox's 20-high pictures' middle)"""
    import re as _re
    problems = []
    for rows in [*port_settings.PAUSE_BOX_BUTTONS, 5, 6]:
        height = port_settings.pause_box_height(rows)
        svg = port_settings.pause_box_svg("center", height)
        rects = [tuple(float(value) for value in match) for match in
                 _re.findall(r'<rect x="[^"]*" y="([^"]+)" width="[^"]*" height="([^"]+)" fill="(?:black|#2896ff)"',
                             svg)]
        # (the line over the footer, and the edge under it)
        line = next(((y, h) for y, h in rects if h < 1 and y > height / 2), None)
        edge = next(((y, h) for y, h in rects if h == 2 and y > height / 2), None)
        if not line or not edge:
            problems.append(f"{rows} rows: no footer line or edge")
            continue
        band_middle = (line[0] + line[1] + edge[0]) / 2
        legends_middle = height - port_settings.PAUSE_BOX_LEGENDS_BOTTOM + 10
        if abs(band_middle - legends_middle) > 0.05:
            problems.append(f"{rows} rows: the footer's middle {band_middle:g}, the legends' {legends_middle:g}")
        rows_bottom = port_settings.PAUSE_BOX_ROWS_TOP + (rows - 1) * port_settings.PAUSE_BOX_ROW_PITCH + \
            port_settings.PAUSE_BOX_ROW_HEIGHT
        if rows_bottom + port_settings.PAUSE_BOX_LEGENDS_GAP > height - port_settings.PAUSE_BOX_LEGENDS_BOTTOM:
            problems.append(f"{rows} rows: the last row over the legends")
        if line[0] < rows_bottom:
            problems.append(f"{rows} rows: the footer's line over the last row")
    return problems


def chooser_off_middle(widgets, names):
    """The map lists' chooser (SINGLEPLAYER or MULTIPLAYER, issue 687): its words' capitals and its arrows' points on
    its row's middle (half a unit off, where whole units cannot), its arrows at least CHOOSER_ARROW_ROOM in from the
    row's ends (its picture's curve)"""
    name = port_settings.MAP_KIND_CHOOSER
    if name not in names:
        return []
    problems = []
    row = widgets[name].attributes
    height, width = int(row["height"]), int(row["width"])
    spinner_name, x, y, _ = widgets[name].children[0]
    spinner = widgets[spinner_name].attributes
    middle = capitals_middle(spinner, y)
    if abs(middle - height / 2) > SLACK:
        problems.append(f"{spinner_name}: its capitals' middle {middle:g}, its row's {height / 2:g}")
    for key in ("header_bounds", "footer_bounds"):
        top, left, bottom, right = (int(value) for value in spinner[key].split())
        point = y + top + port_settings.ARROW_SM_MIDDLE
        if abs(point - height / 2) > SLACK:
            problems.append(f"{spinner_name}'s {key}: its arrow's point at {point:g}, the row's middle {height / 2:g}")
        room = min(x + left, width - (x + right))
        if room < CHOOSER_ARROW_ROOM:
            problems.append(f"{spinner_name}'s {key}: its arrow {room} from the row's end")
    return problems


CHOOSER_ARROW_ROOM = 6


def continue_art_off_middle(widgets, names):
    """CONTINUE's picture (issue 687: its top against the item's, room under it) as far from the item's top as from
    its bottom and its left"""
    item = f"{port_settings.CAMPAIGN}/item_continue"
    if item not in names:
        return []
    height = int(widgets[item].attributes["height"])
    art = widgets[f"{port_settings.CAMPAIGN}/continue_art"].attributes
    top, left, art_height = int(art["top"]), int(art["left"]), int(art["height"])
    bottom = height - top - art_height
    if top != bottom or left != top:
        return [f"continue_art: {left} from the item's left, {top} from its top, {bottom} from its bottom"]
    return []


def unbroken(text):
    """a string of strings.xml as the description draws it (ui_widget.c's ui_widget_port_text_unbreak): its
    paragraphs, each on one line, a word broken with a hyphen whole again (a hyphen before a capital kept)"""
    text = text.replace("\\n", "\n")
    text = re.sub(r"(?<=[^ \n])-\n(?=[a-z])", "", text)
    text = re.sub(r"(?<=[^ \n]-)\n(?=[^\n])", "", text)
    return [" ".join(paragraph.split()) for paragraph in re.split(r"\n\s*\n", text) if paragraph.strip()]


def map_description_misfits(widgets, names):
    """The map lists' description (issue 704: the box's right left empty beside the PC version's 146-unit column), at
    4:3 and 16:9: its pictures, names, words and New Game's badges inside the box's frame (port_settings.
    MAP_DESC_FRAME, less its edge) and in its middle, a kind's apart; the names and words as wide as the frame less
    MAP_DESC_ROOM either side, centred and wrapped to it (text_flags "wrap unbreak"); and each stock map's words,
    wrapped so, on the lines their box holds"""
    import xml.etree.ElementTree as ElementTree
    strings = {element.get("name"): [string.get("text") for string in element.iter("string")]
               for element in ElementTree.parse(menu_layout.CE / "strings.xml").getroot().iter("strings")}
    screens = {"mp": f"{port_settings.MT}/mp_map_select/mp_map_select_screen",
               "solo": "main_menu/solo_level_select/solo_level_select_screen"}
    _, _, frame_width, frame_height = port_settings.MAP_DESC_FRAME
    edge, room = port_settings.MAP_DESC_EDGE, port_settings.MAP_DESC_ROOM
    problems = []
    for kind, (folder, prefix, _, _, _) in port_settings.MAP_DESCRIPTIONS.items():
        if f"{folder}/{prefix}_right_data" not in names:
            continue
        for aspect in ("4:3", "16:9"):
            boxes = menu_layout.layout(screens[kind], port_settings.WIDE_SCREENS[aspect], every=widgets)
            box = menu_layout.one(boxes, "sel_list_desc_bkd")
            x0, y0 = box.x0 + edge, box.y0 + edge
            x1, y1 = box.x0 + frame_width - edge, box.y0 + frame_height - edge
            drawn = [found for found in boxes if f"{prefix}_right_item" in found.path]
            groups = {}
            for found in drawn:
                groups.setdefault("replay_level" if found.name == "difficulty_indicator" else
                                  found.name.split("_right_")[0], []).append(found)
            for found in drawn:
                where = f"{kind} {aspect}: {found.name}"
                if not (found.x0 >= x0 + room and found.x1 <= x1 - room and found.y0 >= y0 + room and
                        found.y1 <= y1 - room):
                    problems.append(f"{where} {found} not {room} inside the box's frame ({x0}, {y0}, {x1}, {y1})")
                if found.name == "difficulty_indicator":
                    continue
                if abs((found.x0 - x0) - (x1 - found.x1)) > 1:
                    problems.append(f"{where}: {found.x0 - x0} in from the frame's left, {x1 - found.x1} from its right")
                if found.name.endswith(("_right_name", "_right_data")):
                    attributes = next(widget.attributes for name, widget in widgets.items()
                                      if name.endswith("/" + found.name))
                    if found.x1 - found.x0 < x1 - x0 - 2 * room:
                        problems.append(f"{where}: {found.x1 - found.x0} wide, the frame {x1 - x0} less {room} "
                                        "either side")
                    if attributes.get("align") != "center" or \
                            not {"wrap", "unbreak"} <= set(attributes.get("text_flags", "").split()):
                        problems.append(f"{where}: not centred and wrapped to its box (align center, text_flags "
                                        "wrap unbreak)")
            badges = [found for found in drawn if found.name == "difficulty_indicator"]
            if badges:
                middle = (min(found.x0 for found in badges) + max(found.x1 for found in badges)) / 2
                if abs(middle - (x0 + x1) / 2) > 1:
                    problems.append(f"{kind} {aspect}: the badges' middle {middle:g}, the frame's {(x0 + x1) / 2:g}")
            for group in groups.values():
                for index, first in enumerate(group):
                    for second in group[index + 1:]:
                        if first.overlaps(second) and first.name != second.name:
                            problems.append(f"{kind} {aspect}: {first.name} over {second.name}")
        data = widgets[f"{folder}/{prefix}_right_data"].attributes
        width, height = int(data["width"]), int(data["height"])
        holds = 1 + (height - port_settings.SMALL_UI_ASCENT - port_settings.SMALL_UI_DESCENT) // \
            port_settings.SMALL_UI_LINE
        for text in strings[data["string_list"]]:
            paragraphs = unbroken(text)
            needs = sum(wrapped_lines(paragraph, width) for paragraph in paragraphs) + len(paragraphs) - 1
            if needs > holds:
                problems.append(f"{prefix}_right_data: \"{' / '.join(paragraphs)}\" takes {needs} lines, its box "
                                f"holds {holds}")
    return problems
