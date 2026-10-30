"""The port's settings screens, which tools/ce_menus.py writes in place of the
PC version's (port/assets/menus/ce): laid out as its are (rows of a label and
a spinner over its row backgrounds, a line of help for the row chosen,
Defaults, OK and Cancel), with what this port has to set instead of what the
PC version had (no connection speed or texture quality: a window or the full
screen, the frame rate, the keyboard's own controls).

A row's spinner sets one of config.toml's settings (setting=, values=), or of
the profile being edited ("profile.<field>": the controller's settings), by
port/linux/game/menu_functions.c: "port setting load" shows its value,
"port settings save" (OK) writes those changed and applies them, "port
settings defaults" shows the defaults. Controls Setup binds the keyboard and
mouse's controls, by the functions the PC version names for it.
"""

import re
import sys
from pathlib import Path
from typing import NamedTuple
from xml.sax.saxutils import quoteattr

PE = "main_menu/settings_select/player_setup/player_profile_edit"
# the mods' screen, and its rows (_mods); the main menu's items
# (ce/main_menu.xml): MODS in Credits' place (Credits is left out:
# tools/ce_menus.py's LEFT_OUT), Quit a row below it (its CHILD_OFFSETS)
MODS = "main_menu/mods"
MOD_ROWS = 11
MODS_ITEM_PLACE = (192, 391)
QUIT_ITEM_PLACE = (192, 427)
# the update question (the Steam Frame's, gamescope's), over the main menu (_update)
UPDATE = "main_menu/update"
YES_NO = [("YES", "true"), ("NO", "false")]
ON_OFF = [("ON", "true"), ("OFF", "false")]
# the multiplayer armor colors, in the profile's order (player_profile.c)
SHIELD_COLORS = ["white", "black", "red", "blue", "gray", "yellow", "green", "pink", "purple", "cyan", "cobalt",
                 "orange", "teal", "sage", "brown", "tan", "maroon", "salmon"]
# input.mouse_vertical_sensitivity's: a multiplier of the horizontal
VERTICAL_SENSITIVITIES = [(f"{value:g}X", f"{value:g}") for value in (0.25, 0.5, 0.6, 0.7, 0.75, 0.8, 0.9, 1, 1.1, 1.25,
                                                                       1.5, 2)]
VOLUMES = [(str(step), f"{step / 10:g}") for step in range(11)]
# the platforms a menu element's platform attribute names (menu_files.c's
# MENU_PLATFORM, "vr": the VR build, playing in the headset, and "hdr": a
# renderer that draws display.hdr)
MENU_PLATFORMS = ("desktop", "android", "vr", "hdr", "touch", "pointer", "computer")
# display.hdr_paper_white's values, percent of the system's SDR white
PAPER_WHITES = (50, 60, 70, 80, 90, 100, 110, 125, 150, 175, 200, 250)

# the old settings screens' layout (rows of a label and a spinner over their
# row backgrounds, a line of help for the row chosen, Defaults, OK and Cancel),
# which VR Setup keeps: each its folder below PE, its screen's widget, its
# header (widget, bitmap), the row spacing, and its rows (label, setting,
# [(shown, value)], help, platform[, key])
SCREENS = {}

# ---------- VR Setup (the VR build's, HALO_VR: port/linux/src/vr.c), its
# item in the profile menu only while the game runs in VR (platform "vr").
# Two screens: what the hands do, then where things are. Its settings are
# read again as config.toml changes (vr.c, settings_read), but those marked
# RESTART, which open the headset's session. A row with a screen in place
# of a setting opens it (VR_LINK), after saving this screen's.
# (A value added to a setting is one more (shown, value) pair here, then
# python3 tools/port_settings.py writes the screens' files.)

RESTART = "(RESTART)"
VR_HANDS = [("ARMS", "arms"), ("FLOATING HANDS", "floating"), ("GAME ARMS", "game")]
VR_AIM = [("CONTROLLER", "controller"), ("HEAD", "head"), ("GAMEPAD", "gamepad")]
VR_DISPLAY = f"{PE}/vr_display/vr_display_screen"
VR_VISOR = f"{PE}/vr_visor/vr_visor_screen"
VR_SCREENS = {
    "vr_controls": {
        "screen": "vr_controls_screen",
        "header": ("header_vr_controls", f"{PE}/vr_controls/header_vr_controls"),
        # (sixteen rows: closer than the others' 28px, the help line lower)
        "spacing": 18,
        "help_top": 366,
        "rows": [
            ("HANDS:", "vr.hands", VR_HANDS,
             "Aiming with the controller, your hands: with arms\nfrom the shoulders, floating, or the game's arms.",
             None),
            ("AIM:", "vr.aim", VR_AIM,
             "What aims: the right controller, which holds the\nweapon, your head, or a gamepad's right stick.", None),
            ("GAMEPAD VIEW:", "vr.gamepad_view", [("CAMERA", "camera"), ("LEVEL", "level"), ("SNAP", "snap")],
             "With a gamepad, the right stick turns and\npitches the view, only turns it, or in steps.", None),
            ("TWO-HANDED AIM:", "vr.two_handed", ON_OFF,
             "The left grip holds a long gun by its foregrip:\nit aims along the line between your hands.", None),
            ("AIM ASSIST:", "vr.aim_assist", [("OFF", "0"), ("WEAK", "0.5"), ("ON", "1"), ("STRONG", "1.5")],
             "Near an enemy your aim is slow to leave it, and\nzoomed, it is drawn gently toward it.", None),
            ("SWING TO MELEE:", "vr.melee_gesture", ON_OFF,
             "A hard swing of the right controller melees, as\nB does; with its grip held, a softer one will do.", None),
            ("HOLSTERS:", "vr.holsters", ON_OFF,
             "Reach over your right shoulder for a long gun, or\nto your right hip for a pistol, and grip to draw it.",
             None),
            ("PHYSICAL CROUCH:", "vr.physical_crouch", ON_OFF,
             "Crouch in the room and you crouch in the game;\nstand up and you stand.", None),
            ("SCOPES:", "vr.scope_view", [("OFF", "off"), ("SMART-LINK", "smartlink"), ("AT THE EYE", "eye")],
             "Smart-link: the right stick's click zooms on your\nvisor. At the eye: raise the scope to your eye.", None),
            ("RELOAD BY HAND:", "vr.manual_reload", ON_OFF,
             "Take a magazine from your left hip and push it\nin: loaded at once. X still reloads.", None),
            ("TURNING:", "vr.turn", [("SNAP", "snap"), ("SMOOTH", "smooth")],
             "How the right stick turns you: in steps, or\ncontinuously.", None),
            ("SNAP TURN ANGLE:", "vr.snap_turn_angle",
             [(f"{angle} DEGREES", str(angle)) for angle in (15, 22.5, 30, 45, 60, 90)],
             "How far each snap turn turns you.", None),
            ("SMOOTH TURN SPEED:", "vr.smooth_turn_speed",
             [(f"{speed} DEG/S", str(speed)) for speed in (60, 90, 120, 150, 180, 240, 300, 360)],
             "How fast smooth turning turns you, the stick\npushed all the way: degrees a second.", None),
            ("VEHICLE VIEW:", "vr.vehicle_view", [("FIRST PERSON", "first_person"), ("THIRD PERSON", "third_person")],
             "In a vehicle's seat, the view from your head, or\nthe game's camera following the vehicle.", None),
            ("DEATH VIEW:", "vr.death_view", [("FACE YOUR BODY", "face"), ("STILL", "still"), ("GAME'S CAMERA", "game")],
             "As you die, the view stays where your head was\n(turned to your body in a blink), or the game's.",
             None),
            ("HAND, HEIGHT, HUD...", None, VR_DISPLAY,
             "The hand that holds the weapon, your height, the\nworld's scale, the HUD, the menus, the resolution.",
             None),
        ],
    },
    "vr_display": {
        "screen": "vr_display_screen",
        "header": ("header_vr_display", f"{PE}/vr_display/header_vr_display"),
        # (twelve rows: closer than the others' 28px, the help line lower)
        "spacing": 24,
        "help_top": 366,
        "rows": [
            ("DOMINANT HAND:", "vr.dominant_hand", [("RIGHT", "right"), ("LEFT", "left")],
             "The hand that holds the weapon. Left: mirrored,\nthe holsters at your left, the magazines at your right.",
             None),
            ("SWAP STICKS:", "vr.swap_sticks", [("AS THE HAND", "auto"), ("NO", "no"), ("YES", "yes")],
             "Move with the right stick and turn with the left:\nby default, with the left hand dominant.", None),
            (f"POSITION {RESTART}:", "vr.height", [("SEATED", "seated"), ("STANDING", "standing")],
             "Seated: your eyes where they are when you\nrecentre. From the next time the game starts.", None),
            ("EYE HEIGHT:", "vr.player_height",
             [(f"{height / 100:.2f} M", f"{height / 100:g}") for height in range(140, 205, 5)],
             "Standing, the height of your eyes above the\nfloor.", None),
            ("WORLD SCALE:", "vr.world_scale",
             [(f"{scale:g}X", f"{scale:g}") for scale in (0.75, 0.8, 0.9, 1, 1.1, 1.2, 1.25, 1.5)],
             "How large the world looks: more than 1 makes it\nlarger, and you smaller in it.", None),
            ("HUD DISTANCE:", "vr.hud_distance",
             [(f"{distance:.1f} M", f"{distance:g}") for distance in (1, 1.5, 2, 2.5, 3, 4, 5)],
             "How far in front of your eyes the HUD is.", None),
            ("HUD SIZE:", "vr.hud_size",
             [(f"{size} DEGREES", str(size)) for size in (40, 50, 60, 70, 80, 90, 100)],
             "How wide the HUD is, in degrees of your view.", None),
            ("MENU DISTANCE:", "vr.menu_distance",
             [(f"{distance:.1f} M", f"{distance:g}") for distance in (1.5, 2, 2.5, 3, 3.5, 4)],
             "How far in front of you the menus are.", None),
            ("MENU WIDTH:", "vr.menu_width",
             [(f"{width:.1f} M", f"{width:g}") for width in (1.6, 2, 2.3, 2.6, 3, 3.5, 4)],
             "How wide the menus are.", None),
            (f"RESOLUTION {RESTART}:", "vr.resolution_scale",
             [(f"{round(scale * 100)}%", f"{scale:g}") for scale in (0.5, 0.6, 0.75, 0.9, 1, 1.1, 1.25, 1.5)],
             "Of the size the headset recommends: less is\nfaster, more sharper. From the next start.", None),
            (f"REFRESH RATE {RESTART}:", "vr.refresh_rate",
             [("HEADSET'S", "0"), *((f"{rate} HZ", str(rate)) for rate in (72, 80, 90, 120, 144))],
             "The headset's rate, if it offers it; the game\ndraws a frame each refresh. From the next start.",
             None),
            ("HUD AND VISOR...", None, VR_VISOR,
             "The HUD on your helmet's visor: its depths, its\nglass and glow, the helmet's rim, the shields' energy.",
             None),
        ],
    },
}
# the visor's page of VR Display (titled as it is: its title's picture is
# VR Display's), every value of it read again as it changes (vr_visor.c)
VR_SCREENS["vr_visor"] = {
    "screen": "vr_visor_screen",
    "header": ("header_vr_display", f"{PE}/vr_display/header_vr_display"),
    # (twelve rows: closer than VR Display's 28px, the help line lower)
    "spacing": 24,
    "help_top": 366,
    "rows": [
        ("HUD:", "vr.hud", [("VISOR", "visor"), ("FLAT", "flat")],
         "On your helmet's visor, curved about your eyes,\nor a flat screen before them.", None),
        ("HUD DEPTH:", "vr.hud_depth",
         [("NONE", "0"), *((f"{depth:g}X", f"{depth:g}") for depth in (0.5, 0.75, 1, 1.25, 1.5, 2))],
         "How far apart in depth the HUD's parts are: the\ntracker nearest, the waypoints farthest.", None),
        ("HUD WRAP:", "vr.hud_wrap",
         [("NONE", "0"), *((f"{round(wrap * 100)}%", f"{wrap:g}") for wrap in (0.5, 1, 1.5, 2))],
         "The HUD's corners turned out along the visor's\ncurve, toward the edges of your view.", None),
        ("HUD LAG:", "vr.hud_lag",
         [("NONE", "0"), *((f"{round(lag * 1000)} MS", f"{lag:g}") for lag in (0.02, 0.03, 0.05, 0.08, 0.12))],
         "How far the HUD trails your head's turns.", None),
        ("HUD GLOW:", "vr.hud_glow",
         [("NONE", "0"), *((f"{round(glow * 100)}%", f"{glow:g}") for glow in (0.25, 0.5, 0.75, 1))],
         "The HUD's glow on the visor's glass.", None),
        ("HUD ON GLASS:", "vr.hud_glass",
         [("NONE", "0"), *((f"{round(glass * 100)}%", f"{glass:g}") for glass in (0.25, 0.5, 0.75, 1))],
         "The HUD as light on the glass: the world through\nit, the glass's tint, its bloom and reflection.", None),
        ("HUD REACTS:", "vr.hud_react",
         [("NONE", "0"), *((f"{round(react * 100)}%", f"{react:g}") for react in (0.25, 0.5, 0.75, 1))],
         "More contrast in bright light, dimmer in the dark,\na flicker as your shields are hit.", None),
        ("HELMET RIM:", "vr.helmet_rim",
         [("NONE", "0"), *((f"{round(rim * 100)}%", f"{rim:g}") for rim in (0.25, 0.5, 0.75, 1))],
         "The helmet's faceplate about the visor, at the\nedges of your view, in depth.", None),
        ("RIM DEPTH:", "vr.helmet_rim_depth",
         [(f"{round(depth * 100)} CM", f"{depth:g}") for depth in (0.06, 0.08, 0.1, 0.12, 0.15, 0.2)],
         "How far before your eyes the helmet's rim is.", None),
        ("VISOR SHADE:", "vr.visor_frame",
         [("NONE", "0"), *((f"{round(frame * 100)}%", f"{frame:g}") for frame in (0.2, 0.4, 0.6, 0.8, 1))],
         "The helmet's shadow on the glass at the edges of\nyour view, and the glass's faint gold.", None),
        ("SHIELD EFFECTS:", "vr.visor_effects", ON_OFF,
         "The shields on the glass: their energy, the red\nwarning, the HUD's flicker as they break.", None),
        ("SHIELD ENERGY:", "vr.visor_energy",
         [(f"{round(energy * 100)}%", f"{energy:g}") for energy in (0.25, 0.5, 0.75, 1, 1.5, 2)],
         "How strong the shields' energy across the glass\nis, as they are hit and as they recharge.", None),
    ],
}
SCREENS.update(VR_SCREENS)
# the profile menu's VR Setup item (in VR only), below About, its
# description and picture the list's tenth (the video settings' picture)
VR_ITEM = f"{PE}/vr_setup_profile_item"
FRAMES_ADDED = {
    f"{PE}/profile_options": [
        '<frame png="ce/shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__5.png" '
        'width="512" height="256"/>'],
}

# ---------- Settings: its tabs and their pages (port/linux/game/menu_functions.c's
# "port settings ..." functions)
#
# Settings opens straight to its tabs, VIDEO to PROFILE across the top (LB and
# RB, Q and E, or a click switch them), each a page of rows under section
# headers, and on the right a panel saying what the row chosen does: its name,
# its value in large type and plain words. A row ending in "›" opens a page of
# its own (Advanced Graphics, Controller...), laid out the same, with its own
# title, OK and Cancel. A change is kept, on every tab and page, until an OK
# (Settings' or a page's) writes them all (config.toml's, and the profile's
# controller settings, name and colour, saved), or Settings' Cancel forgets
# them all; a page's Cancel forgets its own.
#
# Only the controller's settings, the name and the colour are the profile's
# ("profile.<field>"): their page and the PROFILE tab, with no profile, offer
# to make one there (CREATE PROFILE), and Settings opens without one.
#
# Each page is generated laid out for every row (its rows and section headers
# a step apart, the tabs' from SETTINGS_TOP, the pages' from PAGE_TOP), and
# menu_functions.c packs the rows shown each frame at the same steps: a row of
# another platform (left out as the menus load), or one hidden (in a game, with
# no profile, a row that follows another's value), leaves no gap, and a
# section with no row shown is hidden. A tab's page keeps the folder and the
# screen's name of the settings screen it replaces, which the PC version's
# profile menu (still generated, no longer opened) names.
#
# A row: (label, setting, [(shown, value)], help, platform[, key]); a link: (label,
# None, the screen it opens, help, platform[, key, function]); a section:
# (SECTION, title). platform is None for every platform, else the platforms
# (MENU_PLATFORMS) separated by spaces; key names the row's widgets where two
# rows set one setting, else the setting's name does. A help is plain words,
# wrapped to the panel by menu_functions.c; after a "|", a note in smaller
# words. (A value added to a setting is one more (shown, value) pair here,
# then python3 tools/port_settings.py writes the pages' files.)

SECTION = "section"


class Slider(NamedTuple):
    """a slider row's choices, in place of [(shown, value)]: its track runs
    from least to most, left and right step it by step (holding them steps
    on), a click on the track or the mouse dragged along it sets it, and A, or
    a click on its number, types a value (Enter keeps it) from typed_least to
    typed_most. menu_functions.c's settings_sliders has the same"""
    least: float
    most: float
    step: float
    typed_least: float
    typed_most: float


# a slider's track, its thumb and its number, in the value's column
SLIDER_TRACK_WIDTH, SLIDER_THUMB_WIDTH, SLIDER_TRACK_HEIGHT = 92, 8, 14
SLIDER_VALUE_X, SLIDER_VALUE_WIDTH, SLIDER_VALUE_HEIGHT = 94, 46, 20
# input.mouse_sensitivity's: CS2's sensitivity (port/linux/include/mouse_sensitivity.h)
MOUSE_SENSITIVITY = Slider(0.1, 10.0, 0.05, 0.01, 100.0)
# the geometry, in the 640x480 menu units (menu_functions.c's SETTINGS_*
# have the same): the rows' column and the panel beside it
SETTINGS_ROW_X, SETTINGS_ROW_WIDTH, SETTINGS_ROW_HEIGHT, SETTINGS_ROW_STEP = 28, 344, 20, 21
# (a section after rows has SETTINGS_SECTION_GAP more above it)
SETTINGS_SECTION_HEIGHT, SETTINGS_SECTION_STEP, SETTINGS_SECTION_GAP = 18, 20, 6
SETTINGS_TOP, PAGE_TOP = 104, 80
SETTINGS_TABS_TOP = 76
# (the tabs from SETTINGS_TABS_X, LB and RB, or Q and E, either side)
# (SETTINGS_TAB_WIDTH the tab's picture's; laid out SETTINGS_TAB_LAID wide,
# its picture's ends kept (keep_ends), so that LB's and RB's prompts keep
# WIDE_MARGIN from the screen's edges)
SETTINGS_TAB_WIDTH, SETTINGS_TAB_HEIGHT, SETTINGS_TAB_STEP, SETTINGS_TABS_X = 90, 22, 88, 56
SETTINGS_TAB_LAID = 86
# (the tab keys' prompts: LB's at the margin, RB's after the last tab)
SETTINGS_TAB_PROMPT_WIDTH = 30
SETTINGS_PANEL_X, SETTINGS_PANEL_WIDTH, SETTINGS_PANEL_BOTTOM = 388, 224, 400
# the value's column in a row, and the arrows either side of it
SETTINGS_SPINNER_X, SETTINGS_SPINNER_WIDTH = 188, 140
# the Quality row's pictures in the panel (its stills: SETTINGS_STILLS)
SETTINGS_STILL_WIDTH, SETTINGS_STILL_HEIGHT = 96, 60
SETTINGS_BLUE = "#FF2896FF"
SETTINGS_DIM = "#FF6F9CD0"
SETTINGS = "main_menu/settings"
SETTINGS_TITLE = f"{SETTINGS}/header_settings"
SETTINGS_FUNCTIONS = "port settings page"

# ---------- the wide layout of the port's own screens (menu_tags.c's
# ui_widget_port_wide_layout): a widget's wide attribute, "dx dw", moves it
# (and what is in it) dx and makes it dw wider on a 16:9 screen, WIDE_WIDTH
# of the menus' units at 480 lines, and by a part of that on a narrower one,
# none at the menus' 640 (4:3); a wider screen has 16:9's layout, centred. A
# widget with none stays in the centred 640, as the game's own menus do.
# Settings, the server browser, Multiplayer and the lobby use it
WIDE_WIDTH = 852
WIDE_EXTRA = WIDE_WIDTH - 640
WIDE_HALF = WIDE_EXTRA // 2
# the space kept between the screen's edges and what is nearest them (the
# carnage report's CARNAGE_LEFT): the rows', panels', tabs' and buttons'
WIDE_MARGIN = 24
# the screens' widths the layout is checked at (menu units at 480 lines, as
# d3d8_device.c's screen_mode_choose has them): 4:3, 16:10 (the Steam Deck),
# 16:9 and 21:9
WIDE_SCREENS = {"4:3": 640, "16:10": 768, "16:9": 852, "21:9": 1138}


def wide(dx: float = 0, dw: float = 0):
    """a widget's wide attribute: moved dx, dw wider, on a 16:9 screen (None:
    neither, the widget where the 640 have it)"""
    return f"{round(dx)} {round(dw)}" if round(dx) or round(dw) else None


# a widget as far from the screen's left edge as the 640 have it from
# theirs, and from its right
WIDE_LEFT = wide(-WIDE_HALF)
WIDE_RIGHT = wide(WIDE_HALF)


def wide_extra_of(width: int) -> int:
    """the extra width a view this wide lays the screens out in
    (menu_tags.c's ui_widget_port_wide_extra_of)"""
    return 0 if width <= 640 else WIDE_EXTRA if width >= WIDE_WIDTH else width - 640


def wide_scale(value: int, extra: int) -> int:
    """a wide attribute's move or growth at an extra width, rounded as
    menu_tags.c's ui_widget_port_wide_scale (half away from 0)"""
    scaled = value * extra
    rounded = abs(scaled) + WIDE_EXTRA // 2
    return rounded // WIDE_EXTRA * (-1 if scaled < 0 else 1)


def wide_spread(index: int, count: int) -> int:
    """the move of the index'th of count things spread from the screen's
    left edge to its right (tabs)"""
    return round(-WIDE_HALF + index * WIDE_EXTRA / max(count - 1, 1))


# Settings on a wide screen: the rows from the screen's left, SETTINGS_WIDE_ROWS
# wider (the labels' column; the values at the rows' right), the panel beside
# them to the screen's right, wider by the rest, the tabs spread across it
SETTINGS_WIDE_ROWS = 120
SETTINGS_WIDE_PANEL = (-WIDE_HALF + SETTINGS_WIDE_ROWS, WIDE_EXTRA - SETTINGS_WIDE_ROWS)

DISPLAY_MODES = [("FULLSCREEN", "fullscreen"), ("BORDERLESS", "borderless"), ("WINDOWED", "windowed")]
FIELDS_OF_VIEW = [("DEFAULT", "0")] + [(str(n), str(n)) for n in range(80, 151, 5)]
ANTI_ALIASING_HELP = "Smooths jagged edges. FXAA and SMAA cost little; {} sharper and {} more of the graphics card."
STREAMER_LEVELS = [("OFF", "off"), ("SEVERE", "severe"), ("STRONG AND WORSE", "strong"), ("MILD AND WORSE", "mild")]
SEED_UPLOADS = (64, 128, 256, 512, 1024, 2048, 4096, 8192, 10240)
VR_CONTROLS = f"{PE}/vr_controls/vr_controls_screen"


def _page_screen(folder: str, screen: str) -> str:
    return f"{PE}/{folder}/{screen}"


# the pages that rows open (and the tabs'): folder below PE, screen, title
# (a <bitmap> name: TITLES), and for a tab its name
ADVANCED = ("video_settings/advanced", "advanced_graphics_screen")
FIRST_PERSON = ("video_settings/first_person", "first_person_screen")
# (Android's: the picture on a tall screen, and portrait's frame)
SCREEN_LAYOUT = ("video_settings/screen_layout", "screen_layout_screen")
CONTROLLER = ("gamepad_setup", "gamepad_setup_screen")
KEY_BINDINGS = ("controls_setup", "controls_settings_screen")
STREAMER = ("interface_settings/streamer_mode", "streamer_mode_screen")
# (making a profile: as the main menu's items do, its name asked, then the
# profile made and edited, menu_functions.c's settings_profile_new)
CREATE_PROFILE = ("CREATE PROFILE ›", None, None,
                  "Your name, color and controller layout are kept in a player profile. Make one to set them; the "
                  "rest of Settings works without one.", None, "create_profile", "port settings new profile")

PAGES = {
    # ---- the tabs
    "video_settings": {
        "screen": "video_settings_screen", "tab": "VIDEO",
        "about": "The window, the frame rate and the graphics.",
        "rows": [
            (SECTION, "DISPLAY"),
            ("WINDOW", "display.mode", DISPLAY_MODES,
             "How the game takes the screen. Fullscreen sets the display to the game's resolution; borderless covers "
             "the screen with a window, so that other windows open over it at once; windowed is a window you can "
             "move and resize. F11 switches between the window and the screen.", "computer"),
            # (in Window Size's place, the one the display mode shown uses:
            # menu_functions.c's video_rows_show; port/linux/game/menu_tags.c
            # adds the display's resolutions, and puts the sizes that fit the
            # desktop in place of Window Size's own)
            ("RESOLUTION", "display.resolution", [("NATIVE", "native")],
             "What fullscreen and borderless draw at. Native is the display's own, the sharpest; a lower one is "
             "lighter on a slow computer, and softer. Fullscreen sets the display to it; borderless scales it.",
             "computer"),
            ("WINDOW SIZE", "display.window_size", [("1280 x 960", "1280x960")],
             "The window's size: 4:3 first, then 16:10, 16:9 and 21:9. Its edges can also be dragged.", "computer"),
            ("FRAME RATE LIMIT", "display.max_fps",
             [("AUTO", "0"), ("30", "30"), ("60", "60"), ("120", "120"), ("144", "144"), ("165", "165"),
              ("240", "240"), ("NONE", "-1")],
             "The most frames drawn a second while V-Sync is off. Auto is twice the display's refresh rate. More "
             "feels smoother and uses more power; None draws as fast as it can.", "desktop"),
            ("VSYNC", "display.vsync", ON_OFF,
             "Waits for the display before showing each frame, so that the picture never tears. It adds a little "
             "input lag, and holds the frame rate to the display's refresh rate.", None),
            ("FIELD OF VIEW", "display.fov", FIELDS_OF_VIEW,
             "How wide you see on foot, in degrees across a 16:9 screen. Default keeps the Xbox's view; wider shows "
             "more around you.", None),
            ("ORIENTATION", "display.orientation",
             [("LANDSCAPE", "landscape"), ("PORTRAIT", "portrait"), ("AUTO", "auto")],
             "Which way up you hold the device. Portrait has a strip of buttons along the top, the picture under "
             "it and the touch controls below it. Auto turns with the device.", "android"),
            ("SCREEN LAYOUT ›", None, _page_screen(*SCREEN_LAYOUT),
             "Where the picture sits on a screen taller than it, its shape, and the frame around it in portrait.",
             "android"),
            ("FIRST PERSON ›", None, _page_screen(*FIRST_PERSON),
             "Your weapon's field of view and whether it is drawn, your legs and arms, your shield's color, and the "
             "view from a vehicle's passenger seat.", None),
            (SECTION, "GRAPHICS"),
            # (Classic or Enhanced, CUSTOM while any setting it chooses is
            # another value: menu_functions.c's graphics_preset_show; choosing
            # either sets them all again, as config_write does)
            ("QUALITY", "display.graphics", [("ENHANCED", "enhanced"), ("CLASSIC", "classic"), ("CUSTOM", "custom")],
             "Enhanced: smoother edges, sharper textures and shadows, smoother lighting. Classic looks as the "
             "Xbox did.|Changing one shows CUSTOM.", None),
            ("SHADOW QUALITY", "display.shadow_resolution",
             [("XBOX", "128"), ("MEDIUM", "256"), ("HIGH", "512"), ("ULTRA", "1024")],
             "How sharp the shadows of characters, vehicles and weapons are. Xbox is the original's soft, blocky "
             "shadow; each step up is crisper, and costs a little more of the graphics card.", None),
            # (display.hdr's rows, on the renderers that draw it: menu_files.c's
            # "hdr")
            ("HDR", "display.hdr", [("AUTO", "auto"), ("ON", "on"), ("OFF", "off")],
             "Brighter highlights on an HDR display. Auto turns it on while the display shows HDR; a display "
             "without HDR shows the game as it was.", "hdr"),
            ("ADVANCED GRAPHICS ›", None, _page_screen(*ADVANCED),
             "Anti-aliasing, texture filtering, model detail, the lighting and the shields' glow, HDR's look, "
             "smooth motion and the tick rate.", None),
            (SECTION, "BATTERY"),
            # (display.battery_saver: port/linux/src/battery_saver.c)
            ("BATTERY SAVER", "display.battery_saver", [("OFF", "off"), ("ON", "on"), ("AUTO", "auto")],
             "Up to 60 frames a second and Classic graphics, for a longer battery and a cooler device. Auto turns "
             "it on while the device runs on its battery. Your own settings come back as it turns off.", None),
        ],
    },
    "audio_settings": {
        "screen": "audio_settings_screen", "tab": "AUDIO",
        "about": "The sound and its devices, 3D audio, the volumes and voice chat.",
        "rows": [
            # (the devices' rows: port/linux/game/menu_tags.c adds the devices
            # SDL finds; 3D audio, the reverb and the resampler:
            # port/linux/src/dsound_sdl.c)
            (SECTION, "SOUND"),
            ("OUTPUT DEVICE", "audio.output_device", [("SYSTEM DEFAULT", "default")],
             "Where the game's sound and the other players' voices play.", "desktop"),
            ("3D AUDIO", "audio.spatial", [("AUTO", "auto"), ("HEADPHONES", "headphones"), ("SPEAKERS", "speakers")],
             "Where the sounds around you come from. Headphones places each ahead, behind, above or below you, as "
             "the Xbox's 3D audio did; Speakers pans them left and right. Auto chooses by what you are listening "
             "on, and changes when you plug headphones in or take them out.", None),
            ("REVERB", "audio.reverb", ON_OFF,
             "Echoes sounds as the place you are in would, and muffles those behind walls, as the Xbox did. It "
             "costs a little of the processor.", None),
            ("RESAMPLER", "audio.resampler", [("CLEAN", "sinc"), ("CLASSIC", "linear")],
             "How sounds are converted to your speakers' rate. Clean is clear; Classic is brighter but harsher, and "
             "costs a little less.", None),
            ("SOUND", "audio.enabled", ON_OFF,
             "Turns all sound off or on, from the next time the game starts.", None),
            (SECTION, "VOLUME"),
            ("MASTER VOLUME", "audio.volume", VOLUMES, "The volume of everything the game plays.", None),
            ("MUSIC VOLUME", "audio.music_volume", VOLUMES, "The music's volume.", None),
            ("EFFECTS VOLUME", "audio.effects_volume", VOLUMES,
             "Every other sound: weapons, vehicles, the world and the characters' speech.", None),
            # voice chat: this machine's own (port/linux/game/network_voice.c)
            (SECTION, "VOICE CHAT"),
            ("VOICE CHAT", "audio.voice_chat",
             [("PUSH TO TALK", "push_to_talk"), ("OPEN MIC", "open_mic"), ("OFF", "off")],
             "Talk to the other players in network games: while Push to Talk is held (Key Bindings sets its key), "
             "whenever you speak, or never.", None),
            ("INPUT DEVICE", "audio.input_device", [("SYSTEM DEFAULT", "default")],
             "The microphone voice chat listens to.", "desktop"),
            ("VOICE VOLUME", "audio.voice_volume", VOLUMES, "How loud the other players' voices are.", None),
        ],
    },
    # (CONTROLS: the mouse's rows, in Mouse Setup's place)
    "mouse_settings": {
        "screen": "mouse_settings_screen", "tab": "CONTROLS",
        "about": "The mouse, the controller and the keys.",
        "rows": [
            (SECTION, "MOUSE"),
            ("SENSITIVITY", "input.mouse_sensitivity", MOUSE_SENSITIVITY,
             "How far the view turns as the mouse moves, on Counter-Strike 2's scale: your CS2 number turns the "
             "same here. Drag the slider, or press left and right. Click the number (or press A) to type an exact "
             "value, e.g. 1.7, then press Enter.|No acceleration.",
             None),
            ("VERTICAL SENSITIVITY", "input.mouse_vertical_sensitivity", VERTICAL_SENSITIVITIES,
             "How far the view turns up and down, of how far it turns side to side. 1X turns it as far.", None),
            ("ZOOM AIM SPEED", "input.zoom_sensitivity", [("CS2", "cs2"), ("ORIGINAL", "halo")],
             "How fast you turn while zoomed in. CS2 matches Counter-Strike 2's scoped feel; Original is how Halo "
             "felt.|Mouse only: controllers always turn as Halo did.", None),
            ("INVERT MOUSE", "input.invert_mouse", YES_NO,
             "Moving the mouse forward looks down, as a flight stick does.", None),
            ("MOUSE AIM ASSIST", "input.mouse_aim_assist", ON_OFF,
             "Slows and drags your aim along with a target while you aim with the mouse, as it does with a "
             "controller.", None),
            ("INSTANT AIM", "display.direct_camera", ON_OFF,
             "Turns the view the moment the mouse moves, not up to two of the game's updates later. Off feels as "
             "the original did.", "computer"),
            (SECTION, "CONTROLLER"),
            ("CONTROLLER ›", None, _page_screen(*CONTROLLER),
             "The look sensitivity and inversion, the button and stick layouts, and vibration, kept in your player "
             "profile.", None),
            # (input.crouch: the controller's and the keys'; input.touch_crouch: the touch controls')
            ("CROUCH", "input.crouch", [("HOLD", "hold"), ("TOGGLE", "toggle")],
             "Hold crouches while the button is held, as Halo did. Toggle crouches at a tap until you press it "
             "again, jump, take a seat or die; holding it still crouches while held.", None),
            ("CROUCH WALK", "input.crouch_walk", [("OFF", "false"), ("ON", "true")],
             "Off: a full push of the stick stands you up from a crouch, as Halo did. On: you stay crouched "
             "and walk at a crouch's speed.|The touch controls always stay crouched.", None),
            ("TOUCH CROUCH", "input.touch_crouch", [("TOGGLE", "toggle"), ("HOLD", "hold")],
             "The touch controls' Crouch. Toggle crouches at a tap until the next tap, a jump, a seat or death; "
             "Hold crouches while your finger is on it.", "touch"),
            (SECTION, "KEYBOARD"),
            ("KEY BINDINGS ›", None, _page_screen(*KEY_BINDINGS),
             "The keys and mouse buttons for each action, two for each.", None),
            ("BUTTON PROMPTS", "input.button_prompts",
             [("AUTO", "auto"), ("CONTROLLER", "controller"), ("KEYBOARD", "keyboard")],
             "The buttons the HUD's prompts show. Auto shows the keys or the controller's buttons, as you last "
             "played with.", None),
            # (VR Setup, the VR build's, while it plays in the headset)
            (SECTION, "VR"),
            ("VR SETUP ›", None, VR_CONTROLS,
             "Your hands, aiming, turning, your height, the HUD and the menus in VR.", "vr"),
        ],
    },
    "network_setup": {
        "screen": "network_settings_screen", "tab": "ONLINE",
        "about": "Internet play, sharing maps and updates.",
        "rows": [
            # (in a game, the rows that would drop it or are the main menu's
            # are hidden: menu_functions.c's settings_rows_show)
            (SECTION, "INTERNET"),
            ("INTERNET PLAY", "network.online", ON_OFF,
             "Host and join games over the internet with invite links, and find public games. Off keeps to the "
             "local network.", "desktop android"),
            ("OPEN ROUTER PORT", "network.allow_upnp", ON_OFF,
             "Asks your router to open the game's port (UPnP), so that players can reach the games you host.",
             "desktop android"),
            ("JOIN FROM CLIPBOARD", "network.join_from_clipboard", ON_OFF,
             "Joins a game when you switch to the game with its invite link copied.", "desktop android"),
            # (port/linux/game/map_share.c: this machine's part in another's
            # game)
            (SECTION, "MAP SHARING"),
            ("DOWNLOAD MAPS", "network.map_download", ON_OFF,
             "Joining a game on a Custom Edition map you do not have downloads it from the players, if they "
             "share it.", "desktop android"),
            ("HELP SEND MAPS", "network.map_seed", ON_OFF,
             "In another player's game, sends its map to the players who join without it, as a torrent's peers "
             "do.", "desktop android"),
            ("UPLOAD LIMIT", "network.map_seed_upload",
             [(f"{kb} KB/S" if kb < 1024 else f"{kb // 1024} MB/S", str(kb)) for kb in SEED_UPLOADS],
             "The most of your upload that sending maps takes, so that the game keeps the rest.",
             "desktop android"),
            (SECTION, "UPDATES"),
            ("CHECK FOR UPDATES", "update.auto", ON_OFF,
             "Looks for a new version when the game starts.", "desktop android"),
        ],
    },
    "interface_settings": {
        "screen": "interface_settings_screen", "tab": "INTERFACE",
        "about": "The HUD, the players' names, the scoreboard and the chat.",
        "rows": [
            (SECTION, "HUD"),
            ("HIGH-RES HUD", "display.high_res_hud", ON_OFF,
             "Draws the HUD from sharp high-resolution redraws. Off draws the game's own pictures.", None),
            # (shown while HIGH-RES HUD is: menu_functions.c's
            # hud_button_style_row_show)
            ("BUTTON STYLE", "display.hud_button_style",
             [("CLASSIC", "classic"), ("GEMS", "gems"), ("DARK GEMS", "menu_gems")],
             "The buttons the HUD draws. Classic: plain Xbox buttons. Gems: shaded Xbox buttons. Dark Gems: the "
             "menus' button artwork.", None),
            ("HIGH-RES TEXT", "display.high_res_text", ON_OFF,
             "Draws text and titles with sharp high-resolution fonts. Off draws the game's own.", None),
            (SECTION, "MULTIPLAYER"),
            ("PLAYER NAMES", "display.player_names",
             [("ALL", "all"), ("ALLIES", "allies"), ("ENEMIES", "enemies"), ("NONE", "none")],
             "In multiplayer, whose names are drawn above their heads.", None),
            ("NAME SIZE", "display.player_name_scale",
             [(f"{value:g}x", f"{value:g}") for value in (0.5, 0.75, 1, 1.25, 1.5, 2)],
             "How large the players' names are drawn.", None),
            ("SCOREBOARD", "display.scoreboard_team_layout", [("TEAMS", "teams"), ("BY SCORE", "score")],
             "A team game's scoreboard: a column for each team, or every player in order of score.", None),
            ("SCOREBOARD PANEL", "display.scoreboard_background", ON_OFF,
             "Draws a dark panel behind the scoreboard, for clearer text.", None),
            ("MATCH TIMER", "display.match_timer", ON_OFF,
             "Draws a clock at the bottom right in multiplayer, counting up from the start of the game.", None),
            # (the text chat's: port/linux/game/network_chat.c)
            (SECTION, "CHAT"),
            ("TEXT CHAT", "display.text_chat", [("SHOW", "true"), ("HIDE", "false")],
             "Shows the text chat. Hide draws none of its lines, and its keys and D-pad Up open nothing.", None),
            ("CHAT TEXT", "display.chat_style", [("COLORS", "colors"), ("WHITE", "white")],
             "Colors draws the names in their team's color; White draws it all white, as Custom Edition does.",
             None),
            ("STREAMER MODE ›", None, _page_screen(*STREAMER),
             "Hides offensive words in names and text from other players, maps and mods, as stars.", None),
        ],
    },
    "profile_settings": {
        "screen": "profile_settings_screen", "tab": "PROFILE",
        "about": "Your player profile: your name and your color.",
        "rows": [
            # (with no profile, NAME and COLOR are hidden and CREATE PROFILE
            # shown; in a game, those whose screens are ui.map's: the name's
            # keyboard, the colours' pictures: settings_rows_show)
            (SECTION, "PLAYER"),
            ("NAME ›", None, f"{PE}/name_edit/profile_name_screen",
             "The name the other players see, kept in your player profile.", None, "name",
             "player profile change name"),
            ("COLOR ›", None, f"{PE}/color_edit/color_select_screen",
             "Your armor's color in free-for-all multiplayer games, kept in your player profile.", None, "color"),
            CREATE_PROFILE,
            (SECTION, "ABOUT"),
            ("OPENH1", None, None,
             "Halo: Combat Evolved, the Xbox game, on this computer, played from your own copy of its maps. OpenH1 "
             "is a free fan project.", None, "about"),
        ],
    },
    # ---- the pages the rows open
    ADVANCED[0]: {
        "screen": ADVANCED[1], "title": "ADVANCED GRAPHICS",
        "rows": [
            (SECTION, "IMAGE"),
            ("RENDER RESOLUTION", "display.resolution_scaling", [("FULL", "native"), ("XBOX 640X480", "original")],
             "Full draws the game at your screen's resolution. Xbox draws it at the Xbox's 640x480 and scales it "
             "up: softer, and very light on the graphics card.", "computer"),
            ("ANTI-ALIASING", "display.anti_aliasing",
             [("OFF", "off"), ("FXAA", "fxaa"), ("SMAA", "smaa"), ("SSAA 2X", "ssaa2x"), ("MSAA 2X", "msaa2x"),
              ("MSAA 4X", "msaa4x"), ("MSAA 8X", "msaa8x")],
             ANTI_ALIASING_HELP.format("SSAA and MSAA are", "cost"), "desktop"),
            # (Android's Vulkan draws SMAA, but no supersampling)
            ("ANTI-ALIASING", "display.anti_aliasing",
             [("OFF", "off"), ("FXAA", "fxaa"), ("SMAA", "smaa"), ("MSAA 2X", "msaa2x"), ("MSAA 4X", "msaa4x")],
             ANTI_ALIASING_HELP.format("MSAA is", "costs"), "android", "anti_aliasing_android"),
            ("TEXTURE FILTERING", "display.anisotropic_filtering",
             [("OFF", "0"), ("2X", "2"), ("4X", "4"), ("8X", "8"), ("16X", "16")],
             "Keeps floors and walls sharp into the distance, at a small cost. Off looks as the Xbox did: surfaces "
             "blur a few steps away.", None),
            ("MODEL DETAIL", "display.model_detail",
             [("AUTO", "auto"), ("XBOX", "stock"), ("FULL", "native"), ("HIGHEST", "highest")],
             "How detailed characters and vehicles stay in the distance. Xbox simplifies them as on a TV; Full keeps "
             "their detail for your screen; Highest always draws the most. Auto is Full in VR.", None),
            ("SMOOTH MOTION", "display.interpolation", ON_OFF,
             "Draws movement smoothly between the game's 30 updates a second, so that it looks fluid at any frame "
             "rate. Off shows each update as it is.", None),
            (SECTION, "LIGHTING AND HDR"),
            ("SMOOTH LIGHTING", "display.per_pixel_lighting", ON_OFF,
             "Lights characters and weapons smoothly, without the flat facets of the Xbox's lighting, at a small "
             "cost.", None),
            ("DYNAMIC LIGHTS", "display.dynamic_lights", [("CLASSIC", "classic"), ("ENHANCED", "enhanced")],
             "Enhanced: plasma bolts, muzzle flashes and blasts light their surroundings smoothly, with a glow. "
             "Classic lights them as the Xbox did.", "desktop android"),
            ("SHIELD GLOW", "display.shield_glow", ON_OFF,
             "Shields light their surroundings as they flare, in the shield's own color. Online: the host's "
             "choice.", None),
            ("GLOW INTENSITY", "display.shield_glow_intensity", [("DEFAULT", "default"), ("LIGHT SHOW", "light_show")],
             "Light Show: the shields' glow reaches three times as far, and is brighter. Online: the host's choice.",
             None),
            ("HDR MODE", "display.hdr_mode", [("FAITHFUL", "faithful"), ("PUNCHY", "punchy")],
             "Faithful keeps the original picture and brightens its highlights. Punchy lifts the whole scene, as a "
             "remaster would.|The pictures are shown approximately: no SDR picture can show HDR.", "hdr"),
            ("HDR BRIGHTNESS", "display.hdr_paper_white", [(f"{value}%", str(value)) for value in PAPER_WHITES],
             "With HDR, how bright white surfaces and the HUD are, in percent of your system's white. Raise it in a "
             "bright room.", "hdr"),
            # (OpenCE's OVERSHIELD and HDR DISPLAY rows are left out: OpenH1's
            # renderers do not draw the overshield's rise, and HDR output is
            # display.hdr's own; port/linux/README.md)
            (SECTION, "GAME"),
            # (locked at ORIGINAL (30) in a game that plays at 30 whatever it
            # says, a network game of other machines, a film's playback:
            # menu_functions.c's sim_rate_row_show)
            ("TICK RATE", "game.sim_rate", [("ORIGINAL (30)", "30"), ("60", "60"), ("120", "120")],
             "How often the world updates each second, in games on this device: the campaign, split screen and "
             "bots. 60 and 120 respond faster and take more of the processor. Online games play at their host's "
             "tick rate: an OpenH1 game's (OpenH1 60 Tick), a Legacy game's 30.", None),
        ],
    },
    FIRST_PERSON[0]: {
        "screen": FIRST_PERSON[1], "title": "FIRST PERSON",
        "rows": [
            (SECTION, "VIEW"),
            ("WEAPON FIELD OF VIEW", "display.viewmodel_fov", FIELDS_OF_VIEW,
             "How wide your weapon and hands are drawn, in degrees across a 16:9 screen. Default keeps the "
             "weapon's own view.", None),
            ("WEAPON AND HANDS", "display.viewmodel_visible", [("SHOW", "true"), ("HIDE", "false")],
             "Draws your weapon, your hands and what they hold. Hide clears the view; you still fire, reload and "
             "hear them.", None),
            ("LEGS", "display.first_person_legs", ON_OFF,
             "Your legs when you look down, moving as you run: your body as the others see it.", None),
            ("PASSENGER VIEW", "display.passenger_third_person", [("BEHIND", "true"), ("SEAT", "false")],
             "Riding in a Warthog or a Scorpion in the campaign and co-op: the view from behind the vehicle, or "
             "from your seat. Behind, the weapon cannot zoom.", None),
            (SECTION, "SHIELDS"),
            ("ARM SHIELDS", "display.viewmodel_shield", ON_OFF,
             "Your shield's flare on your first-person arms too, as on your body.", None),
            ("SHIELD COLOR", "display.shield_color", [("DEFAULT", "default")] + [(c.upper(), c) for c in SHIELD_COLORS],
             "Your shield's flare in an armor color, whatever your armor. Only you see it. Default keeps its "
             "own.", None),
        ],
    },
    CONTROLLER[0]: {
        "screen": CONTROLLER[1], "title": "CONTROLLER", "profile": True,
        "rows": [
            (SECTION, "LOOK"),
            ("LOOK SENSITIVITY", "profile.look_sensitivity", [(str(step), str(step)) for step in range(1, 11)],
             "How fast the right stick turns the view.", None),
            ("INVERT LOOK", "profile.invert_look", YES_NO, "Pushing the right stick up looks down.", None),
            ("INVERT FLIGHT", "profile.flight_inversion", YES_NO,
             "Flying vehicles pitch as aircraft do: pushing up flies down.", None),
            ("AUTO-CENTER LOOK", "profile.autocenter", YES_NO, "Levels the view while you walk.", None),
            (SECTION, "LAYOUT"),
            ("BUTTON LAYOUT", "profile.button_preset",
             [("DEFAULT", "0"), ("SWAP TRIGGERS", "1"), ("SWAP A, L TRIGGER", "2"), ("SWAP B, L TRIGGER", "3"),
              ("SWAP B, R STICK", "4"), ("BUMPER JUMPER", "5"),
              ("FISHSTICKS", "6"), ("RECON", "7")],
             "Which of the controller's buttons does what. The keyboard's keys are Key Bindings'.", None),
            ("STICK LAYOUT", "profile.joystick_preset",
             [("DEFAULT", "0"), ("SOUTHPAW", "1"), ("LEGACY", "2"), ("LEGACY SOUTHPAW", "3")],
             "Which stick moves and which looks.", None),
            (SECTION, "FEEDBACK"),
            ("VIBRATION", "profile.vibration", ON_OFF, "Rumbles the controller.", None),
            ("IN-GAME HELP", "profile.ingame_help", ON_OFF, "Shows the game's hints about its controls.", None),
            (SECTION, "PROFILE"),
            CREATE_PROFILE,
        ],
    },
    SCREEN_LAYOUT[0]: {
        "screen": SCREEN_LAYOUT[1], "title": "SCREEN LAYOUT",
        "rows": [
            (SECTION, "PICTURE"),
            # (display.picture_position: Android's, on a screen taller than the
            # picture; d3d8_device.c's picture_top, the backend's present)
            ("PICTURE POSITION", "display.picture_position",
             [("CENTER", "center"), ("TOP", "top"), ("BOTTOM", "bottom")],
             "Where the picture sits on a screen taller than it, such as a folding phone's inner screen or a "
             "tablet. Top puts all the black space below it, where the touch controls are; Bottom puts it all "
             "above. Portrait always has it "
             "near the top, under the strip of buttons.", "android"),
            ("PICTURE SHAPE", "display.picture_shape", [("4:3", "4:3"), ("16:9", "16:9")],
             "The picture's shape on a screen taller than it. 4:3 is the Xbox's; 16:9 sees more to the sides and "
             "leaves more of the screen to the touch controls.", "android"),
            (SECTION, "PORTRAIT"),
            # (display.frame_style: Android's, around the picture in portrait;
            # TouchControls.java's drawFrame)
            ("PORTRAIT FRAME", "display.frame_style", [("PLAIN", "plain"), ("HALO", "halo"), ("XBOX", "xbox")],
             "The look of the screen around the picture when you play in portrait: plain black, Halo's blue lines "
             "and rings, or Xbox green. It is never drawn over the picture.", "android"),
        ],
    },
    STREAMER[0]: {
        "screen": STREAMER[1], "title": "STREAMER MODE",
        "rows": [
            # (port/linux/game/text_filter.c: words hidden in what other
            # players, maps and mods show, each group from a severity of Surge
            # AI's on, port/assets/text_filter)
            (SECTION, "STREAMER MODE"),
            ("STREAMER MODE", "streamer.enabled", ON_OFF,
             "Hides offensive words in names and text from other players, maps and mods, as stars.", None),
            (SECTION, "WORDS HIDDEN"),
            ("SLURS", "streamer.slurs", STREAMER_LEVELS,
             "Slurs about race, sexuality, gender and disability.", None),
            ("SWEARING", "streamer.swearing", STREAMER_LEVELS,
             "Swearing and sexual words. Strong and Worse leaves the mildest showing.", None),
            ("INSULTS", "streamer.insults", STREAMER_LEVELS,
             "Name-calling and insults that are not slurs.", None),
        ],
    },
}
SETTINGS_TAB_PAGES = [folder for folder, page in PAGES.items() if "tab" in page]
# the panel's words for the buttons, on a tab and on a page the rows open
SETTINGS_BUTTON_HELP = {
    "tab": ["Puts this tab's settings back to their defaults. Nothing changes until OK.",
            "Keeps your changes, on every tab, and applies them.",
            "Leaves Settings without keeping any change."],
    "page": ["Puts this page's settings back to their defaults.",
             "Saves your changes, on this page and every tab, and applies them.",
             "Goes back without these changes."],
}
# the pages' titles (TITLES): Settings', over every tab, and each page's
SETTINGS_TITLES = {SETTINGS_TITLE: "SETTINGS"}
SETTINGS_TITLES.update({f"{PE}/{folder}/header_{page['screen'][:-len('_screen')]}": page["title"]
                        for folder, page in PAGES.items() if "title" in page})
SETTINGS_TITLES[f"{PE}/controls_setup/header_key_bindings"] = "KEY BINDINGS"

# Controls Setup: the keyboard and mouse's actions, in groups (the order of
# port/linux/game/menu_functions.c's table of them)
CONTROL_GROUPS = ["MOVEMENT", "WEAPONS", "ACTIONS", "COMMUNICATION"]
CONTROL_ROWS = 7

# the settings whose rows have the wider spinner (a device's name)
WIDE_SETTINGS = {"audio.output_device", "audio.input_device", "streamer.slurs", "streamer.swearing",
                 "streamer.insults"}

# Teamplay Options' rows of the port's (_teamplay_options_extras): the
# gametype's FORCE TEAM SPAWNS, then the host's voice chat and vote kicks;
# each row's key, its label, its choices' words (menu_functions.c's
# gametype_options give their values) and their helps
TEAMPLAY_EDIT = "main_menu/settings_select/multiplayer_setup/teamplay_options_edit"
VOICE_QUALITIES = (8, 12, 16, 24, 32, 48, 64)
# (the rows' step: closer than the PC's 30px, so that all of Server Setup's
# fit above the help text, which moves down to TEAMPLAY_HELP_TOP)
TEAMPLAY_STEP = 26
TEAMPLAY_HELP_TOP = 351
# (game_engine.c's game_engine_force_team_spawns: the gametype's own, so
# shown in Edit Gametypes' copy too, and not on Server Setup's VOICE OPTIONS)
TEAMPLAY_GAMETYPE_ROWS = [
    ("force_team_spawns", "FORCE TEAM SPAWNS:", ["OFF", "ON"], [
        "Players will spawn at any spawn point the\\ngametype allows.",
        "Players will only spawn at their own team's\\nCapture the Flag spawn points.",
    ]),
]
TEAMPLAY_ROWS = [
    ("voice_mode", "VOICE CHAT:", ["OFF", "TEAM NEAR", "ANYONE NEAR", "TEAM", "TEAM, ENEMIES NEAR"], [
        "No voice chat during the game.",
        "Players hear their teammates who are near them.",
        "Players hear everyone who is near them.",
        "Players hear all their teammates, wherever they\\nare.",
        "Players hear all their teammates, and the enemies\\nwho are near them.",
    ]),
    ("voice_lobby", "LOBBY VOICE CHAT:", ["ON", "OFF"], [
        "Everyone hears everyone in the lobby, before and\\nafter the game.",
        "No voice chat in the lobby.",
    ]),
    ("voice_kbps", "VOICE QUALITY:", [f"{kbps} KBPS" for kbps in VOICE_QUALITIES],
     [f"Voices at {kbps} kilobits a second, in the lobby\\nand in the game." for kbps in VOICE_QUALITIES]),
    ("voice_proximity", "VOICE NEAR DISTANCE:", ["15 M", "30 M", "45 M", "60 M", "90 M", "150 M"],
     [f"Players {metres} meters apart or less are near\\nfor voice chat." for metres in (15, 30, 45, 60, 90, 150)]),
    # (network_votekick.c)
    ("votekick", "VOTE KICK:", ["ON", "OFF"], [
        "Players can vote to kick a player, from the\\nscoreboard.",
        "Players cannot vote to kick anyone.",
    ]),
    # (network_chat.c: one row for the chat and its links, as Teamplay
    # Options has room for one more)
    ("text_chat", "TEXT CHAT:", ["ON", "WITH LINKS", "OFF"], [
        "Players can type to each other; lines with links\\nare refused. Off in a public internet game.",
        "Players can type to each other, links too. Off\\nin a public internet game.",
        "No text chat in the game.",
    ]),
]
# (Teamplay Options' rows of the port's, in order)
TEAMPLAY_EXTRA_ROWS = TEAMPLAY_GAMETYPE_ROWS + TEAMPLAY_ROWS

# the profile menu's words for what its items now open
STRING_OVERRIDES = {
    f"{PE}/profile_edit_descriptions": [
        "Rename this profile.\\n\\n\\nProfile:",
        "Choose the keys and mouse\\nbuttons for each action.\\n\\nProfile:",
        "Set this profile's controller\\nsensitivity and layouts.\\n\\nProfile:",
        "Adjust the mouse's sensitivity\\nand aiming.\\n\\nProfile:",
        "Adjust the volume of the music\\nand of everything else.\\n\\nProfile:",
        "Choose a window or the full\\nscreen, the frame rate and more.\\n\\nProfile:",
        "Internet play, updates and the\\nmultiplayer HUD.\\n\\nProfile:",
        "Change the current profile's\\nfree-for-all multiplayer color.\\n\\nProfile:",
        "Halo: Combat Evolved, the Xbox\\ngame, on this computer.\\n\\nProfile:",
        # (VR Setup's, in VR only)
        "Your hands, aiming, turning,\\nthe HUD and the menus in VR.\\n\\nProfile:",
    ],
}


def attributes(pairs: list) -> str:
    return "".join(f" {key}={quoteattr(str(value))}" for key, value in pairs if value is not None)


def _widget(name: str, pairs: list, inner: list) -> list:
    return [f"\t<widget{attributes([('name', name)] + pairs)}>", *[f"\t\t{line}" for line in inner], "\t</widget>"]


def _strings(name: str, texts: list) -> list:
    return [f"\t<strings{attributes([('name', name)])}>",
            *[f"\t\t<string{attributes([('text', text)])}/>" for text in texts], "\t</strings>"]


def _button(name: str, caption: int, handlers: list, wide_layout=None) -> list:
    pairs = [("type", "text"), ("width", 128), ("height", 24), ("bitmap", "bitmaps/text_button_background"),
             ("string_list", "strings/common_button_captions"), ("string_index", caption), ("font", "ui\\small_ui"),
             ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2), ("wide", wide_layout)]
    return _widget(name, pairs, handlers + ['<on event="left_mouse" run="mouse emit accept event"/>'])


def _screen(folder: str, spec: dict, rows: list, list_inputs: list, list_handlers: list, extra: list) -> list:
    """a screen: its header, the list of rows (with the help line as its
    description), and the buttons"""
    base = folder if folder.startswith("main_menu/") else f"{PE}/{folder}"
    header, header_bitmap = spec["header"]
    lines = []
    lines += _widget(f"{base}/{spec['screen']}",
                     [("width", 640), ("height", 480), ("flags", "pass_unhandled_to_focused_child pause_game"),
                      ("bitmap", "bitmaps/gradient")],
                     ['<on event="b" back="true"/>', '<on event="back" back="true"/>',
                      f'<child{attributes([("widget", f"{base}/{header}")])}/>',
                      f'<child{attributes([("widget", f"{base}/options_menu")])}/>'])
    lines += _widget(f"{base}/{header}", [("controller", 1), ("left", 35), ("top", 11), ("width", 605), ("height", 59),
                                          ("bitmap", header_bitmap)], [])
    lines += _widget(f"{base}/help", [("type", "text"), ("controller", 1), ("left", 68),
                                      ("top", spec.get("help_top", 350)), ("width", 482),
                                      ("height", 60), ("string_list", f"{base}/help_strings"),
                                      ("font", "ui\\large_ui"), ("color", "#FFFFFFFF")], [])
    children = [f'<data input="{name}"/>' for name in list_inputs] + list_handlers
    # (each row: its widget, platform, and the place it is in, else the next)
    for index, (row, platform, *place) in enumerate(rows):
        y = 73 + (place[0] if place else index) * spec["spacing"]
        children.append(f'<child{attributes([("widget", row), ("x", 54), ("y", y), ("platform", platform)])}/>')
    children.append(f'<child{attributes([("widget", f"{base}/button_bar"), ("y", 414)])}/>')
    lines += _widget(f"{base}/options_menu",
                     [("type", "column_list"), ("width", 640), ("height", 480),
                      ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                      ("description", f"{base}/help")], children)
    lines += _widget(f"{base}/button_bar",
                     [("type", "column_list"), ("width", 640), ("height", 28),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                     [f'<child{attributes([("widget", f"{base}/button_defaults"), ("y", 1)])}/>',
                      f'<child{attributes([("widget", f"{base}/button_ok"), ("x", 380), ("y", 1)])}/>',
                      '<child widget="common_button_cancel" x="510" y="1"/>'])
    lines += extra
    return lines


def _placed(row: str, platform, members: list, places: dict) -> list:
    """a row's children (row, platform, place): one for its platforms where
    they have it in the same place, else one for each place, with the
    platforms whose place it is"""
    groups = {}
    for name in members:
        groups.setdefault(places[name], []).append(name)
    if len(groups) == 1:
        return [(row, platform, places[members[0]])]
    return [(row, " ".join(names) if len(names) < len(places) else None, place) for place, names in groups.items()]


def _setting_screen(folder: str, spec: dict) -> list:
    base = f"{PE}/{folder}"
    rows, extra = [], []
    place = -1
    # (platform_places: each platform's rows in places of their own, with
    # no gap where the others' are; a row in one place for all its
    # platforms, else a child for each place)
    places = {"desktop": -1, "android": -1}
    for index, (label, setting, choices, _, platform, *named) in enumerate(spec["rows"]):
        if setting is None:
            # (a row that opens another screen, choices: its widget)
            key = choices.rsplit("/", 1)[1]
            row = f"{base}/op_{key}"
            place += 1
            rows.append((row, platform, place))
            extra += _widget(row, [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                                   ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF"), ("platform", platform)],
                             [f'<on event="a" run="port settings save" open="{choices}"/>',
                              f'<on event="start" run="port settings save" open="{choices}"/>',
                              '<on event="left_mouse" run="mouse emit accept event"/>',
                              f'<child{attributes([("widget", f"{base}/{key}_label")])}/>'])
            extra += _widget(f"{base}/{key}_label",
                             [("type", "text"), ("controller", 1), ("width", 480), ("height", 22),
                              ("string_list", f"{base}/labels"), ("string_index", index), ("font", "ui\\large_ui"),
                              ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
            continue
        key = named[0] if named else setting.split(".", 1)[1]
        row = f"{base}/op_{key}"
        shares = setting in spec.get("same_place", ()) or key in spec.get("same_place", ())
        if spec.get("platform_places"):
            members = platform.split() if platform else list(places)
            for name in members:
                if not shares:
                    places[name] += 1
            rows += _placed(row, platform, members, places)
        else:
            if setting not in spec.get("same_place", ()) and key not in spec.get("same_place", ()):
                place += 1
            rows.append((row, platform, place))
        # (a device's name wants the wider spinner Server Setup's co-op rows
        # have: WIDE_SETTINGS)
        wide = setting in WIDE_SETTINGS
        extra += _widget(row, [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                               ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF"), ("platform", platform)],
                         [f'<child{attributes([("widget", f"{base}/{key}_label")])}/>',
                          f'<child{attributes([("widget", f"{base}/{key}_spinner"), ("x", 286 if wide else 320), ("y", 1)])}/>'])
        extra += _widget(f"{base}/{key}_label",
                         [("type", "text"), ("controller", 1), ("width", 300), ("height", 22),
                          ("string_list", f"{base}/labels"), ("string_index", index), ("font", "ui\\large_ui"),
                          ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
        extra += _widget(f"{base}/{key}_spinner",
                         [("type", "spinner"), ("left", None if wide else 3), ("top", 2), ("width", 206 if wide else 147),
                          ("height", 20), ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                          ("strings", "|".join(shown for shown, _ in choices)), ("setting", setting),
                          ("values", "|".join(value for _, value in choices)), ("font", "ui\\large_ui"),
                          ("color", "#FF2896FF"), ("align", "center"), ("text_y", 4 if wide else 1),
                          ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                          ("header_bounds", "7 -13 19 -7" if wide else "7 -6 19 0"),
                          ("footer_bounds", "7 208 19 214" if wide else "7 150 19 156")],
                         ['<on event="created" run="port setting load"/>'])
    # (each category's row: its label, the screen it opens, its help, and the
    # platforms it is for, as a row's: every one's when it names none)
    for index, (label, category_folder, _, *category_platform) in enumerate(spec.get("categories", ())):
        key = category_folder.rsplit("/", 1)[-1]
        row = f"{base}/op_{key}"
        target = f"{PE}/{category_folder}/{SCREENS[category_folder]['screen']}"
        platform = category_platform[0] if category_platform else None
        if spec.get("platform_places"):
            members = platform.split() if platform else list(places)
            for name in members:
                places[name] += 1
            rows += _placed(row, platform, members, places)
        else:
            rows.append((row, platform, place + 1 + index))
        extra += _widget(row, [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                               ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF"), ("platform", platform)],
                         [f'<on event="a" open="{target}"/>', f'<on event="start" open="{target}"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          f'<child widget="{base}/{key}_label"/>'])
        extra += _widget(f"{base}/{key}_label",
                         [("type", "text"), ("controller", 1), ("width", 512), ("height", 22),
                          ("string_list", f"{base}/labels"), ("string_index", len(spec["rows"]) + index),
                          ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    extra += _button(f"{base}/button_defaults", 3, ['<on event="a" run="port settings defaults"/>',
                                                    '<on event="start" run="port settings defaults"/>'])
    extra += _button(f"{base}/button_ok", 1, ['<on event="a" run="port settings save" back="true"/>',
                                              '<on event="start" run="port settings save" back="true"/>'])
    extra += _strings(f"{base}/labels", [label for label, *_ in spec["rows"]] +
                      [label for label, *_ in spec.get("categories", ())])
    # (the help of the row whose label is string n is n + 1: the buttons' is 0)
    extra += _strings(f"{base}/help_strings",
                      [""] + [help_text.replace("\n", "\\n") for _, _, _, help_text, *_ in spec["rows"]] +
                      [help_text.replace("\n", "\\n") for _, _, help_text, *_ in spec.get("categories", ())])
    return _screen(folder, spec, rows, ["port settings help"], [], extra)


def _controls_screen() -> list:
    folder = "controls_setup"
    base = f"{PE}/{folder}"
    spec = {"screen": "controls_settings_screen", "spacing": 30,
            "header": ("header_key_bindings", f"{base}/header_key_bindings")}
    rows, extra = [(f"{base}/op_group", None)], []
    extra += _widget(f"{base}/op_group", [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                                          ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                     [f'<child{attributes([("widget", f"{base}/group_label")])}/>',
                      f'<child{attributes([("widget", f"{base}/group_spinner"), ("x", 200), ("y", 1)])}/>'])
    extra += _widget(f"{base}/group_label",
                     [("type", "text"), ("controller", 1), ("width", 200), ("height", 22), ("text", "SHOW:"),
                      ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    extra += _widget(f"{base}/group_spinner",
                     [("type", "spinner"), ("left", 3), ("top", 2), ("width", 297), ("height", 20),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                      ("strings", "|".join(CONTROL_GROUPS)), ("font", "ui\\large_ui"), ("color", "#FF2896FF"),
                      ("align", "center"), ("text_y", 1), ("header_bitmap", "bitmaps/arrow_sm_left"),
                      ("footer_bitmap", "bitmaps/arrow_sm_right"), ("header_bounds", "7 -6 19 0"),
                      ("footer_bounds", "7 300 19 306")], [])
    for index in range(CONTROL_ROWS):
        row = f"{base}/op_command_{index + 1}"
        rows.append((row, None))
        extra += _widget(row, [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                               ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         ['<on event="a" run="controls begin binding"/>',
                          '<on event="start" run="controls begin binding"/>',
                          '<on event="left right" run="controls binding slot"/>',
                          f'<child{attributes([("widget", f"{base}/command_label")])}/>',
                          f'<child{attributes([("widget", f"{base}/command_binding"), ("x", 200)])}/>',
                          f'<child{attributes([("widget", f"{base}/command_binding"), ("x", 356)])}/>'])
    extra += _widget(f"{base}/command_label",
                     [("type", "text"), ("controller", 1), ("width", 200), ("height", 22), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    extra += _widget(f"{base}/command_binding",
                     [("type", "text"), ("controller", 1), ("width", 152), ("height", 22), ("font", "ui\\small_ui"),
                      ("color", "#FF2896FF"), ("align", "center"), ("text_y", 5), ("text_flags", "no_focus_test")],
                     [])
    extra += _button(f"{base}/button_defaults", 3, ['<on event="a" run="controls screen defaults"/>',
                                                    '<on event="start" run="controls screen defaults"/>'])
    extra += _button(f"{base}/button_ok", 1, ['<on event="a" run="controls screen change set" back="true"/>',
                                              '<on event="start" run="controls screen change set" back="true"/>'])
    extra += _strings(f"{base}/help_strings", [
        "Choose what to show, with left and right.",
        "Enter sets the key chosen; left and right choose\\nwhich of two. Then press the new key or button.",
        "Press a key, a mouse button or turn the wheel...\\nEscape cancels, Delete clears it.",
    ])
    return _screen(folder, spec, rows, ["controls update menu"], ['<on event="created" run="controls screen init"/>'],
                   extra)


def _vr_item() -> list:
    """the profile menu's VR Setup item, below About (as its items are)"""
    return _widget(VR_ITEM, [("type", "text"), ("left", 51), ("top", 375), ("width", 232), ("height", 32),
                             ("bitmap", "bitmaps/list_item_bkd"), ("text", "VR SETUP"), ("font", "ui\\large_ui"),
                             ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 5)],
                   [f'<on event="a" open="{PE}/vr_controls/vr_controls_screen"/>',
                    f'<on event="start" open="{PE}/vr_controls/vr_controls_screen"/>',
                    '<on event="left_mouse" run="mouse emit accept event"/>'])


def _settings_text(name: str, pairs: list, inner: list = ()) -> list:
    """a text widget of Settings' (the small font, unless pairs say)"""
    pairs = [("type", "text"), ("controller", 1)] + pairs
    if not any(key == "font" for key, _ in pairs):
        pairs.append(("font", "ui\\small_ui"))
    return _widget(name, pairs, list(inner))


def _settings_platform(rows: list, start: int):
    """a section's platforms: those of its rows (None: every platform)"""
    platforms = set()
    for entry in rows[start + 1:]:
        if entry[0] == SECTION:
            break
        if entry[4] is None:
            return None
        platforms.update(entry[4].split())
    return " ".join(name for name in MENU_PLATFORMS if name in platforms) or None


# the rows in the place of the row before them, each platform having one of
# them at most, or menu_functions.c showing one (video_rows_show): Window Size
# in Resolution's, Android's anti-aliasing in the desktop's
SETTINGS_SAME_PLACE = {"window_size", "anti_aliasing_android"}


def _settings_page(folder: str, page: dict) -> list:
    """a tab of Settings, or a page a row opens: the title, the tabs (a
    tab's), the rows under their sections, the panel (the list's
    description), and Defaults, OK and Cancel"""
    base = f"{PE}/{folder}"
    tab = "tab" in page
    kind = "tab" if tab else "page"
    y = SETTINGS_TOP if tab else PAGE_TOP
    # (a tab's title is Settings'; a page's, its own: its widget named as
    # its picture)
    header = SETTINGS_TITLE if tab else f"{base}/header_{page['screen'][:-len('_screen')]}"
    cancel = "port settings cancel" if tab else "port settings page cancel"
    labels, helps, children, extra = [], [page.get("about", "")], [], []
    for index, entry in enumerate(page["rows"]):
        if entry[0] == SECTION:
            # (a section's title over its line, which the list passes over:
            # menu_functions.c's settings_rows_pack disables it)
            platform = _settings_platform(page["rows"], index)
            name = f"{base}/section_{index}"
            if labels:
                y += SETTINGS_SECTION_GAP
            children.append(f'<child{attributes([("widget", name), ("x", SETTINGS_ROW_X), ("y", y), ("platform", platform)])}/>')
            extra += _widget(name, [("width", SETTINGS_ROW_WIDTH), ("height", SETTINGS_SECTION_HEIGHT),
                                    ("bitmap", f"{SETTINGS}/section"), ("platform", platform),
                                    ("wide", wide(-WIDE_HALF, SETTINGS_WIDE_ROWS))],
                             [f'<child widget="{name}_title"/>'])
            extra += _settings_text(f"{name}_title", [("width", SETTINGS_ROW_WIDTH), ("height", SETTINGS_SECTION_HEIGHT),
                                                      ("wide", wide(0, SETTINGS_WIDE_ROWS)),
                                                      ("string_list", f"{base}/labels"),
                                                      ("string_index", len(labels) or None), ("color", SETTINGS_DIM),
                                                      ("text_x", 2), ("text_y", 1)])
            labels.append(entry[1])
            helps.append("")
            y += SETTINGS_SECTION_STEP
            continue
        label, setting, choices, help_text, platform, *named = entry
        # (a link's by its label: "ADVANCED GRAPHICS ›", advanced_graphics)
        key = named[0] if named else setting.split(".", 1)[1] if setting else \
            re.sub(r"[^a-z0-9]+", "_", label.lower()).strip("_")
        row = f"{base}/op_{key}"
        if key in SETTINGS_SAME_PLACE or setting == "display.window_size":
            y -= SETTINGS_ROW_STEP
        children.append(f'<child{attributes([("widget", row), ("x", SETTINGS_ROW_X), ("y", y), ("platform", platform)])}/>')
        y += SETTINGS_ROW_STEP
        label_widget = _settings_text(f"{base}/{key}_label",
                                      [("width", SETTINGS_SPINNER_X - 4 if setting else SETTINGS_ROW_WIDTH - 12),
                                       ("height", SETTINGS_ROW_HEIGHT), ("string_list", f"{base}/labels"),
                                       ("string_index", len(labels) or None), ("color", SETTINGS_BLUE),
                                       ("text_x", 10), ("text_y", 2), ("wide", wide(0, SETTINGS_WIDE_ROWS))])
        row_pairs = [("width", SETTINGS_ROW_WIDTH), ("height", SETTINGS_ROW_HEIGHT),
                     ("flags", "pass_unhandled_to_focused_child"), ("bitmap", f"{SETTINGS}/row"),
                     ("color", SETTINGS_BLUE), ("platform", platform),
                     ("wide", wide(-WIDE_HALF, SETTINGS_WIDE_ROWS))]
        if setting and isinstance(choices, Slider):
            # (a slider: its track, the thumb on it, and its number, which
            # menu_functions.c's "port slider ..." set and its page update
            # places and shows; while its number is typed into, Enter is
            # Start and Escape B)
            run = "port slider"
            extra += _widget(row, row_pairs,
                             [f'<on event="left" run="{run} left"/>', f'<on event="right" run="{run} right"/>',
                              f'<on event="a" run="{run} edit"/>', f'<on event="start" run="{run} edit"/>',
                              '<on event="left_mouse" run="mouse emit accept event"/>',
                              f'<child widget="{base}/{key}_label"/>',
                              f'<child widget="{base}/{key}_track" x="{SETTINGS_SPINNER_X}" '
                              f'y="{(SETTINGS_ROW_HEIGHT - SLIDER_TRACK_HEIGHT) // 2}"/>',
                              f'<child widget="{base}/{key}_value" x="{SETTINGS_SPINNER_X + SLIDER_VALUE_X}" '
                              f'y="{(SETTINGS_ROW_HEIGHT - SLIDER_VALUE_HEIGHT) // 2}"/>'])
            extra += label_widget
            extra += _widget(f"{base}/{key}_track", [("controller", 1), ("width", SLIDER_TRACK_WIDTH),
                                                     ("height", SLIDER_TRACK_HEIGHT),
                                                     ("bitmap", f"{SETTINGS}/slider_track"),
                                                     ("wide", wide(SETTINGS_WIDE_ROWS))],
                             [f'<child widget="{base}/{key}_thumb"/>'])
            extra += _widget(f"{base}/{key}_thumb", [("controller", 1), ("width", SLIDER_THUMB_WIDTH),
                                                     ("height", SLIDER_TRACK_HEIGHT),
                                                     ("bitmap", f"{SETTINGS}/slider_thumb")], [])
            # (its number in a field's frame, lit while typed into:
            # menu_functions.c's settings_sliders_show)
            extra += _settings_text(f"{base}/{key}_value", [("width", SLIDER_VALUE_WIDTH),
                                                            ("height", SLIDER_VALUE_HEIGHT),
                                                            ("bitmap", f"{SETTINGS}/slider_field"),
                                                            ("color", SETTINGS_BLUE), ("align", "center"),
                                                            ("text_y", 0),
                                                            ("text_flags", "no_focus_test"),
                                                            ("wide", wide(SETTINGS_WIDE_ROWS))])
        elif setting:
            extra += _widget(row, row_pairs, [f'<child widget="{base}/{key}_label"/>',
                                              f'<child widget="{base}/{key}_spinner" x="{SETTINGS_SPINNER_X}"/>'])
            extra += label_widget
            extra += _widget(f"{base}/{key}_spinner",
                             [("type", "spinner"), ("top", 2), ("width", SETTINGS_SPINNER_WIDTH), ("height", 16),
                              ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                              ("strings", "|".join(shown for shown, _ in choices)), ("setting", setting),
                              ("values", "|".join(value for _, value in choices)), ("font", "ui\\small_ui"),
                              ("color", SETTINGS_BLUE), ("align", "center"), ("text_y", 0),
                              ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                              ("header_bounds", "2 -6 14 0"),
                              ("footer_bounds", f"2 {SETTINGS_SPINNER_WIDTH} 14 {SETTINGS_SPINNER_WIDTH + 6}"),
                              ("wide", wide(SETTINGS_WIDE_ROWS))],
                             ['<on event="created" run="port setting load"/>'])
        else:
            # (a row that opens a page, choices: its screen; run: a function
            # first; none: nothing to open)
            run = named[1] if len(named) > 1 else None
            handlers = []
            if choices or run:
                branch = "true" if run == "port settings new profile" else None
                handlers = [f'<on{attributes([("event", event), ("run", run), ("open", choices), ("branch", branch)])}/>'
                            for event in ("a", "start")]
                handlers.append('<on event="left_mouse" run="mouse emit accept event"/>')
                if branch:
                    handlers.append(f'<conditional widget="{SETTINGS}/settings_creating_profile" if_failed="true"/>')
            extra += _widget(row, row_pairs, handlers + [f'<child widget="{base}/{key}_label"/>'])
            extra += label_widget
        labels.append(label)
        helps.append(help_text)
    lines = []
    lines += _widget(f"{base}/{page['screen']}",
                     [("width", 640), ("height", 480), ("flags", "pass_unhandled_to_focused_child pause_game"),
                      ("bitmap", "bitmaps/gradient")],
                     [f'<on event="b" run="{cancel}" back="true"/>',
                      f'<on event="back" run="{cancel}" back="true"/>',
                      *(['<on event="white" run="port settings tab previous"/>',
                         '<on event="black" run="port settings tab next"/>'] if tab else []),
                      f'<child widget="{header}"/>',
                      *([f'<child widget="{SETTINGS}/tab_prompt_previous" x="{WIDE_MARGIN}" '
                         f'y="{SETTINGS_TABS_TOP}"/>',
                         f'<child widget="{SETTINGS}/tab_prompt_next" '
                         f'x="{SETTINGS_TABS_X + 5 * SETTINGS_TAB_STEP + SETTINGS_TAB_LAID + 4}" '
                         f'y="{SETTINGS_TABS_TOP}"/>'] if tab else []),
                      f'<child widget="{base}/options_menu"/>'])
    if not tab:
        lines += _widget(header, [("controller", 1), ("left", 35), ("top", 11), ("width", 605), ("height", 59),
                                  ("bitmap", header), ("wide", WIDE_LEFT)], [])
    # (the tabs last: up from the first row, down from the buttons)
    lines += _widget(f"{base}/options_menu",
                     [("type", "column_list"), ("width", 640), ("height", 480),
                      ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                      ("description", f"{SETTINGS}/{kind}_panel")],
                     [f'<data input="{SETTINGS_FUNCTIONS}"/>', *children,
                      f'<child widget="{base}/button_bar" y="414"/>',
                      *([f'<child widget="{base}/tabs" y="{SETTINGS_TABS_TOP}"/>'] if tab else [])])
    lines += _widget(f"{base}/button_bar",
                     [("type", "column_list"), ("width", 640), ("height", 28),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                     [f'<child widget="{base}/button_defaults" x="{WIDE_MARGIN}" y="1"/>',
                      f'<child widget="{base}/button_ok" x="{640 - WIDE_MARGIN - 2 * 128 - 2}" y="1"/>',
                      f'<child widget="{base}/button_cancel" x="{640 - WIDE_MARGIN - 128}" y="1"/>'])
    if tab:
        # (each tab: its name, the current one framed; A or a click opens it)
        items = []
        for index, other in enumerate(SETTINGS_TAB_PAGES):
            current = other == folder
            name = f"{base}/tab_{PAGES[other]['screen'][:-len('_screen')]}"
            items.append(f'<child widget="{name}" x="{SETTINGS_TABS_X + index * SETTINGS_TAB_STEP}"/>')
            lines += _settings_text(name, [("width", SETTINGS_TAB_LAID), ("height", SETTINGS_TAB_HEIGHT),
                                           ("flags", "keep_ends"),
                                           ("wide", wide(wide_spread(index, len(SETTINGS_TAB_PAGES)))),
                                           ("bitmap", f"{SETTINGS}/{'tab_current' if current else 'tab'}"),
                                           ("string_list", f"{SETTINGS}/tab_names"), ("string_index", index or None),
                                           ("color", "#FFFFFFFF" if current else SETTINGS_BLUE),
                                           ("align", "center"), ("text_y", 3)],
                                    ['<on event="a" run="port settings tab"/>',
                                     '<on event="start" run="port settings tab"/>',
                                     '<on event="left_mouse" run="mouse emit accept event"/>'])
        lines += _widget(f"{base}/tabs", [("type", "column_list"), ("width", 640), ("height", SETTINGS_TAB_HEIGHT),
                                          ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")], items)
    lines += _button(f"{base}/button_defaults", 3, ['<on event="a" run="port settings defaults"/>',
                                                     '<on event="start" run="port settings defaults"/>'], WIDE_LEFT)
    ok = "port settings ok" if tab else "port settings keep"
    lines += _button(f"{base}/button_ok", 1, [f'<on event="a" run="{ok}" back="true"/>',
                                               f'<on event="start" run="{ok}" back="true"/>'], WIDE_RIGHT)
    lines += _button(f"{base}/button_cancel", 2, [f'<on event="a" run="{cancel}" back="true"/>',
                                                   f'<on event="start" run="{cancel}" back="true"/>'], WIDE_RIGHT)
    lines += extra
    lines += _strings(f"{base}/labels", labels)
    lines += _strings(f"{base}/help_strings", helps)
    return lines


def _settings_shared() -> list:
    """what every page of Settings shares: its title, the panels, the tabs'
    names, the buttons' words, and the screens that make a profile"""
    lines = _widget(SETTINGS_TITLE, [("controller", 1), ("left", 35), ("top", 11), ("width", 605), ("height", 59),
                                     ("bitmap", SETTINGS_TITLE), ("wide", WIDE_LEFT)], [])
    for kind, top in (("tab", SETTINGS_TOP), ("page", PAGE_TOP)):
        # (menu_functions.c's settings_panel_show fills it in, and shows the
        # Quality row's pictures)
        height = SETTINGS_PANEL_BOTTOM - top
        # (a description's children are placed on the screen, not in it)
        left, still_y = SETTINGS_PANEL_X + 14, top + 52
        children = [f'<child widget="{SETTINGS}/panel_name" x="{left}" y="{top + 12}"/>',
                    f'<child widget="{SETTINGS}/panel_value" x="{left}" y="{top + 26}"/>']
        for index, still in enumerate(("classic", "enhanced")):
            x = SETTINGS_PANEL_X + 12 + index * (SETTINGS_STILL_WIDTH + 8)
            children += [f'<child widget="{SETTINGS}/panel_{still}" x="{x}" y="{still_y}"/>',
                         f'<child widget="{SETTINGS}/panel_{still}_frame" x="{x}" y="{still_y}"/>',
                         f'<child widget="{SETTINGS}/panel_{still}_caption" x="{x}" y="{still_y + SETTINGS_STILL_HEIGHT + 2}"/>']
        children += [f'<child widget="{SETTINGS}/panel_picture_{key}" '
                     f'x="{SETTINGS_PANEL_X + (SETTINGS_PANEL_WIDTH - SETTINGS_PICTURE_WIDTH) // 2}" y="{still_y}"/>'
                     for key in SETTINGS_VALUE_PICTURES]
        children += [f'<child widget="{SETTINGS}/panel_text" x="{left}" y="{top + 52}"/>',
                     f'<child widget="{SETTINGS}/panel_note" x="{left}" y="{top + height - 40}"/>']
        lines += _widget(f"{SETTINGS}/{kind}_panel",
                         [("controller", 1), ("left", SETTINGS_PANEL_X), ("top", top),
                          ("width", SETTINGS_PANEL_WIDTH), ("height", height),
                          ("bitmap", f"{SETTINGS}/{kind}_panel"), ("wide", wide(*SETTINGS_WIDE_PANEL))], children)
    # (in a wider panel: its words wider, its pictures in its middle)
    grown, middle = wide(0, SETTINGS_WIDE_PANEL[1]), wide(SETTINGS_WIDE_PANEL[1] / 2)
    lines += _settings_text(f"{SETTINGS}/panel_name", [("width", 196), ("height", 18), ("color", SETTINGS_DIM),
                                                      ("wide", grown)])
    lines += _settings_text(f"{SETTINGS}/panel_value", [("width", 196), ("height", 24), ("font", "ui\\large_ui"),
                                                       ("color", "#FFFFFFFF"), ("wide", grown)])
    lines += _settings_text(f"{SETTINGS}/panel_text", [("width", 196), ("height", 220), ("color", SETTINGS_BLUE),
                                                      ("wide", grown)])
    lines += _settings_text(f"{SETTINGS}/panel_note", [("width", 196), ("height", 40), ("color", SETTINGS_DIM),
                                                      ("wide", grown)])
    for index, still in enumerate(("classic", "enhanced")):
        lines += _widget(f"{SETTINGS}/panel_{still}", [("controller", 1), ("width", SETTINGS_STILL_WIDTH),
                                                       ("height", SETTINGS_STILL_HEIGHT),
                                                       ("bitmap", f"{SETTINGS}/graphics_{still}"), ("wide", middle)],
                         [])
        lines += _widget(f"{SETTINGS}/panel_{still}_frame", [("controller", 1), ("width", SETTINGS_STILL_WIDTH),
                                                             ("height", SETTINGS_STILL_HEIGHT),
                                                             ("bitmap", f"{SETTINGS}/still_frame"), ("wide", middle)],
                         [])
        lines += _settings_text(f"{SETTINGS}/panel_{still}_caption",
                                [("width", SETTINGS_STILL_WIDTH), ("height", 18),
                                 ("string_list", f"{SETTINGS}/still_captions"), ("string_index", index or None),
                                 ("color", SETTINGS_BLUE), ("align", "center"), ("wide", middle)])
    for key in SETTINGS_VALUE_PICTURES:
        lines += _widget(f"{SETTINGS}/panel_picture_{key}",
                         [("controller", 1), ("width", SETTINGS_PICTURE_WIDTH), ("height", SETTINGS_PICTURE_HEIGHT),
                          ("bitmap", f"{SETTINGS}/pictures_{key}"), ("wide", middle)], [])
    # (making a profile, from CREATE PROFILE: its name asked, as the main
    # menu's items ask it ("new campaign chosen", settings_profile_new), the
    # profile made and edited ("new campaign decision", which edits it for
    # a screen of this name), then back to the page, which shows its rows)
    lines += _widget(f"{SETTINGS}/settings_creating_profile",
                     [("width", 640), ("height", 480), ("flags", "pass_unhandled_to_focused_child no_history"),
                      ("auto_close", 1)],
                     [f'<on event="deleted" run="new campaign decision" open="{SETTINGS}/settings_saving_profile"/>'])
    lines += _widget(f"{SETTINGS}/settings_saving_profile",
                     [("width", 640), ("height", 480), ("flags", "pass_unhandled_to_focused_child no_history"),
                      ("auto_close", 1000), ("auto_close_fade", 500)],
                     ['<on event="b" run="NULL"/>', '<on event="back" run="NULL"/>',
                      '<on event="deleted" run="NULL" back="true"/>',
                      '<child widget="error/warning_creating_player_profile_screen"/>'])
    # (LB and RB, or Q and E while the keyboard and mouse play, either side
    # of the tabs: the HUD's button pictures, display.hud_button_style, or
    # its key caps, ui_widget.c and hud_key_prompts.c)
    for name, icon in (("previous", "white"), ("next", "black")):
        lines += _settings_text(f"{SETTINGS}/tab_prompt_{name}", [("width", SETTINGS_TAB_PROMPT_WIDTH),
                                                                 ("height", SETTINGS_TAB_HEIGHT),
                                                                 ("wide", WIDE_RIGHT if name == "next" else WIDE_LEFT),
                                                                 ("text", f"%{icon}-button"), ("color", SETTINGS_BLUE), ("text_x", 2),
                                                                 ("text_y", 3)])
    lines += _strings(f"{SETTINGS}/tab_names", [PAGES[folder]["tab"] for folder in SETTINGS_TAB_PAGES])
    lines += _strings(f"{SETTINGS}/tab_about", [PAGES[folder]["about"] for folder in SETTINGS_TAB_PAGES])
    lines += _strings(f"{SETTINGS}/button_help", SETTINGS_BUTTON_HELP["tab"] + SETTINGS_BUTTON_HELP["page"])
    lines += _strings(f"{SETTINGS}/still_captions", ["CLASSIC", "ENHANCED"])
    return lines


def settings_files() -> dict:
    """the files of the port's screens, by their names in ce/"""
    files = {}
    for folder, spec in SCREENS.items():
        files[f"{PE}/{folder}".replace("/", ".") + ".xml"] = _setting_screen(folder, spec)
    for folder, page in PAGES.items():
        files[f"{PE}/{folder}".replace("/", ".") + ".xml"] = _settings_page(folder, page)
    files[SETTINGS.replace("/", ".") + ".xml"] = _settings_shared()
    files[f"{PE}/vr_controls".replace("/", ".") + ".xml"] += _vr_item()
    files[f"{PE}/controls_setup".replace("/", ".") + ".xml"] = _controls_screen()
    return {name: ['<?xml version="1.0" encoding="UTF-8"?>',
                   "<!-- The port's settings screen, in the PC version's style (tools/port_settings.py) -->",
                   "<menus>", *lines, "</menus>", ""] for name, lines in files.items()}


def replaced(folder: str) -> bool:
    """whether the PC version's folder of widgets is replaced by ours (and
    those below it: the controls' advanced screen, the video test)"""
    return any(folder == path or folder.startswith(f"{path}/")
               for path in [*(f"{PE}/{name}" for name in [*SCREENS, *PAGES, "controls_setup"]), *REPLACED_FOLDERS])


# ---------- Settings' pictures (tools/ce_menus.py draws them, with the
# titles): the rows' highlight, a section's line, the tabs, the panel, the
# frames of the Quality row's stills, in the redraws' blue and navy

def _rounded(width: float, height: float, inset: float, radius: float, fill: str = "none",
             fill_opacity: float = 1.0, stroke: str = "none", stroke_width: float = 0) -> str:
    return (f'  <rect x="{inset:g}" y="{inset:g}" width="{width - 2 * inset:g}" height="{height - 2 * inset:g}" '
            f'rx="{radius:g}" fill="{fill}" fill-opacity="{fill_opacity:g}" stroke="{stroke}" '
            f'stroke-width="{stroke_width:g}"/>')


def _svg(width: int, height: int, body: list) -> str:
    return "\n".join([f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
                      f'viewBox="0 0 {width} {height}">', *body, "</svg>", ""])


def settings_pictures() -> list:
    """(bitmap name, [(SVG, width, height)] for its frames) of Settings'
    pictures"""
    row_w, row_h = SETTINGS_ROW_WIDTH, SETTINGS_ROW_HEIGHT
    tab_w, tab_h = SETTINGS_TAB_WIDTH, SETTINGS_TAB_HEIGHT
    still_w, still_h = SETTINGS_STILL_WIDTH, SETTINGS_STILL_HEIGHT
    pictures = [
        # (a row: nothing, and while chosen the option_bkds redraw's framed
        # bar)
        (f"{SETTINGS}/row", [(_svg(row_w, row_h, []), row_w, row_h),
                             (_svg(row_w, row_h, [_rounded(row_w, row_h, 1, 8, "#002244", 0.9, "#2299ff", 2)]),
                              row_w, row_h)]),
        (f"{SETTINGS}/section", [(_svg(row_w, SETTINGS_SECTION_HEIGHT, [
            f'  <rect x="0" y="{SETTINGS_SECTION_HEIGHT - 1.5:g}" width="{row_w}" height="1" fill="#2995ff" '
            'fill-opacity="0.45"/>']), row_w, SETTINGS_SECTION_HEIGHT)]),
        (f"{SETTINGS}/tab", [(_svg(tab_w, tab_h, []), tab_w, tab_h),
                             (_svg(tab_w, tab_h, [_rounded(tab_w, tab_h, 1, 8, "#002244", 0.9, "#2299ff", 2)]),
                              tab_w, tab_h)]),
        (f"{SETTINGS}/tab_current",
         [(_svg(tab_w, tab_h, [_rounded(tab_w, tab_h, 1, 8, "#0a2a5a", 0.75, "#2299ff", 2)]), tab_w, tab_h),
          (_svg(tab_w, tab_h, [_rounded(tab_w, tab_h, 1, 8, "#0d3a7a", 0.9, "#8cc8ff", 2)]), tab_w, tab_h)]),
    ]
    # (a slider's track, a groove in the redraws' navy and blue rim, and its
    # thumb, a blue tab)
    track_w, track_h, thumb_w = SLIDER_TRACK_WIDTH, SLIDER_TRACK_HEIGHT, SLIDER_THUMB_WIDTH
    pictures.append((f"{SETTINGS}/slider_track", [(_svg(track_w, track_h, [
        f'  <rect x="1" y="{track_h / 2 - 2.5:g}" width="{track_w - 2}" height="5" rx="2.5" fill="#000e23" '
        'fill-opacity="0.9" stroke="#2995ff" stroke-width="1"/>']), track_w, track_h)]))
    pictures.append((f"{SETTINGS}/slider_thumb", [(_svg(thumb_w, track_h, [
        _rounded(thumb_w, track_h, 1, 2.5, "#8cc8ff", 1.0, "#2299ff", 1.5)]), thumb_w, track_h)]))
    # (its number's field, as the CE text fields are framed: a navy box with
    # a blue rim and a line under the number; typed into, a brighter rim
    # and field)
    field_w, field_h = SLIDER_VALUE_WIDTH, SLIDER_VALUE_HEIGHT
    underline = (f'  <rect x="6" y="{field_h - 3.5:g}" width="{field_w - 12}" height="1" fill="#2995ff" '
                 'fill-opacity="0.8"/>')
    pictures.append((f"{SETTINGS}/slider_field", [
        (_svg(field_w, field_h, [_rounded(field_w, field_h, 2, 3, "#000e23", 0.9, "#2995ff", 1.5), underline]),
         field_w, field_h),
        (_svg(field_w, field_h, [_rounded(field_w, field_h, 2, 3, "#0d3a7a", 0.95, "#8cc8ff", 1.5),
                                 underline.replace("#2995ff", "#8cc8ff")]), field_w, field_h)]))
    for kind, top in (("tab", SETTINGS_TOP), ("page", PAGE_TOP)):
        height = SETTINGS_PANEL_BOTTOM - top
        # (list_field's framed panel: a navy field, a blue rim)
        pictures.append((f"{SETTINGS}/{kind}_panel", [(_svg(SETTINGS_PANEL_WIDTH, height, [
            _rounded(SETTINGS_PANEL_WIDTH, height, 1, 9.5, "#000e23", 0.88, "#2995ff", 2)]),
            SETTINGS_PANEL_WIDTH, height)]))
    pictures.append((f"{SETTINGS}/still_frame", [
        (_svg(still_w, still_h, [_rounded(still_w, still_h, 0.75, 4, stroke="#1d4f8a", stroke_width=1.5)]),
         still_w, still_h),
        (_svg(still_w, still_h, [_rounded(still_w, still_h, 1, 4, stroke="#2995ff", stroke_width=2)]),
         still_w, still_h)]))
    return pictures


# the stills the Quality row's panel shows (port/assets/menus/stills, made as
# NON_HANDDRAWN.md says): bitmap name, file, width and height in the menu's
# units, and what it is
SETTINGS_STILLS = [
    (f"{SETTINGS}/graphics_classic", "stills/graphics_classic.png", SETTINGS_STILL_WIDTH, SETTINGS_STILL_HEIGHT,
     "Quality CLASSIC (`display.graphics = \"classic\"`)"),
    (f"{SETTINGS}/graphics_enhanced", "stills/graphics_enhanced.png", SETTINGS_STILL_WIDTH, SETTINGS_STILL_HEIGHT,
     "Quality ENHANCED (`display.graphics = \"enhanced\"`)"),
]
# a picture for each value of these rows (by their keys), in the panel as
# their row is chosen (menu_functions.c's settings_panel_show): each value's
# file (port/assets/menus/stills), in the row's order, and what each file is
SETTINGS_PICTURE_WIDTH, SETTINGS_PICTURE_HEIGHT = 160, 100
_FOV_PICTURES = (["stills/fov_default.png"] * 4 + ["stills/fov_100.png"] * 5 + ["stills/fov_130.png"] * 7)
SETTINGS_VALUE_PICTURES = {
    "fov": _FOV_PICTURES,
    "viewmodel_fov": None,
    "anti_aliasing": ["stills/aa_off.png", "stills/aa_smaa.png", "stills/aa_smaa.png"] + ["stills/aa_msaa.png"] * 4,
    "anti_aliasing_android": ["stills/aa_off.png", "stills/aa_smaa.png", "stills/aa_smaa.png",
                              "stills/aa_msaa.png", "stills/aa_msaa.png"],
    "resolution_scaling": ["stills/aa_smaa.png", "stills/render_xbox.png"],
    "high_res_hud": ["stills/hud_high_res.png", "stills/hud_game.png"],
    "hud_button_style": ["stills/buttons_classic.png", "stills/buttons_gems.png", "stills/buttons_dark_gems.png"],
    "hdr_mode": ["stills/fov_default.png", "stills/hdr_punchy.png"],
}
del SETTINGS_VALUE_PICTURES["viewmodel_fov"]
SETTINGS_PICTURE_FILES = {
    "stills/fov_default.png": "Field of view DEFAULT (`display.fov = 0`); HDR mode FAITHFUL",
    "stills/fov_100.png": "Field of view 100, for 95 to 115",
    "stills/fov_130.png": "Field of view 130, for 120 to 150",
    "stills/aa_off.png": "Anti-aliasing OFF, close on an edge (each pixel twice its size)",
    "stills/aa_smaa.png": "Anti-aliasing SMAA (and FXAA), the same; render resolution FULL",
    "stills/aa_msaa.png": "Anti-aliasing MSAA 4X (and SSAA and MSAA's others), the same",
    "stills/render_xbox.png": "Render resolution XBOX 640X480, the same edge, its pixels as drawn",
    "stills/hud_high_res.png": "High-res HUD ON: the health and shields meters, close",
    "stills/hud_game.png": "High-res HUD OFF, the same",
    "stills/hdr_punchy.png": "HDR mode PUNCHY, approximately: the DEFAULT scene lifted in SDR, as no SDR picture can "
                             "show HDR",
    "stills/buttons_classic.png": "Button style CLASSIC: A, B, X and Y, the HUD's own pictures "
                                  "(port/assets/buttons), not a screenshot",
    "stills/buttons_gems.png": "Button style GEMS, the same",
    "stills/buttons_dark_gems.png": "Button style DARK GEMS, the same",
}
# NON_HANDDRAWN.md's words for them (tools/ce_menus.py)
SETTINGS_STILLS_MADE = [
    "Settings' Quality row shows these two small screenshots of the game, the same view drawn by each preset",
    "(`port/assets/menus/stills`, shipped as they are). They are this port's own drawing of the game: a local",
    "Slayer game on Blood Gulch (`debug.network_test = \"local:bloodgulch:slayer\"`, `debug.random_seed = 7`,",
    "four bots), the same frame of each run captured offscreen by the macOS build at 1600x1200",
    "(`debug.screenshot_every`), cropped to 16:10, scaled to 288x180 (three times their size in the menu) and",
    "reduced to 96 colors for size. The pictures of other rows' values are made the same way at the frame 12 s",
    "into the game with the setting set, 320x200 (twice their size in the menu), reduced to 64 colors: the whole",
    "view, or a part of it at 1:1 or larger.",
]


# ---------- multiplayer (PLAN.md section 11)

MT = "main_menu/multiplayer_type_select"

# strings added to the PC version's lists: (at, strings) of each list. Slayer's
# kills to win goes up to 500, for big games (the Xbox editor's are
# source/interface/ui_widget.c's kills_to_win_extra_strings): the values,
# and their helps after the five of its own (its helps are the rows' values'
# in turn: menu_functions.c's gametype_option_help)
SLAYER_EDIT = "main_menu/settings_select/multiplayer_setup/playlist_edit/slayer_edit"
PLAYER_EDIT = "main_menu/settings_select/multiplayer_setup/player_options_edit"
# (its rows, the PC version's, in order: ce_menus.py moves them 27 apart)
PLAYER_OPTIONS_ROWS = ['number_of_lives', 'maximum_health', 'shields', 'respawn_time', 'respawn_time_growth', 'odd_man_out', 'invisible_players', 'suicide_penalty']
# the vehicle sets' PC set (game_engine.h's VARIANT_VEHICLE_SET_PC), between
# GUN TURRETS and CUSTOM (menu_functions.c's VEHICLE_PRESET_PC), and its help
# among the sets' (its rows' values' in turn: gametype_option_help)
VEHICLE_EDIT = "main_menu/settings_select/multiplayer_setup/vehicle_options_edit"
AIM_ASSIST_HELP = [
    "Players keep their current mouse aim assist setting.",
    "Disable mouse aim assist for mouse/keyboard players.\\nController and touch aim assist are unchanged.",
]
# the host's shield effects, for every player of a network game
# (network_shield_effects.c): their words, then their helps
SHIELD_EFFECTS = ['STOCK', 'GLOW', 'LIGHT SHOW', 'OVERSHIELD', 'GLOW+OVERSHIELD', 'SHOW+OVERSHIELD',
                  "PLAYERS' OWN"]
SHIELD_EFFECTS_HELP = [
    "Everyone sees the stock game's shields and\\novershield, whatever their own settings.",
    "Everyone's shields light their surroundings as\\nthey flare.",
    "Everyone's shields light their surroundings as\\nthey flare, three times as far (Light Show).",
    "For everyone, the overshield rises up the body\\nand stays lit, in its own color.",
    "For everyone, shields light their surroundings,\\nand the overshield rises and stays lit.",
    "For everyone, Light Show's shield glow, and the\\novershield rising and staying lit.",
    "Not synced: each player sees the shield effects\\nof their own Video Setup settings.",
]

# OpenCE's gametypes' options (port/linux/game/game_modes.c), a screen of the
# gametype editor's (menu_functions.c's "mp profile init opence options"): the
# mode and Hardcore, then the mode's own rows, which menu_functions.c's
# gametype_option_help shows (op_inf_ for Infection, op_gg_ for Gun Game)
# below them; a row's values are menu_functions.c's gametype_options'
PLAYLIST_EDIT = "main_menu/settings_select/multiplayer_setup/playlist_edit"
OPENCE_EDIT = "main_menu/settings_select/multiplayer_setup/opence_options_edit"
OPENCE_ROWS = [
    ("oce_mode", "MODE:", ["NONE", "SWAT", "INFECTION", "GUN GAME"], [
        "The game type's own rules.",
        "No shields, and a shot to the head kills. Slayer only.",
        "Humans killed join the zombies, who win when\\nno human is left. Team Slayer only.",
        "Each kill gives the next weapon; a kill with the\\nlast wins. Free for all Slayer only."]),
    ("oce_hardcore", "HARDCORE:", ["OFF", "ON"], [
        "The motion sensor and aim assist as the game\\ntype has them.",
        "No motion sensor, no aim assist, and a kill\\nrestores the killer's health and shields."]),
    ("inf_zombies", "FIRST ZOMBIES:", ["1", "2", "3", "4", "8", "16"],
     ["The zombies chosen when the round starts\\n(a human is always left)."] * 6),
    ("inf_speed", "ZOMBIE SPEED:", ["100%", "110%", "125%", "150%", "175%", "200%"],
     ["How fast zombies move, of a human's speed."] * 6),
    ("inf_damage", "SKULL DAMAGE:", ["100%", "150%", "200%", "250%"],
     ["The damage of a zombie's skull's melee."] * 4),
    ("inf_respawn", "ZOMBIE RESPAWN:", ["1 SECOND", "3 SECONDS", "5 SECONDS", "10 SECONDS", "15 SECONDS",
                                        "30 SECONDS"],
     ["How long a dead zombie waits to respawn."] * 6),
    ("inf_countdown", "COUNTDOWN:", ["NONE", "5 SECONDS", "10 SECONDS", "15 SECONDS", "30 SECONDS"],
     ["How long the round waits, once two players are\\nalive, before the first zombies are chosen."] * 5),
    ("inf_shields", "ZOMBIE SHIELDS:", ["OFF", "ON"],
     ["Zombies have no shields.", "Zombies have shields, as humans do."]),
    ("gg_order", "WEAPON ORDER:", ["STRONGEST FIRST", "WEAKEST FIRST", "RANDOM"], [
        "From the rocket launcher to the pistol.", "From the pistol to the rocket launcher.",
        "In an order the host draws for each game."]),
    ("gg_melee", "MELEE DEMOTES:", ["OFF", "ON"], [
        "A melee kill takes nothing from the victim.",
        "A melee kill takes the victim back a weapon."]),
]
OPENCE_ITEM = f"{PLAYLIST_EDIT}/playlist_edit_opence_item"

# Capture the Flag's KILLS SCORE and KILLS PER CAPTURE (game_engine_ctf.c)
# and King of the Hill's MULTI-HILL (game_engine_king.c), OpenCE's (#467):
# rows after each editor's own (_gametype_rules_extras), with their labels
# after the PC version's and their helps before TIME LIMIT's (STRING_INSERTS),
# and TIME LIMIT moved below them (ce_menus.py's CHILD_OFFSETS). Each screen:
# its rows (each its key, its label, its spinner's words and their helps;
# menu_functions.c's gametype_options give their values), its list, where
# the first row goes, the index of its first label and of TIME LIMIT's first
# help, and the PC version's look of its rows, which differs: the prefix of
# its labels' and spinners' names (op_ and var_ have the key alone), its
# labels' width and its spinners' words' offset
CTF_EDIT = f"{PLAYLIST_EDIT}/ctf_edit"
KOTH_EDIT = f"{PLAYLIST_EDIT}/koth_edit"
CTF_KILLS_PER_CAPTURE = (5, 10, 15, 20, 25, 30, 40, 50, 75, 100)
GAMETYPE_RULES_EXTRAS = {
    CTF_EDIT: {"list": "ctf_options_menu", "labels": "ctf_labels", "helps": "cap_ctf", "y": 223,
               "label_index": 6, "help_index": 17, "prefix": "", "label_width": 320, "text_y": 4, "rows": [
        ("kills_score", "KILLS SCORE:", ["YES", "NO"], [
            "Each kill scores a point for the killer's team (a\\nbetrayal takes one away), and each capture is "
            "worth\\nKills per Capture. The limit is in captures.",
            "Only flag captures score."]),
        ("kills_per_capture", "KILLS PER CAPTURE:", [str(points) for points in CTF_KILLS_PER_CAPTURE], [
            f"With Kills Score on, each flag capture is worth\\n{points} points (a kill is worth 1)."
            for points in CTF_KILLS_PER_CAPTURE]),
    ]},
    KOTH_EDIT: {"list": "koth_options_menu", "labels": "koth_labels", "helps": "cap_koth", "y": 163,
                "label_index": 3, "help_index": 9, "prefix": "koth_", "label_width": 300, "text_y": None, "rows": [
        ("multi_hill", "MULTI-HILL:", ["OFF", "2", "3", "4", "5"], [
            "There will be only one Hill at a time.",
            "There will be two Hills at once. Holding either one\\nscores.",
            "There will be three Hills at once. Holding any of\\nthem scores.",
            "There will be four Hills at once. Holding any of\\nthem scores.",
            "There will be five Hills at once. Holding any of\\nthem scores."]),
    ]},
}
# (the rows' step, the PC version's)
GAMETYPE_RULES_STEP = 30

STRING_INSERTS = {
    # (the gametype editor's OPENCE OPTIONS' words, after the PC version's:
    # menu_functions.c's playlist_edit_description)
    f"{PLAYLIST_EDIT}/playlist_edit_list_extended_dsc_text": [(8, [
        "Choose the rules of OpenCE's\\ngametypes: SWAT, Infection,\\nGun Game and Hardcore.\\nThis gametype:"])],
    f"{PLAYER_EDIT}/player_options_labels": [(8, ["AIM ASSIST:", "SHIELD EFFECTS:"])],
    f"{PLAYER_EDIT}/cap_player_options": [(28, AIM_ASSIST_HELP + SHIELD_EFFECTS_HELP)],
    # (Teamplay Options' voice chat and vote kick rows, after its own:
    # TEAMPLAY_ROWS)
    f"{TEAMPLAY_EDIT}/teamplay_options_labels": [(3, [label for _, label, *_ in TEAMPLAY_EXTRA_ROWS])],
    f"{TEAMPLAY_EDIT}/cap_teamplay_options": [(10, [text for *_, helps in TEAMPLAY_EXTRA_ROWS for text in helps])],
    f"{SLAYER_EDIT}/var_kills_to_win": [(5, ["75", "100", "150", "200", "250", "500"])],
    # (Capture the Flag's and King of the Hill's rows: GAMETYPE_RULES_EXTRAS)
    **{f"{folder}/{screen['labels']}": [(screen["label_index"], [label for _, label, *_ in screen["rows"]])]
       for folder, screen in GAMETYPE_RULES_EXTRAS.items()},
    **{f"{folder}/{screen['helps']}": [(screen["help_index"], [text for *_, helps in screen["rows"]
                                                               for text in helps])]
       for folder, screen in GAMETYPE_RULES_EXTRAS.items()},
    f"{VEHICLE_EDIT}/var_vehicle_set": [(8, ["PC"])],
    f"{VEHICLE_EDIT}/cap_vehicle_options": [(17, [
        "Every vehicle the map places will spawn, as in\\nHalo PC.",
    ])],
    f"{SLAYER_EDIT}/cap_slayer": [(11, [
        "Seventy-five kills to win. Settle in for a long\\nfight.",
        "A hundred kills to win. Made for big games.",
        "A hundred and fifty kills to win. Only a crowded\\nserver gets there.",
        "Two hundred kills to win. Bring friends. Lots of\\nthem.",
        "Two hundred and fifty kills to win.",
        "Five hundred kills to win. You'll be here a while.",
    ])],
}


# a custom loadout's weapons (game_engine.h's _loadout_weapon_*)
LOADOUT_WEAPONS = ["NONE", "RANDOM", "ASSAULT RIFLE", "PISTOL", "SHOTGUN", "SNIPER RIFLE", "ROCKET LAUNCHER",
                   "PLASMA PISTOL", "PLASMA RIFLE", "NEEDLER"]


def LOADOUT_HELPS(slot: str) -> list:
    """the helps of a custom loadout's weapon's values"""
    helps = [f"No {slot} weapon.", f"A random {slot} weapon, of those the map has,\\neach time you spawn."]
    for weapon in LOADOUT_WEAPONS[2:]:
        helps.append(f"Everyone spawns with a {weapon.lower()} as their {slot}\\nweapon (where the map has one).")
    return helps


STRING_OVERRIDES.update({
    # (plain names: LOCAL NETWORK for the PC version's LAN, JOIN BY LINK,
    # the two co-ops by how they are played, and the bots' game: _bots)
    f"{MT}/multiplayer_options": ["JOIN GAME", "CREATE GAME", "INTERNET", "LOCAL NETWORK", "JOIN BY LINK",
                                  "EDIT GAMETYPES", "SERVER BROWSER", "SPLIT-SCREEN CO-OP", "ONLINE CO-OP",
                                  "PLAY WITH BOTS"],
    # (the items' in the list's order: menu_functions.c's description of
    # the item chosen)
    f"{MT}/multiplayer_option_descriptions": [
        "Every public game on the\\nInternet. Press A to see\\nthem all.",
        "Games on your Wi-Fi or local\\nnetwork.",
        "Join a friend's game with the\\ninvite link they send you, or\\na Discord invite.",
        "Host a game on the Internet.\\nFriends join with your invite\\nlink.",
        "Host a game for players on\\nyour Wi-Fi or local network.",
        "Play the campaign together\\nover the Internet: friends\\njoin with your invite link.",
        "Play the campaign with a\\nfriend on this screen.\\nPlayer 2 uses a controller.",
        "Play Slayer against computer\\nplayers, alone or with\\nfriends on this screen.",
        "Make your own gametypes:\\nthe rules, weapons and\\nvehicles, kept for next time.",
    ],
    # (and the port's OPENCE: OpenCE's gametypes, port/linux/game/game_modes.c)
    "main_menu/gametype_select/var_gametype_banks": ["STANDARD", "OPENCE", "CUSTOM"],
    # (the PC's weapon sets, then the Xbox's NO GRENADES: menu_functions.c's
    # gametype_options maps them to the engine's)
    "main_menu/settings_select/multiplayer_setup/item_options_edit/item_options_labels": ["INFINITE GRENADES:", "WEAPON SET:", "VEHICLE SET:", "STARTING EQUIPMENT:",
                                  "VEHICLE RESPAWN TIME:", "LOADOUT:", "PRIMARY WEAPON:", "SECONDARY WEAPON:",
                                  "MAP WEAPONS:"],
    # (the helps of each option's values in turn: menu_functions.c's
    # gametype_option_help)
    "main_menu/settings_select/multiplayer_setup/item_options_edit/cap_item_options": [
        "Everyone has a limitless supply of grenades, which\\nmakes for loud, explosive and occasionally messy fun.",
        "Each player can only carry up to 4 grenades of each\\ntype at any given time.",
        "The map will contain whatever weapons the designers\\nplaced on it.",
        "All the weapons on the map will be replaced by\\npistols.",
        "All the weapons on the map will be replaced by\\nassault rifles and plasma rifles.",
        "All the weapons on the map will be replaced by\\nplasma weapons.",
        "The guns on the map will be replaced by weapons\\nwith sniper scopes.",
        "Sniper rifles and pistols will not appear on the map.",
        "All the weapons on the map will be replaced by\\nrocket launchers.",
        "All the weapons on the map will be replaced by\\nshotguns.",
        "All the weapons on the map are only effective at\\nshort ranges.",
        "There are no weapons on the map that were made in\\nCovenant sweat shops.",
        "Only weapons made by the Covenant will appear\\nin the map.",
        "The flamethrower and fuel rod gun will not appear\\nin the map.",
        "Only the most devastating and dangerous of weapons\\nwill appear in the map.",
        "The map's weapons, without any grenades.",
        "You will start the game with the weapon set that the \\nmap designers custom tailored for it.",
        "Use the generic starting weapons across all maps.",
        "The map's weapons appear where its designers\\nplaced them.",
        "No weapons appear on the map: with a loadout of\\nNONE and NONE, melee and grenades only.",
        "The weapons follow the weapon set above.",
        "Everyone starts with the primary and secondary\\nweapons below; the map's weapons are its own.",
        *LOADOUT_HELPS("primary"),
        *LOADOUT_HELPS("secondary"),
    ],
    "main_menu/settings_select/multiplayer_setup/item_options_edit/var_weapon_set": [
        "NORMAL", "PISTOLS", "RIFLES", "PLASMA WEAPONS", "SNIPER", "NO SNIPING", "ROCKET LAUNCHERS", "SHOTGUNS",
        "SHORT RANGE", "HUMAN", "COVENANT", "CLASSIC", "HEAVY WEAPONS", "NO GRENADES"],
})

# which of STRING_OVERRIDES' strings say what one of the PC version's own
# says: its index in the PC version's list, or None (the port's own). A map
# in another language than English has those in its language (map=,
# menu_tags.c's map_strings); the inserted strings (STRING_INSERTS) are the
# port's own, the rest of their lists the PC version's
OVERRIDE_ORIGINALS = {
    f"{PE}/profile_edit_descriptions": [0, None, None, None, None, None, None, 7, None],
    f"{MT}/multiplayer_options": [0, 1, 2, None, None, 5, None, None, None, None],
    f"{MT}/multiplayer_option_descriptions": [None] * 9,
    "main_menu/gametype_select/var_gametype_banks": [0, None, 2],
    "main_menu/settings_select/multiplayer_setup/item_options_edit/item_options_labels": [0, 1, 2, 3, 4] + [None] * 4,
    # (the first's note that infinite grenades work only with 4 players or
    # fewer is left out: not so here)
    "main_menu/settings_select/multiplayer_setup/item_options_edit/cap_item_options":
        [None] + list(range(1, 15)) + [None, 15, 16] + [None] * 24,
    "main_menu/settings_select/multiplayer_setup/item_options_edit/var_weapon_set": list(range(13)) + [None],
}

# the map lists' first row: SINGLEPLAYER, MULTIPLAYER, CUSTOM SINGLEPLAYER or
# CUSTOM MULTIPLAYER maps (_map_kind)
MAP_KIND_CHOOSER = "main_menu/new_select/list_item_0_map_kind"

# Multiplayer's panel's counts: the digits' bitmap (tools/ce_menus.py's
# Art.digits), a frame's size in units and the digits' height, white
STATUS_DIGITS = f"{MT}/status_digits"
DIGIT_SIZE = (24, 36)
DIGIT_CAP = 28
DIGIT_COLOR = (0xFF, 0xFF, 0xFF, 255)
# a game's ping as bars, a frame each: not measured (grey), poor (one, red),
# fair (two, yellow), good (three, green): menu_functions.c's PING_GOOD and
# PING_FAIR
PING_BARS = f"{MT}/ping_bars"
# the browsers' rows' star (menu_functions.c's browser_row_star): its widget
# and bitmap, whose frames are none, a favorite (gold), a favorite offline
# (grey) and the outline a click on the row focused stars (Lucide's star,
# port/assets/icons/lucide)
FAVORITE_STAR_WIDGET = f"{MT}/join_game/server_item_favorite"
FAVORITE_STAR = f"{MT}/join_game/favorite_star"
FAVORITE_STAR_STYLES = [None, ("#E8C547", "#E8C547"), ("#7F8A99", "#7F8A99"), ("none", "#2896FF")]
# the digits of a count (to 999), and the games BEST FOR YOU names
STATUS_DIGIT_PLACES = 3
BEST_GAMES = 3
PING_BAR_COLORS = [None, "#E86A5A", "#E8C547", "#5ED38C"]


# Multiplayer's rows, closer than the PC version's 33 units for Create
# Game's ONLINE CO-OP, SPLIT-SCREEN CO-OP and PLAY WITH BOTS: each item's
# and header's place (x, y) in the list, then the line and EDIT GAMETYPES
# below them, the highlight inside the description panel's bottom curve
# (tools/ce_menus.py's CHILD_OFFSETS)
MULTIPLAYER_ROW_STEP = 28
MULTIPLAYER_ROWS = {
    name: (-32 if name in ("multiplayer_type_join_item", "multiplayer_type_create_item") else 0,
           78 + MULTIPLAYER_ROW_STEP * index)
    for index, name in enumerate([
        "multiplayer_type_join_item", "multiplayer_type_join_internet_item", "multiplayer_type_join_lan_item",
        "multiplayer_type_join_direct_item", "multiplayer_type_create_item", "multiplayer_type_create_internet_item",
        "multiplayer_type_create_lan_item", "multiplayer_type_online_coop_item", "multiplayer_type_coop_item",
        "multiplayer_type_create_bots_item"])}
# (the screen's line, in the PC version's line's place: the hub's, over
# EDIT GAMETYPES, _hub: its list's items HUB_LIST_STEP apart, its sections
# HUB_SECTION_GAP, EDIT GAMETYPES HUB_GAMETYPES_GAP under the line, to keep
# clear of the footer)
MULTIPLAYER_LINE_PLACE = (28, 375)
HUB_LIST_STEP = 27
HUB_SECTION_GAP = 6
HUB_GAMETYPES_GAP = 6
# the screen's children for one platform only (tools/ce_menus.py): the
# computer's list and its line (the hub's, in the PC version's places:
# WIDGET_PATCHES' swap), the touchscreen's tiles in their place (_touch_hub)
CHILD_PLATFORMS = {
    (f"{MT}/multiplayer_type_select_screen", f"{MT}/hub/hub_list"): "pointer",
    (f"{MT}/multiplayer_type_select_screen", f"{MT}/hub/hub_divider"): "pointer",
}
MULTIPLAYER_GAMETYPES_PLACE = (0, 370)

# changes to the PC version's widgets (by our names): attributes set, all
# their handlers replaced, children added, game data inputs added
WIDGET_PATCHES = {
    # The host returns here after a round with no menu history. Back must
    # reopen the main menu when it closes the last screen, as the lobby does.
    f"{MT}/connected/connected_map_select_wrapper": {"set": {
        "flags": "pass_unhandled_to_focused_child pause_game main_menu_if_no_history tag_controller_index force_handle_mouse",
    }},
    # (Settings opens its first tab at once, with a profile or without one:
    # menu_functions.c's settings_begin)
    "main_menu/main_menu_item_settings": {"handlers": [
        f'<on event="{event}" run="port settings begin" open="{PE}/video_settings/video_settings_screen"/>'
        for event in ("a", "start")] + ['<on event="left_mouse" run="mouse emit accept event"/>']},
    # (the main menu's MODS, before Quit: _mods)
    "main_menu/main_menu_select_list": {"insert_before": {"main_menu/main_menu_item_quit_game": [
        f'<child widget="main_menu/main_menu_item_mods" x="{MODS_ITEM_PLACE[0]}" y="{MODS_ITEM_PLACE[1]}"/>']}},
    f"{PLAYER_EDIT}/player_options_menu": {"insert_before": {
        f"{PLAYER_EDIT}/player_button_bar": [
            f'<child widget="{PLAYER_EDIT}/op_aim_assist" x="54" y="289"/>',
            f'<child widget="{PLAYER_EDIT}/op_shield_effects" x="54" y="316"/>']}},
    # (its rows 27 apart, not 30, so that ten fit above the help, which keeps
    # the three lines some have: ce_menus.py's CHILD_OFFSETS)
    f"{PLAYER_EDIT}/player_options_help": {"set": {"top": 345, "height": 60}},
    # (the gametype editor's OPENCE OPTIONS, after its own rows: _opence_options)
    f"{PLAYLIST_EDIT}/edit_playlist_select_list": {
        "insert_before": {f"{PLAYLIST_EDIT}/playlist_edit_button_bar": [f'<child widget="{OPENCE_ITEM}"/>']},
        "inputs": ["port playlist edit desc"]},
    # (the profile settings' picture: on Gamepad Setup's row, the profile's
    # button settings, BITMAP_FRAMES; menu_functions.c's
    # profile_gamepad_layout)
    f"{PE}/profile_edit_extended_desc_pic": {"inputs": ["port gamepad layout preview"]},
    # (straight to their screens: no "checking for updates" dialog, which
    # asked the PC version's servers)
    f"{MT}/multiplayer_type_join_internet_item": {"set": {"string_index": 6}, "handlers": [
        f'<on event="a" run="mp type set mode" open="{MT}/join_game/join_game_screen"/>',
        f'<on event="start" run="mp type set mode" open="{MT}/join_game/join_game_screen"/>',
        '<on event="left_mouse" run="mouse emit accept event"/>',
    ]},
    f"{MT}/multiplayer_type_create_internet_item": {"handlers": [
        '<on event="a" run="join controller to mp game"/>',
        f'<on event="a" run="mp type set mode" open="{MT}/connected/connected_map_select_wrapper"/>',
        '<on event="left_mouse" run="mouse emit accept event"/>',
    ]},
    f"{MT}/multiplayer_type_join_direct_item": {"children": [f'<child widget="{MT}/invite_note"/>'], "handlers": [
        f'<on event="a" run="mp type set mode" open="{MT}/join_game/join_game_screen"/>',
        f'<on event="start" run="mp type set mode" open="{MT}/join_game/join_game_screen"/>',
        '<on event="left_mouse" run="mouse emit accept event"/>',
    ]},
    # (Multiplayer's panel: _multiplayer_status, filled each frame; the menu
    # stops its own looking for games as it closes)
    f"{MT}/multiplayer_type_list_ext_desc": {"children": [
        f'<child widget="{MT}/{name}"/>' for name in [
            "multiplayer_status", "multiplayer_status_detail", "online_now_label",
            *[f"online_{count}_{digit}" for count in ("games", "players") for digit in range(STATUS_DIGIT_PLACES)],
            "online_games_label", "online_players_label", "best_label",
            *[f"best_{row}_{part}" for row in range(BEST_GAMES) for part in ("name", "about", "players", "ping")]]]},
    f"{MT}/multiplayer_type_join_lan_item": {"children": [f'<child widget="{MT}/item_note"/>']},
    # (PARTY, then REFRESH, beside Back: _party, _multiplayer_status)
    f"{MT}/multiplayer_type_button_bar": {"set": {"flags": "pass_unhandled_to_focused_child left_right_tabs_items "
                                                           "anchor_right"}, "insert_before": {
        "common_button_back": [f'<child widget="{MT}/multiplayer_button_party" x="250" y="1"/>',
                               f'<child widget="{MT}/multiplayer_button_refresh" x="380" y="1"/>']}},
    # (the computer's list Multiplayer's own, in Settings' look, and its
    # line over EDIT GAMETYPES: _hub; the right trigger, R on the keyboard,
    # looks for the games again)
    f"{MT}/multiplayer_type_select_screen": {"handlers": [
        '<on event="created" run="mp type menu initialize"/>',
        '<on event="deleted" run="port multiplayer status end"/>',
        '<on event="right_trigger" run="port multiplayer refresh"/>'], "swap": {
        f"{MT}/multiplayer_type_select_list": f"{MT}/hub/hub_list", "main_menu/blueline": f"{MT}/hub/hub_divider",
    }, "children": [f'<child widget="{MT}/hub/touch/hub_list" platform="touch"/>']},
    f"{MT}/header_multiplayer": {"set": {"flags": "anchor_left", "wide": WIDE_LEFT}},
    # (the list's headers, which the touchscreen's list shows too, at its rows'
    # left edge)
    f"{MT}/multiplayer_type_join_item": {"set": {"flags": "anchor_left"}},
    f"{MT}/multiplayer_type_create_item": {"set": {"flags": "anchor_left"}},
    # (Co-op, after Create Game's: _coop; then PLAY WITH BOTS: _bots)
    f"{MT}/multiplayer_type_select_list": {"inputs": ["port multiplayer status"], "insert_before": {
        f"{MT}/multiplayer_type_gametypes_item": [
            f'<child widget="{MT}/{name}" y="{MULTIPLAYER_ROWS[name][1]}"/>'
            for name in ("multiplayer_type_online_coop_item", "multiplayer_type_coop_item",
                         "multiplayer_type_create_bots_item")],
    }},
    # (the Map screen's and New Game's lists: SINGLEPLAYER or MULTIPLAYER
    # maps, the first row's chooser's (_map_kind); B on the Map screen goes
    # from a co-op level's difficulties back to the level, else out of the
    # screen: menu_functions.c's map list)
    f"{MT}/mp_map_select/mp_map_select_list_2": {"swap": {"main_menu/new_select/list_item_0": MAP_KIND_CHOOSER},
                                                 "handlers": [
        '<on event="created" run="mp level list initialize"/>',
        '<on event="deleted" run="mp level list dispose"/>',
        f'<on event="custom_activation" run="mp level select" open="{MT}/connected/gametype_select_screen_wrapper"/>',
        '<on event="b" run="port map list back"/>',
        '<on event="back" run="port map list back"/>',
    ]},
    "main_menu/solo_level_select/solo_level_select_list": {"swap": {"main_menu/new_select/list_item_0": MAP_KIND_CHOOSER}},
    # (each description shows the other kind's maps too: a campaign level's
    # picture, name and words on the Map screen, a multiplayer map's on New
    # Game's)
    f"{MT}/mp_map_select/mp_map_right_item": {"children": [
        '<child widget="main_menu/solo_level_select/replay_level_right_name"/>',
        '<child widget="main_menu/solo_level_select/replay_level_right_pic"/>',
        '<child widget="main_menu/solo_level_select/replay_level_right_data"/>',
    ]},
    "main_menu/solo_level_select/replay_level_right_item": {"children": [
        f'<child widget="{MT}/mp_map_select/mp_map_right_name"/>',
        f'<child widget="{MT}/mp_map_select/mp_map_right_pic"/>',
        f'<child widget="{MT}/mp_map_select/mp_map_right_data"/>',
    ]},
    # (Item Options' loadout rows, over its buttons)
    "main_menu/settings_select/multiplayer_setup/item_options_edit/item_options_menu": {"insert_before": {
        "main_menu/settings_select/multiplayer_setup/item_options_edit/item_button_bar": [
            f'<child widget="main_menu/settings_select/multiplayer_setup/item_options_edit/op_map_weapons" x="54" y="163"/>',
            f'<child widget="main_menu/settings_select/multiplayer_setup/item_options_edit/op_loadout" x="54" y="193"/>',
            f'<child widget="main_menu/settings_select/multiplayer_setup/item_options_edit/op_primary_weapon" x="54" y="223"/>',
            f'<child widget="main_menu/settings_select/multiplayer_setup/item_options_edit/op_secondary_weapon" x="54" y="253"/>',
        ]}},
    # (Teamplay Options' FORCE TEAM SPAWNS, then its voice chat and vote kick
    # rows, Server Setup's only, over its buttons: _teamplay_options_extras)
    f"{TEAMPLAY_EDIT}/teamplay_options_menu": {"insert_before": {
        f"{TEAMPLAY_EDIT}/teamplay_button_bar": [
            f'<child widget="{TEAMPLAY_EDIT}/op_{key}" x="54" y="{163 + TEAMPLAY_STEP * index}"/>'
            for index, (key, *_) in enumerate(TEAMPLAY_EXTRA_ROWS)]}},
    f"{TEAMPLAY_EDIT}/teamplay_options_help": {"set": {"top": TEAMPLAY_HELP_TOP,
                                                       "height": 414 - TEAMPLAY_HELP_TOP}},
    # (Capture the Flag's and King of the Hill's rows, before TIME LIMIT,
    # which King of the Hill's list has of Capture the Flag's:
    # _gametype_rules_extras)
    **{f"{folder}/{screen['list']}": {"insert_before": {f"{CTF_EDIT}/op_time_limit": [
        f'<child widget="{folder}/op_{key}" x="54" y="{screen["y"] + GAMETYPE_RULES_STEP * index}"/>'
        for index, (key, *_) in enumerate(screen["rows"])]}}
       for folder, screen in GAMETYPE_RULES_EXTRAS.items()},

    # (VR Setup, below About, in VR only: _vr_item)
    f"{PE}/profile_edit_select_list": {"insert_before": {
        f"{PE}/profile_edit_button_bar": [f'<child widget="{VR_ITEM}" platform="vr"/>']}},
    # (the PC's Vehicles row's Start opened Item Options)
    "main_menu/settings_select/multiplayer_setup/playlist_edit/playlist_edit_vehicles_list_item": {"handlers": [
        '<on event="a" open="main_menu/settings_select/multiplayer_setup/vehicle_options_edit/vehicle_options_screen"/>',
        '<on event="start" open="main_menu/settings_select/multiplayer_setup/vehicle_options_edit/vehicle_options_screen"/>',
        '<on event="left_mouse" run="mouse emit accept event"/>',
    ]},
}

# frames added after a PC bitmap's (by our names): (the Xbox map's bitmap,
# its frame, the size drawn at, where in the widget). The profile settings'
# picture gets the Xbox's Controller Setup's pictures of its five button
# settings (its first four frames are the thumbstick settings'), which show
# 512 by 235 of their 512 by 512: drawn at 279 wide, in the middle of the
# picture's 279 by 202
XBOX_CONTROLLER_PICTURES = f"ui\\shell\\{PE.replace('/', chr(92))}\\controller_edit\\config_controller"
BITMAP_FRAMES = {
    f"{PE}/profile_options": [(XBOX_CONTROLLER_PICTURES, 4 + preset, 279, 279, 0, 34) for preset in range(5)],
}

# the titles this port has that the PC version has not, set as its headers
# are (port/assets/menus/port_svg): bitmap name, text
TITLES = {
    f"{PE}/vr_controls/header_vr_controls": "VR CONTROLS",
    f"{PE}/vr_display/header_vr_display": "VR DISPLAY",
    "main_menu/mods/header_mods": "MODS",
    f"{MT}/join_game/header_server_browser": "SERVER BROWSER",
    f"{MT}/join_game/header_direct_link": "JOIN BY LINK",
    f"{MT}/join_game/header_local_network": "LOCAL NETWORK",
    f"{MT}/join_game/header_password": "PASSWORD",
    f"{MT}/lobby/header_lobby": "GAME LOBBY",
    f"{MT}/coop/header_player_2": "PLAYER 2 PROFILE",
    f"{MT}/lobby/header_add_player": "ADD PLAYER",
    **SETTINGS_TITLES,
}


def title_backdrop(width: float) -> str:
    """a header's dark backdrop, as the PC headers' redraws have it, for text
    width units wide (the text is set over it in OpenCE: ce_menus.py)"""
    return "\n".join([
        '<svg xmlns="http://www.w3.org/2000/svg" width="512" height="64" viewBox="0 0 512 64">',
        "  <defs>",
        '    <filter id="soft" filterUnits="userSpaceOnUse" x="-60" y="-60" width="632" height="184">',
        '      <feGaussianBlur stdDeviation="14.23 13.06"/>', "    </filter>",
        '    <mask id="cut" maskUnits="userSpaceOnUse" x="0" y="0" width="512" height="64">',
        '      <rect width="512" height="59" fill="#fff"/>', "    </mask>", "  </defs>",
        '  <g mask="url(#cut)">',
        f'    <rect x="29.08" y="28.05" width="{width + 28:.2f}" height="40.95" fill="#021931" fill-opacity="0.893" '
        'filter="url(#soft)"/>',
        "  </g>", "</svg>", ""])


# the in-game pause menu's box, taller for the buttons this port adds
# (menu_tags.c's pause_patch: SETTINGS, and the host's END GAME): the
# pausebox5 redraw's rounded box (ui-svg-handmade/shell/bitmaps), drawn to
# hold each number of buttons, one frame each (the Xbox's 2-button box is
# 159 units high, and each button 35 more)
PAUSE_BOX_BUTTONS = [3, 4]
# the campaign pause box, tall enough for five rows (SETTINGS added) or six
# (network co-op's FRIENDLY FIRE too) at the PC version's pause menu's pitch
# (30) with the prompts still under the last row: the stock box is 159, and
# those rows need 36 and 66 more (menu_tags.c's pause_campaign_patch); the
# mission objectives' box beside it grows with it
PAUSE_BOX_EXTRA_HEIGHTS = [195, 225]


def pause_box_height(buttons: int) -> int:
    return 159 + (buttons - 2) * 35


def pause_box_svg(piece: str, height: int, header: bool = False) -> str:
    """a piece of the pause box (left cap, centre strip, right cap; 9-slice:
    the strip tiles across), height units high in its 256-high texture"""
    width = 4 if piece == "center" else 16
    lines = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="256" viewBox="0 0 {width} 256">']
    fill = "#0a2649"
    body_clip = ""
    if not header:
        # Keep the translucent footer clear of the body: blending two
        # 75%-opaque fills would make it nearly opaque and tint it blue.
        lines += [f'  <defs><clipPath id="body-area"><rect x="-16" y="0" width="48" height="{height - 28}"/></clipPath></defs>']
        body_clip = ' clip-path="url(#body-area)"'
    if height in PAUSE_BOX_EXTRA_HEIGHTS or height in map(pause_box_height, PAUSE_BOX_BUTTONS):
        # The stock campaign body fades from blue to dark navy at 75% opacity.
        # Scale it within the body, excluding the objective header or key footer.
        top, bottom = (29, height - 2) if header else (2, height - 28)
        lines += [f'  <defs><linearGradient id="body" gradientUnits="userSpaceOnUse" x1="0" y1="{top}" x2="0" y2="{bottom}">',
                  '    <stop offset="0" stop-color="#0a2649"/>',
                  '    <stop offset="1" stop-color="#050d1c"/>',
                  '  </linearGradient></defs>']
        fill = "url(#body)"
    if piece == "center":
        lines += [f'  <rect x="0" y="2" width="4" height="{height - 4}" fill="{fill}" fill-opacity="0.75"{body_clip}/>',
                  '  <rect x="0" y="0" width="4" height="2" fill="#2896ff"/>',
                  f'  <rect x="0" y="{height - 2}" width="4" height="2" fill="#2896ff"/>']
    else:
        bottom, corner = height - 1, height - 9.5
        cap = [f'    <path d="M9.5,1 H17 V{bottom} H9.5 A8.5,8.5 0 0 1 1,{corner} V9.5 A8.5,8.5 0 0 1 9.5,1 Z" '
               f'fill="{fill}" fill-opacity="0.75" fill-rule="evenodd"{body_clip}/>',
               f'    <path d="M17,1 H9.5 A8.5,8.5 0 0 0 1,9.5 V{corner} A8.5,8.5 0 0 0 9.5,{bottom} H17" fill="none" '
               'stroke="#2896ff" stroke-width="2"/>']
        lines += (['  <g transform="matrix(-1 0 0 1 16 0)">'] + cap + ['  </g>']) if piece == "right" else \
            [line[2:] for line in cap]
    # The stock objectives have a 28-unit header; the button key has a
    # 28-unit footer. Clip the black fill to the rounded panel interior.
    top = 2 if header else height - 28
    divider = 28 if header else height - 28
    if piece == "left":
        lines += [f'  <defs><clipPath id="strip"><rect x="2" y="2" width="{width + 16}" height="{height - 4}" rx="7.5"/></clipPath></defs>']
    if piece == "right":
        lines += [f'  <defs><clipPath id="strip"><rect x="-16" y="2" width="{width + 14}" height="{height - 4}" rx="7.5"/></clipPath></defs>']
    clip = ' clip-path="url(#strip)"' if piece != "center" else ""
    opacity = ' fill-opacity="0.75"' if not header else ""
    lines += [f'  <g{clip}>',
              f'    <rect x="0" y="{top}" width="{width}" height="26" fill="black"{opacity}/>',
              f'    <rect x="0" y="{divider}" width="{width}" height="0.75" fill="#2896ff"/>',
              '  </g>']
    lines += ["</svg>", ""]
    return "\n".join(lines)


# where the PC headers' text is (units of their 512x64), and its colour
TITLE_LEFT, TITLE_TOP, TITLE_CAP = 30.68, 33.5, 21.5
TITLE_COLOR = (0x29, 0x95, 0xFD, 255)


def _header(name: str, bitmap: str, platform=None) -> list:
    return _widget(name, [("controller", 1), ("left", 35), ("top", 11), ("width", 363), ("height", 59),
                          ("bitmap", bitmap), ("platform", platform)], [])


# a text field's B and Back do nothing of their own (the engine would go
# back from the field): its screen's (SCREEN_BACK) cancel the edit, else
# leave
FIELD_BACK = ['<on event="b"/>', '<on event="back"/>']
SCREEN_BACK = ['<on event="b" run="gamespy back handler"/>', '<on event="back" run="gamespy back handler"/>']


def _value_row(base: str, key: str, label_index: int, run: str) -> list:
    """a row of a label and a value the player sets (A edits it)"""
    lines = _widget(f"{base}/op_{key}", [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                                         ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                    [f'<on event="a" run="{run}"/>', f'<on event="start" run="{run}"/>', *FIELD_BACK,
                     '<on event="left_mouse" run="mouse emit accept event"/>',
                     f'<child widget="{base}/{key}_label"/>', f'<child widget="{base}/{key}_value" x="230" y="1"/>'])
    lines += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1), ("width", 230), ("height", 22),
                                             ("string_list", f"{base}/labels"), ("string_index", label_index or None),
                                             ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13),
                                             ("text_y", 4)], [])
    lines += _widget(f"{base}/{key}_value", [("type", "text"), ("controller", 1), ("width", 280), ("height", 22),
                                             ("font", "ui\\small_ui"), ("color", "#FF2896FF"), ("align", "center"),
                                             ("text_y", 5), ("text_flags", "no_focus_test")], [])
    return lines


def _connect_screen() -> list:
    """internet play's connecting box, laid out as the PC version's Quit is
    (ce/main_menu.quit_select.xml): opened over the browser as a game is
    joined (menu_functions.c's connect_dialog), it names the step the join
    is at (finding the host, opening a path to it, joining), and once it
    gives up (about 10 seconds) why, and what to do; RETRY (shown then)
    asks again, BACK (and B) gives the join up. Its header and its text are
    the game's ("port connect update")"""
    base = f"{MT}/join_game/connect"
    lines = _widget(f"{base}/connect_screen",
                    [("type", "text"), ("controller", 1), ("left", 180), ("top", 160), ("width", 280), ("height", 160),
                     ("flags", "pass_unhandled_to_focused_child"), ("bitmap", "bitmaps/alert_bkd"),
                     ("font", "ui\\small_ui"), ("color", "#FF2896FF"), ("align", "center"), ("text_x", -10),
                     ("text_y", 36)],
                    ['<data input="port connect update"/>',
                     '<on event="b" run="port connect back"/>', '<on event="back" run="port connect back"/>',
                     f'<child widget="{base}/button_bar" x="182" y="293"/>',
                     f'<child widget="{base}/header_connect" x="182" y="164"/>'])
    lines += _widget(f"{base}/header_connect",
                     [("type", "text"), ("controller", 1), ("width", 270), ("height", 20),
                      ("font", "ui\\large_ui"), ("color", "#FF2896FF")], [])
    # (BACK first, with the focus: RETRY is shown only once the join failed)
    lines += _widget(f"{base}/button_bar",
                     [("type", "column_list"), ("width", 256), ("height", 20),
                      ("flags", "pass_unhandled_to_focused_child up_down_tabs_items left_right_tabs_items")],
                     [f'<child widget="{base}/button_back" x="128"/>', f'<child widget="{base}/button_retry"/>'])
    lines += _text_button(f"{base}/button_retry", "RETRY", ['<on event="a" run="port connect retry"/>',
                                                           '<on event="start" run="port connect retry"/>'])
    lines += _text_button(f"{base}/button_back", "BACK", ['<on event="a" run="port connect back"/>',
                                                         '<on event="start" run="port connect back"/>'])
    return lines


def _join_game_extras() -> list:
    """the browser's titles, its connecting box, its rows' star, and the
    password screen"""
    base = f"{MT}/join_game"
    lines = _password_screen()
    lines += _header(f"{base}/header_server_browser", f"{base}/header_server_browser")
    lines += _header(f"{base}/header_direct_link", f"{base}/header_direct_link")
    lines += _connect_screen()
    # (each row's star: _browser puts it in the rows)
    lines += _widget(FAVORITE_STAR_WIDGET, [("width", 16), ("height", 18), ("bitmap", FAVORITE_STAR)], [])
    return lines


def _password_screen() -> list:
    """the server browser's password screen: a game with a password chosen
    (menu_functions.c's lobby_browser_select), its password typed (shown as
    stars), JOIN GAME joins it if it is the game's; else its help says so"""
    base = f"{MT}/join_game/password"
    spec = {"screen": "password_screen", "spacing": 28, "help_top": 120,
            "header": ("header_password", f"{MT}/join_game/header_password")}
    extra = _value_row(base, "password", 0, "port password edit")
    extra += _button(f"{base}/button_defaults", 3, [])
    extra += _widget(f"{base}/button_ok", [("type", "text"), ("width", 128), ("height", 24),
                                           ("bitmap", "bitmaps/text_button_background"), ("text", "JOIN GAME"),
                                           ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"),
                                           ("text_y", 2)],
                     ['<on event="a" run="port password join"/>', '<on event="start" run="port password join"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    extra += _strings(f"{base}/labels", ["PASSWORD:"])
    # (by its state: menu_functions.c's password_screen_update)
    extra += _strings(f"{base}/help_strings", [
        "",
        "This game has a password. Type it, then press\\nEnter to join.",
        "That is not the game's password. Try again.",
        "Couldn't join this game. Refresh the list, then\\ntry again.",
    ])
    lines = _screen(base, spec, [(f"{base}/op_password", None)], ["port password update"],
                    ['<on event="created" run="port password init"/>'], extra)
    # (no Defaults: the bar is JOIN GAME and CANCEL; B goes back at once,
    # even while typing, which the screen starts with)
    lines = [line.replace(f'<child widget="{base}/button_defaults" y="1"/>', "") for line in lines]
    lines = [line.replace('<on event="b" back="true"/>', '<on event="b" run="port password back"/>')
             .replace('<on event="back" back="true"/>', '<on event="back" run="port password back"/>')
             for line in lines]
    return lines


# Server Setup's GAME: the kind of game hosted and its tick rate
# (network.game_mode and network.sim_rate: menu_functions.c's
# server_settings_game_modes, in this order), each choice's help
GAME_MODES = [
    ("OPENH1 30 TICK", "OpenH1 and OpenCE players, all at 30 ticks."),
    ("OPENH1 60 TICK", "OpenH1 players at 60 ticks, OpenCE players at 30."),
    ("OPENH1 120 TICK", "OpenH1 players at 120 ticks, OpenCE players at 30."),
    ("LEGACY", "OpenCE's own netcode for everyone, at 30 ticks."),
]
# (each one line: the help shows one)


def _server_settings() -> list:
    """Create Game's server settings (in the PC version's place): the game's
    name, the most players (up to the port's 128), the kind of game (OpenH1
    at a tick rate, or Legacy), its invite link, and whether the server
    browser lists it (PUBLIC or PRIVATE: an internet game's)"""
    base = f"{MT}/server_settings"
    # (fourteen rows: closer together, the help lower)
    spec = {"screen": "server_settings_screen", "spacing": SERVER_SETTINGS_SPACING, "help_top": 364,
            "header": ("header_server_settings", f"{base}/header_server_settings")}
    rows, extra = [], []
    extra += _value_row(base, "server_name", 0, "ss edit server name")
    rows.append((f"{base}/op_server_name", None))
    extra += _widget(f"{base}/op_max_players", [("width", 512), ("height", 28),
                                                ("flags", "pass_unhandled_to_focused_child"),
                                                ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                     [f'<child widget="{base}/max_players_label"/>',
                      f'<child widget="{base}/max_players_spinner" x="320" y="1"/>'])
    extra += _widget(f"{base}/max_players_label", [("type", "text"), ("controller", 1), ("width", 300),
                                                   ("height", 22), ("string_list", f"{base}/labels"),
                                                   ("string_index", 1), ("font", "ui\\large_ui"),
                                                   ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    extra += _widget(f"{base}/max_players_spinner",
                     [("type", "spinner"), ("left", 3), ("top", 2), ("width", 147), ("height", 20),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                      ("strings", "|".join(str(count) for count in MAXIMUM_PLAYERS)), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("align", "center"), ("text_y", 1),
                      ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                      ("header_bounds", "7 -6 19 0"), ("footer_bounds", "7 150 19 156")], [])
    rows.append((f"{base}/op_max_players", None))
    # GAME: OpenH1 at its tick rate, or Legacy (menu_functions.c's
    # server_settings_update), each choice its help
    extra += _widget(f"{base}/op_game_mode", [("width", 512), ("height", 28),
                                              ("flags", "pass_unhandled_to_focused_child"),
                                              ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                     [f'<child widget="{base}/game_mode_label"/>',
                      f'<child widget="{base}/game_mode_spinner" x="296" y="1"/>'])
    extra += _widget(f"{base}/game_mode_label", [("type", "text"), ("controller", 1), ("width", 280),
                                                 ("height", 22), ("string_list", f"{base}/labels"),
                                                 ("string_index", 12 + len(COOP_SETUP_SCREENS)),
                                                 ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13),
                                                 ("text_y", 4)], [])
    extra += _widget(f"{base}/game_mode_spinner",
                     [("type", "spinner"), ("left", 3), ("top", 2), ("width", 195), ("height", 20),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                      ("strings", "|".join(name for name, _ in GAME_MODES)), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("align", "center"), ("text_y", 1),
                      ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                      ("header_bounds", "7 -6 19 0"), ("footer_bounds", "7 198 19 204")],
                     ['<on event="left_mouse" run="mouse spinner 1wide click"/>'])
    rows.append((f"{base}/op_game_mode", None))
    extra += _value_row(base, "invite", 2, "ss copy invite")
    rows.append((f"{base}/op_invite", None))
    # (PUBLIC or PRIVATE: menu_functions.c's server_settings_update)
    extra += _widget(f"{base}/op_listing", [("width", 512), ("height", 28),
                                            ("flags", "pass_unhandled_to_focused_child"),
                                            ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                     [f'<child widget="{base}/listing_label"/>',
                      f'<child widget="{base}/listing_spinner" x="320" y="1"/>'])
    extra += _widget(f"{base}/listing_label", [("type", "text"), ("controller", 1), ("width", 300),
                                               ("height", 22), ("string_list", f"{base}/labels"),
                                               ("string_index", 9), ("font", "ui\\large_ui"),
                                               ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    extra += _widget(f"{base}/listing_spinner",
                     [("type", "spinner"), ("left", 3), ("top", 2), ("width", 147), ("height", 20),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                      ("strings", "PUBLIC|PRIVATE"), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("align", "center"), ("text_y", 1),
                      ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                      ("header_bounds", "7 -6 19 0"), ("footer_bounds", "7 150 19 156")],
                     ['<on event="left_mouse" run="mouse spinner 1wide click"/>'])
    rows.append((f"{base}/op_listing", None))
    # PASSWORD (a public internet game's: menu_functions.c's server_settings_update),
    # which players from the server browser must type
    extra += _value_row(base, "password", 10 + len(COOP_SETUP_SCREENS), "ss edit server password")
    rows.append((f"{base}/op_password", None))
    # the bots (a game of this machine alone's, in GAME's, INVITE LINK's and
    # LISTING's places: _bots_rows)
    bots_rows, bots_extra = _bots_rows(base, 13 + len(COOP_SETUP_SCREENS))
    rows += bots_rows
    extra += bots_extra
    # MAP SHARING (map_share.c), a screen of its own as co-op's options are,
    # for any game: SETUP_OPTIONS_PLACE below it
    key, _, _, _ = MAP_SHARE_SETUP
    target = f"{base}/{key}/{key}_screen"
    extra += _widget(f"{base}/op_{key}", [("width", 512), ("height", 28),
                                         ("flags", "pass_unhandled_to_focused_child"),
                                         ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                     [f'<on event="a" open="{target}"/>', f'<on event="start" open="{target}"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>',
                      f'<child widget="{base}/{key}_label"/>'])
    extra += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1), ("width", 300), ("height", 22),
                                             ("string_list", f"{base}/labels"),
                                             ("string_index", 11 + len(COOP_SETUP_SCREENS)),
                                             ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13),
                                             ("text_y", 4)], [])
    rows.append((f"{base}/op_{key}", None, SETUP_OPTIONS_PLACE - 1))
    # co-op's options, in the place of the gametype's rows, which co-op hides
    # as multiplayer hides co-op's (menu_functions.c's server_settings_update):
    # rows opening their screens, as the gametype's options' do
    # (_setup_option_screen; COOP_SETUP_SCREENS)
    for index, (key, title, _, _) in enumerate(COOP_SETUP_SCREENS):
        target = f"{base}/{key}/{key}_screen"
        extra += _widget(f"{base}/op_{key}", [("width", 512), ("height", 28),
                                             ("flags", "pass_unhandled_to_focused_child"),
                                             ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         [f'<on event="a" open="{target}"/>', f'<on event="start" open="{target}"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          f'<child widget="{base}/{key}_label"/>'])
        extra += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1), ("width", 300), ("height", 22),
                                                 ("string_list", f"{base}/labels"), ("string_index", 10 + index),
                                                 ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13),
                                                 ("text_y", 4)], [])
        rows.append((f"{base}/op_{key}", None, SETUP_OPTIONS_PLACE + index))
    # the gametype's options for this game (the gametype editor's screens,
    # editing a copy of the gametype chosen: "port setup edit")
    for index, (key, screen) in enumerate(SETUP_OPTION_SCREENS):
        target = f"main_menu/settings_select/multiplayer_setup/{screen}"
        extra += _widget(f"{base}/op_{key}", [("width", 512), ("height", 28),
                                             ("flags", "pass_unhandled_to_focused_child"),
                                             ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         [f'<on event="a" run="port setup edit" open="{target}"/>',
                          f'<on event="start" run="port setup edit" open="{target}"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          f'<child widget="{base}/{key}_label"/>', f'<child widget="{base}/{key}_value" x="230" y="1"/>'])
        extra += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1), ("width", 230), ("height", 22),
                                                 ("string_list", f"{base}/labels"), ("string_index", 3 + index),
                                                 ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13),
                                                 ("text_y", 4)], [])
        extra += _widget(f"{base}/{key}_value", [("type", "text"), ("controller", 1), ("width", 280), ("height", 22),
                                                 ("font", "ui\\small_ui"), ("color", "#FF2896FF"), ("align", "center"),
                                                 ("text_y", 5), ("text_flags", "no_focus_test")], [])
        rows.append((f"{base}/op_{key}", None, SETUP_OPTIONS_PLACE + index))
    # BOTS (a network game's, after the gametype's rows: its bots, which the
    # host plays, on a screen of their own, BOT_SETUP), its value what they
    # are (menu_functions.c's server_settings_bots)
    key, _, _, _ = BOT_SETUP
    target = f"{base}/{key}/{key}_screen"
    extra += _widget(f"{base}/op_{key}", [("width", 512), ("height", 28),
                                         ("flags", "pass_unhandled_to_focused_child"),
                                         ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                     [f'<on event="a" open="{target}"/>', f'<on event="start" open="{target}"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>',
                      f'<child widget="{base}/{key}_label"/>', f'<child widget="{base}/{key}_value" x="230" y="1"/>'])
    extra += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1), ("width", 230), ("height", 22),
                                             ("string_list", f"{base}/labels"),
                                             ("string_index", 13 + len(COOP_SETUP_SCREENS) + len(BOTS_ROWS)),
                                             ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13),
                                             ("text_y", 4)], [])
    extra += _widget(f"{base}/{key}_value", [("type", "text"), ("controller", 1), ("width", 280), ("height", 22),
                                             ("font", "ui\\small_ui"), ("color", "#FF2896FF"), ("align", "center"),
                                             ("text_y", 5), ("text_flags", "no_focus_test")], [])
    rows.append((f"{base}/op_{key}", None, SETUP_OPTIONS_PLACE + len(SETUP_OPTION_SCREENS)))
    extra += _button(f"{base}/button_defaults", 3, [])
    extra += _widget(f"{base}/button_ok", [("type", "text"), ("width", 128), ("height", 24),
                                           ("bitmap", "bitmaps/text_button_background"), ("text", "START GAME"),
                                           ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"),
                                           ("text_y", 2)],
                     [f'<on event="a" run="ss start game" open="{MT}/lobby/lobby_screen"/>',
                      f'<on event="start" run="ss start game" open="{MT}/lobby/lobby_screen"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    extra += _strings(f"{base}/labels", ["GAME NAME:", "MAXIMUM PLAYERS:", "INVITE LINK:", "GAME TYPE:",
                                         "PLAYER OPTIONS:", "ITEM OPTIONS:", "VEHICLE OPTIONS:", "INDICATOR OPTIONS:",
                                         "TEAMPLAY OPTIONS:", "LISTING:",
                                         *(f"{title}:" for _, title, _, _ in COOP_SETUP_SCREENS), "PASSWORD:",
                                         f"{MAP_SHARE_SETUP[1]}:", "GAME:", *(label for _, label, _, _ in BOTS_ROWS),
                                         f"{BOT_SETUP[1]}:"])
    extra += _strings(f"{base}/help_strings", [
        "",
        "The name the game shows in the lists of games.\\nEnter changes it.",
        "The most players the game takes (up to 128).",
        "Players join this game by its invite link, or your\\nDiscord invite. Enter copies the link again.",
        "The gametype and its rules, for this game.",
        "Lives, health, shields and respawning, for this game.",
        "Weapons, grenades and starting equipment, for\\nthis game.",
        "Each team's vehicles and their respawn time, for\\nthis game.",
        "The motion tracker and nav points, for this game.",
        "Friendly fire and team balance, for this game.",
        # (LISTING's, by its choice)
        "Anyone can see and join your game: it is listed\\nin everyone's Server Browser.",
        "Only players with your invite link can join.",
        # (co-op's options' rows: COOP_SETUP_SCREENS)
        *(help_text for _, _, help_text, _ in COOP_SETUP_SCREENS),
        # (PASSWORD's)
        "Players from the Server Browser must type it to\\njoin (an invite link needs none). Enter sets it.",
        # (MAP SHARING's)
        MAP_SHARE_SETUP[2],
        # (GAME's, by its choice)
        *(help_text for _, help_text in GAME_MODES),
        # (the bots' rows': menu_functions.c's BOTS_HELP)
        *(help_text for _, _, _, help_text in BOTS_ROWS),
        # (a network game's BOTS row's, and its line for a gametype the bots
        # do not play: menu_functions.c's BOT_OPTIONS_HELP)
        BOT_SETUP[2],
        BOTS_HINT,
    ])
    lines = _screen(base, spec, rows, ["server settings update"],
                    ['<on event="created" run="server settings init"/>'], extra)
    # (the bots' line, below their rows: _bots_rows)
    lines = [line.replace(f'<child widget="{base}/options_menu"/>',
                          f'<child widget="{base}/options_menu"/>\n\t\t<child widget="{base}/bots_hint"/>')
             for line in lines]
    # (co-op's options' screens, map sharing's and the bots')
    for key, title, _, screen_rows in [*COOP_SETUP_SCREENS, MAP_SHARE_SETUP, BOT_SETUP]:
        lines += _setup_option_screen(f"{base}/{key}", key, screen_rows)
    # (the bots' screen under SERVER SETUP's title: a title of its own is a
    # picture ce_menus.py draws, TITLES)
    key = BOT_SETUP[0]
    lines = [line.replace(f'bitmap="{base}/{key}/header_{key}"', f'bitmap="{base}/header_server_settings"')
             for line in lines]
    # (no Defaults: the bar is START and CANCEL; B cancels the name's edit
    # first)
    lines = [line.replace(f'<child widget="{base}/button_defaults" y="1"/>', "") for line in lines]
    lines = [line.replace('<on event="b" back="true"/>', SCREEN_BACK[0])
             .replace('<on event="back" back="true"/>', SCREEN_BACK[1]) for line in lines]
    return lines


MAXIMUM_PLAYERS = [2, 4, 8, 12, 16, 24, 32, 48, 64, 96, 128]
# CO-OP OPTIONS' amounts of PER PLAYER (network.coop_enemies, percentages)
# and STATIC MULTIPLIER (network.coop_enemies_multiplier), as menu_functions.c's
# gametype_options have their values
COOP_ENEMIES_PERCENTAGES = [25, 50, 100, 150, 200]
COOP_ENEMIES_MULTIPLIERS = [2, 4, 8, 16, 32]
# co-op's Server Setup options, in screens of their own as the gametype's
# are (its rows: _server_settings), each its key, its title, its row's help
# and its rows (as TEAMPLAY_ROWS: each spinner's key, label, words and the
# help of each of them; menu_functions.c's gametype_options give their
# settings' values)
COOP_SETUP_SCREENS = [
    ("coop_options", "CO-OP OPTIONS", "Friendly fire, the enemies and player collisions,\\nfor this game.", [
        ("coop_friendly_fire", "FRIENDLY FIRE:", ["OFF", "ON", "SHIELD ONLY", "EXPLOSIVES ONLY"], [
            "Players can not be hurt by weapons and explosives\\nfired by the other players.",
            "Players can be hurt by weapons or explosives\\nfired by the other players.",
            "Damage from the other players will only reduce\\nshields. Health will be unaffected.",
            "Players can be hurt by damage from explosives\\nfired by the other players.",
        ]),
        ("coop_extra_enemies", "EXTRA ENEMIES:", ["PER PLAYER", "STATIC MULTIPLIER", "NONE"], [
            "Enemy squads grow with the players: by the value of\\n'Per Player' for each player past the first.",
            "Enemy squads use 'Multiplier', however many players\\nthere are.",
            "Enemy squads are as the campaign has them.",
        ]),
        ("coop_extra_pickups", "EXTRA PICKUPS:", ["PER PLAYER", "STATIC MULTIPLIER", "NONE"], [
            "Pickups grow with the players: by the value of\\n'Per Player' for each player past the first.",
            "Pickups use 'Multiplier', up to eight times as many,\\nhowever many players there are.",
            "Pickups are as the campaign has them.",
        ]),
        ("coop_extra_vehicles", "EXTRA VEHICLES:", ["PER PLAYER", "STATIC MULTIPLIER", "NONE"], [
            "Vehicles respawn after being driven a short distance\\nbased on the number of players: by two thirds the value of\\n'Per Player' for each player past the first.",
            "Vehicles respawn after being driven a short distance,\\nwith extra copies equal to ('Multiplier' - 1) * 2/3,\\nrounded down, however many players there are.",
            "Vehicles are as the campaign has them.",
        ]),
        ("coop_enemies_multiplier", "MULTIPLIER:", [f"{value}X" for value in COOP_ENEMIES_MULTIPLIERS],
         ["Static amounts for the options set to this mode.\\nPickups are capped at eight times as many."] * len(COOP_ENEMIES_MULTIPLIERS)),
        ("coop_enemies_per_player", "PER PLAYER:", [f"{value}%" for value in COOP_ENEMIES_PERCENTAGES],
         ["For each player past the first, options set to Per\\nPlayer get this much more (100%: as many again)."] *
         len(COOP_ENEMIES_PERCENTAGES)),
        ("coop_player_collisions", "PLAYER COLLISIONS:", ["ON", "OFF"], [
            "Players bump into each other, as in the campaign.",
            "Players walk through each other, so that no one\\nblocks a doorway. The AI's characters still block.",
        ]),
        # (network.coop_ally_friendly_fire: game_engine.c's
        # game_engine_friendly_damage)
        ("coop_ally_friendly_fire", "ALLY FRIENDLY FIRE:", ["ON", "OFF"], [
            "Players can hurt and kill their AI allies, as in\\nthe campaign.",
            "Players can not hurt their AI allies, so that no\\none can kill Keyes or the Marines a level needs.",
        ]),
    ]),
    ("voice_options", "VOICE AND VOTING", "Voice chat, text chat and votes to kick a player,\\nfor this game.",
     TEAMPLAY_ROWS),
]
# Server Setup's map sharing (port/linux/game/map_share.c), for any game,
# laid out as co-op's options are (its key, its title, its row's help and
# its rows: menu_functions.c's gametype_options give their settings' values)
MAP_SHARE_UPLOADS = (256, 512, 1024, 2048, 4096, 8192, 16384, 20480)
MAP_SHARE_SETUP = ("map_share_options", "MAP SHARING",
                   "Sending players who join without the game's\\nCustom Edition map the map, and how fast.", [
    ("map_share", "SHARE MAPS:", ["ON", "OFF"], [
        "Players who join without the map download it\\nfrom you and the other players.",
        "Players who join without the map cannot play.",
    ]),
    ("map_share_upload", "UPLOAD LIMIT:", [f"{kb} KB/S" if kb < 1024 else f"{kb // 1024} MB/S"
                                           for kb in MAP_SHARE_UPLOADS],
     ["The most you send of maps, so that the game\\nkeeps the rest of your upload."] * len(MAP_SHARE_UPLOADS)),
])
# Server Setup's bots in a network game (port/linux/game/bots.c: the host
# plays them, as players of machines of their own), laid out as co-op's
# options are (its key, its title, its row's help and its rows: menu_functions.c's
# gametype_options give their settings' values, bots.count, bots.skill,
# bots.teams, bots.make_room and bots.fill_to; FILL TO is shown with MAKE
# ROOM FOR PLAYERS on). A local game's are BOTS_ROWS, on Server Setup itself
BOT_FILLS = list(range(2, 17))
BOT_SETUP = ("bot_options", "BOTS", "Computer players you host for this game, and\\nwhether they leave as players join.", [
    ("bot_count", "BOTS:", ["NONE", *(str(count) for count in range(1, 16))],
     ["No bots in this game."] + ["The most bots that play in this game. Your\\ncomputer plays them."] * 15),
    ("bot_skill", "BOT SKILL:", ["EASY", "NORMAL", "HEROIC", "LEGENDARY"],
     ["How well the bots play: how soon they react,\\nhow well they aim, how far they see."] * 4),
    ("bot_teams", "BOT TEAMS:", ["EVEN", "AGAINST YOU"], [
        "In a team game, bots go on both teams, evening\\nthem with the players.",
        "In a team game, every bot goes on the team you\\nare not on.",
    ]),
    ("bot_make_room", "MAKE ROOM:", ["ON", "OFF"], [
        "A bot leaves as a player joins, and comes back\\nas a player leaves.",
        "The bots stay. MAXIMUM PLAYERS counts the\\nplayers alone.",
    ]),
    ("bot_fill_to", "FILL TO:", [str(count) for count in BOT_FILLS],
     ["Bots fill the game up to this many players and\\nbots (no more than MAXIMUM PLAYERS)."] * len(BOT_FILLS)),
])
# (Server Setup's place of its options' rows, co-op's or the gametype's: after
# MAP SHARING's)
SETUP_OPTIONS_PLACE = 7
# (their screens' titles)
TITLES.update({f"{MT}/server_settings/{key}/header_{key}": title
               for key, title, _, _ in [*COOP_SETUP_SCREENS, MAP_SHARE_SETUP]})
# (the rows of a co-op options screen in one place, the one shown: PER
# PLAYER's and MULTIPLIER's, by EXTRA ENEMIES')
SETUP_OPTION_SAME_PLACE = set()


def _setup_option_screen(base: str, key: str, rows: list) -> list:
    """a screen of options laid out as the gametype's are (Teamplay
    Options'), its spinners setting config.toml's settings (menu_functions.c's
    "port setup options init" and "port setup options save", OK), each
    choice's help shown as the gametype's are ("game settings lists text
    update": the help strings, each spinner's choices' in turn)"""
    lines = _widget(f"{base}/{key}_screen", [("width", 640), ("height", 480),
                                            ("flags", "pass_unhandled_to_focused_child"),
                                            ("bitmap", "bitmaps/gradient")],
                    [f'<child widget="{base}/header_{key}"/>', f'<child widget="{base}/{key}_menu"/>'])
    lines += _header(f"{base}/header_{key}", f"{base}/header_{key}")
    lines += _widget(f"{base}/{key}_help", [("type", "text"), ("controller", 1), ("left", 68), ("top", 341),
                                           ("width", 482), ("height", 79), ("string_list", f"{base}/cap_{key}"),
                                           ("font", "ui\\large_ui"), ("color", "#FFFFFFFF")], [])
    children = ['<data input="game settings lists text update"/>',
                '<on event="created" run="port setup options init"/>']
    place = -1
    for row_key, *_ in rows:
        if row_key not in SETUP_OPTION_SAME_PLACE:
            place += 1
        children.append(f'<child widget="{base}/op_{row_key}" x="54" y="{73 + 30 * place}"/>')
    children.append(f'<child widget="{base}/{key}_button_bar" y="414"/>')
    lines += _widget(f"{base}/{key}_menu", [("type", "column_list"), ("width", 640), ("height", 480),
                                           ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                                           ("description", f"{base}/{key}_help")], children)
    lines += _widget(f"{base}/{key}_button_bar", [("type", "column_list"), ("width", 640), ("height", 28),
                                                 ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                     ['<data input="common button bar update"/>', f'<child widget="{base}/{key}_button_ok" x="380" y="1"/>',
                      '<child widget="common_button_cancel" x="510" y="1"/>'])
    lines += _button(f"{base}/{key}_button_ok", 1, ['<on event="a" run="port setup options save" back="true"/>',
                                                     '<on event="start" run="port setup options save" back="true"/>'])
    for index, (row_key, label, strings, _) in enumerate(rows):
        lines += _widget(f"{base}/op_{row_key}", [("width", 512), ("height", 28),
                                                 ("flags", "pass_unhandled_to_focused_child"),
                                                 ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         [f'<child widget="{base}/{row_key}_label"/>',
                          f'<child widget="{base}/{row_key}_spinner" x="286" y="1"/>'])
        lines += _widget(f"{base}/{row_key}_label", [("type", "text"), ("controller", 1), ("width", 300),
                                                     ("height", 22), ("string_list", f"{base}/{key}_labels"),
                                                     ("string_index", index), ("font", "ui\\large_ui"),
                                                     ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
        lines += _widget(f"{base}/{row_key}_spinner",
                         [("type", "spinner"), ("top", 2), ("width", 206), ("height", 20),
                          ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                          ("string_list", f"{base}/var_{row_key}"), ("font", "ui\\large_ui"),
                          ("color", "#FF2896FF"), ("align", "center"), ("text_y", 4),
                          ("list_flags", "items_from_strings"),
                          ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                          ("header_bounds", "7 -13 19 -7"), ("footer_bounds", "7 208 19 214")],
                         ['<on event="left_mouse" run="mouse spinner 1wide click"/>'])
        lines += _strings(f"{base}/var_{row_key}", strings)
    lines += _strings(f"{base}/{key}_labels", [label for _, label, *_ in rows])
    lines += _strings(f"{base}/cap_{key}", [text for *_, helps in rows for text in helps])
    return lines
# Server Setup's rows of the gametype's options: the gametype editor's
# screens
SETUP_OPTION_SCREENS = [
    ("game_type", "playlist_edit/game_type_select/gametype_select_screen"),
    ("player_options", "player_options_edit/player_options_screen"),
    ("item_options", "item_options_edit/item_options_screen"),
    ("vehicle_options", "vehicle_options_edit/vehicle_options_screen"),
    ("indicator_options", "indicator_options_edit/indicator_options_screen"),
    ("team_options", "teamplay_options_edit/teamplay_options_screen"),
]


def lobby_column_x(column: int) -> int:
    """a team's column's left (red's, blue's)"""
    return LOBBY_LIST_X + column * (LOBBY_COLUMN_WIDTH + LOBBY_COLUMN_GAP)


def lobby_column_wide(column: int, dx: int = 0, dw=None):
    """a team's column's part (or a part of it, dx along) on a wide screen:
    each column half of the players' growth, blue's moved by red's"""
    return wide(-WIDE_HALF + column * (LOBBY_WIDE_PLAYERS // 2) + dx, LOBBY_WIDE_PLAYERS // 2 if dw is None else dw)


def _lobby_board(base: str) -> tuple:
    """the lobby on the computer (lobby_menu.inc's lobby_board_update), in
    Multiplayer's look (the menus' shared parts): the game's name, where it
    is and its players over the panels; the players from the screen's left,
    in a column for each team (one list, both columns', in a game without
    teams), each a row of its dot, name and tag, this machine's own framed;
    the details panel beside them to its right: the next game (its gametype,
    map and limits; OPENH1 or LEGACY, its tick rate, whether OpenCE's
    players join), a game of this machine alone's bots (Server Setup's
    BOTS_ROWS, here too), how the game is (the countdown, how another
    player joins), and the host's GAME SETTINGS (Server Setup) and START
    GAME, or WAITING FOR THE HOST; the footer's key prompts. Left and right
    (and a click on a column) change this player's team. Returns the
    screen's children and the widgets (the players' the touchscreen's too)"""
    top, height = LOBBY_TOP, LOBBY_HEIGHT
    panel_left, _, panel_width, _ = LOBBY_PANEL
    inner_left, inner = panel_left + 14, LOBBY_PANEL_INNER
    players_width = 2 * LOBBY_COLUMN_WIDTH + LOBBY_COLUMN_GAP
    panel_moved, panel_grown = LOBBY_WIDE_PANEL
    row_w, row_h = LOBBY_ROW
    pointer = ' platform="pointer"'
    children = [f'<child widget="{base}/board_header"/>', f'<child widget="{base}/board_subtitle"/>']
    lines = _widget(f"{base}/board_header", [("controller", 1), ("left", 35), ("top", 11), ("width", 363),
                                             ("height", 59), ("bitmap", f"{base}/header_lobby"), ("wide", WIDE_LEFT)],
                    [])
    lines += _part_text(f"{base}/board_subtitle", [("left", 300), ("top", 50), ("width", 312), ("height", 18),
                                                   ("font", "ui\\small_ui"), ("color", PART_DIM), ("align", "right"),
                                                   ("wide", WIDE_RIGHT)])
    # (the teams' panels, or one for a game without teams, each its title
    # and how many it has)
    for column, (words, (color, _)) in enumerate(zip(("RED", "BLUE"), LOBBY_TEAM_COLORS)):
        x = lobby_column_x(column)
        children += [f'<child widget="{base}/board_team_{column}" x="{x}" y="{top}"/>',
                     f'<child widget="{base}/board_title_{column}" x="{x + 12}" y="{top + 8}"/>']
        lines += _widget(f"{base}/board_team_{column}", [("controller", 1), ("width", LOBBY_COLUMN_WIDTH),
                                                         ("height", height),
                                                         ("bitmap", part_team_panel(LOBBY_COLUMN_WIDTH, height)),
                                                         ("wide", lobby_column_wide(column))], [])
        lines += _widget(f"{base}/board_title_{column}", [("type", "text"), ("controller", 1), ("width", 100),
                                                          ("height", 22), ("text", words), ("font", "ui\\large_ui"),
                                                          ("color", "#FF" + color[1:]),
                                                          ("wide", lobby_column_wide(column, dw=0)),
                                                          ("text_flags", "no_focus_test")], [])
    children += [f'<child widget="{base}/board_all" x="{LOBBY_LIST_X}" y="{top}"/>',
                 f'<child widget="{base}/board_title_all" x="{LOBBY_LIST_X + 12}" y="{top + 8}"/>']
    lines += _widget(f"{base}/board_all", [("controller", 1), ("width", players_width), ("height", height),
                                           ("bitmap", part_panel(players_width, height)),
                                           ("wide", wide(-WIDE_HALF, LOBBY_WIDE_PLAYERS))], [])
    lines += _widget(f"{base}/board_title_all", [("type", "text"), ("controller", 1), ("width", 100), ("height", 22),
                                                 ("text", "PLAYERS"), ("font", "ui\\large_ui"),
                                                 ("color", SETTINGS_BLUE), ("wide", WIDE_LEFT),
                                                 ("text_flags", "no_focus_test")], [])
    # (the counts at the columns' right; a game without teams' at blue's,
    # its panel's)
    for column in range(2):
        count_x = lobby_column_x(column) + LOBBY_COLUMN_WIDTH - 12 - 90
        children.append(f'<child widget="{base}/board_count_{column}" x="{count_x}" y="{top + 12}"/>')
        lines += _part_text(f"{base}/board_count_{column}", [("width", 90), ("height", 16), ("font", "ui\\small_ui"),
                                                            ("color", PART_DIM), ("align", "right"),
                                                            ("wide", lobby_column_wide(column, LOBBY_WIDE_PLAYERS // 2,
                                                                                       0))])
    # (each column's rows: a player's dot, name and tag, or how many more
    # there are than the rows)
    name_w = row_w - 22 - LOBBY_TAG_WIDTH - LOBBY_TAG_INSET
    for column in range(2):
        for row in range(LOBBY_ROWS):
            key = f"{column}_{row}"
            children.append(f'<child widget="{base}/board_row_{key}" x="{lobby_column_x(column) + 8}" '
                            f'y="{top + LOBBY_ROWS_TOP + LOBBY_ROW_STEP * row}"/>')
            lines += _widget(f"{base}/board_row_{key}", [("controller", 1), ("width", row_w), ("height", row_h),
                                                         ("bitmap", part_player_row(row_w, row_h)),
                                                         ("wide", lobby_column_wide(column))],
                             [f'<child widget="{base}/board_dot_{key}" x="8" y="{(row_h - LOBBY_DOT) // 2}"/>',
                              f'<child widget="{base}/board_name_{key}" x="22"/>',
                              f'<child widget="{base}/board_tag_{key}" x="{row_w - LOBBY_TAG_WIDTH - LOBBY_TAG_INSET}"/>'])
            lines += _widget(f"{base}/board_dot_{key}", [("controller", 1), ("width", LOBBY_DOT),
                                                         ("height", LOBBY_DOT), ("bitmap", part_dot(LOBBY_DOT))], [])
            lines += _part_text(f"{base}/board_name_{key}", [("width", name_w), ("height", row_h),
                                                             ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"),
                                                             ("text_y", 2),
                                                             ("wide", wide(0, LOBBY_WIDE_PLAYERS // 2))])
            lines += _part_text(f"{base}/board_tag_{key}", [("width", LOBBY_TAG_WIDTH), ("height", row_h),
                                                            ("font", "ui\\small_ui"), ("color", PART_DIM),
                                                            ("align", "right"), ("text_y", 2),
                                                            ("wide", wide(LOBBY_WIDE_PLAYERS // 2))])
    # (the details panel: its words as much wider as it, moved with it)
    grown = wide(0, panel_grown)
    panel = []

    def text(name, y, h, font, color, words=None, width=inner, align=None, wide_layout=grown):
        panel.append(f'<child widget="{base}/{name}" x="{inner_left}" y="{top + y}"/>')
        pairs = [("width", width), ("height", h), ("font", font), ("color", color), ("align", align),
                 ("wide", wide_layout)]
        return (_widget(f"{base}/{name}", [("type", "text"), ("controller", 1), *pairs, ("text", words),
                                           ("text_flags", "no_focus_test")], [])
                if words else _part_text(f"{base}/{name}", pairs))

    lines += text("board_next_label", 12, 16, "ui\\small_ui", PART_DIM, "NEXT GAME", wide_layout=None)
    lines += text("board_gametype", 28, 22, "ui\\large_ui", "#FFFFFFFF")
    lines += text("board_map", 52, 16, "ui\\small_ui", SETTINGS_BLUE)
    lines += text("board_limits", 68, 16, "ui\\small_ui", SETTINGS_BLUE)
    for name, x, y, width in (("board_kind", 0, 90, BROWSER_PILL), ("board_tick", BROWSER_PILL + 4, 90, BROWSER_PILL),
                              ("board_opence", 0, 110, BROWSER_WIDE_PILL)):
        panel.append(f'<child widget="{base}/{name}" x="{inner_left + x}" y="{top + y}"/>')
        lines += _widget(f"{base}/{name}", [("type", "text"), ("controller", 1), ("width", width),
                                           ("height", PART_PILL_HEIGHT), ("bitmap", part_pill(width)),
                                           ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"),
                                           ("text_flags", "no_focus_test")], [])
    panel.append(f'<child widget="{base}/board_section_bots" x="{inner_left}" y="{top + 134}"/>')
    lines += _section(f"{base}/board_section_bots", "BOTS", inner, grown)
    # (a network game's host: the game after it, its playlist's or a change
    # asked for, in the bots' place: lobby_board_update)
    lines += text("board_after_label", 134, 16, "ui\\small_ui", PART_DIM, "THEN", wide_layout=None)
    lines += text("board_after", 150, 16, "ui\\small_ui", SETTINGS_BLUE)
    # (how the game is: the countdown, waiting for players, how another
    # player joins; up in the bots' place when it has none:
    # lobby_board_update)
    lines += text("board_status", 226, 28, "ui\\small_ui", SETTINGS_BLUE)
    lines += text("board_waiting", 284, LOBBY_START[1], "ui\\large_ui", SETTINGS_BLUE, align="center")
    children.append(f'<child widget="{base}/board_panel"{pointer}/>')
    lines += _widget(f"{base}/board_panel", [("controller", 1), ("left", panel_left), ("top", top),
                                             ("width", panel_width), ("height", height),
                                             ("bitmap", part_panel(*LOBBY_PANEL[2:])),
                                             ("wide", wide(*LOBBY_WIDE_PANEL))], panel)
    # (what takes the focus: the players (left and right their team, a
    # click a column's; A or Start the host's start), the bots' rows, GAME
    # SETTINGS, START GAME and the footer's prompts)
    controls = wide(panel_moved, panel_grown)
    start = '<on event="start" run="port lobby start"/>'
    items = [f'<child widget="{base}/board_players" x="{LOBBY_LIST_X}" y="{top}"/>']
    lines += _widget(f"{base}/board_players", [("width", players_width), ("height", height),
                                               ("wide", wide(-WIDE_HALF, LOBBY_WIDE_PLAYERS))],
                     ['<on event="left" run="port lobby team red"/>', '<on event="right" run="port lobby team blue"/>',
                      '<on event="a" run="port lobby team click"/>', start,
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    labels = ("COUNT", "SKILL", "TEAMS")
    for index, ((key, _, choices, _), label) in enumerate(zip(BOTS_ROWS, labels)):
        items.append(f'<child widget="{base}/board_{key}" x="{inner_left}" '
                     f'y="{top + 156 + LOBBY_CONTROL_HEIGHT * index}"/>')
        spinner_x, spinner_w = 70, 84
        lines += _widget(f"{base}/board_{key}", [("width", inner), ("height", LOBBY_CONTROL_HEIGHT),
                                                 ("flags", "pass_unhandled_to_focused_child"),
                                                 ("bitmap", part_highlight(inner, LOBBY_CONTROL_HEIGHT)),
                                                 ("wide", controls)],
                         [f'<child widget="{base}/board_{key}_label" x="8"/>',
                          f'<child widget="{base}/board_{key}_spinner" x="{spinner_x}"/>'])
        lines += _widget(f"{base}/board_{key}_label", [("type", "text"), ("controller", 1), ("width", 60),
                                                       ("height", LOBBY_CONTROL_HEIGHT), ("text", label),
                                                       ("font", "ui\\small_ui"), ("color", SETTINGS_BLUE),
                                                       ("text_y", 4)], [])
        lines += _widget(f"{base}/board_{key}_spinner",
                         [("type", "spinner"), ("top", 1), ("width", spinner_w), ("height", 20),
                          ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                          ("strings", "|".join(shown for shown, _ in choices)),
                          ("setting", f"bots.{key.split('_', 1)[1]}"),
                          ("values", "|".join(value for _, value in choices)), ("font", "ui\\small_ui"),
                          ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 3),
                          ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                          ("header_bounds", "7 -6 19 0"),
                          ("footer_bounds", f"7 {spinner_w + 3} 19 {spinner_w + 9}"),
                          ("wide", wide(panel_grown))],
                         ['<on event="created" run="port setting load"/>', start,
                          '<on event="left_mouse" run="mouse spinner 1wide click"/>'])
    items += [f'<child widget="{base}/board_host" x="{inner_left}" y="{top + 176}"/>',
              f'<child widget="{base}/board_settings" x="{inner_left}" y="{top + 256}"/>',
              f'<child widget="{base}/board_start" x="{inner_left}" y="{top + 284}"/>',
              f'<child widget="{base}/board_prompts" y="414"/>']
    # (a network game's host: Host Controls over the lobby, its players,
    # map and gametype, playlist and bans before the game starts)
    lines += _widget(f"{base}/board_host", [("type", "text"), ("width", inner), ("height", LOBBY_CONTROL_HEIGHT),
                                            ("bitmap", part_outline(inner, LOBBY_CONTROL_HEIGHT)),
                                            ("wide", controls), ("text", "HOST CONTROLS"),
                                            ("font", "ui\\small_ui"), ("color", "#FFCFE3FF"), ("align", "center"),
                                            ("text_y", 4)],
                     [f'<on event="a" run="port host open" open="{HOST_CONTROLS}/host_controls_screen" '
                      'branch="true"/>', start, '<on event="left_mouse" run="mouse emit accept event"/>'])
    lines += _widget(f"{base}/board_settings", [("type", "text"), ("width", inner), ("height", LOBBY_CONTROL_HEIGHT),
                                                ("bitmap", part_outline(inner, LOBBY_CONTROL_HEIGHT)),
                                                ("wide", controls), ("text", "GAME SETTINGS"),
                                                ("font", "ui\\small_ui"), ("color", "#FFCFE3FF"), ("align", "center"),
                                                ("text_y", 4)],
                     ['<on event="a" run="port lobby settings"/>', start,
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    # (its words the game's: START GAME, or the countdown; from the left, as
    # a key's picture in them is not measured with them)
    lines += _widget(f"{base}/board_start", [("type", "text"), ("width", LOBBY_START[0]), ("height", LOBBY_START[1]),
                                             ("bitmap", part_button(*LOBBY_START)), ("wide", controls),
                                             ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("text_x", 12),
                                             ("text_y", 6)],
                     ['<on event="a" run="port lobby start"/>', start,
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    children.append(f'<child widget="{base}/board_list"{pointer}/>')
    lines += _widget(f"{base}/board_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                            ("flags", "pass_unhandled_to_focused_child up_down_tabs_items")],
                     ['<data input="net splitscreen prejoin players"/>', '<data input="port lobby board update"/>',
                      *items])
    # (CHANGE TEAM's keys the arrows, the keyboard's and the D-pad's alike:
    # the spinners' arrows before its words)
    team_prompt = LOBBY_PROMPTS[0]
    lines += _prompts(base, "board_prompts", LOBBY_PROMPTS, defined=(team_prompt[0],))
    _, words, x, width, handler = team_prompt
    lines += _widget(f"{base}/{team_prompt[0]}", [("type", "text"), ("width", width), ("height", PART_PROMPT_HEIGHT),
                                                  ("wide", prompt_wide(x, width)),
                                                  ("bitmap", part_highlight(width, PART_PROMPT_HEIGHT)),
                                                  ("text", words), ("font", "ui\\small_ui"), ("color", PART_TEXT),
                                                  ("text_x", 30), ("text_y", 4)],
                     [handler, handler.replace('event="a"', 'event="start"'),
                      '<on event="left_mouse" run="mouse emit accept event"/>',
                      f'<child widget="{base}/prompt_team_left" x="8" y="5"/>',
                      f'<child widget="{base}/prompt_team_right" x="18" y="5"/>'])
    for way in ("left", "right"):
        lines += _widget(f"{base}/prompt_team_{way}", [("width", 6), ("height", 12),
                                                      ("bitmap", f"bitmaps/arrow_sm_{way}")], [])
    return children, lines


def _lobby() -> list:
    """the lobby the host's and the joining players wait in (the Xbox's
    pregame's functions, which need no widget of theirs): up to the port's
    128 players, scrolling; the game's map and gametype; the countdown"""
    base = f"{MT}/lobby"
    # (split screen: another controller's START joins it, its B leaves alone:
    # menu_functions.c's lobby_join. After a game it is the only screen,
    # network_game_reset_to_pregame_ui opening it in place of the Xbox's
    # pregame: leaving it goes to the main menu, as the PC version's postgame
    # screens do)
    # (its players in a column for each team and the details panel,
    # _lobby_board; the touchscreen's panel and buttons, _touch_lobby_board)
    board_children, board = _lobby_board(base)
    lines = _widget(f"{base}/lobby_screen", [("width", 640), ("height", 480),
                                             ("flags", "pass_unhandled_to_focused_child main_menu_if_no_history"),
                                             ("bitmap", "bitmaps/gradient")],
                    ['<on event="created" run="port lobby open"/>',
                     '<on event="created" run="net server accept conx"/>',
                     '<on event="created" run="net server allow start"/>',
                     '<on event="b" run="port lobby leave" back="true"/>',
                     '<on event="back" run="port lobby leave" back="true"/>',
                     f'<on event="start" run="port lobby join" open="{base}/player_profile_screen" branch="true"/>',
                     '<on event="x" run="port lobby add player"/>',
                     '<on event="y" run="port lobby invite"/>',
                     *board_children, *_touch_lobby_board(base)[0]])
    lines += board
    lines += _header(f"{base}/header_lobby", f"{base}/header_lobby")
    lines += _widget(f"{base}/lobby_join_help", [("type", "text"), ("controller", 1), ("left", 355), ("top", 446),
                                                 ("width", 275), ("height", 24), ("font", "ui\\small_ui"),
                                                 ("color", "#FF2896FF"), ("align", "right"), ("text_y", 5),
                                                 ("text_flags", "no_focus_test")], [])
    # (the panel of a game under way's lobby, before joining it: below)
    lines += _widget(f"{base}/lobby_desc", [("width", 640), ("height", 480)],
                     ['<child widget="main_menu/current_profile_name"/>',
                      f'<child widget="{base}/lobby_right_item" x="22" y="2"/>'])
    lines += _widget(f"{base}/lobby_right_item", [("controller", 1), ("left", 406), ("top", 75), ("width", 162),
                                                  ("height", 326),
                                                  ("bitmap", "bitmaps/spinner_list_3_wide_item_background")],
                     # (a campaign level's picture and name in place of the
                     # map's, for network co-op: menu_functions.c's
                     # lobby_map_show)
                     [f'<child widget="{base}/lobby_map_pic"/>', f'<child widget="{base}/lobby_map_name"/>',
                      '<child widget="main_menu/solo_level_select/replay_level_right_pic"/>',
                      '<child widget="main_menu/solo_level_select/replay_level_right_name"/>',
                      f'<child widget="{base}/lobby_game_data"/>'])
    lines += _widget(f"{base}/lobby_map_pic", [("controller", 1), ("left", 419), ("top", 87), ("width", 140),
                                               ("height", 114), ("bitmap", "ui\\shell\\bitmaps\\mp_map_grafix")], [])
    lines += _widget(f"{base}/lobby_map_name", [("type", "text"), ("controller", 1), ("left", 417), ("top", 204),
                                                ("width", 146), ("height", 43), ("string_list", "main_menu/mp_map_list"),
                                                ("font", "ui\\large_ui"), ("color", "#FF2896FF")], [])
    lines += _widget(f"{base}/lobby_game_data", [("type", "text"), ("controller", 1), ("left", 417), ("top", 250),
                                                 ("width", 146), ("height", 144), ("font", "ui\\small_ui"),
                                                 ("color", "#FF2896FF")], [])
    # a split screen player's profile, chosen as they join the lobby (any
    # controller's presses: Co-op's rows; the host's countdown waits)
    lines += _widget(f"{base}/player_profile_screen", [("width", 640), ("height", 480),
                                                       ("flags", "pass_unhandled_to_focused_child"),
                                                       ("bitmap", "bitmaps/gradient")],
                     ['<on event="created" run="net server defer start"/>',
                      '<child widget="main_menu/new_select/sel_list_desc_bkd"/>',
                      f'<child widget="{base}/player_profile_list"/>',
                      f'<child widget="{base}/header_add_player"/>',
                      f'<child widget="{base}/player_profile_help" x="20" y="416"/>'])
    lines += _header(f"{base}/header_add_player", f"{base}/header_add_player")
    lines += _widget(f"{base}/player_profile_list",
                     [("type", "column_list"), ("width", 640), ("height", 480),
                      ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                      ("description", "main_menu/profile_manager/player_profile_extended_desc")],
                     ['<data input="3wide player profile list update"/>',
                      '<on event="created" run="port lobby player list initialize"/>',
                      '<on event="deleted" run="player profile list dispose"/>',
                      '<on event="custom_activation" run="port lobby player choose" back="true" branch="true"/>',
                      *[f'<child widget="{MT}/coop/list_item_{index}" x="20" y="{73 + 30 * index}"/>'
                        for index in range(11)],
                      f'<child widget="{MT}/coop/player_2_button_bar" y="414"/>'])
    lines += _widget(f"{base}/player_profile_help", [("type", "text"), ("width", 350), ("height", 24),
                                                     ("text", "The new player's profile."),
                                                     ("font", "ui\\small_ui"), ("color", "#FF2896FF"), ("text_y", 5),
                                                     ("text_flags", "no_focus_test")], [])
    # a game under way's lobby, before joining it (the browser's rows of
    # games in progress): what its advertisement tells, JOIN GAME. Split
    # screen players join here, as in the lobby, before JOIN GAME: the game
    # starts at once for the machine, with the players it brings
    lines += _widget(f"{base}/preview_screen", [("width", 640), ("height", 480),
                                                ("flags", "pass_unhandled_to_focused_child"),
                                                ("bitmap", "bitmaps/gradient")],
                     ['<on event="created" run="port lobby open"/>',
                      '<on event="b" run="port lobby preview leave" back="true"/>',
                      '<on event="back" run="port lobby preview leave" back="true"/>',
                      f'<on event="start" run="port lobby preview add" open="{base}/player_profile_screen" branch="true"/>',
                      '<child widget="main_menu/new_select/sel_list_desc_bkd"/>',
                      f'<child widget="{base}/preview_list"/>',
                      f'<child widget="{base}/header_lobby"/>',
                      f'<child widget="{base}/lobby_join_help"/>'])
    lines += _widget(f"{base}/preview_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                              ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                                              ("description", f"{base}/lobby_desc")],
                     ['<data input="port lobby preview update"/>',
                      f'<child widget="{base}/preview_status" x="30" y="75"/>',
                      f'<child widget="{base}/preview_button_bar" y="414"/>'])
    lines += _widget(f"{base}/preview_status", [("type", "text"), ("controller", 1), ("width", 360), ("height", 300),
                                                ("font", "ui\\large_ui"), ("color", "#FF2896FF")], [])
    lines += _widget(f"{base}/preview_button_bar", [("type", "column_list"), ("width", 640), ("height", 28),
                                                    ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                     [f'<child widget="{base}/preview_button_join" x="250" y="1"/>',
                      f'<child widget="{base}/preview_button_add" x="380" y="1"/>',
                      f'<child widget="{base}/preview_button_back" x="510" y="1"/>'])
    for key, caption, run in (("join", "JOIN GAME", "port lobby preview join"),
                              ("add", "ADD PLAYER", "port lobby add player"),
                              ("back", "BACK", "mouse emit back event")):
        lines += _widget(f"{base}/preview_button_{key}", [("type", "text"), ("width", 128), ("height", 24),
                                                         ("bitmap", "bitmaps/text_button_background"),
                                                         ("text", caption), ("font", "ui\\small_ui"),
                                                         ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)],
                         [f'<on event="a" run="{run}"/>', f'<on event="start" run="{run}"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>'])
    return lines


def _coop() -> list:
    """Co-op, the campaign for two players on this machine in split screen
    (the Xbox's Cooperative Play): Multiplayer's CO-OP CAMPAIGN, then player
    2's profile, chosen with player 2's controller (its rows take any
    controller's presses, as the shared rows do only controller 1's), then
    New Game's levels and difficulty, which either player's controller uses
    (ui_widget.c's widget_takes_events_of_controller; menu_functions.c's
    coop_begin)"""
    base = f"{MT}/coop"
    lines = _widget(f"{MT}/multiplayer_type_coop_item",
                    [("type", "text"), ("left", 51), ("width", 232), ("height", 32), ("bitmap", "bitmaps/list_item_bkd"),
                     ("string_list", f"{MT}/multiplayer_options"), ("string_index", 7), ("font", "ui\\large_ui"),
                     ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 5)],
                    [f'<on event="a" run="port coop begin" open="{base}/player_2_profile_screen"/>',
                     f'<on event="start" run="port coop begin" open="{base}/player_2_profile_screen"/>',
                     '<on event="left_mouse" run="mouse emit accept event"/>'])
    # (ONLINE CO-OP, under Create Game: hosting over the Internet, the map
    # list opening on the campaign's levels, menu_functions.c's
    # multiplayer_mode_set and map_list_initialize)
    lines += _widget(f"{MT}/multiplayer_type_online_coop_item",
                     [("type", "text"), ("left", 51), ("width", 232), ("height", 32), ("bitmap", "bitmaps/list_item_bkd"),
                      ("string_list", f"{MT}/multiplayer_options"), ("string_index", 8), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 5)],
                     ['<on event="a" run="join controller to mp game"/>',
                      f'<on event="a" run="mp type set mode" open="{MT}/connected/connected_map_select_wrapper"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    lines += _widget(f"{base}/player_2_profile_screen", [("width", 640), ("height", 480),
                                                         ("flags", "pass_unhandled_to_focused_child"),
                                                         ("bitmap", "bitmaps/gradient")],
                     ['<child widget="main_menu/new_select/sel_list_desc_bkd"/>',
                      f'<child widget="{base}/player_2_profile_list"/>',
                      f'<child widget="{base}/header_player_2"/>',
                      f'<child widget="{base}/player_2_help" x="20" y="416"/>'])
    lines += _header(f"{base}/header_player_2", f"{base}/header_player_2")
    rows = [f'<child widget="{base}/list_item_{index}" x="20" y="{73 + 30 * index}"/>' for index in range(11)]
    lines += _widget(f"{base}/player_2_profile_list",
                     [("type", "column_list"), ("width", 640), ("height", 480),
                      ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                      ("description", "main_menu/profile_manager/player_profile_extended_desc")],
                     ['<data input="3wide player profile list update"/>',
                      '<on event="created" run="port coop player 2 list initialize"/>',
                      '<on event="deleted" run="player profile list dispose"/>',
                      '<on event="custom_activation" run="port coop player 2" '
                      'open="main_menu/solo_level_select/solo_level_select_screen" branch="true"/>',
                      *rows, f'<child widget="{base}/player_2_button_bar" y="414"/>'])
    for index in range(11):
        lines += _widget(f"{base}/list_item_{index}",
                         [("width", 390), ("height", 28),
                          ("bitmap", "bitmaps/sel_list_item_bkd_top" if index == 0 else "bitmaps/sel_list_item_bkd"),
                          ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("align", "center"), ("text_y", 3)],
                         ['<on event="a" run="single prev cl item activated"/>',
                          '<on event="start" run="single prev cl item activated"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          '<child widget="main_menu/new_select/list_item_text" x="25"/>',
                          '<child widget="main_menu/new_select/list_item_arrows"/>'])
    lines += _widget(f"{base}/player_2_help", [("type", "text"), ("width", 350), ("height", 24),
                                               ("text", "Player 2: choose with your controller."),
                                               ("font", "ui\\small_ui"), ("color", "#FF2896FF"), ("text_y", 5),
                                               ("text_flags", "no_focus_test")], [])
    lines += _widget(f"{base}/player_2_button_bar", [("type", "column_list"), ("width", 640), ("height", 28),
                                                     ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                     ['<data input="common button bar update"/>',
                      '<child widget="main_menu/profile_manager/profile_manager_button_ok" x="380" y="1"/>',
                      '<child widget="common_button_back" x="510" y="1"/>'])
    return lines


# ---------- the bots (port/linux/game/bots.c): Multiplayer's PLAY WITH
# BOTS hosts a game of this machine alone (menu_functions.c's
# multiplayer_host), whose lobby the bots join; Server Setup has their rows
# in it (menu_functions.c's server_settings_bots), in the places of the
# network's rows, which it hides. Each row's key, its label, its setting's
# [(shown, value)] and its help (menu_functions.c's BOTS_HELP on)
BOTS_ROWS = [
    ("bots_count", "BOTS:", [("NONE", "0"), *((str(count), str(count)) for count in range(1, 16))],
     "How many computer players join the lobby, as\\nmany as the game has room for."),
    ("bots_skill", "BOT SKILL:", [("EASY", "easy"), ("NORMAL", "normal"), ("HEROIC", "heroic"),
                                  ("LEGENDARY", "legendary")],
     "How well the bots play: how soon they react,\\nhow well they aim, how far they see."),
    ("bots_teams", "BOT TEAMS:", [("EVEN", "even"), ("AGAINST YOU", "against")],
     "In a team game: EVEN puts bots on both teams,\\nAGAINST YOU all on the team you are not on."),
]
# (Server Setup's places of their rows: GAME's, INVITE LINK's and LISTING's;
# the line saying why a gametype has none, PASSWORD's, below them)
BOTS_FIRST_PLACE = 2
# (Server Setup's rows' step)
SERVER_SETTINGS_SPACING = 20
BOTS_HINT = "Bots play Slayer, SWAT, Infection and Gun Game only."


def _bots() -> list:
    """Multiplayer's PLAY WITH BOTS, under Create Game: a game of this
    machine alone, the map list then on (multiplayer maps only), as Create
    Game's others"""
    return _widget(f"{MT}/multiplayer_type_create_bots_item",
                   [("type", "text"), ("left", 51), ("width", 232), ("height", 32), ("bitmap", "bitmaps/list_item_bkd"),
                    ("string_list", f"{MT}/multiplayer_options"), ("string_index", 9), ("font", "ui\\large_ui"),
                    ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 5)],
                   ['<on event="a" run="join controller to mp game"/>',
                    f'<on event="a" run="mp type set mode" open="{MT}/connected/connected_map_select_wrapper"/>',
                    '<on event="left_mouse" run="mouse emit accept event"/>'])


def _bots_rows(base: str, first_label: int) -> tuple:
    """Server Setup's bots rows ((row, platform, place) and their widgets),
    each a spinner of its setting (as a settings screen's: "port setting
    load"), and the line below them for a gametype they do not play"""
    rows, extra = [], []
    for index, (key, _, choices, _) in enumerate(BOTS_ROWS):
        extra += _widget(f"{base}/op_{key}", [("width", 512), ("height", 28),
                                              ("flags", "pass_unhandled_to_focused_child"),
                                              ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         [f'<child widget="{base}/{key}_label"/>',
                          f'<child widget="{base}/{key}_spinner" x="320" y="1"/>'])
        extra += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1), ("width", 300),
                                                 ("height", 22), ("string_list", f"{base}/labels"),
                                                 ("string_index", first_label + index), ("font", "ui\\large_ui"),
                                                 ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
        extra += _widget(f"{base}/{key}_spinner",
                         [("type", "spinner"), ("left", 3), ("top", 2), ("width", 147), ("height", 20),
                          ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                          ("strings", "|".join(shown for shown, _ in choices)),
                          ("setting", f"bots.{key.split('_', 1)[1]}"),
                          ("values", "|".join(value for _, value in choices)), ("font", "ui\\large_ui"),
                          ("color", "#FF2896FF"), ("align", "center"), ("text_y", 1),
                          ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                          ("header_bounds", "7 -6 19 0"), ("footer_bounds", "7 150 19 156")],
                         ['<on event="created" run="port setting load"/>',
                          '<on event="left_mouse" run="mouse spinner 1wide click"/>'])
        rows.append((f"{base}/op_{key}", None, BOTS_FIRST_PLACE + index))
    extra += _widget(f"{base}/bots_hint", [("type", "text"), ("controller", 1), ("left", 67),
                                           ("top", 73 + SERVER_SETTINGS_SPACING * (BOTS_FIRST_PLACE + len(BOTS_ROWS)) + 4),
                                           ("width", 480), ("height", 22), ("text", BOTS_HINT),
                                           ("font", "ui\\small_ui"), ("color", "#FF2896FF"),
                                           ("text_flags", "no_focus_test")], [])
    return rows, extra


def _item_options_extras() -> list:
    """Item Options' rows of the port's: the map's weapons (YES or NO), and
    the loadout, CATEGORY (the weapon set) or CUSTOM (each player's primary
    and secondary weapons)"""
    base = "main_menu/settings_select/multiplayer_setup/item_options_edit"
    lines = []
    for key, strings, label in (("map_weapons", "var_map_weapons", 8), ("loadout", "var_loadout", 5),
                                ("primary_weapon", "var_loadout_weapon", 6),
                                ("secondary_weapon", "var_loadout_weapon", 7)):
        lines += _widget(f"{base}/op_{key}", [("width", 512), ("height", 28),
                                               ("flags", "pass_unhandled_to_focused_child"),
                                               ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         [f'<child widget="{base}/{key}_label"/>',
                          f'<child widget="{base}/{key}_spinner" x="286" y="1"/>'])
        lines += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1), ("width", 300), ("height", 22),
                                                   ("string_list", f"{base}/item_options_labels"),
                                                   ("string_index", label), ("font", "ui\\large_ui"),
                                                   ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
        lines += _widget(f"{base}/{key}_spinner",
                         [("type", "spinner"), ("top", 2), ("width", 206), ("height", 20),
                          ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                          ("string_list", f"{base}/{strings}"), ("font", "ui\\large_ui"), ("color", "#FF2896FF"),
                          ("align", "center"), ("text_y", 4), ("list_flags", "items_from_strings"),
                          ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                          ("header_bounds", "7 -13 19 -7"), ("footer_bounds", "7 208 19 214")],
                         ['<on event="left_mouse" run="mouse spinner 1wide click"/>'])
    lines += _strings(f"{base}/var_map_weapons", ["YES", "NO"])
    lines += _strings(f"{base}/var_loadout", ["CATEGORY", "CUSTOM"])
    lines += _strings(f"{base}/var_loadout_weapon", LOADOUT_WEAPONS)
    return lines


def _player_options_extras() -> list:
    """The host's aim-assist policy and shield effects, hidden while editing
    saved gametypes."""
    base = PLAYER_EDIT
    lines = []
    for key, label_index, words in (("aim_assist", 8, ["ALLOWED", "BLOCK M+KB"]),
                                    ("shield_effects", 9, SHIELD_EFFECTS)):
        lines += _widget(f"{base}/op_{key}", [("width", 512), ("height", 28),
            ("flags", "pass_unhandled_to_focused_child"), ("bitmap", "bitmaps/option_bkds"),
            ("color", "#FF2896FF")], [f'<child widget="{base}/{key}_label"/>',
            f'<child widget="{base}/{key}_spinner" x="286" y="1"/>'])
        lines += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1),
            ("width", 280), ("height", 22), ("string_list", f"{base}/player_options_labels"),
            ("string_index", label_index), ("font", "ui\\large_ui"), ("color", "#FF2896FF"),
            ("text_x", 13), ("text_y", 4)], [])
        lines += _widget(f"{base}/{key}_spinner", [("type", "spinner"), ("top", 2),
            ("width", 206), ("height", 20), ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
            ("string_list", f"{base}/var_{key}"), ("font", "ui\\large_ui"),
            ("color", "#FF2896FF"), ("align", "center"), ("text_y", 4),
            ("list_flags", "items_from_strings"), ("header_bitmap", "bitmaps/arrow_sm_left"),
            ("footer_bitmap", "bitmaps/arrow_sm_right"), ("header_bounds", "7 -13 19 -7"),
            ("footer_bounds", "7 208 19 214")], ['<on event="left_mouse" run="mouse spinner 1wide click"/>'])
    return lines + _strings(f"{base}/var_aim_assist", ["ALLOWED", "BLOCK M+KB"]) + \
        _strings(f"{base}/var_shield_effects", SHIELD_EFFECTS)


def _opence_options() -> list:
    """the gametype editor's OPENCE OPTIONS: its row, after the editor's own
    (as they are laid out), and its screen, as the gametype's options' are
    (_setup_option_screen), its spinners the gametype's (menu_functions.c's
    gametype_options), its header its words (no picture of its own)"""
    lines = _setup_option_screen(OPENCE_EDIT, "opence", OPENCE_ROWS)
    lines = [line.replace("port setup options init", "mp profile init opence options")
             .replace("port setup options save", "mp profile set opence options")
             .replace(f'bitmap="{OPENCE_EDIT}/header_opence"',
                      'type="text" text="OPENCE OPTIONS" font="ui\\large_ui" color="#FFFFFFFF" text_x="13" text_y="16"')
             for line in lines]
    lines += _widget(OPENCE_ITEM, [("type", "text"), ("left", 51), ("top", 309), ("width", 232), ("height", 32),
                                   ("bitmap", "bitmaps/list_item_bkd"), ("text", "OPENCE OPTIONS"),
                                   ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 5)],
                     [f'<on event="a" open="{OPENCE_EDIT}/opence_screen"/>',
                      f'<on event="start" open="{OPENCE_EDIT}/opence_screen"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    return lines


def _gametype_rules_extras(folder: str) -> list:
    """Capture the Flag's or King of the Hill's rows of the port's
    (GAMETYPE_RULES_EXTRAS), as the PC version's rows of the screen are"""
    screen = GAMETYPE_RULES_EXTRAS[folder]
    lines = []
    for index, (key, _, strings, _) in enumerate(screen["rows"]):
        name = f"{folder}/{screen['prefix']}{key}"
        lines += _widget(f"{name}_label", [("type", "text"), ("controller", 1), ("width", screen["label_width"]),
                                           ("height", 22), ("string_list", f"{folder}/{screen['labels']}"),
                                           ("string_index", screen["label_index"] + index),
                                           ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13),
                                           ("text_y", 4)], [])
        lines += _widget(f"{name}_spinner",
                         [("type", "spinner"), ("left", 2), ("top", 2), ("width", 148), ("height", 20),
                          ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                          ("string_list", f"{folder}/var_{key}"), ("font", "ui\\large_ui"), ("color", "#FF2896FF"),
                          ("align", "center"), ("text_y", screen["text_y"]), ("list_flags", "items_from_strings"),
                          ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                          ("header_bounds", "7 -6 19 0"), ("footer_bounds", "7 150 19 156")],
                         ['<on event="left_mouse" run="mouse spinner 1wide click"/>'])
        lines += _widget(f"{folder}/op_{key}", [("width", 512), ("height", 28),
                                                ("flags", "pass_unhandled_to_focused_child"),
                                                ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         [f'<child widget="{name}_label"/>', f'<child widget="{name}_spinner" x="320" y="1"/>'])
        lines += _strings(f"{folder}/var_{key}", strings)
    return lines


def _teamplay_options_extras() -> list:
    """Teamplay Options' rows of the port's: the gametype's FORCE TEAM
    SPAWNS (game_engine.c), in both copies, then the host's voice chat
    (port/linux/game/network_voice.c) and vote kicks (network_votekick.c),
    in Server Setup's copy only (menu_functions.c's gametype_options_init),
    kept in config.toml"""
    lines = []
    for index, (key, label, strings, _) in enumerate(TEAMPLAY_EXTRA_ROWS):
        lines += _widget(f"{TEAMPLAY_EDIT}/op_{key}", [("width", 512), ("height", 28),
                                                       ("flags", "pass_unhandled_to_focused_child"),
                                                       ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         [f'<child widget="{TEAMPLAY_EDIT}/{key}_label"/>',
                          f'<child widget="{TEAMPLAY_EDIT}/{key}_spinner" x="286" y="1"/>'])
        lines += _widget(f"{TEAMPLAY_EDIT}/{key}_label", [("type", "text"), ("controller", 1), ("width", 300),
                                                          ("height", 22),
                                                          ("string_list", f"{TEAMPLAY_EDIT}/teamplay_options_labels"),
                                                          ("string_index", 3 + index), ("font", "ui\\large_ui"),
                                                          ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
        lines += _widget(f"{TEAMPLAY_EDIT}/{key}_spinner",
                         [("type", "spinner"), ("top", 2), ("width", 206), ("height", 20),
                          ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                          ("string_list", f"{TEAMPLAY_EDIT}/var_{key}"), ("font", "ui\\large_ui"),
                          ("color", "#FF2896FF"), ("align", "center"), ("text_y", 4),
                          ("list_flags", "items_from_strings"),
                          ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                          ("header_bounds", "7 -13 19 -7"), ("footer_bounds", "7 208 19 214")],
                         ['<on event="left_mouse" run="mouse spinner 1wide click"/>'])
        lines += _strings(f"{TEAMPLAY_EDIT}/var_{key}", strings)
    return lines


def _map_kind() -> list:
    """the map lists' first row (New Game's and the Map screen's), as the
    gametype list's chooser: a spinner of SINGLEPLAYER or MULTIPLAYER maps,
    or the Custom Edition maps of the custom_maps folder of either kind
    (menu_functions.c's map lists), for either controller (split screen
    co-op's New Game takes both)"""
    lines = _widget(MAP_KIND_CHOOSER, [("width", 256), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                                       ("bitmap", "bitmaps/option_bkds_sm"), ("font", "ui\\large_ui"),
                                       ("color", "#FF2896FF"), ("align", "center"), ("text_y", 3)],
                    [f'<child widget="{MAP_KIND_CHOOSER}_spinner" x="9" y="2"/>'])
    # (wider than the gametype chooser's, for CUSTOM SINGLEPLAYER, its
    # arrows at the row's ends)
    lines += _widget(f"{MAP_KIND_CHOOSER}_spinner",
                     [("type", "spinner"), ("width", 238), ("height", 22), ("flags", "left_right_tabs_items"),
                      ("string_list", "main_menu/new_select/var_map_kinds"), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("align", "center"), ("text_y", 1), ("list_flags", "items_from_strings"),
                      ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                      ("header_bounds", "7 -6 19 0"), ("footer_bounds", "7 238 19 244")],
                     ['<on event="left_mouse" run="mouse spinner 1wide click"/>'])
    lines += _strings("main_menu/new_select/var_map_kinds",
                      ["SINGLEPLAYER", "MULTIPLAYER", "CUSTOM SINGLEPLAYER", "CUSTOM MULTIPLAYER"])
    return lines


def favorite_star_frames(icons: Path) -> list:
    """the browsers' star's frames, 16 pixels square (FAVORITE_STAR_STYLES):
    Lucide's star filled and stroked in each frame's colours, the first
    frame empty"""
    blank = '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24"/>\n'
    star = (icons / "star.svg").read_text()
    return [(blank, 16, 16)] + [
        (star.replace('fill="none"', f'fill="{fill}"').replace('stroke="currentColor"', f'stroke="{stroke}"')
         .replace('width="24"', 'width="16"').replace('height="24"', 'height="16"'), 16, 16)
        for fill, stroke in FAVORITE_STAR_STYLES[1:]]


def ping_bars_frames() -> list:
    """the ping bars' frames: three bars rising, those lit in the frame's
    colour, the rest dim"""
    frames = []
    for lit, color in enumerate(PING_BAR_COLORS):
        bars = []
        for bar in range(3):
            height = 6 + 4 * bar
            fill = color if color and bar < lit else "#2A4A6A"
            bars.append(f'<rect x="{3 + 7 * bar}" y="{16 - height}" width="5" height="{height}" rx="1" '
                        f'fill="{fill}"/>')
        frames.append(('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="16" viewBox="0 0 24 16">'
                       + "".join(bars) + "</svg>\n", 24, 16))
    return frames


def _multiplayer_status() -> list:
    """Multiplayer's panel: in the place of the PC version's pictures, what
    is live for the item chosen, a headline and a line below it (the games
    online, an invite link copied, the campaign's next level:
    menu_functions.c's multiplayer_status_update), over the item's
    description"""
    lines = _widget(f"{MT}/multiplayer_status", [("type", "text"), ("controller", 1), ("left", 290), ("top", 96),
                                                 ("width", 295), ("height", 30), ("font", "ui\\large_ui"),
                                                 ("color", "#FFFFFFFF"), ("text_flags", "no_focus_test")], [])
    lines += _widget(f"{MT}/multiplayer_status_detail", [("type", "text"), ("controller", 1), ("left", 290),
                                                         ("top", 128), ("width", 295), ("height", 150),
                                                         ("font", "ui\\large_ui"), ("color", "#FF2896FF"),
                                                         ("text_flags", "no_focus_test")], [])
    # (the Server Browser's: ONLINE NOW, its games and players in the
    # titles' type, and BEST FOR YOU, the games most worth joining)
    def text(name, left, top, width, height, font, color, align=None, value=None):
        return _widget(f"{MT}/{name}", [("type", "text"), ("controller", 1), ("left", left), ("top", top),
                                        ("width", width), ("height", height), ("text", value),
                                        ("font", font), ("color", color), ("align", align),
                                        ("text_flags", "no_focus_test")], [])
    lines += text("online_now_label", 290, 84, 200, 18, "ui\\small_ui", "#FF2896FF", value="ONLINE NOW")
    for count, left in (("games", 290), ("players", 436)):
        for digit in range(STATUS_DIGIT_PLACES):
            lines += _widget(f"{MT}/online_{count}_{digit}",
                             [("controller", 1), ("left", left + DIGIT_SIZE[0] * digit), ("top", 102),
                              ("width", DIGIT_SIZE[0]), ("height", DIGIT_SIZE[1]), ("bitmap", STATUS_DIGITS)], [])
    lines += text("online_games_label", 290, 120, 140, 20, "ui\\large_ui", "#FF2896FF")
    lines += text("online_players_label", 436, 120, 150, 20, "ui\\large_ui", "#FF2896FF")
    lines += text("best_label", 290, 146, 200, 18, "ui\\small_ui", "#FF2896FF", value="BEST FOR YOU")
    for row in range(BEST_GAMES):
        top = 164 + 38 * row
        lines += text(f"best_{row}_name", 290, top, 195, 20, "ui\\large_ui", "#FFFFFFFF")
        lines += text(f"best_{row}_about", 290, top + 18, 195, 18, "ui\\small_ui", "#FF2896FF")
        lines += text(f"best_{row}_players", 486, top + 6, 72, 20, "ui\\large_ui", "#FFFFFFFF", "right")
        lines += _widget(f"{MT}/best_{row}_ping", [("controller", 1), ("left", 564), ("top", top + 9), ("width", 24),
                                                   ("height", 16), ("bitmap", PING_BARS)], [])
    # (at the end of the items, beside their names, what is live for them:
    # the local games found, an invite link copied, in green; SERVER
    # BROWSER's name leaves no room, and the panel counts its games)
    for name, color in (("item_note", "#FF7FB2FF"), ("invite_note", "#FF5ED38C")):
        lines += _widget(f"{MT}/{name}", [("type", "text"), ("width", 226), ("height", 32),
                                          ("font", "ui\\small_ui"), ("color", color), ("align", "right"),
                                          ("text_y", 8), ("text_flags", "no_focus_test")], [])
    lines += _text_button(f"{MT}/multiplayer_button_refresh", "REFRESH",
                          ['<on event="a" run="port multiplayer refresh"/>',
                           '<on event="start" run="port multiplayer refresh"/>'])
    return lines


# ---------- the menus' shared parts, in Settings' look (its pictures' shapes
# and colours: settings_pictures): a strip of tabs (the current one framed,
# LB and RB, or Q and E, either side), a list's rows' highlight, the line
# under a section's or a list's column titles, a details panel of any size,
# pills (a game's kind, its tick rate), a large button, and a footer of key
# prompts (the HUD's button pictures, or the keyboard's keys while it plays:
# ui_widget.c's ui_widget_port_menu_key), each prompt a button the pointer
# clicks too. menu_functions.c's menu parts fill them in (menu_tabs_show,
# menu_text_fit, menu_text_box_wrap, menu_prompts_show). On a wide screen
# (wide: the wide layout) the tabs spread from its left edge to its right,
# a footer's prompts keep to its edges (those left of the middle to its
# left, the others to its right), and a section's title and line, a list's
# rows and its panel take the wide attribute their screen gives them. The
# server browser and Multiplayer use them; another screen can, by the same
# names
PARTS = "main_menu/parts"
PARTS_TAB = f"{PARTS}/tab"
PART_TEXT = "#FFCFE3FF"
PART_DIM = SETTINGS_DIM
PART_PROMPT_HEIGHT = 22
PART_PILL_HEIGHT = 16


def part_highlight(width: int, height: int) -> str:
    """a list item's highlight (nothing, and Settings' framed row while it
    has the focus: two frames, which the engine shows by the focus)"""
    return f"{PARTS}/item_{width}x{height}"


def part_row(width: int, height: int) -> str:
    """a row of a list whose functions choose its frame: nothing, focused,
    and the row chosen (brighter, as the current tab)"""
    return f"{PARTS}/row_{width}x{height}"


def part_line(width: int) -> str:
    return f"{PARTS}/line_{width}"


def part_section(width: int) -> str:
    """a section's title's picture: its line along its foot, under the
    title, as Settings' section"""
    return f"{PARTS}/section_{width}"


PART_SECTION_HEIGHT = 20


def part_panel(width: int, height: int) -> str:
    return f"{PARTS}/panel_{width}x{height}"


def part_pill(width: int) -> str:
    """a pill: an OpenH1 game's (blue), a Legacy game's (grey), and an
    outline, by the frame"""
    return f"{PARTS}/pill_{width}"


def part_button(width: int, height: int) -> str:
    return f"{PARTS}/button_{width}x{height}"


def part_outline(width: int, height: int) -> str:
    """a button drawn as its outline (the lobby's GAME SETTINGS), filled
    while it has the focus"""
    return f"{PARTS}/outline_{width}x{height}"


def part_team_panel(width: int, height: int) -> str:
    """a team's panel (the lobby's columns): red's edge, then blue's, by the
    frame"""
    return f"{PARTS}/team_panel_{width}x{height}"


def part_player_row(width: int, height: int) -> str:
    """a player's row (the lobby's): a player's, then this machine's own
    (framed), then its own while the players have the focus (brighter)"""
    return f"{PARTS}/player_row_{width}x{height}"


def part_dot(size: int) -> str:
    """a player's dot: red's, blue's, and a game without teams', by the
    frame"""
    return f"{PARTS}/dot_{size}"


# the browser's geometry, in the 640x480 menu units: the tabs (Settings'),
# the list's left and width, its rows, the details panel beside it, and each
# column's place and width in a row (menu_functions.c's browser_row_show
# places the tick badge and the lock after the name)
BROWSER_TABS = [("tab_all", "ALL"), ("tab_dedicated", "DEDICATED"), ("tab_openh1", "OPENH1"),
                ("tab_legacy", "LEGACY"), ("tab_coop", "CO-OP"), ("tab_favorites", "FAVORITES")]
BROWSER_LIST_X, BROWSER_LIST_WIDTH = 28, 404
BROWSER_HEADERS_TOP = 104
BROWSER_ROWS_TOP, BROWSER_ROW_HEIGHT, BROWSER_ROW_STEP, BROWSER_ROWS = 124, 18, 19, 14
BROWSER_PANEL = (440, 104, 172, 296)
BROWSER_COLUMNS = {
    "favorite": (6, 16), "server_name": (26, 110), "map": (140, 78), "type": (222, 60), "players": (284, 56),
    "ping": (344, 54),
}
# on a wide screen (wide): the list from the screen's left, BROWSER_WIDE_LIST
# wider, the Server, Map and Mode columns taking the room (each column's move
# and growth in a row), the panel beside it to the screen's right, wider by
# the rest
BROWSER_WIDE_LIST = 144
BROWSER_WIDE_COLUMNS = {
    "favorite": (0, 0), "server_name": (0, 64), "map": (64, 44), "type": (108, 36), "players": (144, 0),
    "ping": (144, 0),
}
BROWSER_WIDE_PANEL = (-WIDE_HALF + BROWSER_WIDE_LIST, WIDE_EXTRA - BROWSER_WIDE_LIST)
# (the star in a place of its own, in from the highlight's rounded end, and
# the ping's number as far in from its other end)
# the ping's column: its bars, then its milliseconds at its right (the bars
# left out for a number too wide for both: menu_functions.c's browser_row_show)
BROWSER_PING_BARS = 24
# (a title wider or narrower than its column: Players' ends where its
# column ends, with its numbers)
BROWSER_HEADER_WIDTHS = {"type": 40, "players": 70}
# the column titles: (widget, words, column)
BROWSER_HEADERS = [("op_sort_header_server_name", "SERVER", "server_name"), ("op_sort_header_map", "MAP", "map"),
                   ("op_sort_header_type", "MODE", "type"), ("op_sort_header_players", "PLAYERS", "players"),
                   ("op_sort_header_ping", "PING", "ping")]
BROWSER_TICK_BADGE = 30
# the panel's pills: a game's kind and tick rate, and OpenCE's
BROWSER_PILL, BROWSER_WIDE_PILL = 64, 132
BROWSER_JOIN = (148, 24)
# the footer's prompts: (widget, words, x, width, its function); the
# browser shows those of its list (menu_functions.c's browser_prompts_show)
BROWSER_PROMPTS = [
    ("prompt_favorite", "%y-button FAVORITE", 28, 124, '<on event="a" run="port browser favorite"/>'),
    ("prompt_refresh", "%right-trigger REFRESH", 156, 124, '<on event="a" run="port browser refresh"/>'),
    ("prompt_paste", "%right-trigger PASTE LINK", 156, 144, '<on event="a" run="port browser refresh"/>'),
    # (the next column's title sorts: menu_functions.c's browser_sort_choose)
    ("prompt_sort", "%x-button SORT", 304, 100, '<on event="a" run="port browser sort"/>'),
    ("prompt_back", "%b-button BACK", 516, 96, '<on event="a" run="mouse emit back event"/>'),
]
# Multiplayer's: its list's items (and its two pages': CREATE GAME's and
# CO-OP CAMPAIGN's), the panel beside them, the line over EDIT GAMETYPES
HUB = f"{MT}/hub"
HUB_ITEM = (280, 26)
HUB_LIST_X = 28
HUB_PANEL = (316, 80, 296, 320)
# on a wide screen (wide): the items from its left, HUB_WIDE_ITEMS wider, the
# panel beside them to its right, wider by the rest
HUB_WIDE_ITEMS = 80
HUB_WIDE_PANEL = (-WIDE_HALF + HUB_WIDE_ITEMS, WIDE_EXTRA - HUB_WIDE_ITEMS)
HUB_PROMPTS = [
    ("prompt_refresh", "%right-trigger REFRESH", 28, 124, '<on event="a" run="port multiplayer refresh"/>'),
    ("prompt_back", "%b-button BACK", 516, 96, '<on event="a" run="mouse emit back event"/>'),
]
HUB_PAGE_PROMPTS = [HUB_PROMPTS[1]]
# the Party screen's (_party): a row for each member (its most,
# p2p.h's P2P_PARTY_MAXIMUM_MEMBERS), and its footer's prompts, those of a
# place shown one at a time (menu_functions.c's party_screen_update)
PARTY_ROWS, PARTY_ROW_STEP = 16, 19
PARTY_PROMPTS = [
    ("prompt_make", "%a-button MAKE PARTY", 28, 144, '<on event="a" run="port party button"/>'),
    ("prompt_copy", "%a-button COPY INVITE", 28, 144, '<on event="a" run="port party button"/>'),
    ("prompt_paste", "%x-button PASTE LINK", 176, 144, '<on event="a" run="port party button"/>'),
    ("prompt_follow", "%x-button JOIN LEADER", 176, 144, '<on event="a" run="port party button"/>'),
    ("prompt_end", "%x-button END PARTY", 176, 144, '<on event="a" run="port party button"/>'),
    ("prompt_leave", "%y-button LEAVE PARTY", 324, 144, '<on event="a" run="port party button"/>'),
    ("prompt_back", "%b-button BACK", 516, 96, '<on event="a" run="port party button"/>'),
]
# the lobby's (_lobby_board, menu_functions.c's lobby_menu.inc): its players
# from the screen's left, in a column for each team (one list, both
# columns', in a game without teams), the details panel beside them to its
# right; on a wide screen the players LOBBY_WIDE_PLAYERS wider (each column
# half of it), the panel to the screen's right, wider by the rest
LOBBY_TOP, LOBBY_HEIGHT = 80, 320
LOBBY_LIST_X, LOBBY_COLUMN_WIDTH, LOBBY_COLUMN_GAP = 28, 186, 12
LOBBY_PANEL = (420, LOBBY_TOP, 192, LOBBY_HEIGHT)
LOBBY_WIDE_PLAYERS = 140
LOBBY_WIDE_PANEL = (-WIDE_HALF + LOBBY_WIDE_PLAYERS, WIDE_EXTRA - LOBBY_WIDE_PLAYERS)
# a column's rows (lobby_menu.inc's LOBBY_BOARD_ROWS), under its title: each
# its player's dot, name and tag (HOST, PARTY, OPENH1, OPENCE, BOT)
LOBBY_ROWS, LOBBY_ROWS_TOP, LOBBY_ROW_STEP = 13, 36, 20
LOBBY_ROW = (LOBBY_COLUMN_WIDTH - 16, 18)
LOBBY_DOT, LOBBY_TAG_WIDTH = 8, 58
# (the tag kept in from its row's end, clear of your own row's frame)
LOBBY_TAG_INSET = 10
# the panel's words, controls and buttons, LOBBY_PANEL_INNER wide
LOBBY_PANEL_INNER = LOBBY_PANEL[2] - 28
LOBBY_CONTROL_HEIGHT = 22
LOBBY_START = (LOBBY_PANEL_INNER, 26)
# the team colors: red's and blue's, their columns' edges, and a game
# without teams' dot
LOBBY_TEAM_COLORS = [("#FF6B5E", "#7A2A2A"), ("#5AA9FF", "#1F4F9A")]
LOBBY_FREE_DOT = "#9CBBE6"
# the footer's prompts, each shown when it applies (lobby_menu.inc's
# lobby_board_controls)
LOBBY_PROMPTS = [
    ("prompt_team", "CHANGE TEAM", 28, 144, '<on event="a" run="port lobby team toggle"/>'),
    ("prompt_invite", "%y-button COPY INVITE", 176, 124, '<on event="a" run="port lobby invite"/>'),
    ("prompt_add", "%x-button ADD PLAYER", 324, 124, '<on event="a" run="port lobby add player"/>'),
    ("prompt_leave", "%b-button LEAVE", 516, 96, '<on event="a" run="mouse emit back event"/>'),
]
# the items' descriptions, in menu_functions.c's _multiplayer_item_* order
HUB_DESCRIPTIONS = [
    "Every public game on the Internet, OpenH1 and Legacy. Press %a-button to see them all.",
    "Games hosted on your Wi-Fi or local network appear here as they open.",
    "Join a friend's game with the invite link they sent you.",
    "Host a game on the Internet: friends join with your invite link.",
    "Host a game for players on your Wi-Fi or local network.",
    "Play the campaign together over the Internet: friends join with your invite link.",
    "Play the campaign with a friend on this screen. Player 2 uses a controller.",
    "Play Slayer against computer players, alone or with friends on this screen.",
    "Make your own gametypes: the rules, weapons and vehicles, kept for next time.",
    "Host a game on the Internet, or for players on your local network only.",
    "Play the campaign together: online, or split screen on this computer.",
    "Group up with friends: they join with your party's link, and come with you to every game you host or join.",
    "Pick a playlist: OpenH1 finds a game with room for you and your party, or hosts one that fills up.",
    "Your friends, who's online and where, with your friend code. Join them, invite them, or add someone you played.",
]


def parts_pictures() -> list:
    """(bitmap name, [(SVG, width, height)] for its frames) of the parts the
    multiplayer screens use, at their sizes"""
    tab_w, tab_h = SETTINGS_TAB_WIDTH, SETTINGS_TAB_HEIGHT

    def framed(width, height, fill, opacity, stroke, radius=8):
        return _svg(width, height, [_rounded(width, height, 1, radius, fill, opacity, stroke, 2)])

    def highlight(width, height):
        return [(_svg(width, height, []), width, height),
                (framed(width, height, "#002244", 0.9, "#2299ff"), width, height)]

    def line(width):
        return [(_svg(width, 4, [f'  <rect x="0" y="2.5" width="{width}" height="1" fill="#2995ff" '
                                 'fill-opacity="0.45"/>']), width, 4)]

    def pill(width):
        height = PART_PILL_HEIGHT
        return [(_svg(width, height, [_rounded(width, height, 0.5, 3, "#1e5fd1", 1.0)]), width, height),
                (_svg(width, height, [_rounded(width, height, 0.5, 3, "#4a5a78", 1.0)]), width, height),
                (_svg(width, height, [_rounded(width, height, 0.75, 3, stroke="#2f7fff", stroke_width=1.5)]),
                 width, height)]

    rows = [(part_row(BROWSER_LIST_WIDTH, BROWSER_ROW_HEIGHT), highlight(BROWSER_LIST_WIDTH, BROWSER_ROW_HEIGHT) +
             [(framed(BROWSER_LIST_WIDTH, BROWSER_ROW_HEIGHT, "#0d3a7a", 0.9, "#8cc8ff"), BROWSER_LIST_WIDTH,
               BROWSER_ROW_HEIGHT)])]
    prompt_widths = sorted({width for *_, width, _ in BROWSER_PROMPTS + HUB_PROMPTS + PARTY_PROMPTS +
                            LOBBY_PROMPTS + FRIENDS_PROMPTS})
    join_w, join_h = BROWSER_JOIN
    return [
        # (Settings' tab, then its current one: menu_functions.c's
        # menu_tabs_show chooses the frame)
        (PARTS_TAB, [(_svg(tab_w, tab_h, []), tab_w, tab_h),
                     (framed(tab_w, tab_h, "#002244", 0.9, "#2299ff"), tab_w, tab_h),
                     (framed(tab_w, tab_h, "#0a2a5a", 0.75, "#2299ff"), tab_w, tab_h),
                     (framed(tab_w, tab_h, "#0d3a7a", 0.9, "#8cc8ff"), tab_w, tab_h)]),
        *rows,
        (part_highlight(*HUB_ITEM), highlight(*HUB_ITEM)),
        *((part_highlight(width, PART_PROMPT_HEIGHT), highlight(width, PART_PROMPT_HEIGHT))
          for width in prompt_widths),
        (part_line(BROWSER_LIST_WIDTH), line(BROWSER_LIST_WIDTH)),
        (part_line(HUB_ITEM[0]), line(HUB_ITEM[0])),
        (part_section(HUB_ITEM[0]), [(_svg(HUB_ITEM[0], PART_SECTION_HEIGHT, [
            f'  <rect x="0" y="{PART_SECTION_HEIGHT - 1.5:g}" width="{HUB_ITEM[0]}" height="1" fill="#2995ff" '
            'fill-opacity="0.45"/>']), HUB_ITEM[0], PART_SECTION_HEIGHT)]),
        (part_panel(*BROWSER_PANEL[2:]), [(framed(*BROWSER_PANEL[2:], "#000e23", 0.88, "#2995ff", 9.5),
                                           *BROWSER_PANEL[2:])]),
        (part_panel(*HUB_PANEL[2:]), [(framed(*HUB_PANEL[2:], "#000e23", 0.88, "#2995ff", 9.5), *HUB_PANEL[2:])]),
        *((part_pill(width), pill(width)) for width in (BROWSER_TICK_BADGE, BROWSER_PILL, BROWSER_WIDE_PILL)),
        (part_button(join_w, join_h), [(framed(join_w, join_h, "#1e5fd1", 0.9, "#5aa9ff"), join_w, join_h),
                                       (framed(join_w, join_h, "#2a6fe0", 1.0, "#b8dbff"), join_w, join_h)]),
        *lobby_pictures(framed, highlight),
        *touch_pictures(),
    ]


def lobby_pictures(framed, highlight) -> list:
    """the lobby's own parts (_lobby_board): its teams' panels, the panel of
    a game without teams and the details panel, a player's row and dot, the
    panel's section, controls and buttons"""
    column_w, height = LOBBY_COLUMN_WIDTH, LOBBY_HEIGHT
    players_w = 2 * LOBBY_COLUMN_WIDTH + LOBBY_COLUMN_GAP
    row_w, row_h = LOBBY_ROW
    inner, control_h = LOBBY_PANEL_INNER, LOBBY_CONTROL_HEIGHT
    start_w, start_h = LOBBY_START
    dot = LOBBY_DOT

    def disc(color):
        return (_svg(dot, dot, [f'  <circle cx="{dot / 2:g}" cy="{dot / 2:g}" r="{dot / 2 - 0.5:g}" '
                                f'fill="{color}"/>']), dot, dot)

    return [
        (part_team_panel(column_w, height), [(framed(column_w, height, "#081a40", 0.9, edge, 9.5), column_w, height)
                                             for _, edge in LOBBY_TEAM_COLORS]),
        (part_panel(players_w, height), [(framed(players_w, height, "#081a40", 0.9, "#1f4f9a", 9.5), players_w,
                                          height)]),
        (part_panel(*LOBBY_PANEL[2:]), [(framed(*LOBBY_PANEL[2:], "#000e23", 0.88, "#2995ff", 9.5),
                                         *LOBBY_PANEL[2:])]),
        (part_player_row(row_w, row_h), [(framed(row_w, row_h, "#0b2252", 0.9, "none", 5), row_w, row_h),
                                         (framed(row_w, row_h, "#0e2a63", 1.0, "#3d8bff", 5), row_w, row_h),
                                         (framed(row_w, row_h, "#0d3a7a", 1.0, "#b8dbff", 5), row_w, row_h)]),
        (part_dot(dot), [disc(color) for color, _ in LOBBY_TEAM_COLORS] + [disc(LOBBY_FREE_DOT)]),
        (part_section(inner), [(_svg(inner, PART_SECTION_HEIGHT, [
            f'  <rect x="0" y="{PART_SECTION_HEIGHT - 1.5:g}" width="{inner}" height="1" fill="#2995ff" '
            'fill-opacity="0.45"/>']), inner, PART_SECTION_HEIGHT)]),
        (part_highlight(inner, control_h), highlight(inner, control_h)),
        (part_outline(inner, control_h), [(framed(inner, control_h, "none", 1.0, "#2f7fff"), inner, control_h),
                                          (framed(inner, control_h, "#0d3a7a", 0.9, "#b8dbff"), inner, control_h)]),
        (part_button(start_w, start_h), [(framed(start_w, start_h, "#1e5fd1", 0.9, "#5aa9ff"), start_w, start_h),
                                         (framed(start_w, start_h, "#2a6fe0", 1.0, "#b8dbff"), start_w, start_h)]),
    ]


def _part_text(name: str, pairs: list) -> list:
    """a text the game fills in (no words of its own, which would be drawn
    over the game's: a text given here is fixed)"""
    return _widget(name, [("type", "text"), ("controller", 1), *pairs, ("text_flags", "no_focus_test")], [])


def _tab_strip(base: str, strip: str, tabs: list, handlers: list) -> list:
    """a strip of tabs, a column list (menu_functions.c's menu_tabs_show
    knows it by its name's end, "_tabs"): each tab's words, its picture
    Settings' tab (menu_tabs_show frames the current one), spread across a
    wide screen; A or a click runs the handlers"""
    lines = _widget(f"{base}/{strip}", [("type", "column_list"), ("width", 640), ("height", SETTINGS_TAB_HEIGHT),
                                       ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                    [f'<child widget="{base}/{name}" x="{SETTINGS_TABS_X + index * SETTINGS_TAB_STEP}"/>'
                     for index, (name, _) in enumerate(tabs)])
    for index, (name, words) in enumerate(tabs):
        lines += _widget(f"{base}/{name}", [("type", "text"), ("controller", 1), ("width", SETTINGS_TAB_LAID),
                                           ("height", SETTINGS_TAB_HEIGHT), ("flags", "keep_ends"),
                                           ("wide", wide(wide_spread(index, len(tabs)))),
                                           ("bitmap", PARTS_TAB), ("text", words),
                                           ("font", "ui\\small_ui"), ("color", SETTINGS_BLUE), ("align", "center"),
                                           ("text_y", 3)],
                         handlers + ['<on event="left_mouse" run="mouse emit accept event"/>'])
    return lines


def _prompts(base: str, bar: str, prompts: list, defined: tuple = ()) -> list:
    """a footer of key prompts, a bar of buttons: each its button's picture
    in its words (the device's, menu_functions.c's), highlighted while it
    has the focus, and its function run by A or a click (those another
    footer has, by their names in defined, its)"""
    lines = _widget(f"{base}/{bar}", [("type", "column_list"), ("width", 640), ("height", PART_PROMPT_HEIGHT + 4),
                                     ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                    [f'<child widget="{base}/{name}" x="{x}" y="1"/>' for name, _, x, _, _ in prompts])
    for name, words, x, width, handler in (prompt for prompt in prompts if prompt[0] not in defined):
        lines += _widget(f"{base}/{name}", [("type", "text"), ("width", width), ("height", PART_PROMPT_HEIGHT),
                                           ("wide", prompt_wide(x, width)),
                                           ("bitmap", part_highlight(width, PART_PROMPT_HEIGHT)), ("text", words),
                                           ("font", "ui\\small_ui"), ("color", PART_TEXT), ("text_x", 8),
                                           ("text_y", 4)],
                         [handler, handler.replace('event="a"', 'event="start"'),
                          '<on event="left_mouse" run="mouse emit accept event"/>'])
    return lines


def prompt_wide(x: int, width: int):
    """a footer's prompt on a wide screen: at its left edge if left of the
    middle, else at its right"""
    return WIDE_LEFT if x + width / 2 < 320 else WIDE_RIGHT


def _section(name: str, words: str, width: int, wide_layout=None) -> list:
    """a section's title over its line, which a list passes over (it takes
    no events)"""
    return _widget(name, [("type", "text"), ("width", width), ("height", PART_SECTION_HEIGHT), ("wide", wide_layout),
                          ("bitmap", part_section(width)),
                          ("text", words), ("font", "ui\\small_ui"), ("color", PART_DIM), ("text_x", 4)], [])


def _header_x(column: str) -> int:
    """a column title's place in a row: its column's left, or for Players'
    and Ping's (their numbers at their right) where it ends where they do"""
    x, width = BROWSER_COLUMNS[column]
    return x + width - BROWSER_HEADER_WIDTHS.get(column, width) if column in ("players", "ping") else x


def _browser() -> list:
    """Join Game's list of games (the server browser's, the local network's
    and Join by Link's, by the mode Multiplayer chose: menu_functions.c's
    browser_initialize), in Settings' look: its tabs (every game, OpenH1's,
    Legacy ones, co-op, the favorites: LB and RB, Q and E, or a click), the
    games' rows under their columns (a long name, map or mode cut short with
    "...": menu_functions.c's menu_text_fit), the game chosen's details in
    the panel beside them with JOIN GAME, words in the list's place while it
    has none, and the footer's key prompts"""
    base = f"{MT}/join_game"
    list_name = f"{base}/join_game_items_list"
    panel_left, panel_top, panel_width, panel_height = BROWSER_PANEL
    inner_left, inner_width = panel_left + 12, panel_width - 24
    lines = _widget(f"{base}/join_game_screen",
                    [("width", 640), ("height", 480), ("flags", "pass_unhandled_to_focused_child"),
                     ("bitmap", "bitmaps/gradient")],
                    ['<on event="created" run="gamespy screen init"/>',
                     '<on event="deleted" run="gamespy screen dispose"/>',
                     '<on event="b" run="gamespy back handler"/>', '<on event="back" run="gamespy back handler"/>',
                     '<on event="white" run="port browser tab previous"/>',
                     '<on event="black" run="port browser tab next"/>',
                     '<on event="x" run="port browser sort"/>', '<on event="y" run="port browser favorite"/>',
                     '<on event="right_trigger" run="port browser refresh"/>',
                     f'<child widget="{base}/header_join_game"/>',
                     f'<child widget="{base}/browser_counts"/>',
                     f'<child widget="{SETTINGS}/tab_prompt_previous" x="{WIDE_MARGIN}" y="{SETTINGS_TABS_TOP}" '
                     'platform="pointer"/>',
                     f'<child widget="{SETTINGS}/tab_prompt_next" '
                     f'x="{SETTINGS_TABS_X + (len(BROWSER_TABS) - 1) * SETTINGS_TAB_STEP + SETTINGS_TAB_LAID + 4}" '
                     f'y="{SETTINGS_TABS_TOP}" platform="pointer"/>',
                     f'<child widget="{base}/browser_columns_line" platform="pointer"/>',
                     f'<child widget="{base}/browser_empty" platform="pointer"/>',
                     f'<child widget="{list_name}" platform="pointer"/>',
                     *_touch_browser()[0]])
    # (the title of the mode chosen: menu_functions.c's browser_initialize)
    lines += _widget(f"{base}/header_join_game", [("controller", 1), ("left", 35), ("top", 11), ("width", 363),
                                                  ("height", 59), ("wide", WIDE_LEFT)],
                     [f'<child widget="{base}/{name}"/>' for name in
                      ("header_server_browser", "header_lan", "header_direct_link")])
    lines += _header(f"{base}/header_lan", f"{base}/header_local_network")
    lines += _part_text(f"{base}/browser_counts", [("left", 420), ("top", 46), ("width", 192), ("height", 18),
                                                  ("font", "ui\\small_ui"), ("color", PART_DIM), ("align", "right"),
                                                  ("wide", WIDE_RIGHT)])
    lines += _widget(f"{base}/browser_columns_line", [("controller", 1), ("left", BROWSER_LIST_X),
                                                      ("top", BROWSER_HEADERS_TOP + 14), ("width", BROWSER_LIST_WIDTH),
                                                      ("height", 4), ("bitmap", part_line(BROWSER_LIST_WIDTH)),
                                                      ("wide", wide(-WIDE_HALF, BROWSER_WIDE_LIST))], [])
    # (the list's words while it has no rows: menu_functions.c's
    # browser_empty_text)
    lines += _part_text(f"{base}/browser_empty", [("left", BROWSER_LIST_X + 24), ("top", 200),
                                                 ("width", BROWSER_LIST_WIDTH - 48), ("height", 80),
                                                 ("font", "ui\\large_ui"), ("color", SETTINGS_BLUE),
                                                 ("align", "center"), ("wide", wide(-WIDE_HALF, BROWSER_WIDE_LIST))])
    # (the list: the tabs, the column titles (up from the first row), the
    # rows, JOIN GAME, the footer; the panel is its description)
    rows = [f'<child widget="{base}/server_item_{row}" x="{BROWSER_LIST_X}" '
            f'y="{BROWSER_ROWS_TOP + BROWSER_ROW_STEP * (row - 1)}"/>' for row in range(1, BROWSER_ROWS + 1)]
    lines += _widget(list_name, [("type", "column_list"), ("width", 640), ("height", 480),
                                 ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                                 ("description", f"{base}/browser_panel")],
                     ['<data input="gamespy screen update"/>',
                      f'<child widget="{base}/browser_tabs" y="{SETTINGS_TABS_TOP}"/>',
                      f'<child widget="{base}/join_game_header" y="{BROWSER_HEADERS_TOP}"/>',
                      *rows,
                      f'<child widget="{base}/browser_join_bar" y="{panel_top + panel_height - BROWSER_JOIN[1] - 10}"/>',
                      f'<child widget="{base}/join_game_button_bar" y="414"/>'])
    lines += _tab_strip(base, "browser_tabs", BROWSER_TABS, ['<on event="a" run="port browser tab"/>',
                                                             '<on event="start" run="port browser tab"/>'])
    # (the column titles: A or a click sorts by one, again the other way;
    # its arrow says which way: menu_functions.c's browser_sort_headers_show)
    lines += _widget(f"{base}/join_game_header", [("type", "column_list"), ("width", 640), ("height", 16),
                                                  ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                     [f'<child widget="{base}/{name}" x="{BROWSER_LIST_X + _header_x(column)}"/>'
                      for name, _, column in BROWSER_HEADERS])
    for name, words, column in BROWSER_HEADERS:
        width = BROWSER_HEADER_WIDTHS.get(column, BROWSER_COLUMNS[column][1])
        right = column in ("players", "ping")
        # (over its column, on a wide screen too: the list's move and the
        # column's, its growth)
        moved, grown = BROWSER_WIDE_COLUMNS[column]
        lines += _widget(f"{base}/{name}", [("type", "text"), ("width", width), ("height", 16), ("text", words),
                                           ("font", "ui\\small_ui"), ("color", PART_DIM),
                                           ("align", "right" if right else None),
                                           ("wide", wide(-WIDE_HALF + moved, grown))],
                         ['<on event="a" run="gamespy select header"/>',
                          '<on event="start" run="gamespy select header"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          f'<child widget="{base}/header_sort_arrows"/>'])
    lines += _widget(f"{base}/header_sort_arrows", [("width", 16), ("height", 16)],
                     [f'<child widget="{base}/header_sort_up_arrow"/>',
                      f'<child widget="{base}/header_sort_down_arrow"/>'])
    for way in ("up", "down"):
        lines += _widget(f"{base}/header_sort_{way}_arrow", [("width", 16), ("height", 16),
                                                            ("bitmap", f"bitmaps/arrow_sm_{way}")], [])
    # (a row: its game's star, its name with its tick rate's badge (OpenH1's
    # games) and its lock (a password) after it, its map, its mode, its
    # players and its ping's bars)
    for row in range(1, BROWSER_ROWS + 1):
        lines += _widget(f"{base}/server_item_{row}",
                         [("width", BROWSER_LIST_WIDTH), ("height", BROWSER_ROW_HEIGHT),
                          ("bitmap", part_row(BROWSER_LIST_WIDTH, BROWSER_ROW_HEIGHT)),
                          ("wide", wide(-WIDE_HALF, BROWSER_WIDE_LIST))],
                         ['<on event="a" run="gamespy select item"/>', '<on event="start" run="gamespy select item"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          '<on event="double_click" run="gamespy select item"/>',
                          f'<child widget="{FAVORITE_STAR_WIDGET}" x="{BROWSER_COLUMNS["favorite"][0]}" y="1"/>',
                          *(f'<child widget="{base}/server_item_{column}" x="{BROWSER_COLUMNS[column][0]}"/>'
                            for column in ("server_name", "map", "type", "players")),
                          f'<child widget="{base}/server_item_tick" y="1"/>',
                          f'<child widget="{base}/server_item_locked" y="1"/>',
                          f'<child widget="{base}/server_item_ping" x="{BROWSER_COLUMNS["ping"][0]}" y="1"/>',
                          f'<child widget="{base}/server_item_ping_ms" x="{BROWSER_COLUMNS["ping"][0]}"/>'])
    for column in ("server_name", "map", "type", "players"):
        lines += _widget(f"{base}/server_item_{column}",
                         [("type", "text"), ("width", BROWSER_COLUMNS[column][1]), ("height", BROWSER_ROW_HEIGHT),
                          ("font", "ui\\small_ui"),
                          ("color", "#FFFFFFFF" if column == "server_name" else "#FFC9DCF7"),
                          ("align", "right" if column == "players" else None), ("text_y", 2),
                          ("wide", wide(*BROWSER_WIDE_COLUMNS[column]))], [])
    lines += _widget(f"{base}/server_item_tick", [("type", "text"), ("width", BROWSER_TICK_BADGE),
                                                  ("height", PART_PILL_HEIGHT),
                                                  ("bitmap", part_pill(BROWSER_TICK_BADGE)), ("font", "ui\\small_ui"),
                                                  ("color", "#FFFFFFFF"), ("align", "center"),
                                                  ("text_flags", "no_focus_test")], [])
    lines += _widget(f"{base}/server_item_locked", [("width", 16), ("height", 16),
                                                    ("bitmap", f"{base}/locked_icon")], [])
    lines += _widget(f"{base}/server_item_ping", [("width", BROWSER_PING_BARS), ("height", 16),
                                                  ("bitmap", PING_BARS), ("wide", wide(*BROWSER_WIDE_COLUMNS["ping"]))],
                     [])
    # (its milliseconds, coloured by how good, "..." faded while measured:
    # menu_functions.c's pc_menu_text_color)
    lines += _widget(f"{base}/server_item_ping_ms", [("type", "text"), ("width", BROWSER_COLUMNS["ping"][1]),
                                                     ("height", BROWSER_ROW_HEIGHT), ("font", "ui\\small_ui"),
                                                     ("color", "#FFC9DCF7"), ("align", "right"), ("text_y", 2),
                                                     ("text_flags", "no_focus_test"),
                                                     ("wide", wide(*BROWSER_WIDE_COLUMNS["ping"]))], [])
    # (the panel: the game chosen's name, map and mode, its kind, tick rate
    # and whether OpenCE's players join it, its players and ping, a line of
    # how it is (under way, a password, joining), and JOIN GAME over it:
    # menu_functions.c's browser_panel_show)
    # (PLAYERS and PING each a line: its title, and its value at its right)
    children = [("panel_title", 12, inner_width, 48, "ui\\large_ui", "#FFFFFFFF", None),
                ("panel_where", 60, inner_width, 44, "ui\\small_ui", SETTINGS_BLUE, None),
                ("panel_players_label", 154, 70, 20, "ui\\small_ui", PART_DIM, None),
                ("panel_players", 151, inner_width, 24, "ui\\large_ui", "#FFFFFFFF", "right"),
                ("panel_ping_label", 176, 70, 20, "ui\\small_ui", PART_DIM, None),
                ("panel_ping", 173, inner_width, 24, "ui\\large_ui", "#FFFFFFFF", "right"),
                ("panel_status", 202, inner_width, 60, "ui\\small_ui", SETTINGS_BLUE, None)]
    panel_children = []
    # (in a wider panel, the words as much wider as it, moved with it)
    panel_moved, panel_grown = BROWSER_WIDE_PANEL
    for name, y, width, height, font, color, align in children:
        panel_children.append(f'<child widget="{base}/{name}" x="{inner_left}" y="{panel_top + y}"/>')
        lines += _part_text(f"{base}/{name}", [("width", width), ("height", height), ("font", font),
                                               ("color", color), ("align", align),
                                               ("wide", wide(0, panel_grown) if width == inner_width else None)])
    for name, x, y, width in (("panel_kind", 0, 108, BROWSER_PILL), ("panel_tick", BROWSER_PILL + 4, 108, BROWSER_PILL),
                              ("panel_opence", 0, 128, BROWSER_WIDE_PILL)):
        panel_children.append(f'<child widget="{base}/{name}" x="{inner_left + x}" y="{panel_top + y}"/>')
        lines += _widget(f"{base}/{name}", [("type", "text"), ("controller", 1), ("width", width),
                                           ("height", PART_PILL_HEIGHT), ("bitmap", part_pill(width)),
                                           ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"),
                                           ("text_flags", "no_focus_test")], [])
    lines += _widget(f"{base}/browser_panel", [("controller", 1), ("left", panel_left), ("top", panel_top),
                                               ("width", panel_width), ("height", panel_height),
                                               ("bitmap", part_panel(panel_width, panel_height)),
                                               ("wide", wide(*BROWSER_WIDE_PANEL))], panel_children)
    join_w, join_h = BROWSER_JOIN
    lines += _widget(f"{base}/browser_join_bar", [("type", "column_list"), ("width", 640), ("height", join_h),
                                                  ("flags", "pass_unhandled_to_focused_child")],
                     [f'<child widget="{base}/button_join" x="{inner_left}"/>'])
    lines += _widget(f"{base}/button_join", [("type", "text"), ("width", join_w), ("height", join_h),
                                             ("bitmap", part_button(join_w, join_h)),
                                             ("wide", wide(panel_moved, panel_grown)),
                                             ("text", "%a-button JOIN GAME"), ("font", "ui\\small_ui"),
                                             ("color", "#FFFFFFFF"), ("text_x", 12), ("text_y", 5)],
                     ['<on event="a" run="gamespy select button"/>', '<on event="start" run="gamespy select button"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    lines += _prompts(base, "join_game_button_bar", BROWSER_PROMPTS)
    return lines


def _hub_panel(folder: str, place: tuple, wide_panel: tuple) -> list:
    """Multiplayer's panel (its list's description), in folder, at place (left, top, width, height),
    moved and widened on a wide screen by wide_panel: what is live for the item chosen, and its
    description (menu_functions.c's multiplayer_status_update finds them by their names: the
    computer's _hub, the touchscreen's _touch_hub, narrower)"""
    left, top, width, height = place
    x, inner = left + 14, width - 28
    panel, lines = [], []
    # (a narrow panel's best games: each its players and ping on its second line)
    compact = inner < 240

    # (each placed by its own left and top, which the panel's functions move
    # it from: menu_functions.c's multiplayer_status_number, the badges)
    def text(name, tx, ty, w, h, font, color, align=None, words=None, wide_layout=None):
        panel.append(f'<child widget="{folder}/{name}"/>')
        pairs = [("left", tx), ("top", ty), ("width", w), ("height", h), ("font", font), ("color", color),
                 ("align", align), ("wide", wide_layout)]
        return (_widget(f"{folder}/{name}", [("type", "text"), ("controller", 1), *pairs, ("text", words),
                                         ("text_flags", "no_focus_test")], [])
                if words else _part_text(f"{folder}/{name}", pairs))

    # (in a wider panel, moved with it: its words wider, the players counted
    # and each best game's players and ping at its right)
    grown, players_moved, right = wide(0, wide_panel[1]), wide_panel[1] // 2, wide(wide_panel[1])
    lines += text("multiplayer_status", x, top + 14, inner, 24, "ui\\small_ui" if compact else "ui\\large_ui",
                  "#FFFFFFFF", wide_layout=grown)
    lines += text("multiplayer_status_detail", x, top + 42, inner, 120, "ui\\large_ui", SETTINGS_BLUE,
                  wide_layout=grown)
    lines += text("online_now_label", x, top + 12, 200, 16, "ui\\small_ui", PART_DIM, words="ONLINE NOW")
    players_x = x + min(124, inner - 84)
    for count, count_x in (("games", x), ("players", players_x)):
        for digit in range(STATUS_DIGIT_PLACES):
            panel.append(f'<child widget="{folder}/online_{count}_{digit}"/>')
            lines += _widget(f"{folder}/online_{count}_{digit}", [("controller", 1),
                                                               ("left", count_x + DIGIT_SIZE[0] * digit),
                                                               ("top", top + 28), ("width", DIGIT_SIZE[0]),
                                                               ("height", DIGIT_SIZE[1]), ("bitmap", STATUS_DIGITS),
                                                               ("wide", wide(players_moved if count == "players"
                                                                             else 0))],
                             [])
    lines += text("online_games_label", x, top + 46, 80, 18, "ui\\small_ui", SETTINGS_BLUE)
    lines += text("online_players_label", players_x, top + 46, 90, 18, "ui\\small_ui", SETTINGS_BLUE,
                  wide_layout=wide(players_moved))
    lines += text("best_label", x, top + 76, 200, 16, "ui\\small_ui", PART_DIM, words="BEST FOR YOU")
    for row in range(BEST_GAMES):
        row_top = top + 94 + 40 * row
        lines += text(f"best_{row}_name", x, row_top, inner - 4 if compact else 166, 20, "ui\\large_ui", "#FFFFFFFF",
                      wide_layout=wide(0, wide_panel[1]))
        panel.append(f'<child widget="{folder}/best_{row}_badge"/>')
        lines += _widget(f"{folder}/best_{row}_badge", [("type", "text"), ("controller", 1), ("left", x),
                                                     ("top", row_top + 3), ("width", BROWSER_PILL),
                                                     ("height", PART_PILL_HEIGHT),
                                                     ("bitmap", part_pill(BROWSER_PILL)),
                                                     ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"),
                                                     ("align", "center"), ("text_flags", "no_focus_test")], [])
        lines += text(f"best_{row}_about", x, row_top + 19, inner - 104 if compact else 176, 18, "ui\\small_ui",
                      SETTINGS_BLUE,
                      wide_layout=wide(0, wide_panel[1]))
        lines += text(f"best_{row}_players", x + inner - 100, row_top + (19 if compact else 6), 70, 20,
                      "ui\\large_ui", "#FFFFFFFF",
                      "right", wide_layout=right)
        panel.append(f'<child widget="{folder}/best_{row}_ping"/>')
        lines += _widget(f"{folder}/best_{row}_ping", [("controller", 1), ("left", x + inner - 24),
                                                    ("top", row_top + (21 if compact else 9)), ("width", 24),
                                                    ("height", 16),
                                                    ("bitmap", PING_BARS), ("wide", right)], [])
    lines += text("multiplayer_options_txt", x, top + height - (96 if compact else 76), inner, 84 if compact else 64,
                  "ui\\small_ui", SETTINGS_BLUE,
                  wide_layout=grown)
    lines += _widget(f"{folder}/hub_panel", [("controller", 1), ("left", left), ("top", top), ("width", width),
                                            ("height", height), ("bitmap", part_panel(width, height)),
                                            ("wide", wide(*wide_panel))], panel)
    return lines


def _hub() -> list:
    """Multiplayer, in Settings' look: PLAY ONLINE's items (the server
    browser, its games counted; the local network, its games found; a
    friend's link; the party, its players: _party), HOST's (CREATE GAME and
    CO-OP CAMPAIGN, each a page of
    two, and PLAY WITH BOTS), EDIT GAMETYPES under a line, the panel beside
    them (menu_functions.c's multiplayer_status_update: the games online
    and the best of them for the server browser, else what is live for the
    item, and its description), and the footer's key prompts. Each item
    opens what the PC version's (and the port's) did; the touchscreen has
    them as tiles (_touch_hub)"""
    left, top, width, height = HUB_PANEL
    item_w, item_h = HUB_ITEM
    joins = lambda screen: [f'<on event="a" run="mp type set mode" open="{screen}"/>',  # noqa: E731
                            f'<on event="start" run="mp type set mode" open="{screen}"/>']
    creates = ['<on event="a" run="join controller to mp game"/>',
               f'<on event="a" run="mp type set mode" open="{MT}/connected/connected_map_select_wrapper"/>']
    browser = f"{MT}/join_game/join_game_screen"
    # (each item's name tells menu_functions.c's multiplayer_mode_set what
    # it is, and multiplayer_items its panel)
    items = {
        "hub_join_internet": ("SERVER BROWSER", joins(browser), "browser_note"),
        "hub_join_lan_item": ("LOCAL NETWORK", joins(browser), "item_note"),
        "hub_join_direct": ("JOIN BY LINK", joins(browser), "invite_note"),
        # (matchmaking's screen: _matchmaking)
        "hub_matchmaking": ("MATCHMAKING", [f'<on event="{event}" open="{MT}/matchmaking/matchmaking_screen"/>'
                                            for event in ("a", "start")], "matchmaking_note"),
        # (the party's screen: _party)
        "hub_party": ("PARTY", [f'<on event="{event}" open="{MT}/party/party_screen"/>' for event in ("a", "start")],
                      "party_note"),
        # (the friends' screen: _friends)
        "hub_friends": ("FRIENDS", [f'<on event="{event}" open="{MT}/friends/friends_screen"/>'
                                    for event in ("a", "start")], "friends_note"),
        "hub_create": ("CREATE GAME ›", [f'<on event="{event}" open="{HUB}/create_screen"/>'
                                              for event in ("a", "start")], None),
        "hub_create_bots": ("PLAY WITH BOTS", creates, None),
        "hub_coop": ("CO-OP CAMPAIGN ›", [f'<on event="{event}" open="{HUB}/coop_screen"/>'
                                               for event in ("a", "start")], None),
        "hub_gametypes": ("EDIT GAMETYPES", [
            f'<on event="{event}" open="main_menu/settings_select/multiplayer_setup/playlist_select/'
            'gametype_select_screen"/>' for event in ("a", "start")], None),
        "hub_create_inter": ("INTERNET", creates, None),
        "hub_create_lan_item": ("LOCAL NETWORK", creates, None),
        "hub_online_coop": ("ONLINE CO-OP", creates, None),
        "hub_split_coop": ("SPLIT-SCREEN CO-OP", [
            f'<on event="{event}" run="port coop begin" open="{MT}/coop/player_2_profile_screen"/>'
            for event in ("a", "start")], None),
    }
    def page_list(name, sections, prompts, step=item_h + 2, gap=8):
        children, y = [], top
        for section, words, names in sections:
            if section:
                children.append(f'<child widget="{HUB}/{section}" x="{HUB_LIST_X}" y="{y}"/>')
                y += PART_SECTION_HEIGHT + 2
            for item in names:
                if item == "hub_gametypes":
                    y = MULTIPLAYER_LINE_PLACE[1] + HUB_GAMETYPES_GAP
                children.append(f'<child widget="{HUB}/{item}" x="{HUB_LIST_X}" y="{y}"/>')
                y += step
            y += gap
        children.append(f'<child widget="{HUB}/{prompts}" y="414"/>')
        return _widget(f"{HUB}/{name}", [("type", "column_list"), ("width", 640), ("height", 480),
                                         ("flags", "pass_unhandled_to_focused_child up_down_tabs_items"),
                                         ("description", f"{HUB}/hub_panel")],
                       ['<data input="port multiplayer status"/>', *children])

    # (Multiplayer's own list a unit closer, its sections closer, for its
    # eleven items: HUB_LIST_STEP)
    lines = page_list("hub_list", [("section_online", "PLAY ONLINE", ["hub_matchmaking", "hub_join_internet",
                                                                      "hub_join_lan_item", "hub_join_direct",
                                                                      "hub_party", "hub_friends"]),
                                   ("section_host", "HOST", ["hub_create", "hub_create_bots", "hub_coop"]),
                                   (None, None, ["hub_gametypes"])], "hub_prompts", HUB_LIST_STEP, HUB_SECTION_GAP)
    lines += _section(f"{HUB}/section_online", "PLAY ONLINE", item_w, wide(-WIDE_HALF, HUB_WIDE_ITEMS))
    lines += _section(f"{HUB}/section_host", "HOST", item_w, wide(-WIDE_HALF, HUB_WIDE_ITEMS))
    lines += _widget(f"{HUB}/hub_divider", [("width", item_w), ("height", 4), ("bitmap", part_line(item_w)),
                                            ("wide", wide(-WIDE_HALF, HUB_WIDE_ITEMS))], [])
    # (CREATE GAME's page and CO-OP CAMPAIGN's: their two items, the same
    # panel, B back to Multiplayer)
    for page, words, names in (("create", "CREATE GAME", ["hub_create_inter", "hub_create_lan_item"]),
                               ("coop", "CO-OP CAMPAIGN", ["hub_online_coop", "hub_split_coop"])):
        lines += _widget(f"{HUB}/{page}_screen", [("width", 640), ("height", 480),
                                                  ("flags", "pass_unhandled_to_focused_child"),
                                                  ("bitmap", "bitmaps/gradient")],
                         ['<on event="created" run="mp type menu initialize"/>',
                          '<on event="deleted" run="port multiplayer status end"/>',
                          f'<child widget="{MT}/header_multiplayer"/>',
                          f'<child widget="{HUB}/{page}_list" platform="pointer"/>',
                          f'<child widget="{HUB}/touch/{page}_list" platform="touch"/>'])
        lines += page_list(f"{page}_list", [(f"section_{page}", words, names)], "page_prompts")
        lines += _section(f"{HUB}/section_{page}", words, item_w, wide(-WIDE_HALF, HUB_WIDE_ITEMS))
    for name, (words, handlers, note) in items.items():
        lines += _widget(f"{HUB}/{name}", [("type", "text"), ("width", item_w), ("height", item_h),
                                           ("wide", wide(-WIDE_HALF, HUB_WIDE_ITEMS)),
                                           ("bitmap", part_highlight(item_w, item_h)), ("text", words),
                                           ("font", "ui\\large_ui"), ("color", SETTINGS_BLUE), ("text_x", 10),
                                           ("text_y", 5)],
                         [*handlers, '<on event="left_mouse" run="mouse emit accept event"/>',
                          *([f'<child widget="{HUB}/{note}"/>'] if note else [])])
    # (at the items' ends: the server browser's games, the local network's
    # found, an invite link copied, in green, the party's players:
    # menu_functions.c's multiplayer_status_notes)
    # (right-aligned in from the highlight's end, clear of the items' words)
    for name, color in (("browser_note", "#FF7FB2FF"), ("item_note", "#FF7FB2FF"), ("invite_note", "#FF5ED38C"),
                        ("party_note", "#FF7FB2FF"), ("matchmaking_note", "#FF5ED38C"),
                        ("friends_note", "#FF5ED38C")):
        lines += _widget(f"{HUB}/{name}", [("type", "text"), ("width", item_w - 16), ("height", item_h),
                                           ("wide", wide(0, HUB_WIDE_ITEMS)),
                                           ("font", "ui\\small_ui"), ("color", color), ("align", "right"),
                                           ("text_y", 8), ("text_flags", "no_focus_test")], [])
    lines += _prompts(HUB, "hub_prompts", HUB_PROMPTS)
    lines += _prompts(HUB, "page_prompts", HUB_PAGE_PROMPTS, defined=tuple(name for name, *_ in HUB_PROMPTS))
    lines += _hub_panel(HUB, HUB_PANEL, HUB_WIDE_PANEL)
    lines += _strings(f"{HUB}/descriptions", HUB_DESCRIPTIONS)
    return lines


def _party() -> list:
    """the Party screen (port/linux/game/party_menu.inc), in Multiplayer's
    look (_hub's parts): PARTY's section over its members, a row each (its
    name, the leader's pill, and where it is: with the leader, or why not),
    words in their place out of a party; the panel beside them (the party's
    size, its link, where the leader is and the follow, what just happened,
    and what a party is); the footer's key prompts, each shown when it
    applies: MAKE PARTY or COPY INVITE (A), PASTE LINK, JOIN LEADER or END
    PARTY (X), LEAVE PARTY (Y), BACK. Every prompt runs "port party button"
    ("port party x" and "y" the screen's X and Y), which joins the leader's
    game first once it is found (an A press posted). The touchscreen opens it
    from PARTY on Multiplayer's bar (WIDGET_PATCHES). On a wide screen it is
    laid out as Multiplayer is (HUB_WIDE_ITEMS, HUB_WIDE_PANEL)"""
    base = f"{MT}/party"
    left, top, width, height = HUB_PANEL
    item_w = HUB_ITEM[0]
    # (the members' column as wide as Multiplayer's items, its names and
    # where they are each half the room)
    items_wide, half = wide(-WIDE_HALF, HUB_WIDE_ITEMS), HUB_WIDE_ITEMS // 2
    lines = _text_button(f"{MT}/multiplayer_button_party", "PARTY",
                         [f'<on event="{event}" open="{base}/party_screen"/>' for event in ("a", "start")])
    rows = [f'<child widget="{base}/party_{part}_{row}"/>' for row in range(PARTY_ROWS)
            for part in ("name", "where", "leader")]
    lines += _widget(f"{base}/party_screen", [("width", 640), ("height", 480),
                                              ("flags", "pass_unhandled_to_focused_child"),
                                              ("bitmap", "bitmaps/gradient")],
                     ['<on event="created" run="port party open"/>', '<on event="b" back="true"/>',
                      '<on event="back" back="true"/>', '<on event="x" run="port party x"/>',
                      '<on event="y" run="port party y"/>', f'<child widget="{MT}/header_multiplayer"/>',
                      f'<child widget="{base}/section_party" x="{HUB_LIST_X}" y="{top}"/>',
                      f'<child widget="{base}/party_count"/>', f'<child widget="{base}/party_empty"/>', *rows,
                      f'<child widget="{base}/party_panel"/>',
                      f'<child widget="{base}/party_list" platform="pointer"/>',
                      f'<child widget="{base}/touch/party_list" platform="touch"/>'])
    lines += _section(f"{base}/section_party", "PARTY", item_w, items_wide)
    lines += _part_text(f"{base}/party_count", [("left", HUB_LIST_X), ("top", top + 2), ("width", item_w - 4),
                                               ("height", 16), ("font", "ui\\small_ui"), ("color", PART_DIM),
                                               ("align", "right"), ("wide", items_wide)])
    # (the list's words out of a party: menu_functions.c's browser_empty's)
    lines += _part_text(f"{base}/party_empty", [("left", HUB_LIST_X + 16), ("top", 180), ("width", item_w - 32),
                                               ("height", 100), ("font", "ui\\large_ui"), ("color", SETTINGS_BLUE),
                                               ("align", "center"), ("wide", items_wide)])
    # (a member's row: its name, the leader's pill in its place's, else where
    # it is)
    for row in range(PARTY_ROWS):
        y = top + PART_SECTION_HEIGHT + 4 + PARTY_ROW_STEP * row
        lines += _part_text(f"{base}/party_name_{row}", [("left", HUB_LIST_X + 6), ("top", y), ("width", 150),
                                                        ("height", PARTY_ROW_STEP), ("font", "ui\\small_ui"),
                                                        ("color", "#FFFFFFFF"), ("wide", wide(-WIDE_HALF, half))])
        lines += _part_text(f"{base}/party_where_{row}", [("left", HUB_LIST_X + 156), ("top", y),
                                                         ("width", item_w - 160), ("height", PARTY_ROW_STEP),
                                                         ("font", "ui\\small_ui"), ("color", SETTINGS_BLUE),
                                                         ("align", "right"), ("wide", wide(-WIDE_HALF + half, half))])
        lines += _widget(f"{base}/party_leader_{row}", [("type", "text"), ("controller", 1),
                                                         ("left", HUB_LIST_X + item_w - BROWSER_PILL - 4),
                                                         ("top", y + 1), ("width", BROWSER_PILL),
                                                         ("height", PART_PILL_HEIGHT),
                                                         ("bitmap", part_pill(BROWSER_PILL)), ("text", "LEADER"),
                                                         ("wide", wide(-WIDE_HALF + HUB_WIDE_ITEMS)),
                                                         ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"),
                                                         ("align", "center"), ("text_flags", "no_focus_test")], [])
    # (the panel, Multiplayer's: menu_functions.c's party_screen_update)
    x, inner = left + 14, width - 28
    panel = []

    def text(name, ty, h, font, color, words=None):
        panel.append(f'<child widget="{base}/{name}"/>')
        pairs = [("left", x), ("top", top + ty), ("width", inner), ("height", h), ("font", font), ("color", color),
                 ("wide", wide(0, HUB_WIDE_PANEL[1]))]
        return (_widget(f"{base}/{name}", [("type", "text"), ("controller", 1), *pairs, ("text", words),
                                           ("text_flags", "no_focus_test")], [])
                if words else _part_text(f"{base}/{name}", pairs))

    lines += text("panel_title", 14, 24, "ui\\large_ui", "#FFFFFFFF")
    lines += text("panel_link_label", 44, 16, "ui\\small_ui", PART_DIM, words="PARTY LINK")
    lines += text("panel_link", 60, 18, "ui\\small_ui", "#FFFFFFFF")
    lines += text("panel_status", 86, 66, "ui\\large_ui", SETTINGS_BLUE)
    lines += text("panel_notice", 160, 66, "ui\\small_ui", "#FFFFFFFF")
    lines += text("panel_about", height - 76, 64, "ui\\small_ui", SETTINGS_BLUE)
    lines += _widget(f"{base}/party_panel", [("controller", 1), ("left", left), ("top", top), ("width", width),
                                             ("height", height), ("bitmap", part_panel(width, height)),
                                             ("wide", wide(*HUB_WIDE_PANEL))], panel)
    lines += _widget(f"{base}/party_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                            ("flags", "pass_unhandled_to_focused_child up_down_tabs_children")],
                     ['<data input="port party update"/>', f'<child widget="{base}/party_prompts" y="414"/>'])
    lines += _prompts(base, "party_prompts", PARTY_PROMPTS)
    return lines


# the Matchmaking screen's (_matchmaking): a row for each playlist
# (matchmaking.c's), and its footer's prompts (menu_functions.c's
# matchmaking_screen_update shows SEARCH or CANCEL)
MATCHMAKING_ROWS = 5
MATCHMAKING_PROMPTS = [
    ("prompt_search", "%a-button SEARCH", 28, 124, '<on event="a" run="port matchmaking button"/>'),
    ("prompt_cancel", "%x-button CANCEL", 28, 124, '<on event="a" run="port matchmaking button"/>'),
    ("prompt_back", "%b-button BACK", 516, 96, '<on event="a" run="port matchmaking button"/>'),
]


def _matchmaking() -> list:
    """the Matchmaking screen (port/linux/game/matchmaking_menu.inc), in
    Multiplayer's look (_hub's parts): MATCHMAKING's section over a row for
    each playlist (its name, and at its end what its search is doing), the
    panel beside them (the playlist chosen's name and what it is, the search:
    its time, the games it found and what it does now, and what matchmaking
    is), and the footer's key prompts: SEARCH (A, a row's A or click: the
    playlist searched for the whole party) or, while a search goes on, CANCEL
    (X), and BACK. On a wide screen it is laid out as Multiplayer is
    (HUB_WIDE_ITEMS, HUB_WIDE_PANEL)"""
    base = f"{MT}/matchmaking"
    left, top, width, height = HUB_PANEL
    item_w, item_h = HUB_ITEM
    step = item_h + 2
    items_wide = wide(-WIDE_HALF, HUB_WIDE_ITEMS)
    rows = [f'<child widget="{base}/mm_row_{row}" x="{HUB_LIST_X}" y="{top + PART_SECTION_HEIGHT + 2 + step * row}"/>'
            for row in range(MATCHMAKING_ROWS)]
    lines = _widget(f"{base}/matchmaking_screen", [("width", 640), ("height", 480),
                                                   ("flags", "pass_unhandled_to_focused_child"),
                                                   ("bitmap", "bitmaps/gradient")],
                    ['<on event="b" back="true"/>', '<on event="back" back="true"/>',
                     '<on event="x" run="port matchmaking x"/>', f'<child widget="{MT}/header_multiplayer"/>',
                     f'<child widget="{base}/section_matchmaking" x="{HUB_LIST_X}" y="{top}"/>',
                     f'<child widget="{base}/matchmaking_panel"/>', f'<child widget="{base}/matchmaking_list"/>'])
    lines += _section(f"{base}/section_matchmaking", "MATCHMAKING", item_w, items_wide)
    lines += _widget(f"{base}/matchmaking_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                                  ("flags", "pass_unhandled_to_focused_child up_down_tabs_items")],
                     ['<data input="port matchmaking update"/>', *rows,
                      f'<child widget="{base}/matchmaking_prompts" y="414"/>'])
    # (a playlist's row: its name, filled in, and what its search does at
    # its end)
    for row in range(MATCHMAKING_ROWS):
        lines += _widget(f"{base}/mm_row_{row}", [("type", "text"), ("width", item_w), ("height", item_h),
                                                  ("wide", items_wide), ("bitmap", part_highlight(item_w, item_h)),
                                                  ("font", "ui\\large_ui"), ("color", SETTINGS_BLUE), ("text_x", 10),
                                                  ("text_y", 5)],
                         ['<on event="a" run="port matchmaking button"/>',
                          '<on event="start" run="port matchmaking button"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          f'<child widget="{base}/mm_note_{row}"/>'])
        lines += _widget(f"{base}/mm_note_{row}", [("type", "text"), ("width", item_w - 16), ("height", item_h),
                                                   ("wide", wide(0, HUB_WIDE_ITEMS)), ("font", "ui\\small_ui"),
                                                   ("color", "#FF5ED38C"), ("align", "right"), ("text_y", 8),
                                                   ("text_flags", "no_focus_test")], [])
    # (the panel, Multiplayer's: menu_functions.c's matchmaking_screen_update)
    x, inner = left + 14, width - 28
    panel = []

    def text(name, ty, h, font, color, words=None):
        panel.append(f'<child widget="{base}/{name}"/>')
        pairs = [("left", x), ("top", top + ty), ("width", inner), ("height", h), ("font", font), ("color", color),
                 ("wide", wide(0, HUB_WIDE_PANEL[1]))]
        return (_widget(f"{base}/{name}", [("type", "text"), ("controller", 1), *pairs, ("text", words),
                                           ("text_flags", "no_focus_test")], [])
                if words else _part_text(f"{base}/{name}", pairs))

    lines += text("panel_title", 14, 24, "ui\\large_ui", "#FFFFFFFF")
    lines += text("panel_about", 40, 36, "ui\\small_ui", SETTINGS_BLUE)
    lines += text("panel_status_label", 84, 16, "ui\\small_ui", PART_DIM, words="SEARCH")
    lines += text("panel_status", 100, 24, "ui\\large_ui", "#FFFFFFFF")
    lines += text("panel_detail", 126, 84, "ui\\small_ui", SETTINGS_BLUE)
    lines += text("panel_help", height - 76, 64, "ui\\small_ui", SETTINGS_BLUE,
                  words="Matchmaking finds a game with room for your whole party, by ping, or hosts one that "
                        "players searching the same playlist join. Your party comes with you.")
    lines += _widget(f"{base}/matchmaking_panel", [("controller", 1), ("left", left), ("top", top),
                                                   ("width", width), ("height", height),
                                                   ("bitmap", part_panel(width, height)),
                                                   ("wide", wide(*HUB_WIDE_PANEL))], panel)
    lines += _prompts(base, "matchmaking_prompts", MATCHMAKING_PROMPTS)
    return lines


# the Friends screen's (_friends, menu_functions.c's friends_menu.inc): its
# rows (the first this profile's own code, then the list:
# port/linux/src/p2p_friends.c's order), and its footer's prompts, those of
# the row chosen shown (friends_screen_update): its first action (A), its
# second (X), its third (Y: REMOVE, BLOCK, or the own row's APPEAR OFFLINE
# or ONLINE), each wide enough for the keyboard's keys' names ("Enter")
FRIENDS_ROWS = 10
FRIENDS_PROMPTS = [
    ("prompt_copy", "%a-button COPY CODE", 28, 144, '<on event="a" run="port friends button"/>'),
    ("prompt_join", "%a-button JOIN", 28, 144, '<on event="a" run="port friends button"/>'),
    ("prompt_accept", "%a-button ACCEPT", 28, 144, '<on event="a" run="port friends button"/>'),
    ("prompt_add", "%a-button ADD FRIEND", 28, 144, '<on event="a" run="port friends button"/>'),
    ("prompt_unblock", "%a-button UNBLOCK", 28, 144, '<on event="a" run="port friends button"/>'),
    ("prompt_code", "%x-button ADD BY CODE", 176, 144, '<on event="a" run="port friends button"/>'),
    ("prompt_invite", "%x-button INVITE", 176, 144, '<on event="a" run="port friends button"/>'),
    ("prompt_ignore", "%x-button IGNORE", 176, 144, '<on event="a" run="port friends button"/>'),
    ("prompt_cancel", "%x-button CANCEL", 176, 144, '<on event="a" run="port friends button"/>'),
    ("prompt_remove", "%y-button REMOVE", 324, 164, '<on event="a" run="port friends button"/>'),
    ("prompt_block", "%y-button BLOCK", 324, 164, '<on event="a" run="port friends button"/>'),
    ("prompt_hide", "%y-button APPEAR OFFLINE", 324, 164, '<on event="a" run="port friends button"/>'),
    ("prompt_show", "%y-button APPEAR ONLINE", 324, 164, '<on event="a" run="port friends button"/>'),
    ("prompt_back", "%b-button BACK", 516, 96, '<on event="a" run="port friends button"/>'),
]
# (the touchscreen's: buttons, their words alone)
TOUCH_FRIENDS_PROMPTS = [
    (name, words.split(" ", 1)[1], x, width, handler) for name, words, x, width, handler in [
        ("prompt_copy", "% COPY CODE", 28, 120, '<on event="a" run="port friends button"/>'),
        ("prompt_join", "% JOIN", 28, 120, '<on event="a" run="port friends button"/>'),
        ("prompt_accept", "% ACCEPT", 28, 120, '<on event="a" run="port friends button"/>'),
        ("prompt_add", "% ADD FRIEND", 28, 120, '<on event="a" run="port friends button"/>'),
        ("prompt_unblock", "% UNBLOCK", 28, 120, '<on event="a" run="port friends button"/>'),
        ("prompt_code", "% ADD BY CODE", 152, 120, '<on event="a" run="port friends button"/>'),
        ("prompt_invite", "% INVITE", 152, 120, '<on event="a" run="port friends button"/>'),
        ("prompt_ignore", "% IGNORE", 152, 120, '<on event="a" run="port friends button"/>'),
        ("prompt_cancel", "% CANCEL", 152, 120, '<on event="a" run="port friends button"/>'),
        ("prompt_remove", "% REMOVE", 276, 106, '<on event="a" run="port friends button"/>'),
        ("prompt_block", "% BLOCK", 276, 106, '<on event="a" run="port friends button"/>'),
        ("prompt_hide", "% HIDE ME", 276, 106, '<on event="a" run="port friends button"/>'),
        ("prompt_show", "% SHOW ME", 276, 106, '<on event="a" run="port friends button"/>'),
        ("prompt_back", "% BACK", 502, 110, '<on event="a" run="port friends button"/>'),
    ]]


def _friends_panel(base: str, place: tuple, wide_panel: tuple, folder: str) -> list:
    """the Friends screen's panel, at place (left, top, width, height), wider
    on a wide screen by wide_panel: the row chosen's kind, name, where it is
    and what it does (menu_functions.c's friends_screen_update), and this
    profile's code at its foot"""
    left, top, width, height = place
    x, inner = left + 14, width - 28
    narrow = inner < 240
    panel, lines = [], []

    def text(name, ty, h, font, color, words=None):
        panel.append(f'<child widget="{folder}/{name}"/>')
        pairs = [("left", x), ("top", top + ty), ("width", inner), ("height", h), ("font", font), ("color", color),
                 ("wide", wide(0, wide_panel[1]))]
        return (_widget(f"{folder}/{name}", [("type", "text"), ("controller", 1), *pairs, ("text", words),
                                             ("text_flags", "no_focus_test")], [])
                if words else _part_text(f"{folder}/{name}", pairs))

    lines += text("panel_kind", 12, 16, "ui\\small_ui", PART_DIM)
    lines += text("panel_title", 28, 24, "ui\\large_ui", "#FFFFFFFF")
    lines += text("panel_status", 56, 44, "ui\\large_ui", SETTINGS_BLUE)
    lines += text("panel_detail", 104, 100 if narrow else 84, "ui\\small_ui", SETTINGS_BLUE)
    lines += text("panel_code_label", height - 96, 16, "ui\\small_ui", PART_DIM, words="YOUR FRIEND CODE")
    lines += text("panel_code", height - 80, 24, "ui\\large_ui", "#FFFFFFFF")
    lines += text("panel_help", height - 54, 44, "ui\\small_ui", SETTINGS_BLUE)
    lines += _widget(f"{folder}/friends_panel", [("controller", 1), ("left", left), ("top", top), ("width", width),
                                                ("height", height), ("bitmap", part_panel(width, height)),
                                                ("wide", wide(*wide_panel))], panel)
    return lines


def _friends() -> list:
    """the Friends screen (port/linux/game/friends_menu.inc), in
    Multiplayer's look (_hub's parts): FRIENDS's section (how many are
    online at its end) over its rows: this profile's code first (COPY CODE,
    ADD BY CODE), then the friends online, the friend requests, the friends
    offline, the requests sent, the recent players and the codes blocked,
    each its name and, at its end, where it is or what it is; the panel
    beside them (the row chosen: its kind, name, where it is and its game,
    and this profile's code); the footer's prompts of the row chosen: COPY
    CODE, JOIN, ACCEPT, ADD FRIEND or UNBLOCK (A), ADD BY CODE, INVITE,
    IGNORE or CANCEL (X), APPEAR OFFLINE or ONLINE, REMOVE or BLOCK (Y),
    BACK. The list scrolls
    at its ends. On a wide screen it is laid out as Multiplayer is
    (HUB_WIDE_ITEMS, HUB_WIDE_PANEL); the touchscreen's is _touch_friends"""
    base = f"{MT}/friends"
    top = HUB_PANEL[1]
    item_w, item_h = HUB_ITEM
    step = item_h + 2
    items_wide = wide(-WIDE_HALF, HUB_WIDE_ITEMS)
    lines = _widget(f"{base}/friends_screen", [("width", 640), ("height", 480),
                                               ("flags", "pass_unhandled_to_focused_child"),
                                               ("bitmap", "bitmaps/gradient")],
                    ['<on event="created" run="port friends open"/>', '<on event="b" back="true"/>',
                     '<on event="back" back="true"/>', '<on event="x" run="port friends x"/>',
                     '<on event="y" run="port friends y"/>',
                     f'<child widget="{MT}/header_multiplayer"/>',
                     f'<child widget="{base}/section_friends" x="{HUB_LIST_X}" y="{top}" platform="pointer"/>',
                     f'<child widget="{base}/friends_count" platform="pointer"/>',
                     f'<child widget="{base}/touch/section_friends" x="{BROWSER_LIST_X}" y="{top}" '
                     'platform="touch"/>',
                     f'<child widget="{base}/touch/friends_count" platform="touch"/>',
                     f'<child widget="{base}/friends_panel" platform="pointer"/>',
                     f'<child widget="{base}/friends_list" platform="pointer"/>',
                     f'<child widget="{base}/touch/friends_panel" platform="touch"/>',
                     f'<child widget="{base}/touch/friends_list" platform="touch"/>'])
    lines += _section(f"{base}/section_friends", "FRIENDS", item_w, items_wide)
    lines += _part_text(f"{base}/friends_count", [("left", HUB_LIST_X), ("top", top + 2), ("width", item_w - 4),
                                                 ("height", 16), ("font", "ui\\small_ui"), ("color", PART_DIM),
                                                 ("align", "right"), ("wide", items_wide)])
    rows = [f'<child widget="{base}/fr_row_{row}" x="{HUB_LIST_X}" y="{top + PART_SECTION_HEIGHT + 2 + step * row}"/>'
            for row in range(FRIENDS_ROWS)]
    lines += _widget(f"{base}/friends_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                              ("flags", "pass_unhandled_to_focused_child up_down_tabs_items")],
                     ['<data input="port friends update"/>', *rows,
                      f'<child widget="{base}/friends_prompts" y="414"/>'])
    # (a row: its name, filled in, and at its end where it is: green for a
    # friend online or a request, blue otherwise)
    for row in range(FRIENDS_ROWS):
        lines += _widget(f"{base}/fr_row_{row}", [("type", "text"), ("width", item_w), ("height", item_h),
                                                  ("wide", items_wide), ("bitmap", part_highlight(item_w, item_h)),
                                                  ("font", "ui\\large_ui"), ("color", SETTINGS_BLUE), ("text_x", 10),
                                                  ("text_y", 5)],
                         ['<on event="a" run="port friends button"/>',
                          '<on event="start" run="port friends button"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          f'<child widget="{base}/fr_note_{row}"/>', f'<child widget="{base}/fr_live_{row}"/>'])
        for note, color in ((f"fr_note_{row}", "#FF7FB2FF"), (f"fr_live_{row}", "#FF5ED38C")):
            lines += _widget(f"{base}/{note}", [("type", "text"), ("width", item_w - 16), ("height", item_h),
                                                ("wide", wide(0, HUB_WIDE_ITEMS)), ("font", "ui\\small_ui"),
                                                ("color", color), ("align", "right"), ("text_y", 8),
                                                ("text_flags", "no_focus_test")], [])
    lines += _friends_panel(base, HUB_PANEL, HUB_WIDE_PANEL, base)
    lines += _prompts(base, "friends_prompts", FRIENDS_PROMPTS)
    lines += _friends_add()
    return lines


def _friends_add() -> list:
    """the Friends screen's ADD BY CODE: a friend's code typed (a code on the
    clipboard first, if there is one), SEND REQUEST sends it a friend request
    if it is a code (its check character right); else its help says so"""
    base = f"{MT}/friends/add"
    spec = {"screen": "add_screen", "spacing": 28, "help_top": 120,
            "header": ("header_add_friend", f"{MT}/header_multiplayer")}
    extra = _value_row(base, "code", 0, "port friends code edit")
    extra += _button(f"{base}/button_defaults", 3, [])
    extra += _widget(f"{base}/button_ok", [("type", "text"), ("width", 128), ("height", 24),
                                           ("bitmap", "bitmaps/text_button_background"), ("text", "SEND REQUEST"),
                                           ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"),
                                           ("text_y", 2)],
                     ['<on event="a" run="port friends code send"/>', '<on event="start" run="port friends code send"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    extra += _strings(f"{base}/labels", ["FRIEND CODE:"])
    # (by its state: friends_menu.inc's friends_add_update)
    extra += _strings(f"{base}/help_strings", [
        "",
        "Type your friend's code (like K7QM-2XPA-9DTR), then\\npress Enter to send them a friend request.",
        "That code has a typo: check it and try again.",
        "That is not a friend code: it has 12 letters and digits.",
        "That is your own code. Send it to your friends.",
        "Already your friend, or a request is waiting.",
        "Too many friends or requests. Remove some first.",
        "Make a player profile first (Settings).",
    ])
    lines = _screen(base, spec, [(f"{base}/op_code", None)], ["port friends code update"],
                    ['<on event="created" run="port friends code init"/>'], extra)
    lines = [line.replace(f'<child widget="{base}/button_defaults" y="1"/>', "") for line in lines]
    lines = [line.replace('<on event="b" back="true"/>', '<on event="b" run="port friends code back"/>')
             .replace('<on event="back" back="true"/>', '<on event="back" run="port friends code back"/>')
             for line in lines]
    return lines


# ---------- Host Controls (port/linux/game/host_controls_menu.inc,
# network_host_controls.c): the pause menu's HOST CONTROLS, the host's of a
# network game alone (menu_tags.c's pause_patch), laid out as the server
# browser is (its rows, columns, panel and button: the menus' shared parts)
HOST_CONTROLS = "main_menu/host_controls"
# (BOTS last: host_controls_menu.inc hides it in co-op, leaving no gap)
HOST_TABS = [("tab_players", "PLAYERS"), ("tab_game", "GAME"), ("tab_maps", "MAPS"), ("tab_playlist", "PLAYLIST"),
             ("tab_bans", "BANS"), ("tab_bots", "BOTS")]
# the rows (the browser's), and each column's place and width in a row, with
# its move and growth on a wide screen (the list BROWSER_WIDE_LIST wider): a
# player's name, team, score, ping and tag (a pill: HOST, BOT, OPENH1,
# OPENCE), or Game's and Bans' label and value (at the row's right)
HOST_ROWS = BROWSER_ROWS
HOST_COLUMNS = {
    "label": (6, 150, 0, 84), "team": (160, 66, 84, 20), "score": (230, 50, 104, 20), "ping": (284, 44, 124, 20),
    "tag": (336, BROWSER_PILL, BROWSER_WIDE_LIST, 0), "value": (190, 208, 0, BROWSER_WIDE_LIST),
}
# the panel's buttons (a player's Kick, Ban, Move and Mute; a ban's Unban),
# the browser's JOIN GAME's size, a step apart up from the panel's foot
HOST_ACTIONS, HOST_ACTION_STEP = 4, 28
HOST_PROMPTS = [
    ("prompt_back", "%b-button BACK", 516, 96, '<on event="a" run="mouse emit back event"/>'),
]
TITLES[f"{HOST_CONTROLS}/header_host_controls"] = "HOST CONTROLS"


def _host_controls() -> list:
    """Host Controls (port/linux/game/host_controls_menu.inc), in the server
    browser's look: its tabs (PLAYERS, GAME, MAPS, PLAYLIST, BANS, BOTS: LB
    and RB, Q and E, or a click), the counts of the game's players and its kind, the column titles
    over the rows (each tab's own: menu_functions.c fills them), the rows
    (the players, the game's settings, the bans), the row chosen's details in
    the panel beside them with its buttons, words in the rows' place while
    there are none, and the footer's BACK. A or a click on a row chooses it
    (a player's or a ban's: the focus to its buttons; a game's row: it
    changes, or for End game and Restart round asks again), left and right
    step a game's row's value"""
    base = HOST_CONTROLS
    list_name = f"{base}/host_list"
    panel_left, panel_top, panel_width, panel_height = BROWSER_PANEL
    inner_left, inner_width = panel_left + 12, panel_width - 24
    panel_moved, panel_grown = BROWSER_WIDE_PANEL
    list_wide = wide(-WIDE_HALF, BROWSER_WIDE_LIST)
    lines = _widget(f"{base}/host_controls_screen",
                    [("width", 640), ("height", 480), ("flags", "pass_unhandled_to_focused_child"),
                     ("bitmap", "bitmaps/gradient")],
                    ['<on event="b" run="port host back" back="true"/>',
                     '<on event="back" run="port host back" back="true"/>',
                     '<on event="white" run="port host tab previous"/>',
                     '<on event="black" run="port host tab next"/>',
                     f'<child widget="{base}/header_host_controls"/>',
                     f'<child widget="{base}/host_counts"/>',
                     f'<child widget="{SETTINGS}/tab_prompt_previous" x="{WIDE_MARGIN}" y="{SETTINGS_TABS_TOP}" '
                     'platform="pointer"/>',
                     f'<child widget="{SETTINGS}/tab_prompt_next" '
                     f'x="{SETTINGS_TABS_X + (len(HOST_TABS) - 1) * SETTINGS_TAB_STEP + SETTINGS_TAB_LAID + 4}" '
                     f'y="{SETTINGS_TABS_TOP}" platform="pointer"/>',
                     *(f'<child widget="{base}/header_{column}" platform="pointer"/>' for column in HOST_COLUMNS
                       if column != "tag"),
                     f'<child widget="{base}/host_columns_line" platform="pointer"/>',
                     f'<child widget="{base}/host_empty" platform="pointer"/>',
                     f'<child widget="{list_name}" platform="pointer"/>',
                     *_touch_host_controls()[0]])
    lines += _widget(f"{base}/header_host_controls", [("controller", 1), ("left", 35), ("top", 11), ("width", 363),
                                                      ("height", 59), ("bitmap", f"{base}/header_host_controls"),
                                                      ("wide", WIDE_LEFT)], [])
    lines += _part_text(f"{base}/host_counts", [("left", 420), ("top", 46), ("width", 192), ("height", 18),
                                               ("font", "ui\\small_ui"), ("color", PART_DIM), ("align", "right"),
                                               ("wide", WIDE_RIGHT)])
    # (the column titles, each tab's: menu_functions.c's host_controls_headers)
    for column, (x, width, moved, grown) in HOST_COLUMNS.items():
        if column == "tag":
            continue
        lines += _part_text(f"{base}/header_{column}",
                            [("left", BROWSER_LIST_X + x), ("top", BROWSER_HEADERS_TOP), ("width", width),
                             ("height", 16), ("font", "ui\\small_ui"), ("color", PART_DIM),
                             ("align", "right" if column in ("score", "ping", "value") else None),
                             ("wide", wide(-WIDE_HALF + moved, grown))])
    lines += _widget(f"{base}/host_columns_line", [("controller", 1), ("left", BROWSER_LIST_X),
                                                   ("top", BROWSER_HEADERS_TOP + 14), ("width", BROWSER_LIST_WIDTH),
                                                   ("height", 4), ("bitmap", part_line(BROWSER_LIST_WIDTH)),
                                                   ("wide", list_wide)], [])
    lines += _part_text(f"{base}/host_empty", [("left", BROWSER_LIST_X + 24), ("top", 200),
                                              ("width", BROWSER_LIST_WIDTH - 48), ("height", 80),
                                              ("font", "ui\\large_ui"), ("color", SETTINGS_BLUE),
                                              ("align", "center"), ("wide", list_wide)])
    # (the list: the tabs, the rows, the panel's buttons, the footer; the
    # panel is its description)
    rows = [f'<child widget="{base}/host_item_{row}" x="{BROWSER_LIST_X}" '
            f'y="{BROWSER_ROWS_TOP + BROWSER_ROW_STEP * (row - 1)}"/>' for row in range(1, HOST_ROWS + 1)]
    actions_top = panel_top + panel_height - 10 - BROWSER_JOIN[1] - (HOST_ACTIONS - 1) * HOST_ACTION_STEP
    actions = [f'<child widget="{base}/host_action_{index}" x="{inner_left}" '
               f'y="{actions_top + index * HOST_ACTION_STEP}"/>' for index in range(HOST_ACTIONS)]
    lines += _widget(list_name, [("type", "column_list"), ("width", 640), ("height", 480),
                                 ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                                 ("description", f"{base}/host_panel")],
                     ['<data input="port host update"/>', '<on event="created" run="port host init"/>',
                      f'<child widget="{base}/host_tabs" y="{SETTINGS_TABS_TOP}"/>', *rows, *actions,
                      f'<child widget="{base}/host_prompts" y="414"/>'])
    lines += _tab_strip(base, "host_tabs", HOST_TABS, ['<on event="a" run="port host tab"/>',
                                                       '<on event="start" run="port host tab"/>'])
    for row in range(1, HOST_ROWS + 1):
        lines += _widget(f"{base}/host_item_{row}",
                         [("width", BROWSER_LIST_WIDTH), ("height", BROWSER_ROW_HEIGHT),
                          ("bitmap", part_row(BROWSER_LIST_WIDTH, BROWSER_ROW_HEIGHT)), ("wide", list_wide)],
                         ['<on event="a" run="port host row"/>', '<on event="start" run="port host row"/>',
                          '<on event="left" run="port host left"/>', '<on event="right" run="port host right"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          *(f'<child widget="{base}/host_item_{column}" x="{x}"' + (' y="1"/>' if column == "tag" else "/>")
                            for column, (x, *_) in HOST_COLUMNS.items())])
    for column, (x, width, moved, grown) in HOST_COLUMNS.items():
        if column == "tag":
            lines += _widget(f"{base}/host_item_tag", [("type", "text"), ("width", width),
                                                       ("height", PART_PILL_HEIGHT), ("bitmap", part_pill(width)),
                                                       ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"),
                                                       ("align", "center"), ("text_flags", "no_focus_test"),
                                                       ("wide", wide(moved, grown))], [])
            continue
        lines += _widget(f"{base}/host_item_{column}",
                         [("type", "text"), ("width", width), ("height", BROWSER_ROW_HEIGHT), ("font", "ui\\small_ui"),
                          ("color", "#FFFFFFFF" if column == "label" else "#FFC9DCF7"),
                          ("align", "right" if column in ("score", "ping", "value") else None), ("text_y", 2),
                          ("text_flags", None if column == "label" else "no_focus_test"),
                          ("wide", wide(moved, grown))], [])
    # (the panel: the row chosen's name, what it is, and what its buttons
    # do or what was done: menu_functions.c's host_controls_panel)
    children = [("panel_title", 12, 48, "ui\\large_ui", "#FFFFFFFF"),
                ("panel_where", 60, 44, "ui\\small_ui", SETTINGS_BLUE),
                ("panel_status", 106, 160, "ui\\small_ui", "#FFFFFFFF")]
    panel_children = []
    for name, y, height, font, color in children:
        panel_children.append(f'<child widget="{base}/{name}" x="{inner_left}" y="{panel_top + y}"/>')
        lines += _part_text(f"{base}/{name}", [("width", inner_width), ("height", height), ("font", font),
                                               ("color", color), ("wide", wide(0, panel_grown))])
    lines += _widget(f"{base}/host_panel", [("controller", 1), ("left", panel_left), ("top", panel_top),
                                            ("width", panel_width), ("height", panel_height),
                                            ("bitmap", part_panel(panel_width, panel_height)),
                                            ("wide", wide(*BROWSER_WIDE_PANEL))], panel_children)
    join_w, join_h = BROWSER_JOIN
    for index in range(HOST_ACTIONS):
        lines += _widget(f"{base}/host_action_{index}", [("type", "text"), ("width", join_w), ("height", join_h),
                                                         ("bitmap", part_button(join_w, join_h)),
                                                         ("wide", wide(panel_moved, panel_grown)),
                                                         ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"),
                                                         ("text_x", 12), ("text_y", 5)],
                         ['<on event="a" run="port host action"/>', '<on event="start" run="port host action"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>'])
    lines += _prompts(base, "host_prompts", HOST_PROMPTS)
    return lines


# ---------- the touchscreen's layouts of the redesigned multiplayer screens
# (the menus' "touch" platform: the Android and iOS apps, debug.touch_menus):
# the server browser and Multiplayer, the lobby, Host Controls and the Party
# screen, each in the same look as the computer's (the menus' shared parts),
# by the same widgets' names (menu_functions.c finds them by their names'
# last part: each screen's own are in a "touch" folder beside its own), laid
# out for a finger: every tap target TOUCH_TAP tall, the tabs a row of chips,
# the footer's key prompts buttons (their words alone: a tap names no key),
# the details panel beside the list as on the computer, and the wide layout
# the computer's (to 16:9's, centred on a wider phone). A tap on a row of a
# list with a panel chooses it and shows it there, a second tap uses it
# (ui_widget.c's ui_mouse_tap_chooses_first): TOUCH_HINT says so

# the least height of a tap target: 44 points on a phone 1080 pixels tall at
# 420 dots an inch (its 480 lines 411 points: 0.86 points a unit), more on any
# taller or less dense display (tools/harness/tests/test_touch_screens.py's
# TOUCH_DISPLAYS)
TOUCH_TAP = 52
# the space between two targets, and the rows' step
TOUCH_GAP = 4
TOUCH_STEP = TOUCH_TAP + TOUCH_GAP
# the chips' row (under the title), the rows under it, the footer's buttons
TOUCH_CHIPS_TOP = 76
TOUCH_ROWS_TOP = TOUCH_CHIPS_TOP + TOUCH_TAP + 6
TOUCH_FOOTER_TOP = 414
# the footer's band (bitmaps/gradient's line across the screens): rows and
# panels end over it, the footer's buttons are under it
TOUCH_FOOTER_BAND = 410
TOUCH_HINT = "Tap to choose, tap again to open."
# the browser's and Host Controls' list (its rows TOUCH_LIST_WIDTH wide,
# BROWSER_WIDE_LIST wider on a wide screen) and the panel beside it
TOUCH_LIST_WIDTH = 364
TOUCH_PANEL_X = BROWSER_LIST_X + TOUCH_LIST_WIDTH + 8
TOUCH_PANEL_WIDTH = 640 - BROWSER_LIST_X - TOUCH_PANEL_X
TOUCH_BROWSER_ROWS = 5
TOUCH_BROWSER_PANEL = (TOUCH_PANEL_X, TOUCH_ROWS_TOP, TOUCH_PANEL_WIDTH,
                       TOUCH_ROWS_TOP + (TOUCH_BROWSER_ROWS - 1) * TOUCH_STEP + TOUCH_TAP - TOUCH_ROWS_TOP)
# a browser row's parts: (x, y, width, wide move, wide growth) in the row; its
# name and players on its first line, its map, mode and ping under them
TOUCH_BROWSER_CELLS = {
    "server_name": (32, 7, 220, 0, BROWSER_WIDE_LIST), "players": (272, 7, 80, BROWSER_WIDE_LIST, 0),
    "map": (32, 30, 120, 0, BROWSER_WIDE_LIST // 2),
    "type": (158, 30, 100, BROWSER_WIDE_LIST // 2, BROWSER_WIDE_LIST // 2),
    "ping": (272, 30, 80, BROWSER_WIDE_LIST, 0),
}
# the footer's buttons: (widget, words, x, width, its function); SORT's words
# are the game's (the sort: menu_functions.c's browser_header_show)
TOUCH_BROWSER_PROMPTS = [
    ("prompt_favorite", "FAVORITE", 28, 136, '<on event="a" run="port browser favorite"/>'),
    ("prompt_refresh", "REFRESH", 170, 136, '<on event="a" run="port browser refresh"/>'),
    ("prompt_paste", "PASTE LINK", 170, 136, '<on event="a" run="port browser refresh"/>'),
    ("prompt_sort", None, 312, 136, '<on event="a" run="port browser sort"/>'),
    ("prompt_back", "BACK", 502, 110, '<on event="a" run="mouse emit back event"/>'),
]
# Multiplayer's tiles: two columns TOUCH_HUB_WIDTH wide in all (HUB_WIDE_ITEMS
# wider, between them, on a wide screen) under PLAY ONLINE's and HOST's
# titles, each its name over what is live for it (its note), their words
# short enough for a tile; the panel beside them (_hub_panel's, narrower)
TOUCH_HUB_WIDTH = 368
TOUCH_TILE = ((TOUCH_HUB_WIDTH - TOUCH_GAP) // 2, TOUCH_TAP)
TOUCH_HUB_PANEL = (HUB_LIST_X + TOUCH_HUB_WIDTH + 8, HUB_PANEL[1],
                   640 - HUB_LIST_X - (HUB_LIST_X + TOUCH_HUB_WIDTH + 8), HUB_PANEL[3])
TOUCH_TILES = [
    ("section_online", "PLAY ONLINE", [("hub_matchmaking", "MATCHMAKING", "matchmaking_note"),
                                       ("hub_join_internet", "BROWSE GAMES", "browser_note"),
                                       ("hub_join_lan_item", "LOCAL GAMES", "item_note"),
                                       ("hub_join_direct", "JOIN BY LINK", "invite_note"),
                                       ("hub_party", "PARTY", "party_note"),
                                       ("hub_friends", "FRIENDS", "friends_note")]),
    ("section_host", "HOST", [("hub_create", "CREATE GAME ›", None), ("hub_create_bots", "PLAY WITH BOTS", None),
                              ("hub_coop", "CO-OP ›", None), ("hub_gametypes", "GAMETYPES", None)]),
]
# (the hint's place, width: beside the title, over the panel)
TOUCH_HUB_HINT = (330, 46, 640 - 28 - 330)
TOUCH_HUB_PROMPTS = [
    ("prompt_refresh", "REFRESH", 28, 136, '<on event="a" run="port multiplayer refresh"/>'),
    ("prompt_back", "BACK", 502, 110, '<on event="a" run="mouse emit back event"/>'),
]
# the lobby's footer: the computer's prompts and its panel's GAME SETTINGS and
# START GAME (whose words are the game's: the countdown), a row of buttons
# under the players and the panel; the panel's bots' rows TOUCH_TAP tall
TOUCH_LOBBY_PROMPTS = [
    ("prompt_team", "CHANGE TEAM", 28, 120, '<on event="a" run="port lobby team toggle"/>'),
    ("prompt_invite", "INVITE", 154, 66, '<on event="a" run="port lobby invite"/>'),
    ("prompt_add", "ADD PLAYER", 226, 106, '<on event="a" run="port lobby add player"/>'),
    ("board_settings", "SETTINGS", 338, 84, '<on event="a" run="port lobby settings"/>'),
    ("board_start", None, 428, 118, '<on event="a" run="port lobby start"/>'),
    ("prompt_leave", "LEAVE", 552, 60, '<on event="a" run="mouse emit back event"/>'),
]
TOUCH_LOBBY_PANEL = (LOBBY_PANEL[0], LOBBY_TOP, LOBBY_PANEL[2], 324)
# Host Controls': the chips over the list alone, the column titles under
# them, its rows (TOUCH_HOST_ROWS, TOUCH_HOST_STEP apart to end over the
# footer's band), each a player's name with its tag under it and its team,
# score and ping at its right (or a setting's or a ban's words and value);
# the panel beside them from the chips' top to the band, its four buttons
# stacked up from its foot under its words; BACK in the footer
TOUCH_HOST_ROWS = 5
TOUCH_HOST_STEP = TOUCH_TAP + 2
TOUCH_HOST_HEADERS_TOP = TOUCH_CHIPS_TOP + TOUCH_TAP
TOUCH_HOST_ROWS_TOP = TOUCH_FOOTER_BAND - (TOUCH_HOST_ROWS - 1) * TOUCH_HOST_STEP - TOUCH_TAP
TOUCH_HOST_PANEL = (TOUCH_PANEL_X, TOUCH_CHIPS_TOP, TOUCH_PANEL_WIDTH, TOUCH_FOOTER_BAND - TOUCH_CHIPS_TOP)
# (x, y, width, height, wide move, wide growth) in a row; the column titles over
# the team, score, ping and value
TOUCH_HOST_CELLS = {
    "label": (12, 9, 164, 20, 0, 84), "team": (180, 16, 48, 20, 84, 20), "score": (230, 16, 50, 20, 104, 20),
    "ping": (284, 16, 66, 20, 124, 20), "tag": (12, 31, BROWSER_PILL, PART_PILL_HEIGHT, 0, 0),
    "value": (180, 16, 170, 20, 84, BROWSER_WIDE_LIST - 84),
}
TOUCH_HOST_BACK = (502, 110)
# the Friends screen's (_touch_friends): its rows under FRIENDS's title
# (each TOUCH_TAP tall, to end over the footer's band), and its panel beside
# them, as Host Controls' is
TOUCH_FRIENDS_ROWS = 5
TOUCH_FRIENDS_TOP = HUB_PANEL[1] + PART_SECTION_HEIGHT + 4
TOUCH_FRIENDS_PANEL = TOUCH_HOST_PANEL
TOUCH_PARTY_PROMPTS = [
    ("prompt_make", "MAKE PARTY", 28, 140, '<on event="a" run="port party button"/>'),
    ("prompt_copy", "COPY LINK", 28, 140, '<on event="a" run="port party button"/>'),
    ("prompt_paste", "PASTE LINK", 172, 140, '<on event="a" run="port party button"/>'),
    ("prompt_follow", "JOIN LEADER", 172, 140, '<on event="a" run="port party button"/>'),
    ("prompt_end", "END PARTY", 172, 140, '<on event="a" run="port party button"/>'),
    ("prompt_leave", "LEAVE PARTY", 316, 140, '<on event="a" run="port party button"/>'),
    ("prompt_back", "BACK", 502, 110, '<on event="a" run="port party button"/>'),
]


def touch_chip(width: int, height: int) -> str:
    """a tab as a chip: an outline, filled while it has the focus, the
    current one filled brighter (menu_tabs_show's four frames)"""
    return f"{PARTS}/chip_{width}x{height}"


def touch_row(width: int, height: int) -> str:
    """a row of a list whose functions choose its frame, for a finger: faintly
    filled (a target), focused, and chosen"""
    return f"{PARTS}/touch_row_{width}x{height}"


def touch_tile(width: int, height: int) -> str:
    """a tile of a list (Multiplayer's items): faintly filled, and focused
    (two frames, which the engine shows by the focus)"""
    return f"{PARTS}/tile_{width}x{height}"


def touch_button(width: int, height: int) -> str:
    """a footer's button: outlined, and filled while it has the focus"""
    return f"{PARTS}/touch_button_{width}x{height}"


def touch_sizes() -> dict:
    """each touch part's sizes, as the layouts use them"""
    panel_inner = TOUCH_PANEL_WIDTH - 24
    buttons = {(width, TOUCH_TAP) for prompts in (TOUCH_BROWSER_PROMPTS, TOUCH_HUB_PROMPTS, TOUCH_LOBBY_PROMPTS,
                                                  TOUCH_PARTY_PROMPTS, TOUCH_FRIENDS_PROMPTS)
               for *_, width, _ in prompts}
    buttons |= {(panel_inner, TOUCH_TAP), (TOUCH_HOST_BACK[1], TOUCH_TAP)}
    return {
        "chip": sorted({(TOUCH_CHIP_WIDTHS[count], TOUCH_TAP) for count in TOUCH_CHIP_WIDTHS} |
                       {(width, TOUCH_TAP) for width in TOUCH_HOST_CHIP_WIDTHS}),
        "row": [(TOUCH_LIST_WIDTH, TOUCH_TAP)],
        "tile": sorted({TOUCH_TILE, (TOUCH_HUB_WIDTH, TOUCH_TAP), (TOUCH_LOBBY_PANEL[2] - 28, TOUCH_TAP)}),
        "button": sorted(buttons),
        "panel": sorted({TOUCH_BROWSER_PANEL[2:], TOUCH_HOST_PANEL[2:], TOUCH_LOBBY_PANEL[2:],
                         TOUCH_HUB_PANEL[2:]}),
        "section": [TOUCH_HUB_WIDTH],
    }


def touch_pictures() -> list:
    """(bitmap name, [(SVG, width, height)] for its frames) of the touch
    layouts' parts, in the shared parts' look"""
    def framed(width, height, fill, opacity, stroke, radius=8, stroke_width=2):
        return (_svg(width, height, [_rounded(width, height, stroke_width / 2, radius, fill, opacity, stroke,
                                              stroke_width)]), width, height)

    sizes = touch_sizes()
    pictures = []
    for width, height in sizes["chip"]:
        radius = height / 2 - 4
        pictures.append((touch_chip(width, height), [
            framed(width, height, "#000e23", 0.6, "#1f5fb0", radius),
            framed(width, height, "#002244", 0.9, "#2299ff", radius),
            framed(width, height, "#0d3a7a", 0.9, "#5aa9ff", radius),
            framed(width, height, "#1e5fd1", 1.0, "#b8dbff", radius)]))
    for width, height in sizes["row"]:
        pictures.append((touch_row(width, height), [
            framed(width, height, "#081a40", 0.55, "none", 8, 0),
            framed(width, height, "#002244", 0.9, "#2299ff"),
            framed(width, height, "#0d3a7a", 0.9, "#8cc8ff")]))
    for width, height in sizes["tile"]:
        pictures.append((touch_tile(width, height), [
            framed(width, height, "#081a40", 0.75, "#1f4f9a"),
            framed(width, height, "#0d3a7a", 0.95, "#8cc8ff")]))
    for width, height in sizes["button"]:
        pictures.append((touch_button(width, height), [
            framed(width, height, "#0a2a5a", 0.8, "#2f7fff"),
            framed(width, height, "#1e5fd1", 1.0, "#b8dbff")]))
    # (Multiplayer's sections' titles, as wide as its tiles)
    for width in sizes["section"]:
        pictures.append((part_section(width), [(_svg(width, PART_SECTION_HEIGHT, [
            f'  <rect x="0" y="{PART_SECTION_HEIGHT - 1.5:g}" width="{width}" height="1" fill="#2995ff" '
            'fill-opacity="0.45"/>']), width, PART_SECTION_HEIGHT)]))
    # (the details panels, the shared parts' panel at the touch layouts' sizes)
    for width, height in sizes["panel"]:
        pictures.append((part_panel(width, height), [framed(width, height, "#000e23", 0.88, "#2995ff", 9.5)]))
    return pictures


# the chips' widths for a strip of so many (the browser's five), TOUCH_GAP + 2
# apart; Host Controls' six over its list alone, each as wide as its words
# need (PLAYERS, GAME, MAPS, NEXT, BANS, BOTS: TOUCH_LIST_WIDTH in all)
TOUCH_CHIP_WIDTHS = {6: 92, 5: 112}
TOUCH_HOST_CHIP_WIDTHS = [74, 52, 52, 52, 52, 52]


def _touch_chips(base: str, strip: str, tabs: list, handlers: list, span: int, width: int = None) -> list:
    """a strip of tabs as chips (a column list whose name ends "_tabs", as
    menu_tabs_show knows it): each its words, TOUCH_TAP tall, spread over
    span of the 640's width and each its part wider on a wide screen; a tap
    runs the handlers (width: each chip's, or a list of them, one a chip)"""
    width = width or TOUCH_CHIP_WIDTHS[len(tabs)]
    widths = width if isinstance(width, list) else [width] * len(tabs)
    lefts = [BROWSER_LIST_X + sum(widths[:index]) + index * (TOUCH_GAP + 2) for index in range(len(tabs))]
    grown = span * WIDE_EXTRA // 584 // len(tabs)
    lines = _widget(f"{base}/{strip}", [("type", "column_list"), ("width", 640), ("height", TOUCH_TAP),
                                       ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                    [f'<child widget="{base}/{name}" x="{lefts[index]}"/>'
                     for index, (name, _) in enumerate(tabs)])
    for index, (name, words) in enumerate(tabs):
        width = widths[index]
        lines += _widget(f"{base}/{name}", [("type", "text"), ("controller", 1), ("width", width),
                                           ("height", TOUCH_TAP), ("wide", wide(-WIDE_HALF + index * grown, grown)),
                                           ("bitmap", touch_chip(width, TOUCH_TAP)), ("text", words),
                                           ("font", "ui\\small_ui"), ("color", SETTINGS_BLUE), ("align", "center"),
                                           ("text_y", 19)],
                         handlers + ['<on event="left_mouse" run="mouse emit accept event"/>'])
    return lines


def _touch_prompts(base: str, bar: str, prompts: list, defined: tuple = (), start: str = None) -> list:
    """a footer of buttons, TOUCH_TAP tall: each its words (None: the
    game's), its function run by a tap (or A, with a controller; Start runs
    start if given, else the same); those left of the middle keep to a wide
    screen's left edge, the others to its right (those another footer has,
    by their names in defined, its)"""
    lines = _widget(f"{base}/{bar}", [("type", "column_list"), ("width", 640), ("height", TOUCH_TAP),
                                     ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                    [f'<child widget="{base}/{name}" x="{x}"/>' for name, _, x, _, _ in prompts])
    for name, words, x, width, handler in (prompt for prompt in prompts if prompt[0] not in defined):
        lines += _widget(f"{base}/{name}", [("type", "text"), ("width", width), ("height", TOUCH_TAP),
                                           ("wide", prompt_wide(x, width)),
                                           ("bitmap", touch_button(width, TOUCH_TAP)), ("text", words),
                                           ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"),
                                           ("text_y", 19)],
                         [handler, start or handler.replace('event="a"', 'event="start"'),
                          '<on event="left_mouse" run="mouse emit accept event"/>'])
    return lines


def _touch_hint(name: str, y: int) -> list:
    return _widget(name, [("type", "text"), ("controller", 1), ("left", BROWSER_LIST_X), ("top", y),
                          ("width", 300), ("height", 18), ("text", TOUCH_HINT), ("font", "ui\\small_ui"),
                          ("color", PART_DIM), ("wide", WIDE_LEFT), ("text_flags", "no_focus_test")], [])


def _touch_browser() -> tuple:
    """the server browser on a touchscreen (_browser's widgets by the same
    names, its own in join_game/touch): its tabs a row of chips, TOUCH_BROWSER_ROWS
    rows each a game's name and players over its map, mode and ping (the
    columns' titles left out: SORT says the sort), the panel beside them
    with JOIN GAME, and the footer's buttons. Returns the screen's children
    and the widgets"""
    base = f"{MT}/join_game"
    touch = f"{base}/touch"
    left, top, width, height = TOUCH_BROWSER_PANEL
    inner_left, inner_width = left + 12, width - 24
    panel_moved, panel_grown = BROWSER_WIDE_PANEL
    list_wide = wide(-WIDE_HALF, BROWSER_WIDE_LIST)
    children = [f'<child widget="{touch}/browser_empty" platform="touch"/>',
                f'<child widget="{touch}/join_game_items_list" platform="touch"/>']
    lines = _part_text(f"{touch}/browser_empty", [("left", BROWSER_LIST_X + 24), ("top", 230),
                                                 ("width", TOUCH_LIST_WIDTH - 48), ("height", 80),
                                                 ("font", "ui\\large_ui"), ("color", SETTINGS_BLUE),
                                                 ("align", "center"), ("wide", list_wide)])
    rows = [f'<child widget="{touch}/server_item_{row}" x="{BROWSER_LIST_X}" '
            f'y="{TOUCH_ROWS_TOP + TOUCH_STEP * (row - 1)}"/>' for row in range(1, TOUCH_BROWSER_ROWS + 1)]
    join_y = top + height - TOUCH_TAP - 10
    lines += _widget(f"{touch}/join_game_items_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                                      ("flags", "pass_unhandled_to_focused_child "
                                                                "up_down_tabs_children"),
                                                      ("description", f"{touch}/browser_panel")],
                     ['<data input="gamespy screen update"/>',
                      f'<child widget="{touch}/browser_tabs" y="{TOUCH_CHIPS_TOP}"/>', *rows,
                      f'<child widget="{touch}/browser_join_bar" y="{join_y}"/>',
                      f'<child widget="{touch}/join_game_button_bar" y="{TOUCH_FOOTER_TOP}"/>'])
    lines += _touch_chips(touch, "browser_tabs", BROWSER_TABS, ['<on event="a" run="port browser tab"/>',
                                                                '<on event="start" run="port browser tab"/>'], 584)
    row_w = TOUCH_LIST_WIDTH
    for row in range(1, TOUCH_BROWSER_ROWS + 1):
        lines += _widget(f"{touch}/server_item_{row}",
                         [("width", row_w), ("height", TOUCH_TAP), ("bitmap", touch_row(row_w, TOUCH_TAP)),
                          ("wide", list_wide)],
                         ['<on event="a" run="gamespy select item"/>', '<on event="start" run="gamespy select item"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          f'<child widget="{FAVORITE_STAR_WIDGET}" x="10" y="18"/>',
                          *(f'<child widget="{touch}/server_item_{column}" x="{TOUCH_BROWSER_CELLS[column][0]}" '
                            f'y="{TOUCH_BROWSER_CELLS[column][1]}"/>' for column in ("server_name", "map", "type",
                                                                                      "players")),
                          f'<child widget="{touch}/server_item_tick" y="{TOUCH_BROWSER_CELLS["server_name"][1] + 2}"/>',
                          f'<child widget="{touch}/server_item_locked" y="{TOUCH_BROWSER_CELLS["server_name"][1] + 2}"/>',
                          f'<child widget="{touch}/server_item_ping" x="{TOUCH_BROWSER_CELLS["ping"][0]}" '
                          f'y="{TOUCH_BROWSER_CELLS["ping"][1] - 1}"/>',
                          f'<child widget="{touch}/server_item_ping_ms" x="{TOUCH_BROWSER_CELLS["ping"][0]}" '
                          f'y="{TOUCH_BROWSER_CELLS["ping"][1]}"/>'])
    for column in ("server_name", "map", "type", "players"):
        _, _, cell_w, moved, grown = TOUCH_BROWSER_CELLS[column]
        large = column in ("server_name", "players")
        lines += _widget(f"{touch}/server_item_{column}",
                         [("type", "text"), ("width", cell_w), ("height", 20 if large else 16),
                          ("font", "ui\\large_ui" if large else "ui\\small_ui"),
                          ("color", "#FFFFFFFF" if column == "server_name" else "#FFC9DCF7"),
                          ("align", "right" if column == "players" else None), ("wide", wide(moved, grown))], [])
    lines += _widget(f"{touch}/server_item_tick", [("type", "text"), ("width", BROWSER_TICK_BADGE),
                                                   ("height", PART_PILL_HEIGHT),
                                                   ("bitmap", part_pill(BROWSER_TICK_BADGE)), ("font", "ui\\small_ui"),
                                                   ("color", "#FFFFFFFF"), ("align", "center"),
                                                   ("text_flags", "no_focus_test")], [])
    lines += _widget(f"{touch}/server_item_locked", [("width", 16), ("height", 16),
                                                     ("bitmap", f"{base}/locked_icon")], [])
    _, _, ping_w, ping_moved, _ = TOUCH_BROWSER_CELLS["ping"]
    lines += _widget(f"{touch}/server_item_ping", [("width", BROWSER_PING_BARS), ("height", 16),
                                                   ("bitmap", PING_BARS), ("wide", wide(ping_moved))], [])
    lines += _widget(f"{touch}/server_item_ping_ms", [("type", "text"), ("width", ping_w), ("height", 16),
                                                      ("font", "ui\\small_ui"), ("color", "#FFC9DCF7"),
                                                      ("align", "right"), ("text_flags", "no_focus_test"),
                                                      ("wide", wide(ping_moved))], [])
    # (the panel: _browser's words but its players and ping, which the row
    # has; its status four lines, as menu_functions.c's browser_panel_show
    # wraps it on a touchscreen: the party's room, why it is closed, its
    # password; JOIN GAME at its foot)
    panel_children = []
    for name, y, text_h, font, color, align in (
            ("panel_title", 10, 44, "ui\\large_ui", "#FFFFFFFF", None),
            ("panel_where", 54, 30, "ui\\small_ui", SETTINGS_BLUE, None),
            ("panel_status", 126, 84, "ui\\small_ui", SETTINGS_BLUE, None)):
        text_w = 70 if name.endswith("_label") else inner_width
        panel_children.append(f'<child widget="{touch}/{name}" x="{inner_left}" y="{top + y}"/>')
        lines += _part_text(f"{touch}/{name}", [("width", text_w), ("height", text_h), ("font", font),
                                                ("color", color), ("align", align),
                                                ("wide", wide(0, panel_grown) if text_w == inner_width else None)])
    for name, x, y, pill_w in (("panel_kind", 0, 86, BROWSER_PILL), ("panel_tick", BROWSER_PILL + 4, 86, BROWSER_PILL),
                               ("panel_opence", 0, 106, BROWSER_WIDE_PILL)):
        panel_children.append(f'<child widget="{base}/{name}" x="{inner_left + x}" y="{top + y}"/>')
    lines += _widget(f"{touch}/browser_panel", [("controller", 1), ("left", left), ("top", top), ("width", width),
                                                ("height", height), ("bitmap", part_panel(width, height)),
                                                ("wide", wide(*BROWSER_WIDE_PANEL))], panel_children)
    lines += _widget(f"{touch}/browser_join_bar", [("type", "column_list"), ("width", 640), ("height", TOUCH_TAP),
                                                   ("flags", "pass_unhandled_to_focused_child")],
                     [f'<child widget="{touch}/button_join" x="{inner_left}"/>'])
    lines += _widget(f"{touch}/button_join", [("type", "text"), ("width", inner_width), ("height", TOUCH_TAP),
                                              ("bitmap", touch_button(inner_width, TOUCH_TAP)),
                                              ("wide", wide(panel_moved, panel_grown)), ("text", "JOIN GAME"),
                                              ("font", "ui\\large_ui"), ("color", "#FFFFFFFF"), ("align", "center"),
                                              ("text_y", 16)],
                     ['<on event="a" run="gamespy select button"/>', '<on event="start" run="gamespy select button"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    lines += _touch_prompts(touch, "join_game_button_bar", TOUCH_BROWSER_PROMPTS)
    return children, lines


def _touch_hub() -> tuple:
    """Multiplayer on a touchscreen (_hub's items, by the same names, in
    hub/touch): PLAY ONLINE's and HOST's items as two columns of tiles, each
    its name over what is live for it, the panel beside them (the
    computer's), REFRESH and BACK under them; CREATE GAME's and CO-OP
    CAMPAIGN's pages their two items as wide tiles"""
    touch = f"{HUB}/touch"
    tile_w, tile_h = TOUCH_TILE
    half = HUB_WIDE_ITEMS // 2
    lines = []
    handlers = {}
    joins = lambda screen: [f'<on event="a" run="mp type set mode" open="{screen}"/>',  # noqa: E731
                            f'<on event="start" run="mp type set mode" open="{screen}"/>']
    creates = ['<on event="a" run="join controller to mp game"/>',
               f'<on event="a" run="mp type set mode" open="{MT}/connected/connected_map_select_wrapper"/>']
    browser = f"{MT}/join_game/join_game_screen"
    handlers.update({
        "hub_matchmaking": [f'<on event="{event}" open="{MT}/matchmaking/matchmaking_screen"/>'
                            for event in ("a", "start")],
        "hub_join_internet": joins(browser), "hub_join_lan_item": joins(browser), "hub_join_direct": joins(browser),
        "hub_party": [f'<on event="{event}" open="{MT}/party/party_screen"/>' for event in ("a", "start")],
        "hub_friends": [f'<on event="{event}" open="{MT}/friends/friends_screen"/>' for event in ("a", "start")],
        "hub_create": [f'<on event="{event}" open="{HUB}/create_screen"/>' for event in ("a", "start")],
        "hub_create_bots": creates,
        "hub_coop": [f'<on event="{event}" open="{HUB}/coop_screen"/>' for event in ("a", "start")],
        "hub_gametypes": [f'<on event="{event}" open="main_menu/settings_select/multiplayer_setup/playlist_select/'
                          'gametype_select_screen"/>' for event in ("a", "start")],
        "hub_create_inter": creates, "hub_create_lan_item": creates, "hub_online_coop": creates,
        "hub_split_coop": [f'<on event="{event}" run="port coop begin" open="{MT}/coop/player_2_profile_screen"/>'
                           for event in ("a", "start")],
    })

    def tile(name, words, note, width, wide_layout):
        inner = [*handlers[name], '<on event="left_mouse" run="mouse emit accept event"/>']
        if note:
            inner.append(f'<child widget="{touch}/{note}" x="10" y="31"/>')
        return _widget(f"{touch}/{name}", [("type", "text"), ("width", width), ("height", tile_h),
                                           ("wide", wide_layout), ("bitmap", touch_tile(width, tile_h)),
                                           ("text", words), ("font", "ui\\large_ui"), ("color", SETTINGS_BLUE),
                                           ("text_x", 10), ("text_y", 9 if note else 17)], inner)

    # (PLAY ONLINE's three rows and HOST's two, each title just over its
    # tiles, to end over the footer's band; the hint beside the title)
    children, y = [], HUB_PANEL[1]
    for section, words, items in TOUCH_TILES:
        children.append(f'<child widget="{touch}/{section}" x="{HUB_LIST_X}" y="{y}"/>')
        lines += _section(f"{touch}/{section}", words, TOUCH_HUB_WIDTH, wide(-WIDE_HALF, HUB_WIDE_ITEMS))
        y += PART_SECTION_HEIGHT + 4
        for index, (name, label, note) in enumerate(items):
            column = index % 2
            children.append(f'<child widget="{touch}/{name}" x="{HUB_LIST_X + column * (tile_w + TOUCH_GAP)}" '
                            f'y="{y + index // 2 * TOUCH_STEP}"/>')
            lines += tile(name, label, note, tile_w, wide(-WIDE_HALF + column * half, half))
        y += (len(items) + 1) // 2 * TOUCH_STEP - TOUCH_GAP + 6
    children.append(f'<child widget="{touch}/hub_hint"/>')
    lines += _widget(f"{touch}/hub_hint", [("type", "text"), ("controller", 1), ("left", TOUCH_HUB_HINT[0]),
                                          ("top", TOUCH_HUB_HINT[1]), ("width", TOUCH_HUB_HINT[2]), ("height", 18),
                                          ("text", TOUCH_HINT), ("font", "ui\\small_ui"), ("color", PART_DIM),
                                          ("align", "right"), ("wide", WIDE_RIGHT),
                                          ("text_flags", "no_focus_test")], [])
    children.append(f'<child widget="{touch}/hub_prompts" y="{TOUCH_FOOTER_TOP}"/>')
    lines += _widget(f"{touch}/hub_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                          ("flags", "pass_unhandled_to_focused_child up_down_tabs_items"),
                                          ("description", f"{touch}/hub_panel")],
                     ['<data input="port multiplayer status"/>', *children])
    lines += _hub_panel(touch, TOUCH_HUB_PANEL, HUB_WIDE_PANEL)
    for note, color in (("browser_note", "#FF7FB2FF"), ("item_note", "#FF7FB2FF"), ("invite_note", "#FF5ED38C"),
                        ("party_note", "#FF7FB2FF"), ("matchmaking_note", "#FF5ED38C"),
                        ("friends_note", "#FF5ED38C")):
        lines += _widget(f"{touch}/{note}", [("type", "text"), ("width", tile_w - 20), ("height", 16),
                                            ("wide", wide(0, half)), ("font", "ui\\small_ui"), ("color", color),
                                            ("text_flags", "no_focus_test")], [])
    lines += _touch_prompts(touch, "hub_prompts", TOUCH_HUB_PROMPTS)
    lines += _touch_prompts(touch, "page_prompts", [TOUCH_HUB_PROMPTS[1]],
                            defined=tuple(name for name, *_ in TOUCH_HUB_PROMPTS))
    # (the pages: their two items as wide tiles under their title)
    for page, words, names in (("create", "CREATE GAME", [("hub_create_inter", "INTERNET"),
                                                          ("hub_create_lan_item", "LOCAL NETWORK")]),
                               ("coop", "CO-OP CAMPAIGN", [("hub_online_coop", "ONLINE CO-OP"),
                                                           ("hub_split_coop", "SPLIT-SCREEN CO-OP")])):
        y = HUB_PANEL[1]
        page_children = [f'<child widget="{touch}/section_{page}" x="{HUB_LIST_X}" y="{y}"/>']
        lines += _section(f"{touch}/section_{page}", words, TOUCH_HUB_WIDTH, wide(-WIDE_HALF, HUB_WIDE_ITEMS))
        y += PART_SECTION_HEIGHT + 6
        for index, (name, label) in enumerate(names):
            page_children.append(f'<child widget="{touch}/{name}" x="{HUB_LIST_X}" y="{y + index * TOUCH_STEP}"/>')
            lines += tile(name, label, None, TOUCH_HUB_WIDTH, wide(-WIDE_HALF, HUB_WIDE_ITEMS))
        page_children.append(f'<child widget="{touch}/page_prompts" y="{TOUCH_FOOTER_TOP}"/>')
        lines += _widget(f"{touch}/{page}_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                                 ("flags", "pass_unhandled_to_focused_child up_down_tabs_items"),
                                                 ("description", f"{touch}/hub_panel")],
                         ['<data input="port multiplayer status"/>', *page_children])
    return lines


def _touch_party() -> list:
    """the Party screen on a touchscreen: the computer's (_party), its footer
    a row of buttons (party/touch's party_list, by the same names)"""
    base = f"{MT}/party"
    touch = f"{base}/touch"
    lines = _widget(f"{touch}/party_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                           ("flags", "pass_unhandled_to_focused_child up_down_tabs_children")],
                    ['<data input="port party update"/>', f'<child widget="{touch}/party_prompts" '
                                                          f'y="{TOUCH_FOOTER_TOP}"/>'])
    return lines + _touch_prompts(touch, "party_prompts", TOUCH_PARTY_PROMPTS)


def _touch_friends() -> list:
    """the Friends screen on a touchscreen (_friends's widgets by the same
    names, its own in friends/touch): TOUCH_FRIENDS_ROWS rows TOUCH_TAP tall
    under FRIENDS's title, each its name over where it is, the panel beside
    them (_friends_panel's, narrower), and the footer's buttons, those of the
    row chosen shown"""
    base = f"{MT}/friends"
    touch = f"{base}/touch"
    row_w = TOUCH_LIST_WIDTH
    list_wide = wide(-WIDE_HALF, BROWSER_WIDE_LIST)
    rows = [f'<child widget="{touch}/fr_row_{row}" x="{BROWSER_LIST_X}" y="{TOUCH_FRIENDS_TOP + TOUCH_STEP * row}"/>'
            for row in range(TOUCH_FRIENDS_ROWS)]
    lines = _widget(f"{touch}/friends_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                              ("flags", "pass_unhandled_to_focused_child up_down_tabs_children")],
                    ['<data input="port friends update"/>', *rows,
                     f'<child widget="{touch}/friends_prompts" y="{TOUCH_FOOTER_TOP}"/>'])
    lines += _section(f"{touch}/section_friends", "FRIENDS", TOUCH_HUB_WIDTH, list_wide)
    lines += _part_text(f"{touch}/friends_count", [("left", BROWSER_LIST_X), ("top", HUB_PANEL[1] + 2),
                                                  ("width", row_w - 4), ("height", 16), ("font", "ui\\small_ui"),
                                                  ("color", PART_DIM), ("align", "right"), ("wide", list_wide)])
    for row in range(TOUCH_FRIENDS_ROWS):
        lines += _widget(f"{touch}/fr_row_{row}", [("type", "text"), ("width", row_w), ("height", TOUCH_TAP),
                                                   ("bitmap", touch_row(row_w, TOUCH_TAP)), ("wide", list_wide),
                                                   ("font", "ui\\large_ui"), ("color", "#FFFFFFFF"), ("text_x", 12),
                                                   ("text_y", 7)],
                         ['<on event="a" run="port friends button"/>', '<on event="start" run="port friends button"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          f'<child widget="{touch}/fr_note_{row}" x="12" y="30"/>',
                          f'<child widget="{touch}/fr_live_{row}" x="12" y="30"/>'])
        for note, color in ((f"fr_note_{row}", "#FF7FB2FF"), (f"fr_live_{row}", "#FF5ED38C")):
            lines += _widget(f"{touch}/{note}", [("type", "text"), ("width", row_w - 24), ("height", 16),
                                                 ("wide", wide(0, BROWSER_WIDE_LIST)), ("font", "ui\\small_ui"),
                                                 ("color", color), ("text_flags", "no_focus_test")], [])
    lines += _friends_panel(touch, TOUCH_FRIENDS_PANEL, BROWSER_WIDE_PANEL, touch)
    lines += _touch_prompts(touch, "friends_prompts", TOUCH_FRIENDS_PROMPTS)
    return lines


def _touch_lobby_board(base: str) -> tuple:
    """the lobby on a touchscreen (_lobby_board's players and panel, by the
    same names, in lobby/touch): the computer's players' columns; the panel
    beside them its next game and the bots' rows, each TOUCH_TAP tall; the
    footer a row of buttons under both, GAME SETTINGS and START GAME among
    them. Returns the screen's children and the widgets"""
    touch = f"{base}/touch"
    left, top, width, height = TOUCH_LOBBY_PANEL
    inner_left, inner = left + 14, width - 28
    panel_moved, panel_grown = LOBBY_WIDE_PANEL
    grown = wide(0, panel_grown)
    children = [f'<child widget="{touch}/board_panel" platform="touch"/>',
                f'<child widget="{touch}/board_list" platform="touch"/>']
    panel = []

    def text(name, y, h, font, color, words=None, align=None):
        panel.append(f'<child widget="{touch}/{name}" x="{inner_left}" y="{top + y}"/>')
        pairs = [("width", inner), ("height", h), ("font", font), ("color", color), ("align", align),
                 ("wide", grown)]
        return (_widget(f"{touch}/{name}", [("type", "text"), ("controller", 1), *pairs, ("text", words),
                                            ("text_flags", "no_focus_test")], [])
                if words else _part_text(f"{touch}/{name}", pairs))

    lines = text("board_next_label", 8, 16, "ui\\small_ui", PART_DIM, "NEXT GAME")
    lines += text("board_gametype", 24, 22, "ui\\large_ui", "#FFFFFFFF")
    lines += text("board_map", 46, 16, "ui\\small_ui", SETTINGS_BLUE)
    lines += text("board_limits", 62, 16, "ui\\small_ui", SETTINGS_BLUE)
    for name, x, y in (("board_kind", 0, 82), ("board_tick", BROWSER_PILL + 4, 82), ("board_opence", 0, 100)):
        panel.append(f'<child widget="{base}/{name}" x="{inner_left + x}" y="{top + y}"/>')
    panel.append(f'<child widget="{touch}/board_section_bots" x="{inner_left}" y="{top + 120}"/>')
    lines += _section(f"{touch}/board_section_bots", "BOTS", inner, grown)
    lines += text("board_after_label", 120, 16, "ui\\small_ui", PART_DIM, "THEN")
    lines += text("board_after", 136, 16, "ui\\small_ui", SETTINGS_BLUE)
    lines += text("board_status", height - 24, 24, "ui\\small_ui", SETTINGS_BLUE)
    lines += text("board_waiting", 142, LOBBY_START[1], "ui\\large_ui", SETTINGS_BLUE, align="center")
    lines += _widget(f"{touch}/board_panel", [("controller", 1), ("left", left), ("top", top), ("width", width),
                                              ("height", height), ("bitmap", part_panel(width, height)),
                                              ("wide", wide(*LOBBY_WIDE_PANEL))], panel)
    controls = wide(panel_moved, panel_grown)
    start = '<on event="start" run="port lobby start"/>'
    items = [f'<child widget="{base}/board_players" x="{LOBBY_LIST_X}" y="{top}"/>']
    # (each bots' row its label over its value, the value's arrows at the
    # row's ends: a tap on either half steps it)
    labels = ("BOTS", "BOT SKILL", "BOT TEAMS")
    for index, ((key, _, choices, _), label) in enumerate(zip(BOTS_ROWS, labels)):
        items.append(f'<child widget="{touch}/board_{key}" x="{inner_left}" '
                     f'y="{top + 142 + (TOUCH_TAP + 2) * index}"/>')
        lines += _widget(f"{touch}/board_{key}", [("width", inner), ("height", TOUCH_TAP),
                                                  ("flags", "pass_unhandled_to_focused_child"),
                                                  ("bitmap", touch_tile(inner, TOUCH_TAP)), ("wide", controls)],
                         [f'<child widget="{touch}/board_{key}_label" x="10" y="5"/>',
                          f'<child widget="{touch}/board_{key}_spinner"/>'])
        lines += _widget(f"{touch}/board_{key}_label", [("type", "text"), ("controller", 1), ("width", inner - 20),
                                                        ("height", 16), ("text", label), ("font", "ui\\small_ui"),
                                                        ("color", PART_DIM), ("wide", wide(0, panel_grown)),
                                                        ("text_flags", "no_focus_test")], [])
        lines += _widget(f"{touch}/board_{key}_spinner",
                         [("type", "spinner"), ("width", inner), ("height", TOUCH_TAP),
                          ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                          ("strings", "|".join(shown for shown, _ in choices)),
                          ("setting", f"bots.{key.split('_', 1)[1]}"),
                          ("values", "|".join(value for _, value in choices)), ("font", "ui\\small_ui"),
                          ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 27),
                          ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                          ("header_bounds", "28 8 40 14"),
                          ("footer_bounds", f"28 {inner - 14} 40 {inner - 8}"),
                          ("wide", wide(0, panel_grown))],
                         ['<on event="created" run="port setting load"/>', start,
                          '<on event="left_mouse" run="mouse spinner 1wide click"/>'])
    items.append(f'<child widget="{touch}/board_host" x="{inner_left}" y="{top + 160}"/>')
    lines += _widget(f"{touch}/board_host", [("type", "text"), ("width", inner), ("height", TOUCH_TAP),
                                             ("bitmap", touch_tile(inner, TOUCH_TAP)), ("wide", controls),
                                             ("text", "HOST CONTROLS"), ("font", "ui\\small_ui"),
                                             ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 19)],
                     [f'<on event="a" run="port host open" open="{HOST_CONTROLS}/host_controls_screen" '
                      'branch="true"/>', start, '<on event="left_mouse" run="mouse emit accept event"/>'])
    items.append(f'<child widget="{touch}/board_prompts" y="{TOUCH_FOOTER_TOP}"/>')
    lines += _widget(f"{touch}/board_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                             ("flags", "pass_unhandled_to_focused_child up_down_tabs_items")],
                     ['<data input="net splitscreen prejoin players"/>', '<data input="port lobby board update"/>',
                      *items])
    lines += _touch_prompts(touch, "board_prompts", TOUCH_LOBBY_PROMPTS, start=start)
    return children, lines


def _touch_host_controls() -> tuple:
    """Host Controls on a touchscreen (_host_controls' widgets by the same
    names, in host_controls/touch): its tabs a row of chips with BACK beside
    them over the list, the column titles over TOUCH_HOST_ROWS rows, the panel
    beside them with its buttons stacked from its foot, BACK in the footer.
    Returns the screen's children and the widgets"""
    base = HOST_CONTROLS
    touch = f"{base}/touch"
    left, top, width, height = TOUCH_HOST_PANEL
    inner_left, inner_width = left + 12, width - 24
    panel_moved, panel_grown = BROWSER_WIDE_PANEL
    list_wide = wide(-WIDE_HALF, BROWSER_WIDE_LIST)
    children = [*(f'<child widget="{touch}/header_{column}" platform="touch"/>'
                  for column in TOUCH_HOST_CELLS if column != "tag"),
                f'<child widget="{touch}/host_empty" platform="touch"/>',
                f'<child widget="{touch}/host_list" platform="touch"/>']
    lines = []
    for column, (x, _, cell_w, _, moved, grown) in TOUCH_HOST_CELLS.items():
        if column == "tag":
            continue
        lines += _part_text(f"{touch}/header_{column}",
                            [("left", BROWSER_LIST_X + x), ("top", TOUCH_HOST_HEADERS_TOP), ("width", cell_w),
                             ("height", 16), ("font", "ui\\small_ui"), ("color", PART_DIM),
                             ("align", "right" if column in ("score", "ping", "value") else None),
                             ("wide", wide(-WIDE_HALF + moved, grown))])
    lines += _part_text(f"{touch}/host_empty", [("left", BROWSER_LIST_X + 24), ("top", 230),
                                               ("width", TOUCH_LIST_WIDTH - 48), ("height", 80),
                                               ("font", "ui\\large_ui"), ("color", SETTINGS_BLUE),
                                               ("align", "center"), ("wide", list_wide)])
    rows = [f'<child widget="{touch}/host_item_{row}" x="{BROWSER_LIST_X}" '
            f'y="{TOUCH_HOST_ROWS_TOP + TOUCH_HOST_STEP * (row - 1)}"/>' for row in range(1, TOUCH_HOST_ROWS + 1)]
    actions_top = top + height - 8 - TOUCH_TAP - (HOST_ACTIONS - 1) * TOUCH_HOST_STEP
    actions = [f'<child widget="{touch}/host_action_{index}" x="{inner_left}" '
               f'y="{actions_top + index * TOUCH_HOST_STEP}"/>' for index in range(HOST_ACTIONS)]
    back_x, back_w = TOUCH_HOST_BACK
    lines += _widget(f"{touch}/host_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                           ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                                           ("description", f"{touch}/host_panel")],
                     ['<data input="port host update"/>', '<on event="created" run="port host init"/>',
                      f'<child widget="{touch}/host_tabs" y="{TOUCH_CHIPS_TOP}"/>', *rows, *actions,
                      f'<child widget="{touch}/host_prompts" y="{TOUCH_FOOTER_TOP}"/>'])
    # (PLAYLIST as NEXT: six chips over the list, PLAYERS's the widest)
    tabs = [(name, "NEXT" if name == "tab_playlist" else words) for name, words in HOST_TABS]
    lines += _touch_chips(touch, "host_tabs", tabs, ['<on event="a" run="port host tab"/>',
                                                          '<on event="start" run="port host tab"/>'],
                          TOUCH_LIST_WIDTH, TOUCH_HOST_CHIP_WIDTHS)
    lines += _touch_prompts(touch, "host_prompts", [("prompt_back", "BACK", back_x, back_w,
                                                     '<on event="a" run="mouse emit back event"/>')])
    for row in range(1, TOUCH_HOST_ROWS + 1):
        lines += _widget(f"{touch}/host_item_{row}",
                         [("width", TOUCH_LIST_WIDTH), ("height", TOUCH_TAP),
                          ("bitmap", touch_row(TOUCH_LIST_WIDTH, TOUCH_TAP)), ("wide", list_wide)],
                         ['<on event="a" run="port host row"/>', '<on event="start" run="port host row"/>',
                          '<on event="left" run="port host left"/>', '<on event="right" run="port host right"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          *(f'<child widget="{touch}/host_item_{column}" x="{x}" y="{y}"/>'
                            for column, (x, y, *_) in TOUCH_HOST_CELLS.items())])
    for column, (_, _, cell_w, cell_h, moved, grown) in TOUCH_HOST_CELLS.items():
        if column == "tag":
            lines += _widget(f"{touch}/host_item_tag", [("type", "text"), ("width", cell_w), ("height", cell_h),
                                                        ("bitmap", part_pill(cell_w)), ("font", "ui\\small_ui"),
                                                        ("color", "#FFFFFFFF"), ("align", "center"),
                                                        ("text_flags", "no_focus_test")], [])
            continue
        lines += _widget(f"{touch}/host_item_{column}",
                         [("type", "text"), ("width", cell_w), ("height", cell_h),
                          ("font", "ui\\large_ui" if column == "label" else "ui\\small_ui"),
                          ("color", "#FFFFFFFF" if column == "label" else "#FFC9DCF7"),
                          ("align", "right" if column in ("score", "ping", "value") else None),
                          ("text_y", 0 if column == "label" else 3),
                          ("text_flags", None if column == "label" else "no_focus_test"),
                          ("wide", wide(moved, grown))], [])
    panel_children = []
    for name, y, text_h, font, color in (("panel_title", 8, 22, "ui\\large_ui", "#FFFFFFFF"),
                                         ("panel_where", 32, 28, "ui\\small_ui", SETTINGS_BLUE),
                                         ("panel_status", 62, 48, "ui\\small_ui", "#FFFFFFFF")):
        panel_children.append(f'<child widget="{touch}/{name}" x="{inner_left}" y="{top + y}"/>')
        lines += _part_text(f"{touch}/{name}", [("width", inner_width), ("height", text_h), ("font", font),
                                                ("color", color), ("wide", wide(0, panel_grown))])
    lines += _widget(f"{touch}/host_panel", [("controller", 1), ("left", left), ("top", top), ("width", width),
                                             ("height", height), ("bitmap", part_panel(width, height)),
                                             ("wide", wide(*BROWSER_WIDE_PANEL))], panel_children)
    for index in range(HOST_ACTIONS):
        lines += _widget(f"{touch}/host_action_{index}", [("type", "text"), ("width", inner_width),
                                                          ("height", TOUCH_TAP),
                                                          ("bitmap", touch_button(inner_width, TOUCH_TAP)),
                                                          ("wide", wide(panel_moved, panel_grown)),
                                                          ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"),
                                                          ("align", "center"), ("text_y", 19)],
                         ['<on event="a" run="port host action"/>', '<on event="start" run="port host action"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>'])
    return children, lines


def multiplayer_files() -> dict:
    """the port's multiplayer widgets: the browser's additions, the server
    settings, the lobby, and the touchscreen's layouts of them (each folder's
    touch folder's file)"""
    head = ['<?xml version="1.0" encoding="UTF-8"?>',
            "<!-- The port's multiplayer screens, in the PC version's style (tools/port_settings.py) -->", "<menus>"]
    return {
        f"{MT}/join_game".replace("/", ".") + ".xml": head + _browser() + ["</menus>", ""],
        f"{MT}/join_game/touch".replace("/", ".") + ".xml": head + _touch_browser()[1] + ["</menus>", ""],
        f"{MT}/join_game".replace("/", ".") + ".port.xml": head + _join_game_extras() + ["</menus>", ""],
        f"{HUB}".replace("/", ".") + ".xml": head + _hub() + ["</menus>", ""],
        f"{HUB}/touch".replace("/", ".") + ".xml": head + _touch_hub() + ["</menus>", ""],
        f"{MT}/server_settings".replace("/", ".") + ".xml": head + _server_settings() + ["</menus>", ""],
        f"{MT}/lobby".replace("/", ".") + ".xml": head + _lobby() + ["</menus>", ""],
        f"{MT}/lobby/touch".replace("/", ".") + ".xml": head + _touch_lobby_board(f"{MT}/lobby")[1] +
        ["</menus>", ""],
        f"{MT}/coop".replace("/", ".") + ".xml": head + _coop() + ["</menus>", ""],
        f"{MT}/bots".replace("/", ".") + ".xml": head + _bots() + ["</menus>", ""],
        f"{MT}/party".replace("/", ".") + ".xml": head + _party() + ["</menus>", ""],
        f"{MT}/matchmaking".replace("/", ".") + ".xml": head + _matchmaking() + ["</menus>", ""],
        f"{MT}/party/touch".replace("/", ".") + ".xml": head + _touch_party() + ["</menus>", ""],
        f"{MT}/friends".replace("/", ".") + ".xml": head + _friends() + ["</menus>", ""],
        f"{MT}/friends/touch".replace("/", ".") + ".xml": head + _touch_friends() + ["</menus>", ""],
        HOST_CONTROLS.replace("/", ".") + ".xml": head + _host_controls() + ["</menus>", ""],
        f"{HOST_CONTROLS}/touch".replace("/", ".") + ".xml": head + _touch_host_controls()[1] + ["</menus>", ""],
        MT.replace("/", ".") + ".port.xml": head + _multiplayer_status() + ["</menus>", ""],
        "main_menu/new_select".replace("/", ".") + ".port.xml": head + _map_kind() + ["</menus>", ""],
        "main_menu/settings_select/multiplayer_setup/item_options_edit".replace("/", ".") + ".port.xml": head + _item_options_extras() + ["</menus>", ""],
        PLAYER_EDIT.replace("/", ".") + ".port.xml": head + _player_options_extras() + ["</menus>", ""],
        TEAMPLAY_EDIT.replace("/", ".") + ".port.xml": head + _teamplay_options_extras() + ["</menus>", ""],
        **{folder.replace("/", ".") + ".port.xml": head + _gametype_rules_extras(folder) + ["</menus>", ""]
           for folder in GAMETYPE_RULES_EXTRAS},
        OPENCE_EDIT.replace("/", ".") + ".port.xml": head + _opence_options() + ["</menus>", ""],
        MODS.replace("/", ".") + ".xml": [head[0], "<!-- The port's Mods screen, in the PC version's style "
                                          "(tools/port_settings.py) -->", head[2]] + _mods() + ["</menus>", ""],
        UPDATE.replace("/", ".") + ".xml": [head[0], "<!-- The update question (the Steam Frame's, gamescope's), in "
                                            "the style of the PC version's Quit (tools/port_settings.py) -->",
                                            head[2]]
        + _update() + ["</menus>", ""],
    }


# (the server browser's folder, and with it its filters, which it no longer
# has: _browser)
REPLACED_FOLDERS = [f"{MT}/server_settings", f"{MT}/join_game"]


# ---------- the mods (port/linux/game/mods.c, docs/mods.md): the main menu's
# MODS, which opens the list of the mods folder's mods, in their load order,
# each turned on or off and moved up or down (menu_functions.c's "port mods"
# functions)

# the redraws' main menu items (svg/shell/main_menu/menu_*.svg): their
# letters' top and scale, and where their middle is
MENU_ITEM_TOP, MENU_ITEM_SCALE, MENU_ITEM_MIDDLE = 6.57, 0.9037, 130.0
# the space between two of the redraws' letters, in their units (their
# capitals are 21.5 high), for the pairs the port's items have: no redraw has
# these pairs, so each is the sum of a space after the first letter and one
# before the second, fitted (least squares) to the 53 pairs the redraws' items
# and headers have (D-I, of the main menu's Credits, gives D's right side)
MENU_ITEM_SPACES = {"MO": 3.17, "OD": 2.8, "DS": 2.3}
# the mods' status icons (port/assets/icons/lucide): a mod out of the order it
# asks for, and one that cannot be played (another mod says it cannot be
# played with it, or this build cannot play it): menu_functions.c's
# mods_update shows frame 1 or 2
MOD_STATUS_ICONS = [("triangle-alert", "#FFB020"), ("circle-x", "#FF4040")]


def redraw_letters(redraws: Path) -> dict:
    """the redraws' letters (their <path id="gA">s, one drawing of each
    letter in every item and header), by letter"""
    letters = {}
    for svg in sorted(redraws.rglob("*.svg")):
        if any(part.startswith("_") for part in svg.relative_to(redraws).parts):
            continue
        for letter, path in re.findall(r'<path id="g([A-Z])" d="([^"]+)"', svg.read_text()):
            if letters.setdefault(letter, path) != path:
                sys.exit(f"{svg}: the redraws draw {letter} two ways")
    return letters


def menu_item_frames(text: str, redraws: Path) -> list:
    """the frames (svg, width, height) of a main menu item of the port's: its
    text in the redraws' own letters (redraw_letters), spaced, placed and
    coloured as their items are, dim, then white with their blue glow"""
    from fontTools.pens.boundsPen import BoundsPen
    from fontTools.svgLib.path import parse_path

    letters = redraw_letters(redraws)
    widths = []
    for letter in text:
        pen = BoundsPen(None)
        parse_path(letters[letter], pen)
        widths.append(pen.bounds[2])
    spaces = [MENU_ITEM_SPACES[first + second] for first, second in zip(text, text[1:])]
    x = MENU_ITEM_MIDDLE - (sum(widths) + sum(spaces)) * MENU_ITEM_SCALE / 2
    uses = []
    for index, letter in enumerate(text):
        uses.append(f'<use href="#g{letter}" transform="translate({x:.2f},{MENU_ITEM_TOP}) scale({MENU_ITEM_SCALE})"/>')
        x += (widths[index] + (spaces[index] if index < len(spaces) else 0)) * MENU_ITEM_SCALE
    paths = [f'    <path id="g{letter}" d="{letters[letter]}"/>' for letter in sorted(set(text))]
    head = ['<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="256" '
            'height="64" viewBox="0 0 256 64">', "  <defs>", *paths]
    dim = [*head, "  </defs>", f'  <!-- "{text}" (normal state: dim blue) -->',
           '  <g id="text" fill="#2895ff" fill-opacity="0.467">', *[f"    {use}" for use in uses], "  </g>", "</svg>", ""]
    glow = [*head, '    <filter id="glow" filterUnits="userSpaceOnUse" x="0" y="0" width="256" height="64">',
            '      <feGaussianBlur stdDeviation="2.19"/>', "    </filter>", "  </defs>",
            f'  <!-- "{text}" (highlighted: white with blue glow) -->',
            '  <g fill="#2895ff" stroke="#2895ff" stroke-width="6.3" stroke-linejoin="round" opacity="0.650" '
            'filter="url(#glow)">', *[f"    {use}" for use in uses], "  </g>",
            '  <g id="text" fill="#fff">', *[f"    {use}" for use in uses], "  </g>", "</svg>", ""]
    return [("\n".join(dim), 256, 64), ("\n".join(glow), 256, 64)]


def mod_status_frames(icons: Path, size: int = 24) -> list:
    """the frames of the mods' status icon, `size` pixels square (the menus
    draw a bitmap at its size): none, out of order, incompatible"""
    blank = f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 24 24"/>\n'
    return [(blank, size, size)] + [
        ((icons / f"{icon}.svg").read_text().replace('stroke="currentColor"', f'stroke="{color}"')
         .replace('width="24"', f'width="{size}"').replace('height="24"', f'height="{size}"'), size, size)
        for icon, color in MOD_STATUS_ICONS]


def _words_button(name: str, words: list, handlers: list) -> list:
    """a button of the button bar whose words change: its string list's, the
    one shown chosen by its string_list_index (menu_functions.c), as a text
    set on a button of fixed words is drawn over by them"""
    pairs = [("type", "text"), ("width", 128), ("height", 24), ("bitmap", "bitmaps/text_button_background"),
             ("string_list", f"{name}_words"), ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"),
             ("text_y", 2)]
    return (_widget(name, pairs, handlers + ['<on event="left_mouse" run="mouse emit accept event"/>']) +
            _strings(f"{name}_words", words))


def _text_button(name: str, text: str, handlers: list) -> list:
    """a button of the button bar with its own words"""
    pairs = [("type", "text"), ("width", 128), ("height", 24), ("bitmap", "bitmaps/text_button_background"),
             ("text", text), ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)]
    return _widget(name, pairs, handlers + ['<on event="left_mouse" run="mouse emit accept event"/>'])


def _mods() -> list:
    """the main menu's MODS and its screen, laid out as the lobby is: on the
    left two thirds the mods as a resource pack list has them (the one at the
    top playing over those below it: the load order upside down), a row each
    with its status icon, name and ON or OFF; on the right third, in the
    lobby's frame, the mod chosen: its name, what is wrong with it, whether it
    is on and its priority, and what its mod.toml says. A turns the mod chosen
    on or off, left and right (and MOVE UP and MOVE DOWN, for the mouse) move
    it; OK keeps it all (load_order.toml) and plays them, Cancel forgets it
    (menu_functions.c)"""
    base = MODS
    # (its game data input notes the main menu shown, over which the update
    # question opens: _update)
    lines = _widget("main_menu/main_menu_item_mods",
                    [("type", "text"), ("width", 256), ("height", 33), ("bitmap", "main_menu/menu_mods")],
                    ['<data input="port update question"/>',
                     f'<on event="a" open="{base}/mods_screen"/>', f'<on event="start" open="{base}/mods_screen"/>',
                     '<on event="left_mouse" run="mouse emit accept event"/>'])
    lines += _widget(f"{base}/mods_screen", [("width", 640), ("height", 480),
                                             ("flags", "pass_unhandled_to_focused_child pause_game"),
                                             ("bitmap", "bitmaps/gradient")],
                     ['<on event="b" back="true"/>', '<on event="back" back="true"/>',
                      '<child widget="main_menu/new_select/sel_list_desc_bkd"/>',
                      f'<child widget="{base}/mods_list"/>',
                      f'<child widget="{base}/header_mods"/>'])
    lines += _header(f"{base}/header_mods", f"{base}/header_mods")
    # (the lobby's rows, MOD_ROWS of them, scrolling over more mods)
    rows = [f'<child widget="{base}/op_mod_{index + 1}" x="20" y="{73 + 30 * index}"/>' for index in range(MOD_ROWS)]
    lines += _widget(f"{base}/mods_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                           ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                                           ("description", f"{base}/mod_details")],
                     ['<data input="port mods update"/>', '<on event="created" run="port mods init"/>',
                      '<on event="deleted" run="port mods close"/>', *rows,
                      f'<child widget="{base}/button_bar" y="414"/>'])
    for index in range(MOD_ROWS):
        lines += _widget(f"{base}/op_mod_{index + 1}",
                         [("width", 390), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                          ("bitmap", "bitmaps/sel_list_item_bkd_top" if index == 0 else "bitmaps/sel_list_item_bkd"),
                          ("color", "#FF2896FF")],
                         ['<on event="a" run="port mods toggle"/>', '<on event="start" run="port mods toggle"/>',
                          '<on event="left" run="port mods move up"/>', '<on event="right" run="port mods move down"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          f'<child widget="{base}/mod_status" x="8" y="2"/>',
                          f'<child widget="{base}/mod_name" x="34"/>',
                          f'<child widget="{base}/mod_state" x="318"/>'])
    lines += _widget(f"{base}/mod_status", [("controller", 1), ("width", 24), ("height", 24),
                                            ("bitmap", "mods/status")], [])
    lines += _widget(f"{base}/mod_name",
                     [("type", "text"), ("controller", 1), ("width", 284), ("height", 22), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("text_y", 4)], [])
    lines += _widget(f"{base}/mod_state",
                     [("type", "text"), ("controller", 1), ("width", 66), ("height", 22), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("align", "center"), ("text_y", 4), ("text_flags", "no_focus_test")], [])
    # (in the lobby's frame, sel_list_desc_bkd's: the mod chosen's name, the
    # icon of what is wrong with it, and the rest)
    lines += _widget(f"{base}/mod_details", [("width", 640), ("height", 480)],
                     [f'<child widget="{base}/mod_details_name"/>', f'<child widget="{base}/mod_details_status"/>',
                      f'<child widget="{base}/mod_details_text"/>'])
    lines += _widget(f"{base}/mod_details_name",
                     [("type", "text"), ("controller", 1), ("left", 412), ("top", 84), ("width", 200), ("height", 44),
                      ("font", "ui\\large_ui"), ("color", "#FFFFFFFF")], [])
    lines += _widget(f"{base}/mod_details_status", [("controller", 1), ("left", 411), ("top", 132), ("width", 16),
                                                    ("height", 16), ("bitmap", "mods/status_small")], [])
    lines += _widget(f"{base}/mod_details_text",
                     [("type", "text"), ("controller", 1), ("left", 412), ("top", 130), ("width", 200),
                      ("height", 272), ("font", "ui\\small_ui"), ("color", "#FF2896FF")], [])
    lines += _widget(f"{base}/button_bar",
                     [("type", "column_list"), ("width", 640), ("height", 28),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                     [f'<child widget="{base}/button_up" y="1"/>', f'<child widget="{base}/button_down" x="130" y="1"/>',
                      f'<child widget="{base}/button_ok" x="380" y="1"/>',
                      '<child widget="common_button_cancel" x="510" y="1"/>'])
    lines += _text_button(f"{base}/button_up", "MOVE UP", ['<on event="a" run="port mods move up"/>',
                                                          '<on event="start" run="port mods move up"/>'])
    lines += _text_button(f"{base}/button_down", "MOVE DOWN", ['<on event="a" run="port mods move down"/>',
                                                              '<on event="start" run="port mods move down"/>'])
    lines += _button(f"{base}/button_ok", 1, ['<on event="a" run="port mods save" back="true"/>',
                                              '<on event="start" run="port mods save" back="true"/>'])
    return lines


def _update() -> list:
    """the update question of the Steam Frame and of gamescope (the Steam
    Deck's Game Mode: port/linux/src/updater.c, where a system dialog would
    not show), laid out as the PC version's Quit is
    (ce/main_menu.quit_select.xml): its box, its header, and its two
    buttons, UPDATE ("port update now": the game closes, and its launcher,
    or the game itself under gamescope, puts the update in place and starts
    it) and NOT NOW (back to
    the main menu, which menu_functions.c's pc_menu_update_question_open
    opened it over); B is NOT NOW. To stop the questions: Network Setup's
    CHECK FOR UPDATES"""
    base = UPDATE
    lines = _widget(f"{base}/update_screen",
                    [("type", "text"), ("controller", 1), ("left", 180), ("top", 160), ("width", 280), ("height", 160),
                     ("flags", "pass_unhandled_to_focused_child"), ("bitmap", "bitmaps/alert_bkd"),
                     ("string_list", f"{base}/update_prompt"), ("font", "ui\\small_ui"), ("color", "#FF2896FF"),
                     ("align", "center"), ("text_x", -10), ("text_y", 40)],
                    ['<on event="b" back="true"/>', '<on event="back" back="true"/>',
                     f'<child widget="{base}/button_bar" x="182" y="293"/>',
                     f'<child widget="{base}/header_update" x="182" y="164"/>'])
    lines += _widget(f"{base}/header_update",
                     [("type", "text"), ("controller", 1), ("width", 200), ("height", 20), ("text", "NEW VERSION"),
                      ("font", "ui\\large_ui"), ("color", "#FF2896FF")], [])
    lines += _widget(f"{base}/button_bar",
                     [("type", "column_list"), ("width", 256), ("height", 20),
                      ("flags", "pass_unhandled_to_focused_child up_down_tabs_items left_right_tabs_items")],
                     [f'<child widget="{base}/button_update"/>', f'<child widget="{base}/button_later" x="128"/>'])
    lines += _text_button(f"{base}/button_update", "UPDATE", ['<on event="a" run="port update now"/>',
                                                             '<on event="start" run="port update now"/>'])
    lines += _text_button(f"{base}/button_later", "NOT NOW", ['<on event="a" back="true"/>',
                                                              '<on event="start" back="true"/>'])
    lines += _strings(f"{base}/update_prompt",
                      ["A new version is ready.\\nUpdate now? The game will\\nclose, update and start again."])
    return lines


def main() -> None:
    """writes the port's own screens' files (port/assets/menus/ce), as
    tools/ce_menus.py does with the rest, which needs the PC version's tags:
    for a change to them alone (a value added to a setting's spinner).
    A new file is listed in menus.json by hand, a new title drawn by
    ce_menus.py"""
    from pathlib import Path

    folder = Path(__file__).resolve().parent.parent / "port/assets/menus/ce"
    for name, lines in [*settings_files().items(), *multiplayer_files().items()]:
        (folder / name).write_text("\n".join(lines))


if __name__ == "__main__":
    main()
