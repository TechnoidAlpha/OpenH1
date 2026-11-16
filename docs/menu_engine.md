# The menu engine

The generated menus (`tools/ce_menus.py`, `tools/port_settings.py`) are
today flattened into Halo's widget engine: each `<widget>` of
`port/assets/menus/ce` becomes a widget definition tag (`menu_tags.c`),
which `ui_widget.c` runs as the Xbox's own. What a screen *means* is lost on
the way and found again at run time: a row's help is the string after its
label's (`settings_row_help`, index + 1), a slider's range is a table copied
by hand into `menu_functions.c` (`settings_sliders`), a row's platform is
decided as the file is read (`menu_files.c`'s `for_this_platform`, so a row
for a capability found later is never there: #853), and which rows show is
a row-name predicate run each frame (`video_rows_show`, `gamma_row_show`,
...).

The menu engine renders the generators' model instead: what the generators
already hold as data, written as data, read by the game, and drawn with
Halo's own primitives so that the look is the same. `display.menu_engine`
chooses it (`"classic"`, the default, or `"new"`). The map-tag menus
(`display.menus = "xbox"`) stay on Halo's engine; the in-game pause menu is a
hand-written screen in place of the map's ([menu_screens.md](menu_screens.md),
"The pause menu").

The engine also draws the hand-written screens
([menu_screens.md](menu_screens.md)): a screen of our own, one file in
`port/assets/menus/screens`, made into widgets in place of a generated
screen's (the main menu first), with its actions and bindings resolved in C.
The generated screens are moved to them a few at a time, and then the engine
no longer needs the generators.

## Split screen

In split screen each player's screen (the pause menu, and what it opens) is
drawn in that player's view, a half or a quarter of the screen
(`port/linux/game/menu_view.c`). It is laid out for the room it was made for
(its widget's bounds: the menus' 640 by 480, the engine's screens always; the
Xbox's own split screen pause menus 640 by 240 and 320 by 240), scaled to fit
the view's height and width (never larger than it was made) and centred in
it, so nothing of it is cut off. The view's room either side is its canvas:
the wide layout reaches across it, and a dim that covers the screen's design
(a pause menu's) covers the whole view. The game draws its menus in the units
of the viewport it is told it has, so the scale is the viewport it is told
of while the screen is drawn (the view's size divided by the scale), the
GPU's staying the view. The first player's view is where the pointer is read
back through. At full screen nothing changes.

What a split screen player's pause menu opens, a screen of the engine's
(Settings, Host Controls, Basic Training's chapters), is drawn full screen
over every view instead (`menu_view_over_views`), once a frame in the whole
screen's window after the views (`render.c`'s), and B brings back that
player's pause menu in their view. It is still that player's: their
controller drives it, and Settings edits their profile (`menu_functions.c`'s
`settings_local_player`). Settings' band shows whose profile it edits at its
right, `PROFILE: <name>` (as Select Profile's band has player 1's), and in a
split screen game `PLAYER 2 · <name>` (`settings_profile_text`, each frame:
a profile chosen or renamed shows at once; nothing without one).

## The model

`tools/ce_menus.py` writes, beside each `ce/<file>.xml`, the screens of that
file as the model: `port/assets/menus/model/<file>.xml`, read by the same
reader as the menus (`menu_files.c`, expat), embedded the same way
(`menus.json`). The XML is unchanged by it; the generators stay the one
source of both. The model is XML rather than JSON because the guest already
reads XML and has no JSON reader.

```xml
<model>
  <!-- a settings tab or page: port_settings.py's PAGES -->
  <screen name="main_menu/settings_select/.../video_settings/video_settings_screen"
          kind="settings" tab="VIDEO" title="main_menu/settings/header_settings">
    <about>The window, the frame rate and the graphics.</about>
    <section title="DISPLAY"/>
    <row key="mode" kind="choice" setting="display.mode" when="computer">
      <label>WINDOW</label>
      <help>How the game takes the screen. ...</help>
      <choice shown="FULLSCREEN" value="fullscreen"/> ...
    </row>
    <row key="fov" kind="slider" setting="display.fov"
         least="80" most="160" step="5" typed_least="20" typed_most="160">
      <label>FIELD OF VIEW</label><help>...</help><note>Click the number ...</note>
      <stop shown="..." value="..."/> ...
    </row>
    <row key="frame_rate_sync" kind="link" open="..."><label>FRAME RATE &amp; SYNC ›</label>...</row>
    <row key="command_1" kind="binding" .../>
    <row key="discord_account" kind="status" run="..."/>
    <row key="remap_player" kind="stepped"/>
    <button kind="defaults"/> <button kind="ok" run="port settings ok"/> <button kind="cancel" .../>
  </screen>
  <!-- every other screen: its tree of nodes, as the XML's widgets -->
  <screen name="main_menu/multiplayer_type_select/lobby/lobby_screen" kind="nodes">
    <node name="..." type="text" box="x0 y0 x1 y1" wide="dx dw" when="touch" .../>
  </screen>
</model>
```

- **Kinds of screen.** `settings` (a Settings tab or page, and the setup
  options' screens of rows), whose rows are their meaning, laid out by the
  engine from the generator's geometry (`SETTINGS_*`, carried in the model's
  header so that the C and the Python never disagree; a screen's
  `geometry`, another of the model's by name: the tall layout's, below); `nodes`, every other
  screen (the PC version's from its tags, the lobby, the server browser, the
  campaign, the dialogs), a tree of nodes with the XML widgets' attributes.
  The setup screens (Server Setup, its co-op options, the gametype editor's
  Item Options and OpenCE Options) are the first in the model as nodes
  (`model/nodes.xml`, written by `tools/menu_nodes.py` from `ce/`'s widgets
  after the generators have written them): each node is a widget, keyed by
  its name as the look and the behaviour are, with its parent, what A does
  (`open`, `run`), whether the focus may take it (`focusable`), the one it
  starts on (`initial`) and its condition (`when`, `tools/port_settings.py`'s
  `SETUP_NODE_WHEN`).
- **Kinds of row.** `choice` (a spinner: two values are a toggle), `slider`
  (its range, step, typed range and stops: the table `menu_functions.c`
  copies today goes), `link` (opens a page or runs a function), `binding`
  (Key Bindings' slot pair), `status` (a value the game writes), `stepped`
  (left and right step a list `menu_functions.c` keeps), `text` (a field typed
  into). Each has its label, help and note: no index + 1.
- **Conditions** (`when`, and `enabled`, without which a row is dimmed, its
  value held): an expression of the platforms
  (`desktop android web vr touch pointer computer`), capabilities found at
  run time (`hdr`, `refresh_rates`, `vrr_found`, `vrr_system`, `gyro`,
  `vr_build`, ...), states (`in_game`, `profile`, `weapon_held`) and another
  row's value shown (`mode=windowed`), with `!`, space for *or*, `+` for
  *and*. `tools/port_settings.py` writes them (`SETTINGS_ROW_WHEN`,
  `SETTINGS_ROW_ENABLED`) beside the rows' platforms, and `menu_functions.c`
  tests them each frame as a Settings page is shown, on both engines
  (`settings_rows_conditions`); a platform's term still holds as the file is
  read (#853 is a later change). The predicates `menu_functions.c` ran by
  row name on Settings' tabs and pages are gone. The setup screens' nodes'
  conditions are tested so too (`setup_nodes_conditions`), with Server
  Setup's states (`cooperative`, `local`, `internet_host`, `online`,
  `public_lobby`, `private`) and a game's spinner at its nth word
  (`loadout=1`), in place of the predicates of `server_settings_update` and
  `gametype_option_help`; Discord's UNLINK is a Settings row's condition
  (`discord_linked`).
- **Actions** are the XML's (`open`, `replace`, `back`, `close`, `run`,
  `branch`/`otherwise`, `sound`), on a row, a button or a node.
- **Behaviour** (`model/behaviour/<file>.xml`, written by
  `tools/menu_behaviour.py` from `ce/`'s widgets, as `tools/menu_look.py`
  writes their look): each generated widget's behaviour flags (how it takes
  its events: `pass_unhandled_to_focused_child`, `up_down_tabs_children`,
  ...; the place and effect flags are the look's) and its actions, its
  `<on>` handlers, as the menus' own. `menu_tags.c` makes the handlers the
  game's as it makes the widgets' (`behaviours_build`), and the engine takes
  a screen's events by them, not by the widgets' definitions.

### The look

`tools/menu_look.py` writes the look of every widget of the generated
screens to `model/look/<file>.xml`, one file for each of `ce/`'s files of
widgets. The generators call it once they have written `ce/`, so the widgets
and their looks cannot disagree, and `python3 tools/menu_look.py` writes it
again from `ce/` alone. A widget's look is its own element, keyed by the
widget's name, as the game's definitions are:

```xml
<look name="main_menu/.../button_bar" type="column_list" box="0 0 640 28" wide="-106 212" past="-143 286"
      place="anchor_left stretch keep_ends" picture="settings/panel" radius="8" effects="flash_bitmap nifty_fx"
      list="items_in_code" header="..." header_box="x0 y0 x1 y1" footer="..." footer_box="x0 y0 x1 y1">
  <text font="large" color="#FF6F9CD0" align="center" at="4 2" flags="wrap no_focus_test" scale="1 1.2"
        string_list="..." index="3" text="..." items="A|B" replace="%controller controller"/>
  <at widget="main_menu/.../button_ok" x="464" y="2"/>
</look>
```

- `box` is the widget's area in its parent's units (left, top, right,
  bottom); `wide` its wide layout (`dx dw`, as the XML's); `past` its layout
  past 16:9 (`dx dw` at 21:9 past 16:9's: the tall layout's, below); `place` the flags
  that place it on a touchscreen's wide screen and keep its picture's ends;
  `effects` the plasma and the flashing picture.
- `<text>` is the text and its style: its font, colour, alignment, where it
  starts (`at`), its text flags, its scale (`least most`: drawn so many times
  its font's size at 4:3 and at 16:9 and wider), and what it says (a `<strings>` list and an
  index, its own `text`, a spinner's `items`, its replaces). A text style's
  effects go here as attributes when they come (outlines, glows and drop
  shadows: the MSDF atlas's fourth channel is a plain distance, so each is a
  threshold in the text's shader).
- `role` is what the game's code finds the widget by, where it is not a
  model row's: Settings' `tabs` and `buttons` bars, the panel's
  `panel name`, `panel value`, `panel text` and `panel note`, Quality's
  stills (`tools/port_settings.py`'s `settings_look_roles`).
- `<at>` is where a child is in it. A child is found by its name and its
  place among those of the same name, so a widget may not have two children
  of one name for different platforms (`tools/menu_look.py` refuses it).
- What a widget does is not in its look: the flags that route its events
  and focus and its handlers are its behaviour (`model/behaviour`, above);
  its game data inputs, conditionals, controller and closing stay the
  widget definitions' until step 4.

## The engine

`port/linux/game/menu_engine.c` (layout, focus, input), `menu_engine_draw.c`
(drawing), `menu_engine_model.c` (the model read and its conditions), in the
guest, compiled as the game's sources are.

- **Each widget's look** is the model's: `ui_widget.c` reads every
  widget's definition through `menu_engine_definition`, which, under
  `"new"`, gives it the definition `menu_tags.c` makes from the widget's
  look in the model (`pc_menu_look_definition`, the first time the widget is
  asked for): its type, bounds, flags of place and effect, pictures, list
  flags, header and footer, text and its style, and its children's places
  from the look; its events, game data inputs, conditionals, description,
  controller, auto-close and other flags from its own definition. So the
  game's drawing (and its layout, mouse targets and children's creation)
  takes the look from the model without a line of it changed, and the
  look is exactly the same. The port's tables by widget (the wide layout,
  a picture's radius) answer from the look too. A widget whose look the
  model lacks, or whose look names a picture, font or strings that are not
  there, is drawn from its definition and logged ("menu engine: look from
  the widget definition"), which `tools/menu_parity.py` fails on.
- **A screen** opened under `display.menu_engine = "new"` is a tree of
  engine nodes made from the model. Each node has its box in the 640x480
  menu units, laid out each frame: `wide`, `anchor_*`, `stretch` and
  `keep_ends` as `menu_tags.c` and `ui_widget.c` apply them, a settings
  screen's rows packed from the top with their sections (what
  `settings_rows_pack` does after the fact today), hidden rows taking no
  room.
- **Drawing** uses the primitives `ui_widget.c` draws with, called the same
  way: `draw_bitmap_in_rect`, `draw_bitmap_widened_in_rect` (pictures,
  framed panels, a focused row's frame), `rasterizer_draw_unicode_string` and
  `draw_string_and_hack_in_icons` (text, through the high-res text path:
  `display.text_renderer`, MSDF), the key prompts (`hud_key_prompt_*`), the
  carets and rounded pictures (`menu_shapes.c`), the colours
  (`get_ui_argb_white`, the text's focus white, `pc_menu_text_color`) and the
  fade's alpha. The port's helpers in `ui_widget.c` that lay text out (its
  wrap and fit, #523 and #689, the key labels and legends, the caret) are
  made callable (`port:` exports of the static functions, unchanged), not
  copied. Every draw is recorded for `debug.menu_layout` through
  `menu_layout.c`, and every pointer target noted through
  `ui_mouse_note_target`, as `ui_widget.c` records them.
- **Input**: the events `ui_widget.c` makes from the controller, keyboard,
  mouse and touch (`process_ui_widgets`, `ui_widgets_process_mouse`) come to
  the screen's stand-in widget and on to the engine, which moves the focus
  (up and down a screen's rows, left and right a button bar or a spinner,
  its tabs on LB and RB), repeats held directions on `ui_widget.c`'s clock,
  and runs the actions. Sounds are `ui_play_audio_feedback_sound`'s.
- **The game's functions** (`menu_functions.c`, 12,900 lines that read and
  write widget instances by name: `named(list, "op_gamma")->visible`) run
  unchanged on both engines through a small node interface,
  `menu_node.h`: a node's name, parent, children, visibility, disabled,
  offsets, focused child and selected item, picture frame, string index and
  text. `struct widget_instance` and the engine's node both implement it;
  `menu_functions.c` moves to it mechanically (its `->visible` becomes
  `menu_node_visible()`, and so on). Behaviour is then the same code on both
  engines, which is what makes parity of behaviour provable; the row-name
  predicates are then replaced by the model's conditions screen by screen,
  each change a diff the parity harness checks.

## The hooks

Three `port:` hooks in `ui_widget.c`, each a call into the engine when it
owns the widget:

1. `ui_widget_load_by_name_or_tag`: a generated screen opened while
   `display.menu_engine` is `"new"` is made as one stand-in widget (a
   container, the screen's size, with the screen's flags: `pause_game`,
   `no_history`, ...), whose instance the engine keeps its tree on
   (`menu_engine_screen_open`). History, the stack, fades and `close all`
   stay Halo's.
2. `widget_instance_render_recursive`: the stand-in is drawn by the engine
   (`menu_engine_screen_draw`), with the offset, clip and alpha it is given.
3. `widget_instance_process_one_event_recursive`: an event for the
   stand-in goes to the engine (`menu_engine_screen_event`), which answers
   handled or not, as a widget does. (Its deletion, `ui_widget_delete`,
   frees the tree: a fourth, one-line call.)

## Parity, proved by machine

The bar: identical layout, identical pixels and identical behaviour,
allowing only the differences listed below as deliberate.

1. **Layout.** `tools/menu_parity.py --layout` runs `tools/menu_gallery.py`'s
   tour (`debug.menu_tour`) twice, once on each engine, for every screen x
   aspect (4:3, 16:10, 16:9, 21:9) x input (controller, keyboard and mouse,
   touch), `debug.null_renderer`, and diffs the `debug.menu_layout` JSON of
   each pair: every widget's box and clip, every string's text, box, clip and
   extent, every glyph and every pointer target. The stand-in's own record
   is dropped; the engine names its nodes as the widgets were named. Each
   run starts from the behaviour scripts' player profile, and a screen that
   the game opens only in some state has the game's own functions run first
   (`menu_gallery.TOUR_SETUP`: a new gametype being edited for the gametype
   editor's screens, a name's keyboard for the name screens). A screen with
   no layout on either side is not the same but not compared, which fails,
   but for the name screens, which the game shows only under the virtual
   keyboard (no widget is drawn while it is up), whose frames are compared.
2. **Pixels.** `debug.menu_parity` (the tour's mode): each screen is drawn
   by the classic engine and then by the new one into the same frame size,
   both read back in memory as the Screenshot key reads the frame
   (`d3d8_device.c`), and compared there; the log has each screen's count of differing pixels and their
   bounds. No picture is written.
3. **Behaviour.** `tools/menu_parity.py --behaviour` runs scripts of
   `debug.test_input "menu:ready ..."` on each engine from the same
   `config.toml` and player profile, and compares where the focus was after
   each press, the resulting `config.toml`, the profile's files, the
   settings the log says were written and the last screen's layout. The
   presses are counted from when the screen opened is up, on the frames'
   clock (`debug.frame_time`, which the menus' clock follows), so that a
   press lasts as many of the menus' frames in every run, however many runs
   share the computer; `menu_tour.c` logs the focus at the end of each
   second. The profile is made first by a script (a name typed into the
   profile's field) and copied into each run. The scripts: each kind of row
   (a spinner, a toggle, a slider by keys, by a click, held and by typing, a
   binding slot, by the mouse's button and wheel, a sub-page kept and
   cancelled), the tabs by LB and RB and by a click, the wheel, Defaults, OK
   and Cancel by the pad and by clicks, with the keyboard and mouse, a
   controller and touch. `--sweep` adds the navigation sweep: every generated
   screen opened, the focus moved about it (down, up, left, right, held), with
   no press that leaves it. `--baseline` runs the classic engine on both
   sides, to show the scripts give the same results run to run.

`tools/menu_parity.py` runs all three and fails on any difference not in
its list of deliberate ones, and on any widget the new engine drew from its
widget definition rather than its look in the model (step 3);
`--look-control` proves that this check fires: the first screen's look
taken out of the model (a menus folder's copy of its file) must be logged.

## Deliberate differences

Each one is listed here with its reason, and `tools/menu_parity.py` has a
matcher for it (`DELIBERATE`, `is_tall_layout`); every other screen stays
the same in layout, pixels and behaviour.

- **Profiles' MENU ENGINE row** shows the engine that draws it.
- **The hand-written screens** ([menu_screens.md](menu_screens.md)): the
  main menu (`main_menu/main_menu`) is `screens/main.xml`'s on the new engine
  and the PC version's on the classic. `tools/menu_parity.py` does not compare
  them (`HAND_WRITTEN`, `is_hand_written`). It does check that the classic
  engine drew the screen's own widgets and the new engine the hand-written
  blocks. Screenshots and scripted runs check the hand-written screens
  instead.
- **The browsers' tall layout.** The server browser (and the local
  network's list and Join by Link, its screen) and PRO 2V2's browser are
  laid out for the screen's whole height by the new engine alone
  (`tools/port_settings.py`'s `BROWSER_TALL`), in the tall layout's shared
  parts (below: `TALL_BAND`, `TALL_FOOTER`, `TALL_BACKGROUND`,
  `tall_title`): the slim header, a dark band with the
  title in words (the heading font, not the PC version's picture) and the
  counts in it, the tabs under it, the footer at the very bottom, the rows (taller, 24 units, `BROWSER_ROWS` of them,
  scrolling beyond) and the panel filling what is between; on a screen
  wider than 16:9 the list, the panel, the title and the counts reach its
  edges, the tabs and the footer as 16:9 has them. The classic engine keeps
  the PC version's places. The same generator writes both: its places are
  swapped for `BROWSER_TALL`'s (`_browser_geometry`), and the widgets that
  then differ (`tall_widgets`) have those looks in `model/look` in place of
  `ce/`'s, `tools/menu_look.py` writing them;
  their pictures at the new sizes are parts of their own
  (`parts_pictures`). A look may have a `past` ("dx dw"): its move and
  growth at 21:9 past 16:9's, and its part of that on a screen between
  them (`menu_tags.c`'s `ui_widget_port_past_scale`), which the browsers'
  give each widget with a wide layout but the tabs and the footer, so that
  they reach a wider screen's edges; the menus are drawn out to them
  (`ui_widget_port_past_extra`). The parity tool lets each item of those two
  screens differ in its place (`PLACE_KEYS`) and a text in its line breaks,
  and their titles be words in place of pictures, nothing else
  (`tall_items`).
- **Settings' search** (the new engine's alone). The first feature that
  needs the model: it reads every tab's and page's rows, their labels, help
  and choices, which the classic engine has only as widgets of one screen at a
  time, so it is the new engine's and the classic engine is unchanged. A field
  at the left end of Settings' tabs (`tools/port_settings.py`'s
  `SETTINGS_SEARCH`: each tab's `tabs_search`, the tabs after the field, which
  the new engine shows in the place of its `tabs`; at 4:3 the field is its
  magnifying glass, or a controller's Y, from 16:10 its word too), and its own
  screen (`settings_search/settings_search_screen`, in the history: back is
  where it was opened). Typing on any Settings screen, Ctrl+F, `/`, Y (the
  on-screen keyboard) or the field opens it. `port/linux/game/menu_search.c`
  ranks the rows against what is typed (a label's whole word, then the start
  of one, then a part of one; the other words of
  `SETTINGS_SEARCH_WORDS`; the choices' words but the common ones; the
  help's; every word typed must find something); `menu_engine.c` makes the
  rows found in the screen's list from their own pages' widgets (each the
  real row, which `menu_functions.c` runs as on its page, its model's row
  given by `menu_engine_model_row_of`), as many as fit, grouped under the way
  to them (each row's `crumb` in the model) and laid out from the top (in the tall layout's geometry, as Settings' tabs);
  `settings_search_menu.inc` takes what is typed, the rows' conditions (a
  term naming a row of its page that was not found: that row's setting's
  value) and the panel. Esc (B) clears it and goes back to the row it was
  opened from, the changes kept for OK, as a tab's. A word for a place that is
  not Settings' (`SETTINGS_SEARCH_ELSEWHERE`: the motion tracker, a game's)
  says where it is. `menu_parity.py` leaves the search's screen out, and each
  tab's row of tabs from its layout (its frames may differ in that row
  alone); `--search` runs the search's own scripts on the new engine
  (`SEARCH_SCRIPTS`: typed and Esc back to the row, a controller's Y and the
  on-screen keyboard, a row found that opens a page and back), and
  `tools/harness/tests/test_settings_search.py` its ranking on the real model.
  Not yet: Host Controls' and the server browser's lists.
- **Settings' tall layout.** The new engine lays Settings' tabs and pages
  out full screen, in a geometry of their own that the classic engine's
  widgets have not (`tools/port_settings.py`'s `SETTINGS_TALL`): a slim
  header (a dark band 40 units tall with its line under it, the title in it
  as words), the tabs under it, the rows from under them to the footer's
  line (24 units tall, 25 apart; their words drawn up to 1.2 times their
  font's size, as tall against the row as the classic's), the buttons at
  the bottom, and the panel from under the tabs to the footer. Past 16:9 it
  goes edge to edge (the panel about a quarter of a 21:9 screen); 4:3 has it
  at its width. The rows scroll where they are more than their room (none
  of today's pages is): the focused row kept in view, eased, a thin track at
  their right, the wheel and a touch drag moving the focus as they do. The
  classic engine keeps the 640x480 frame (`display.menu_engine = "classic"`).
  Parity: as the browsers' (`is_tall_layout`, `tall_items`), a Settings
  screen's items may differ in their places, its title be words, and its
  panel (taller, so with more of its words, facts and pictures) differ; all
  else must be the same, the new engine's rows the classic's in order (a
  run of them where it scrolls, the rest not compared). The behaviour
  scripts' clicks are each engine's places of what they click (`CLICKS`).

## The tall layout

A screen the engine draws may be laid out in a geometry of its own. It is
written by the generators, not decided by the engine: each of the screen's
widgets is generated a second time in the tall geometry
(`tools/port_settings.py`'s `tall_widgets`), and `tools/menu_look.py`
writes the look of every widget of that name from it in place of `ce/`'s,
so that the classic engine's widgets are unchanged and the new engine's
looks are the tall ones. A screen opts in so:

- its widgets in `tall_widgets` (the same names as its `ce/` widgets, so its
  behaviour and its game functions are the same): Settings' are its pages
  written in `SETTINGS_TALL`, the browsers' their generator run again with
  `BROWSER_TALL`'s places (`_browser_tall_widgets`);
- `TALL_BACKGROUND` in place of `bitmaps/gradient` (the slim header's band
  and line, the footer's line at `480 - TALL_FOOTER`), and `tall_title` for
  its title (words of a `<strings>` of `ce/`'s, in the heading font,
  `TALL_TITLE_SCALE` larger: a look's text is the strings its `ce/` widget
  was built with);
- a look's `past` ("dx dw", `port_settings.past`) for its place past 16:9:
  the move and growth at 21:9 past 16:9's, more on a wider screen
  (`menu_tags.c`'s `ui_widget_port_past_scale`); without one, a widget keeps
  16:9's place in the middle;
- a look's text `scale` ("least most": at 4:3, and at 16:9 and wider), its
  words laid out in their font and drawn larger by the high-res text's
  distance fields, about the text box's middle (`ui_widget.c`'s
  `ui_widget_port_text_scale_begin`, `rasterizer_text_set_scale`; a field
  typed into keeps its font's size, for its caret);
- on a touchscreen, its footer raised `TALL_FOOT_LIFT` units, so that its
  prompts' words clear a phone's safe area at the screen's foot
  (`tall_foot`, which the generators run on every tall screen's widgets):
  the bars placed in the footer's band flagged `foot` (moved up by the lift,
  `menu_tags.c`'s `ui_widget_port_foot_offset`), the panels that reach down
  to its line flagged `above_foot` (the lift shorter), the footer's line and
  those panels drawn from their pictures' `_touch` versions (`look_make`),
  the tall geometry's rows and panel ending the lift higher
  (`menu_engine_geometry_bottom`), and Choose Mission's CONTINUE and cards
  moved up into the room around them (`campaign_menu.inc`'s
  `mission_resume_raise`); the computer's layout is as it was;
- for a settings screen, its model's `geometry="tall"`, the model's
  `<geometry name="tall" ... scrolls="true">`, which the engine lays its rows
  out in (`engine_geometry`) and scrolls by (`engine_scroll`: its rows cut to
  their room, their pictures with their texture, `ui_widget_port_cut_set`,
  and their tap targets, `ui_widget_port_targets_clip`), and whose
  `panel_bottom` `settings_panel_show` fits the panel's words to
  (`menu_engine_settings_geometry`).

## Where it ends, and the steps there

The engine is worth having only at its end: one source, the generators'
model, rendered by one engine. Until then, a screen's look comes from its
widgets (the flattened XML, `menu_tags.c`), its meaning from the model, and
its behaviour from `menu_functions.c` on `widget_instance`s: three places,
more than before. Each step below removes one, and each is proved by
`tools/menu_parity.py` (layout, pixels and, from step 2, behaviour) before it
merges. `display.menu_engine` is `"new"` by default (since the maintainers
chose it, before the last steps); `"classic"` keeps the game's widgets.

The end:

- the model holds everything a generated screen is: its layout (bounds,
  anchors, sections, rows) and its look (bitmaps, colours, fonts), as well
  as its meaning (labels, help, choices, ranges, conditions, actions);
- the engine draws, lays out, focuses and runs every generated screen from
  the model alone;
- the screens' behaviour is handlers on the model's rows and actions, not
  `menu_functions.c` reading `widget_instance` fields by name;
- `port/assets/menus/ce`'s XML and its widget definitions stay only for the
  menus the maps carry (the in-game pause menu, `display.menus = "xbox"`),
  which Halo's engine keeps;
- `display.menu_engine` is retired, the engine the only one for those
  screens.

The steps, in order:

1. **Drawn by the engine** (done): the model written for the settings
   screens, the engine drawing every generated screen under `"new"`, the
   rows laid out and their help found from the model; 972 of 972 screen
   variants alike in layout and pixels.
2. **Input and behaviour by the engine** (done): focus, navigation, pad, keys,
   mouse, touch and repeat, the rows' actions, sliders' ranges and stops
   from the model (`settings_sliders` gone), the rows' conditions from the
   model (the row-name predicates gone); the parity tool's behaviour scripts,
   config.toml and the profile compared after each. Done so far: the
   behaviour scripts (`--behaviour`, `--sweep`); the screens' events taken
   by the engine (`menu_engine.c`'s `engine_event`, by the definitions'
   flags and handlers, which step 3 moves into the model); the sliders made
   from the model's rows on both engines (`settings_slider_made`; the table
   `settings_sliders` gone); Settings' rows' conditions from the model (the
   row-name predicates of its tabs and pages gone). `--against <build>`
   compares a build before such a change, which both engines run, with the
   one after. The widgets' behaviour flags and actions in the model
   (`model/behaviour`), by which the engine takes the events. The focus's
   way through a screen's children and a generated list's items, over its
   labels (`engine_tab`, `engine_list_step`), by the model's behaviour, not
   the definitions'; giving a widget the focus stays `ui_widget.c`'s, as do
   a list made in code and a list stepped by a handler's function. The
   setup screens in the model as nodes (`model/nodes.xml`), and their rows'
   conditions there, in place of their row-name predicates. Step 2 is done:
   what `ui_widget.c` still does for the engine (giving the focus, a list's
   frames, the history) is Halo's, as the hooks say.
3. **The look in the model** (done): bounds, anchors, places, pictures, text
   and its style, and the children's places written by the generators into
   the model for every widget of every generated screen
   (`tools/menu_look.py`, `model/look`); the engine draws them from there,
   not from widget definitions, and the parity tool fails on any widget drawn
   from its definition; 972 of 972 screen variants alike in layout and
   pixels. (For runs to be comparable, a field's caret, a menu picture's
   upscale and the icons in text follow the frames' time where it is
   fixed.)
4. **Behaviour off `widget_instance`**: each screen's handlers in
   `menu_functions.c` moved onto the model, a batch of screens at a time.
   Done so far: Settings' spinners' settings from their model rows
   (`settings_each`, `settings_setting_of`: each choice row's setting and
   its choices, the value it was loaded at kept with the row, not the
   spinner's widget definition); the tabs, the model's screens laid out
   as tabs (`settings_tab_screen`: the table `settings_tabs` gone), and the
   panel's words for a tab and a button from the model; the status rows'
   values written, and their panels' rows found, by their model rows
   (`settings_model_row`, `settings_status_set`); the setup screens' spinners
   of the host's own settings (voice and voting, co-op's options, map
   sharing, aim assist, shield effects), their settings and values from
   their nodes in the model (`gametype_option_setting`; `tools/port_settings.py`'s
   `SETUP_ROW_SETTINGS`, in place of `gametype_options`' copies). The
   gametype's own options stay in C, decided: `gametype_options` says which
   field of the game variant each spinner sets, by `offsetof`, which the C
   compiler checks where the structure is; a model naming the fields would
   need a table of names to offsets in C, the same table moved. Settings'
   tabs' bar, button bar and the panel's parts are found by their roles in
   their looks (`model/look`'s `role`, `tools/port_settings.py`'s
   `settings_look_roles`: `settings_part`), not their names. Server Setup's
   help by each row's place in it, in its node (`help_index`,
   `help_choices`: `tools/port_settings.py`'s `SERVER_SETTINGS_HELP`,
   `setup_help`), in place of `menu_functions.c`'s copies of the places.
   Left: the setup screens' values' text boxes, which their handlers still
   find by name.
5. **The switch**: `"new"` the default, played for a while; then the
   generated screens' widget XML no longer written, `display.menu_engine`
   retired.

Features that need the model (scrolling long strings, #899; one dialog
style, #901, begun as the hand-written screens' `<dialog>`; search, Settings' the first: "Deliberate differences"; live
values) come after step 2, on the engine alone.
