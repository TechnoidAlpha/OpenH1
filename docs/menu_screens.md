# The hand-written screens

The menu engine (`display.menu_engine = "new"`, [menu_engine.md](menu_engine.md))
draws the generated screens from the generators' model. That takes three
layers for one screen: Custom Edition's tags, the Python generators that
write `port/assets/menus/ce` and the model, and the C that reaches widgets
by Custom Edition's names. The hand-written screens replace those layers, one
screen at a time, with a file of our own that says what the screen is, and C
that says what its actions do. The main menu is the first.

The classic engine (`display.menu_engine = "classic"`) is not touched: it keeps
the PC version's screens, generated as before, and its parity check.

## Where things are

| Path | What it is |
| --- | --- |
| `port/assets/menus/screens/<name>.xml` | One screen a file: its building blocks, words and actions |
| `port/assets/menus/screens/blocks.xml` | The building blocks' widgets, written by hand in the menus' own format (`port/assets/menus/README.md`) |
| `port/linux/src/menu_screens.c`, `port/linux/include/menu_screens.h` | The reader: a file read with Expat into blocks and checked; hot reload |
| `port/linux/game/menu_screen.c` | The screens made into widgets, and the registry: actions, bindings, row sources, and which screen a hand-written one replaces |
| `port/linux/game/menu_screen_tall.inc` | The tall layout's blocks (a screen with a `<table>`): tabs, the table, its panel, searches and the spread bar, made and updated each frame |
| `port/linux/game/menu_table.h` | A table's row source: what C gives a `<table>` each frame, and the menus' text fields and fitted text the blocks use (menu_functions.c's) |
| `tools/harness/tests/test_menu_screens.py` | The reader's tests, asset-free |

Release builds embed the files with the other menu files (`menus.json`,
`tools/embed_assets.py`). A file of the same name in a menus folder
(`debug.menu_files`, or `menus` beside `config.toml`) replaces the embedded
one, as for every menu file.

## The format

```xml
<screen name="main" back="quit">
	<title-art/>
	<list focus="campaign">
		<item id="training" label="VR TUTORIAL" when="vr" hint="Learn to move, aim and fight in the headset" do="open training"/>
		<item id="campaign" label="CAMPAIGN" do="open campaign">
			<hint>Continue · {campaign.level} · {campaign.difficulty}</hint>
			<hint>The story · solo, or with friends online</hint>
		</item>
		<item id="versus" label="VERSUS" hint="Slayer, CTF, 2v2, Infection · find or host a game" do="open versus"/>
		<item id="settings" label="SETTINGS" hint="Video, sound, controls and your profile" do="open settings"/>
		<item id="quit" label="QUIT" do="quit"/>
	</list>
	<space/>
	<buttons>
		<button key="accept" label="SELECT"/>
		<button key="back" label="QUIT"/>
	</buttons>
</screen>
```

A file describes what is on the screen and what it does, not where each
thing is in pixels. The blocks are laid out from the top of the screen down,
in the menus' 480-unit-tall canvas (the same canvas the tall layout uses). A
`<space/>` takes the height left over, so a `<space/>` before `<buttons>` puts
the bar at the foot of the screen at every aspect ratio. Centred blocks stay
centred, and the bar's wide layout reaches the screen's edges up to 16:9.

| Element | Attributes | Meaning |
| --- | --- | --- |
| `<screen>` | `name` (required), `back`, `start`, `background` | The screen. `back` is the action for B and Back; without it, B goes back to the screen before. `start` is the action for Start anywhere on the screen (the lobby's start, as the generated lobby's every control's Start: a controller not yet in the game joins, on Add Player's screen, and only a player's starts the game); where it does nothing (a player not the host), Start is the screen's widgets' and its own handlers'. `background` (a tall screen's): `own` (the default), the generated screen's own background, the tall layout's where it has none; `tall`, the tall layout's in place of the generated screen's own (Profile Color's, whose generated screen's is the PC version's gradient, its band's line lower) |
| `<title-art/>` | | Halo's title art, where the main menu has it. If the screen's blocks need more height than the canvas has, this block gives up some of its room |
| `<list>` | `id`, `focus` | A list of items. The d-pad, the stick and the mouse wheel move through it, wrapping from the last item to the first, as the classic lists do. `focus` is the id of the item focused when the screen first opens. After that the screen remembers the last focused item |
| `<item>` | `label` (required), `do` (required), `id`, `hint`, `when`, `if` | One item. A, Start, a click or a tap runs `do`. `hint` is the line shown under the item while it is focused. Write `<hint>` children instead to give alternatives: the first whose bindings all have values is shown. `when` lists platforms (`desktop android vr hdr touch pointer computer`, as for `platform=` in the menu files) and the item is made only on one of them; `if` names a binding that must have a value for it to be made |
| `<space/>` | | Takes the height left over (several spaces share it) |
| `<buttons>` | | The button bar: each button a prompt, as Choose Mission's footer has them (its key's picture and word, lit while it has the focus; A, a click or a tap presses its key, accept running the chosen item's action), BACK at the left and the rest from the right, past 16:9 at the screen's edges, and on a touchscreen raised above a phone's safe area with the tall layout's footer (`foot`) |
| `<button>` | `key` (`accept`, `back`, `x`, `y`, `lb`, `rb`, `lt`, `rt`, `r3`, `view`, `dpad`), `label`, `side`, `do`, `width`, `touch-width`, `if`, `when` | One hint in the bar: the key's picture (on the keyboard, the key the menus use for it) and its word. `do` is the action the key (anywhere on the screen) and a click run; without it, the key is pressed. In a tall screen's bar (below), `width` is its least width, `if` a binding that must have a value for it to be shown, two buttons of one key share one place, shown as their `if`s say, and the buttons run from the left one after another, but those of `side="right"` (written after the rest) end at the right margin, as Select Profile's BACK at the left and OK, CREATE NEW and DELETE at the right (on a touchscreen, `side` changes nothing; on a wide screen those of `side="right"` keep to its right edge, wherever their place); `touch-width` is its width on a touchscreen (the bar's own by default). `dpad`'s picture is the D-pad's left and right (on the keyboard, the left and right arrow keys' caps, `<` and `>`, which are the menus' left and right) and no key presses it: it is a click's, a tap's or A's on it. A prompt hidden by its `if` leaves no gap: the shown ones are laid out again as their `if`s change |
| `<colors>` | `id` | The player profile's colors, one item each, under one another in its name and its color (white while focused). It is the screen's list (a screen has one `<list>`, `<cards>` or `<colors>`). The profile's own color is focused first. A chooses the color for the profile being edited, as the PC version's color picker did, and goes back. No shipped screen uses it now (Profile Color is a tall screen, its colors the `profile.colors` table's); it is kept for a simple screen of the colors, with `<spartan color="focus">` beside them |
| `<spartan>` | `x`, `y`, `width`, `height`, `touch-height`, `color` (`focus`, `profile`, or a binding), `players` (1 to 4) | The Spartan, which the game draws (port/linux/game/menu_spartan.c) standing in this box of the 640 by 480 canvas, its feet at the box's foot, as tall as the box. Each Spartan is drawn in a view of its own where its block is in the screen's order: over the screen's background and the blocks before it, under those after, with its own fixed camera looking at it from about its knees, a little up, as the original pictures were taken (one at a screen's edge is drawn as one in its middle, not seen from the side), depth and light, so nothing of the menus' scene (the ring, the sky) cuts it and the scene's camera never moves or tilts it. It takes no room in the layout. `color="focus"` follows the focused `<colors>` item; `profile` (the default) is the color of the profile being edited, else the player's own, live as it changes; any other word is a binding, read each frame, whose value is the color's word: a profile color's number, `random`, or a team game's `red` or `blue` (as the game gives its teams their colors); while it has no value there is neither Spartan nor picture. A binding of a screen's own `<spartan>` may give several words, up to 8, each a place side by side (`-` an empty place): a lineup. `players` puts that many side by side, each its local player's profile's color. A screen has one `<spartan>` of its own and one in each `<panel>`; `touch-height` is its height on a touchscreen, `when` the platforms it is on. A random color's Spartan cycles through the profile colors in their list's order, one a second (each held three quarters of a second, then blended into the next), on the menus' frame clock, so it cycles while the game is paused too; side by side, each random one is an eighth of the round on from the one before. Where there is no Spartan (no multiplayer map exported one, a refused file, `display.menu_pictures` off, no profile), the box shows the original picture of the color (ui.map's `player_color_marine_large`, frame 18, the "?", for random or no profile). It stands in the rifle's idle, aimed nearly at the camera (the rifle at its shoulder, its muzzle at the viewer, as the original pictures hold it), both hands on the rifle as a unit's are (the rifle class's weapon IK puts the left hand on the rifle's forend; off while it reloads or readies, whose own hands are blended to), and every 10 to 20 seconds, each at its own times (never two starting together), fidgets: reloads, checks its weapon (puts it away and readies it again), looks around (its aim and head turned slowly to one side and the other, through the cyborg's aiming and looking screens) or dips into a crouch and stands again, each blended in from the idle and back out to it; all on the menus' frame clock, drawn only. It is gone as soon as its screen closes |

### A box of rows: the pause menu's

A screen with a `<box>` is laid out as the Xbox's pause menu is, as the port
has it: a framed box of rows (its `<list>`), the legend row under them, and
beside it an objectives' box. Its places are the box's, not a flow from the
top.

| Element | Attributes | Meaning |
| --- | --- | --- |
| `<box>` | `x`, `y`, `width` (all required), `align` (`top`, the default, or `middle`) | The rounded blue box (the port's pause box, its frame for the number of rows), `width` wide from `x`, its top at `y` (or its middle, `align="middle"`), as tall as its rows and its legend need: the first row 9 units below its top, the rows 30 apart and 27 tall, the legend 14 below the last and the box's foot 25 below the legend's top. It holds the screen's `<list>`, whose items are its rows (in the middle of it, the focused one framed, its words white), and a `<legend>`. While the keyboard's keys are drawn in its legend, the box is as much wider as they need, half each side, and what is beside it moves right by half (the Xbox's width while a controller's buttons are) |
| `<legend>` | `x` | (in a `<box>`) The Xbox's legend row under the rows, `B = CANCEL  A = SELECT` (the keys' caps on the keyboard), `x` units into the box |
| `<objectives>` | `title` | A framed box beside the `<box>` (5 units to its right, as tall), its title at its top and the mission's objectives under it, a line each, as the game writes them |

A row's label can be a binding's (`{pause.friendly_fire}`), read again each
frame.

### A pop-up: `<dialog>`

A screen with a `<dialog>` is a question in a box in the middle of the
screen, drawn as the Quit question is (the PC version's `quit_screen`: its
`alert_bkd` box with its blue border and bands): its title in the box's top
band, its words under it, and its `<list>`'s items its choices, side by side
in the box's bottom band, each a button of Quit's OK and BACK. The box is
Quit's width, or wider for three or four choices (its picture's ends kept),
and as tall as its words' lines need (up to 4): the box's picture cut above
its bottom band, and that band drawn under the cut (`dialog/dialog_foot`,
`tools/port_settings.py`'s `dialog_foot_svg`). Behind it is the screen it was
opened from, as it was, dimmed and taking no input (where that is a generated
screen: the gametype editor; Quit's is over the menus' scene, its main menu
being a hand-written screen). Left and right (the d-pad, the stick, the arrow keys)
step the choices, the pointer over one focuses it, and A, Start, a click or a
tap does its `do`; B does the screen's `back`, else goes back. It is the same
at every aspect ratio (in the menus' middle 640) and on a phone.

| Element | Attributes | Meaning |
| --- | --- | --- |
| `<dialog>` | `title` (required), `text` | The box: its title and its words (wrapped to the box, bindings filled). A screen with one has nothing else but its `<list>` |
| `<list>` | `focus` | (in a `<dialog>`) Its choices: 1 to 4 `<item>`s, `focus` the one focused as it opens (each opening asks anew) |

`screens/quit.xml` (in place of the PC version's Quit question) and
`screens/save_changes.xml` (below) are pop-ups.

### The tall layout: tabs, tables, panels and searches

A screen with a `<table>` is laid out as Settings' and the server browser's
generated screens are (the tall layout, `tools/port_settings.py`'s
`BROWSER_TALL`): the whole height of the screen, the `<header>`'s title in
the band at the top, the `<tabs>` under it, the table at the left with its
`<panel>` beside it at the right, and the `<buttons>` at the foot. The
blocks' places are the tall layout's (`menu_screen_tall.inc`), not a flow from
the top, and a `<space/>` there does nothing. On a 16:9 screen the table, its
columns and the panel are as much wider as their layout says, and past 16:9
they reach the screen's edges; the tabs and the bar keep 16:9's places. On a
touchscreen (`debug.touch_menus`, the phones' builds) the tabs are chips, a
row is two lines of its cells (each column's `touch` place), the columns have
no titles, and the bar's buttons are a touchscreen's, BACK at the right.
The
background is the tall layout's (`main_menu/parts/background_tall`: the
blue, the band's line and the footer's), the generated screen's own where it
has one (the browser's), else made for it (`screens/background`: Select
Profile's, whose generated screen's was its inner screen's).

| Element | Attributes | Meaning |
| --- | --- | --- |
| `<header>` | `title`, `picture`, `detail`, `fallback` | (in a tall screen) its title's words in the band, `detail` at the band's right (on a touchscreen, under the title), and a `<button>` in the band. `fallback` gives shorter forms of the detail, each on a line of its own (a binding's value may hold several): where the detail does not fit its place, the first of them that fits is shown, else the last cut short with "..." (the lobby's drops the bots, then where the game is, then the players' word, then the game's name) |
| `<tabs>` | `current`, `if`, `when` | A strip of tabs: `current` names the binding whose value is the current tab's id (framed, its words white). LB and RB (Q and E) run the `do` of the tab either side, the keys' prompts beside the strip; the d-pad steps the strip, A or a click runs a tab's `do` |
| `<tab>` | `id` (required), `label` (required), `do` (required), `when` | One tab |
| `<table>` | `source` (required), `sorted`, `do`, `empty`, `lanes`, `left`, `right` | A live table: its rows come from the row source C names `source` (below), as many as fit over the panel's foot, scrolling at their ends. `do` is the action A, a click or a double click on a row runs (the row given to it); `sorted` names the binding whose value is the column sorted by and its way (`players down`); `empty` the words in the rows' place while there are none. `left` and `right` are the actions left and right on a row run (the row given). `lanes` (1 to 4, 1 by default) lays its rows out in that many lanes side by side, as the generated lobby's teams' columns: each lane a panel from the titles' place to the rows' foot, its `<lane>`'s title and detail at its top, its rows (18 tall, 20 apart, 8 in from its sides, the computer's on a touchscreen too) under them; it has no columns' titles nor headings, and no row scrolls: the source's rows are each lane's places in turn (the first lane's, then the next's), every place a row, shown while the source says one is there (its `state`). A row's picture is framed while the source marks it as its own, the brighter with the focus. Its columns' places are from a row's left, their `width` and `grow` a lane's |
| `<lane>` | `label` (required), `detail`, `tone` | (in a `<table lanes="N">`, one for each lane, in order) A lane's title at its top's left, large, in its tone's colour, and its `detail` at its top's right. `tone` (a number, or a binding giving one): 0 red's (the panel's edge and the title red), 1 blue's, 2 neither (the panel across all the lanes, the title its own blue). A lane whose label has no value is gone, and the panel across them all is drawn: the lobby's PLAYERS, one list down both lanes, its count at the second lane's right |
| `<column>` | `id` (required), `width` (required), `label`, `kind`, `grow`, `gap`, `align`, `sort`, `do`, `title-width`, `touch`, `when` | One column, from the left: `kind` its cells, `text` (the default), `strong` (white words), `ping` (the ping's bars and its number), `star` (a favorite's star), `dot` (a dot of a team's colour: red's, blue's or no team's, by the source's frame) or `swatch` (a profile color's 16-unit swatch: the 18 colors' by the source's frame, then random's, 18). `width` and `gap` (the room before it, 4) in the menus' units; `grow`, how much wider it is at 16:9; `align` its words at its `right`. Its title (`label`) runs `sort` when chosen, its arrow after the words when `sorted` names it; `title-width` its title's width, if not its words'. `do`: a click on a cell of it runs that, not the table's. `touch`: its place in a touchscreen's row, `x y width move grow` |
| `<search>` | `value` (required), `label`, `if`, `when` | A search: in a `<column>`, its magnifying glass after the column's title, the field typed into in the title's place and its X; at the screen's level, a field of its own at the top's right (a touchscreen's). A, a click or a tap types into it; each letter typed sets the binding `value` (the list filtered at once); Enter, A again or the focus taken off it keeps the words; B and Esc clear them; the X clears them |
| `<button>` | (in a `<table>`) | A button under the table's words for none (CLEAR SEARCH), shown as its `if` says, the focus on it as it comes |
| `<panel>` | `if` | The details beside the table: its lines one after another from its top, and its `<button>`s at its foot. A screen may have up to 4, in one place: the first whose `if` has a value (or that has none) is shown, the others hidden with their lines, buttons and Spartans (the lobby's next game, a player's card, the friends to invite). As another than the first is shown, the focus goes to its first choice or button, and back where it was as the first comes again |
| `<picture>` | `set` (required: `maps`, `missions`), `frame` (required: a binding), `height` (24 to 240, 120 by default), `touch-height`, `if`, `when` | (in a `<panel>`) A picture of the menus' set, a map's (ui.map's `mp_map_grafix`; a Custom Edition map's or a campaign level's by its display index, `custom_edition_maps.c`) or a mission's (`sp_levels`), the frame its binding's value gives (the menus' sharp pictures in the originals' place where they are made: `menu_pictures.h`), its 140 by 114 drawn whole, its corners round, in the middle of the panel. It is as tall as its height, or as the room the panel's other lines and its buttons shown leave it, as wide as its shape; with less than 40 units of room, or no frame, it is gone and the lines after it move up. In a panel with a picture, a text's lines its words do not fill (a one-line status where it has two) are the picture's room, the lines after it moved up |
| `<heading>` | `text` (required), `lines`, `touch-lines`, `when`, `if` | The panel's heading, large and white, wrapped to its lines |
| `<text>` | `text` (required), `lines`, `touch-lines`, `when`, `if` | A line of the panel's, wrapped to its lines (`touch-lines` on a touchscreen). While its `if` has no value it is gone, and the panel's lines after it move up into its room |
| `<pills>`, `<pill>` | `text` (required), `width` (required), `tone` | A row of pills (as many to a line as fit): each its words, its frame `tone` (a number, or a binding giving one). A line of them none of whose words have a value is gone, the lines after it moved up |
| `<pair>` | `label` (required), `value` (required), `when` | A label at the left and its value, large, at the right |
| `<spartan>` | (in a `<panel>`) `height`, `touch-height`, `color` | A line of the panel as tall as `height` (200 by default, at most 360), the Spartan standing in it in the middle of the panel (moved and grown with it on a wide screen), its feet at the line's foot (the line after it a 25th of its height lower, for the feet drawn under it); its color as a screen's `<spartan>`'s (a binding's first word), and where there is no Spartan the original picture is shrunk to the line, whole. A panel has one |
| `<spartan>` | (of a tall screen's own) `height` (at most 160), `touch-height`, `color`, `names`, `when` | A lineup over the table's lanes (a table of lanes only): as tall as its height, as wide as the table (grown with it on a wide screen), its binding's places side by side across it (a table of two lanes and 8 places: 4 over each lane), each a Spartan in its place's color, its feet at the lineup's foot, each turned a few degrees its own way, a little nearer or further and idling at its own time (from its place, the same each frame; a Spartan alone is hardly turned); the lanes begin under it. With `names` (a binding giving each place's name, a line each), each place's name is in a small line under its Spartan, centred under its feet (lower or higher with its place's stagger), cut short with "..." where it is wider than its place, in its team's color in a team game (red's, blue's, as the lanes' titles) else white; the line is in the lineup's height (its Spartans 14 shorter), so the lanes do not move. An empty place, or one with no name, shows none. No original picture where there is no Spartan |
| `<choice>` | `label` (required), `value` (required), `left` (required), `right` (required), `if`, `dim`, `when` | (in a `<panel>`) A choice of the panel's, a line of it the focus steps to: its label at its left, its value's words between the spinners' arrows. Left runs `left`, right and A `right`, a click or a tap on its left half `left`, on its right half `right` (the source of its value then changes it: the lobby's BOTS, SKILL and TEAMS). While its `if` has no value it is gone, the lines after it moved up into its place; while `dim` has a value it is dimmed and passed over by the focus, and its keys do nothing. 22 tall (32 on a touchscreen) |
| `<chat>` | `source` (required), `lines`, `do`, `if`, `when` | A chat under the table, across it (the table's rows end over it): its newest `lines` lines (1 to 12, 5 by default, the lobby's 3: older ones scroll off the top; each its sender's name in its colour, then its words), and under them the line being typed (with its caret), or the source's words for none (`Press T to chat`). The focus steps to the line typed's place; A, a click or a tap on it runs `do` (the lobby's begins a line). While its source's on-screen keyboard is up, it is drawn over the screen (over the panel's foot; on a touchscreen across the screen's foot), its taps its keys. `source` names a chat source of C's (below); a screen has one `<chat>` |
| `<button>` | (in a `<panel>`) | The panel's buttons, one over another up from its foot, the last at the foot (JOIN GAME; the lobby's START GAME): filled, its key's picture before its words; those over it outlined, their words alone in their middle (the lobby's HOST CONTROLS and GAME SETTINGS), each shown as its `if` says, in its place. Up to 4 |

A panel's line whose text has a binding of no value (or none at all) is not
shown, and its place stays (one whose `if` has none is gone, its place
taken). Columns' titles that sort nothing and hold no
search are words alone: the focus passes over them. Each text of a tall screen (the header's detail,
the panel's lines, the buttons' labels, the words for none) is filled again
every frame, and the `if`s read, so a binding's value can change while the
screen is open.

## Row sources

A table's rows are C's: `source` names a `struct menu_table_source`
(`menu_table.h`) in `menu_screen.c`'s `screen_sources`, by its name in
`screen_source_names`. Each frame the table (the view) tells the source what
it shows: its rows, the row focused, whether the pointer resting on it put the
focus there, whether the titles are shown. The source (the model) answers how
many rows there are, which is first on the table, which is chosen (the panel's
row, its picture brighter), and which row of the table to give the focus (a
row moved, a heading passed over, a scroll at the table's ends), and then
gives each row: a heading across it, or its cells by their columns' ids, each
its words, their colour, a picture's frame (a ping's bars, a star) and a pill
or a lock after the words. The table cuts words too long for their column
short with "...", puts the pill and the lock after them, and leaves a ping's
bars out where they would meet its number. The actions a row runs get the row
(0 up, on the table). A source may also be told when its screen opens (not
when a hot reload makes it again): Select Profile's then chooses player 1's
profile.

| Source | Rows |
| --- | --- |
| `browser.games` | The server browser's games (below) |
| `profiles` | The player profiles, and the row that makes one (Select Profile, below); its one column `name` |
| `lobby.players` | The lobby's players (below), in a table of two lanes: red's and blue's (a game without teams' one list down both); its columns `dot`, `name` (a player's speaker beside it in voice chat) and `tag` |
| `profile.colors` | The profile colors, the PC version's picker's 18 and Random after them (Profile Color, below); its columns `swatch` and `name` (the color's name in its color, black's a grey) |
| `add.players` | Add Player's rows (below): this machine's four controllers, P1 to P4; its columns `color` (a `swatch` of the profile's color), `controller`, `profile` and `state` (IN THE GAME, CHOOSING, READY), its players in the game and those joining framed as its own |

A `<chat>`'s lines come from a chat source (`struct menu_chat_source`,
`menu_table.h`) in `menu_screen.c`'s `screen_chats`, by its name in
`screen_chat_names`: each frame its lines (the oldest first: each a name, its
colour and its words), the line being typed and whether its on-screen
keyboard is up, the keyboard drawn in its place, and the words for none.

| Chat source | Lines |
| --- | --- |
| `lobby.chat` | The lobby's text chat (network_chat.c's lobby lines: below, "The lobby") |

A cell may give shorter words (`fallback`), shown in place of its own where
they would be cut short (a bot's tag: BOT where BOT · NORMAL does not fit),
and a network machine whose speaker (network_voice.c's: talking, or muted)
is drawn just left of its words. A source's `state` (optional) says of each
row whether it is there, and whether it is marked as its own.

The reader checks each file whole. A file is refused if it has any of these
problems: bad XML; an element where it may not be; an attribute an element
does not have; an item with no label or no `do`; two items with one id; a
list's `focus` that names no item; a `when` that names no platform; a `key`
other than the four; an action or binding the game does not have. The
problem is logged with its file and line, for example `menus:
screens/main.xml:6: <list> is not one of what its element may hold; the
screen stays as it was`. A refused file never crashes the game:

A tall screen's file is refused, too, for: a `<tabs>` with no `<tab>`, a tab
with no id, label or `do`, two tabs or columns of one id; a `<table>` with no
source, a source the game has not got, no `<column>`, a column with no id or
width, a `kind`, `align` or `touch` it may not have; a `<search>` with no
value; a `<panel>` with nothing in it, a `<pill>` with no width, a `<pair>`
with no value; a binding named alone (`current`, `sorted`, `value`, `if`,
`tone`, `dim`) that the game has not got; a `<lane>` in a table of fewer
than 2 lanes, a table of lanes with some `<lane>`s but not one for each lane,
a lane's `tone` past 2; a `<choice>` with no label, value, left or right, or
a left or right the game has not got; a `<table>`'s `left` or `right` the
game has not got; a second `<tabs>` or `<table>`; a fifth `<panel>`; a second
`<spartan>` in a panel, or of the screen's own; a `<picture>` whose `set` is
not `maps` or `missions`, with no `frame`, a frame or `if` the game has not
got, a height past 24 to 240; a tall screen's own `<spartan>` over a table
without lanes.

- an embedded or menus-folder file that is refused at start leaves no
  hand-written screen, and the screen the file would have replaced is drawn
  from the model as before;
- a file that is refused when it is read again (hot reload) keeps the last
  good version of its screen.

## Bindings

Text in a `label`, `hint` or `<hint>` can contain `{name}`, which is filled
from C when the screen is made. If any binding in a line has no value, the
whole line is left out: an item whose label is left out is not made, and an
item uses the next of its `<hint>`s. The bindings are listed in
`menu_screen.c`'s `screen_binding_names`, and their values come from
`screen_value`:

| Binding | Value |
| --- | --- |
| `{campaign.level}` | The saved game's mission (player 1's profile), for example `The Silent Cartographer`; none without a saved game, or in co-op |
| `{campaign.difficulty}` | The saved game's difficulty, for example `Legendary` |
| `{browser.title}`, `{browser.counts}`, `{browser.empty}`, `{browser.host}` | The server browser's title (SERVER BROWSER, LOCAL NETWORK, JOIN BY LINK), its counts (`20 GAMES    24 PLAYERS`, `2 OF 18 GAMES ...` while some are hidden), its words while it lists none, its HOST button's (HOST & MORE, HOST CO-OP) |
| `{browser.tab}`, `{browser.sort}`, `{browser.sort.title}` | The tab shown (`all`, `coop`...), the sort (`players down`), its column's title (PLAYERS) |
| `{browser.search.server}`, `{browser.search.map}`, `{browser.search.any}` | The searches' words (set as they are typed) |
| `{browser.panel.title}` ... `{browser.panel.status}` | The chosen game's name (or the list's title), where (map and mode), kind (GOAT H1, LEGACY) and its `tone`, tick rate, OPENCE CAN JOIN, players, ping, and its status line |
| `{browser.searched}`, `{browser.joinable}`, `{browser.server}`, `{browser.link}`, `{browser.filtered}` | (for `if`s) the searches hide every game; a game to join is chosen; the server browser's list; Join by Link's; a list filtered and searched (not Join by Link's) |
| `{browser.fix}` | The co-op list's: its empty list's fix's words (`TURN IT ON`, `RETRY`, `SHOW THEM`, `HOST CO-OP`; refilled each frame); none while it lists a game, or elsewhere |
| `{coop.listing}`, `{coop.link}`, `{coop.problem}`, `{coop.fix}` | A network co-op game's host's lobby: who can join and see it (`FRIENDS CAN JOIN: 3 friends online see it.`), the invite link (its start), what keeps people from finding it in plain words (`Internet play is off, so only players on your network can join.`) and its fix's words; none for anyone else |
| `{pause.friendly_fire}` | Network co-op's FRIENDLY FIRE row's words, `FRIENDLY FIRE ON` or `OFF` (this machine's choice); none elsewhere |
| `{pause.host_controls}`, `{pause.end_game}`, `{pause.chapters}` | (for `if`s) the pause menu's HOST CONTROLS (a network game's host's), END GAME (the host of a game of this machine alone) and CHAPTERS (Basic Training's), as `menu_tags.c` adds them to the map's; the host machine's first local player's alone (split screen's player 1: the screen's own player), an admin's too |
| `{profile.title}` | Select Profile's chosen row: its profile's name, or `Create New Player Profile` on the row that makes one |
| `{profile.name}`, `{profile.controls}`, `{profile.difficulty}`, `{profile.level}` | The chosen profile's name, controls (`Default`, `Inverted`), best difficulty finished (none before a level is) and current level (`The Pillar of Autumn`); none on the row that makes one |
| `{profile.color}`, `{profile.chosen}` | (for a `<spartan>`'s color and `if`s) the chosen profile's color's number, or `random`; whether a profile (not the row that makes one) is chosen |
| `{profile.current}` | Player 1's profile's name (the active one, else the one used last) |
| `{color.profile}`, `{color.title}`, `{color.spartan}`, `{color.current}`, `{color.about}` | Profile Color's: the edited profile's name; the focused color's name (`Red`, `Random`), its word for a `<spartan>` (its number, or `random`), `Current color` while it is the profile's own, and a line of what it does |
| `{lobby.title}`, `{lobby.subtitle}`, `{lobby.subtitle.short}` | The lobby's title (GAME LOBBY), and the game's name, where it is and its players (`Player's game · Local network · 4/16 players · 2 bots`; a game of this machine alone's `Offline with bots · 3 players`); its shorter forms for the band's `fallback`, a line each (`Player's game · Local network · 4/16 players`, `Player's game · 4/16 players`, `Player's game · 4/16`, `4/16 players`, `4/16`) |
| `{lobby.red}`, `{lobby.blue}`, `{lobby.red.count}`, `{lobby.blue.count}`, `{lobby.red.tone}`, `{lobby.blue.tone}` | The lanes: RED and BLUE in a team game, each its count (`2 PLAYERS`) and tone (0, 1); in a game without teams PLAYERS alone (blue's none), `3 OF 16` at blue's right (an offline game's `3 PLAYERS`), tone 2 |
| `{lobby.gametype}`, `{lobby.map}`, `{lobby.limits}`, `{lobby.kind}`, `{lobby.kind.tone}`, `{lobby.tick}`, `{lobby.opence}`, `{lobby.after}` | The next game: its gametype, map (a co-op game's level and difficulty), limits (`50 kills · 10 min`), GOAT H1 or LEGACY and its pill's tone, its tick rate (`120 TICK`), OPENCE CAN JOIN (the host's, while OpenCE's players may join), the game after it (a host's playlist's) |
| `{lobby.bots}`, `{lobby.skill}`, `{lobby.teams}`, `{lobby.bots.shown}`, `{lobby.bots.locked}`, `{lobby.bots.count.locked}` | The host's bots: BOTS' choice (`ADD 3`), SKILL, TEAMS; (for `if`s and `dim`s) whether the host has them (any game but co-op), whether the gametype is one they do not play, and BOTS' a matchmaking game's too |
| `{lobby.status}`, `{lobby.start}`, `{lobby.waiting}` | How the game is (the countdown, the invite copied, how another controller joins...); START GAME's words (or `STARTING IN 3`); a player's (`Waiting for the host`) |
| `{lobby.chat}` | (for `if`s) the lobby has its text chat now: the host's LOBBY TEXT CHAT on, as it told this machine, and `display.text_chat` |
| `{lobby.host}`, `{lobby.settings}`, `{lobby.controls}`, `{lobby.team.change}`, `{lobby.invite}`, `{lobby.add}` | (for `if`s) START GAME's (the host's), GAME SETTINGS', HOST CONTROLS' (a network game's host's, a dedicated server's admin's), CHANGE TEAM's, the invite link's (INVITE FRIENDS'), ADD PLAYER's |
| `{lobby.bots.some}` | (for `if`s) SKILL's and TEAMS': the host's BOTS adds some |
| `{lobby.map.picture}`, `{lobby.description}`, `{lobby.where}` | The next game's map's frame of `mp_map_grafix` (an Xbox map's, a Custom Edition map's or a campaign level's display index, else the unknown map's), its gametype in a few words (what wins it), its map and limits on a line (`Blood Gulch · 50 kills`) |
| `{lobby.lineup}` | The lineup's colors: a team game's four places of each team (red's, then blue's, `-` an empty one), else the first eight players; each the host first, then as they joined, in the color the game gives them (`red`, `blue`, else their profile color's number) |
| `{lobby.lineup.names}` | The lineup's names: each place of `{lobby.lineup}`'s its player's name, in the same order, a line each (an empty place's line empty) |
| `{add.count}`, `{add.title}`, `{add.profile}`, `{add.color}`, `{add.help}`, `{add.done}` | Add Player's: how many more may join (`2 MORE CAN JOIN`); the chosen row's player (`PLAYER 2`), profile, its color for a `<spartan>` (a number, or `random`) and what its controller does next; (for `if`s) DONE's, once every one joining is ready |
| `{lobby.details}`, `{lobby.card}`, `{lobby.inviting}`, `{lobby.panel.open}` | (for the panels' `if`s) the panel shown: the next game's, a player's card, the friends to invite; another than the next game's (BACK's) |
| `{lobby.card.name}`, `{lobby.card.color}`, `{lobby.card.ping}`, `{lobby.card.platform}`, `{lobby.card.party}`, `{lobby.card.facts}`, `{lobby.card.note}` | A card's player: their name, color (the lineup's word), ping (`45 ms`: a client's own to the host, the host's of a client as a game measured it; `Host`, `Bot`, `Not yet`), platform (GOAT H1, OpenCE, Bot; `Unknown` where this machine cannot tell), party (`Your party`, `You`, `None`), the three on one line (a touchscreen's), and what was just done or what they are to this profile (`Friend request sent.`) |
| `{lobby.card.friend}`, `{lobby.card.mutable}`, `{lobby.card.mute}`, `{lobby.card.kick}` | (for `if`s, and MUTE's words) ADD FRIEND's (a code the host told, of no friend, request or block yet), MUTE's (another machine's player), `MUTE` or `UNMUTE`, KICK's (Host Controls' own, the host's) |
| `{lobby.friends}`, `{lobby.friends.count}`, `{lobby.friend}`, `{lobby.friend.where}`, `{lobby.friends.said}` | INVITE FRIENDS: offered (an invite link, Internet play on); how many friends are online (or why none are listed); the friend chosen and where they are (the Friends screen's words); what was done |

## Actions

An action is a name, such as `do="open versus"` or `back="quit"`, which C
resolves in `menu_screen.c`'s `screen_actions`. The files never name the
game's widgets. Each action is what the old screen's item did: the function
it ran, the screen opened if that succeeded, and the screen opened if it
failed (a new profile's name is asked for first). Each screen is opened
through the menus' own handler code, so the history (B comes back), the
sounds and the fades are the game's.

| Action | Does |
| --- | --- |
| `open campaign` | The Campaign screen, as the PC version's CAMPAIGN opens it (a profile made first if there is none) |
| `open multiplayer` | The server browser, as Multiplayer's SERVER BROWSER opens it |
| `open coop`, `open host coop` | Co-op's screen (`screens/coop.xml`, below: "Co-op") |
| `open host` | Multiplayer's hub (the server browser's HOST & MORE) |
| `coop host` | HOST: Choose Mission, as the campaign's (a profile made first if there is none), HOST's choosing begun (its Choose Difficulty's PLAY opens WHO CAN JOIN) |
| `coop join`, `coop split` | JOIN: the co-op list (`screens/coop_join.xml`); SPLIT SCREEN: the hub's SPLIT-SCREEN CO-OP (`port coop begin`) |
| `coop host friends`, `coop host anyone`, `coop host invite` | WHO CAN JOIN's cards: the choice kept (`network.coop_join`), the game hosted, the lobby |
| `coop fix`, `coop join link` | The co-op list's: its empty list's fix (HOST CO-OP's: `coop host`); Join by Link |
| `lobby fix` | The lobby's fix of what keeps people from finding a co-op game (`coop.fix`) |
| `open settings` | Settings, as the main menu's SETTINGS opens it |
| `open training` | The VR tutorial (Basic Training's chapters) |
| `quit` | The quit question, as the main menu's QUIT and B open it |
| `quit game` | The game quit (the quit question's OK) |
| `back` | Back to the screen before, as B goes (a pop-up's BACK and CANCEL) |
| `edit save`, `edit discard` | Back to the editor the question was opened from, its changes then saved as its OK saves them (a built-in gametype's new name asked first), or let go of, and the editor closed (below) |
| `search next` | The next of the screen's searches typed into (the first, if none is) |
| `search clear` | Every search's words cleared, as a table's CLEAR SEARCH |
| `browser back` | The browser's back (its own: `gamespy back handler`) |
| `browser host` | HOST & MORE, or HOST CO-OP on the co-op visit |
| `browser tab <tab>` | The tab shown (`all`, `dedicated`, `openh1`, `legacy`, `coop`, `favorites`) |
| `browser sort <column>`, `browser sort next` | Sorted by the column (`players`, `ping`, `server`, `map`, `mode`; again the other way), or the next (X) |
| `browser favorite` | The row's game (or the chosen one) made a favorite, or no longer one |
| `browser join` | The row's game chosen, or joined if it is chosen (a password asked for first, the connecting box opened); JOIN GAME's |
| `browser refresh` | The public games asked for again; Join by Link's PASTE LINK |
| `browser filters` | The Filters page |
| `close`, `close all` | The screen closed (the multiplayer pause menu's RESUME GAME), or every screen (B on a pause menu, the campaign's RESUME GAME) |
| `open host controls` | Host Controls, as the pause menu's HOST CONTROLS opens it |
| `pause leave` | The player out of the game, as the multiplayer pause menu's LEAVE GAME (`mp game player quit`), and the screen closed |
| `pause end game` | The game ended as its time limit would (END GAME) |
| `pause chapters` | Basic Training's chapters, as the pause menu's CHAPTERS opens them (the host's first local player's alone) |
| `pause friendly fire` | This machine's friendly fire in network co-op on or off (FRIENDLY FIRE) |
| `pause revert`, `pause restart` | The campaign's questions for REVERT TO SAVED and RESTART LEVEL (the map's) |
| `pause quit` | SAVE AND QUIT: the main menu, as the campaign's pause menu's |
| `profile select` | The row's profile (OK: the chosen one's) made player 1's, and back to the screen before; on the row that makes one, the question whether to make one |
| `profile new` | A new profile's name asked (CREATE NEW) |
| `profile delete` | The question whether to delete the chosen profile (DELETE) |
| `profile color` | The row's color (SELECT: the focused one's) made the profile being edited's, and back to the screen before; Random its color none, a new profile's |
| `lobby row` | A lobby's row chosen: a click on it puts this player on its lane's team (in a team game), as a click on the generated lobby's column; A (Start) starts the game (the host's) |
| `lobby start`, `lobby team red`, `lobby team blue`, `lobby team toggle` | The game started (START GAME, a player's Start: a controller not in the game joins instead, on Add Player's screen); this player put on red's or blue's team (left and right on the players), or the other (CHANGE TEAM) |
| `lobby invite`, `lobby settings`, `lobby host`, `lobby add player` | The invite copied; Server Setup in the lobby's place (GAME SETTINGS); Host Controls (HOST CONTROLS); Add Player's screen opened (ADD PLAYER) |
| `add player done`, `add player back` | Add Player's: those joining, all ready, brought into the game (DONE, a player's START) and back; or let go of (BACK, B) and back |
| `lobby chat` | A line of the lobby's chat begun: on the keyboard typed (Enter sends it, Esc lets it go); on a controller and a touchscreen with the on-screen keyboard |
| `lobby bots left`, `lobby bots right`, `lobby skill left`, `lobby skill right`, `lobby teams left`, `lobby teams right` | The host's bots' choices stepped: BOTS to the choice before or after (at its ends no further), SKILL and TEAMS round, written to `config.toml` as the generated lobby's spinners write them |
| `lobby card` | The row's player's card shown in the panel (theirs again: closed) |
| `lobby card friend`, `lobby card mute`, `lobby card kick` | The card's ADD FRIEND (a friend request to their code: `network_party_slot_add_friend`), MUTE or UNMUTE (their machine's voice, lines and pings on this machine: `network_voice_mute_machine`), KICK (Host Controls' kick, and the card closed) |
| `lobby friends` | INVITE FRIENDS: the panel of the friends to invite and the invite link (again: closed) |
| `lobby friend left`, `lobby friend right`, `lobby friend invite` | The friend chosen stepped among those online; invited to this machine's party, as the Friends screen's INVITE (`p2p_friends_invite`), which then follows its leader into the game |
| `lobby close` | A card or the invite closed, the next game's panel shown (BACK, B); nothing while it is shown |

A file is checked against `screen_action_names` as it is read, and
`tools/harness/tests/test_menu_screens.py` checks that every name there has an
entry in `screen_actions`.

## The server browser

`screens/browser.xml` is the server browser of the new engine: it takes the
place of Join Game's screen (`main_menu/multiplayer_type_select/join_game/
join_game_screen`) in each of its modes, by `{browser.*}` bindings and `if`s:

- the server browser (the main menu's MULTIPLAYER), with its HOST & MORE;
  its CO-OP tab opens co-op's own screen ("Co-op", below), whose co-op list
  (`screens/coop_join.xml`) is this model's too;
- LOCAL NETWORK (Multiplayer's hub, which HOST & MORE opens): the games on
  this network, its favorites offline after them; no REFRESH nor HOST;
- JOIN BY LINK (the hub): the invites taken; no tabs, searches, FILTERS nor
  SEARCH, PASTE LINK in REFRESH's place, and an invite link copied joined
  as it opens.

The classic engine, PRO 2V2's browser and a refused or missing file keep the
generated screen. HOST & MORE sits in the band in a bar of its own, the
body's first block: the d-pad and the stick reach it (up from the tabs) as
they reach every other focusable block, and A, its key (View, F1), a click
and a tap each press it, as the pointer clicks only a list's items.

What it lists and does is menu_functions.c's, as for the generated screen: the
games found, their order, the groups and their headings, the filters, the
searches, the favorites, the tab and the sort kept in `config.toml`, joining
(the password screen, the connecting box, the A press posted once the host is
reached) and the panel's words. Its model (`lobby_browser_model`,
`local_browser_model`) works out each frame what is listed, which row is
first and which chosen, and the panel's and the counts' words
(`browser_display`), for its two views: the generated screen's list
(`lobby_browser_update`, `browser_update`) and the hand-written screen's table
(`pc_menu_browser_source`, the `browser.*` bindings and actions). The screen's
stand-in (Join Game's own widget) still opens and closes the browser
(`gamespy screen init`, `gamespy screen dispose`).

## How a screen is made

`screen_places` in `menu_screen.c` names the screen a hand-written one
replaces (`main` replaces `main_menu/main_menu`). The game opens that screen as
before: `ui_widget.c` makes its stand-in widget and keeps it in the history.
The engine (`menu_engine_screen_opened`) then asks `menu_screen_opened`, which
deletes the stand-in's own children and makes the screen's blocks in their
place. Each block is a widget of `screens/blocks.xml` (`screens/title_art`,
`screens/list`, `screens/item`, `screens/hint`, `screens/prompt`). It gets
its words (`parameters.text_box.text`) and its place (its offsets) from the
file. From then on the engine draws the widgets and takes their events
exactly as it does for any screen's widgets. Focus, the controller, the
keyboard, the mouse and touch (their pointer targets come from the widgets
drawn), the menus' sounds and the high-res text are therefore the game's own.
The blocks are drawn from their own definitions, not from the model's looks
(`menu_tags.c` gives them no look). Before each frame is drawn
(`menu_screen_frame`), the screen shows the focused item's hint, and the main
menu notes that it is shown, which the update and crash questions and the
Party screen open over (`pc_menu_main_menu_shown`).

Because the items share one widget tag, the engine gives the focus to the
item widget itself, not to the first widget of its tag
(`ui_widget_port_give_focus`).

## Hot reload

`debug.menu_screens` (`HALO_MENU_SCREENS`) names a folder of screen files
(normally a source tree's `port/assets/menus/screens`). If it is empty,
`debug.menu_files`' `screens` folder is used when that setting is set. The
game looks at the folder twice a second, on the menus' clock
(`menu_screens_poll`). A file that changed is read again, and an open screen
whose file was read again is made again, with the focus kept on the item of
the same id. A file with a problem is logged and its screen stays as it was.
Hot reload works in any build, release or not, but only while the setting is
set, and only on desktops.

```sh
HALO_MENU_SCREENS=$PWD/port/assets/menus/screens build/linux_x86_64/halo
# edit port/assets/menus/screens/main.xml: the main menu changes within half a second
```

The blocks' widgets (`blocks.xml`) are menu files like any other: the menu
studio's reload (`debug.menu_studio`, F7) reads them again.

## Adding a screen

1. Write `port/assets/menus/screens/<name>.xml`, and add it to
   `port/assets/menus/menus.json` so that releases embed it.
2. In `port/linux/game/menu_screen.c`:
   - add each new action to `screen_action_names` and `screen_actions`;
   - add each new binding to `screen_binding_names` and give it a value in
     `screen_value`;
   - if the screen takes the place of a generated one, add it to
     `screen_places` (a screen opened only by an action needs no place yet:
     step 2 opens hand-written screens directly).
3. A block that does not exist yet: add its widget to `blocks.xml`, its
   element to the reader (`menu_screens.c`, `menu_screens.h`) and its layout
   to `screen_build` (a tall screen's to `menu_screen_tall.inc`). A table's
   rows: a row source in `screen_sources`.
4. Tests: add a case to `tools/harness/tests/test_menu_screens.py` for what
   the reader must accept or refuse. Take screenshots of the screen at 16:9,
   16:10 and 4:3, and make a scripted run (`debug.test_input "menu:ready
   ..."` with `debug.menu_open`) that presses through it.
5. If it replaces a screen that `tools/menu_parity.py` compares, add the
   screen to `HAND_WRITTEN` there. The classic engine still draws the old
   screen; the new engine's side must have drawn the hand-written blocks.

## The lobby

`screens/lobby.xml` takes the place of the lobby (`main_menu/
multiplayer_type_select/lobby/lobby_screen`), a network game's and a game of
this machine alone's, in the new engine; the classic engine keeps the
generated one. GAME LOBBY in the band, the game's name, where it is and its
players at its right; the players in a table of two lanes, red's and blue's
(`RED`, `BLUE`, each its count; in a game without teams `PLAYERS`, one list
down both, `3 OF 16` at its right, an offline game's `3 PLAYERS`), each row a player's dot, name (and its
controller, `(P2)`, of this machine's players when it has more than one; its
speaker while it talks in voice chat) and tag (HOST, its machine's first player alone; SPLIT SCREEN, a machine's
players past its first; BOT and the bots' skill where it fits, PARTY, GOAT H1,
OPENCE), this machine's own framed with a dim edge (the focus alone lit), `+3 more`
in a lane's last row where it has more than its rows. Over the lanes, on
the computer (a phone's lanes keep their rows), a lineup of the players' Spartans (`<spartan color="lobby.lineup" names="lobby.lineup.names">`), each its name under it: in a
team game four of each team over its lane, else the first eight; each the
host first, then as they joined, in the color the game gives them (its
team's in a team game). The roster still lists everyone.

The panel is one of three (`<panel if>`):

- the next game: its map's picture (`<picture set="maps">`, as large as the
  panel's room lets it be: smaller for a host, none for a host with bots), its gametype and
  what wins it, its map and limits, GOAT H1 or LEGACY, its tick rate,
  OPENCE CAN JOIN, THEN, the host's bots (BOTS; SKILL and TEAMS while BOTS
  adds some: dimmed for a gametype they do not play), how the game is, and
  HOST CONTROLS, GAME SETTINGS and START GAME, or a player's `Waiting for
  the host`;
- a player's card, as A, a click or a tap on their row opens it (again
  closes it): their Spartan in their color, name, ping, platform and party,
  what they are to this profile, and ADD FRIEND (a friend request to the
  friend code the host told for them: `network_party.c`'s lobby list),
  MUTE or UNMUTE (on this machine) and the host's KICK (Host Controls');
- INVITE FRIENDS: the friends online, the one chosen stepped (FRIEND) and
  invited to this machine's party as the Friends screen does (the party
  follows its leader into the game), and COPY INVITE LINK.

BACK (B) closes a card or the invite; with the next game's panel, LEAVE
leaves. The bar: BACK or LEAVE at its left, CHANGE TEAM, INVITE FRIENDS
(INVITE on a touchscreen), ADD PLAYER and CHAT at its right (on a
touchscreen HOST CONTROLS too).

What it shows and does is the generated lobby's (menu_functions.c's
`lobby_menu.inc`): its model (`lobby_board_model`, the players, their
columns, tags and order, what is offered, the panel's words) is worked out
for both views, the generated screen's widgets (`lobby_board_update`) and the
hand-written screen's table and bindings (`pc_menu_lobby_source`,
`pc_menu_lobby_value`). Left and right on the players put this player on
red's or blue's team, Start the host's start; the generated screen's own
keys stay its stand-in's (B leaves, X opens Add Player, Y the map vote or COPY
INVITE); the Start of a controller not yet in the game opens Add Player with
it joining (not the host's start: `pc_menu_lobby_start`), and what its list did each frame (the split screen's
players joining, the map vote's screen) is done each frame it is drawn
(`pc_menu_lobby_frame`).

Under the players, the lobby's text chat (`<chat source="lobby.chat">`): its
newest lines, each name in its team's colour (no team's blue, the host's
notices pale gold), and the line being typed. T (or Y: the lobby has one
channel, everyone, as its voice has: its teams are not yet the game's, and
anyone may change theirs) begins a line on the keyboard; on a controller LB
(CHAT) or A on the chat's line opens the on-screen keyboard (the game's own,
network_chat.c's: D-pad Up moves the lobby's focus here), and on a touchscreen
a tap on it. The sending, the host's relay, its rate, its refusals (links),
its word filter and its mutes are the game's chat's (port/linux/NETCODE.md,
"Text chat"); the lobby's lines are kept apart from a game's, and neither
shows the other's. The host's Host Controls has LOBBY TEXT CHAT and LOBBY
VOICE CHAT (`network.lobby_text_chat`, `network.voice_lobby`), on or off for
everyone at once.

## Co-op

`screens/coop.xml` is co-op's one screen, every way in landing on it:
Campaign's CO-OP (`open coop`), the server browser's CO-OP tab (`open
coop`: the tab is a door, the browser opening on ALL where it was kept on
CO-OP) and Multiplayer's CO-OP CAMPAIGN (it takes the hub's co-op page's
place, `main_menu/multiplayer_type_select/hub/coop_screen`). CO-OP in the
band, three cards as the Campaign screen's, and the bar:

- HOST (`coop host`): Choose Mission and Choose Difficulty, the campaign's
  own (no saved game offered: a co-op game does not continue one), then
  `screens/coop_who.xml`, WHO CAN JOIN: FRIENDS (friends see the game in
  their co-op list and join it; strangers do not), ANYONE (listed in
  everyone's co-op list and the server browser too) or INVITE ONLY (only
  its invite link joins: `p2p_friends_set_game_closed`, its presence offers
  no JOIN), the choice kept focused (`network.coop_join`). A card hosts the
  game at once as Create Game's ONLINE CO-OP and Server Setup's START would
  (its name the player's, its most players co-op's, no password) and opens
  the lobby. The lobby's panel then says who can join and see it
  (`{coop.listing}`), its invite link (`{coop.link}`; Y, COPY LINK, copies
  it; INVITE FRIENDS as ever), and what keeps people from finding it
  (`{coop.problem}`: internet play off, the brokers not reached, public
  games off, appearing offline to friends, no friends yet), with the button
  that fixes it (`{coop.fix}`, `lobby fix`). TURN ON INTERNET PLAY starts
  internet play while the game runs (`xnet_internet_play_start`; the
  game's sockets were told it while it was off: `p2p_socket_port`).
- JOIN (`coop join`): `screens/coop_join.xml`, the co-op list. It is the
  server browser's model (`browser.games` and the `browser.*` bindings and
  actions) on its co-op list while its screen is open (its stand-in's
  `port coop join init` and `dispose`): only co-op games, none hidden by the
  browser's filters, searches or tab, in two groups: the player's friends'
  (each a friend online whose presence says they host a co-op game they
  offer JOIN to, `Co-op Heroic on The Silent Cartographer`, lobby_menu.inc's
  `lobby_presence_tell`; a public game of their name is taken into their
  row and joined as any listing, else the row is joined through their party,
  `p2p_friends_join`, which follows them in), then the public ones. Each row
  its host, mission, difficulty (the listing's
  gametype's end, `Co-op Heroic`), players and ping. With none, the list
  says why in plain words, with its fix in a button under the words
  (`{browser.fix}`, `coop fix`): `Internet play is off` (TURN IT ON),
  `Can't reach the game list` (RETRY), `No friends are hosting co-op
  right now, and there are no public co-op games` (HOST CO-OP). JOIN BY LINK
  (Y) opens Join by Link, which joins an invite link copied at once. An
  invite accepted (a link opened, a friend's, Discord's) goes to its game
  as before.
- SPLIT SCREEN (`coop split`): the hub's SPLIT-SCREEN CO-OP.

What it does is menu_functions.c's `coop_menu.inc`. The classic engine
keeps Multiplayer's co-op page, ONLINE CO-OP's map list and Server Setup.

## The profile color screen

`screens/profile_color.xml` takes the place of Settings' COLOR screen
(`color_edit/color_select_screen`), in the tall layout, as Select Profile's:
PROFILE COLOR in the band with the profile being edited at its right
(`{color.profile}`), the colors in the table (`profile.colors`: each its
swatch and its name in its color, the PC version's picker's 18 and Random
after them), and in the panel the focused color's name, `Current color`
while it is the profile's own, its Spartan in that color (`<spartan
color="color.spartan">`, live as the focus moves; Random's cycling through
the colors), and a line of what it does. The profile's own color is focused
as it opens, the list from its top where that row is on it, else scrolled to
put it in the middle. BACK is at the bar's left, SELECT at its right. A (SELECT, a
click or a tap on a row) chooses the color for the profile being edited and
goes back; B goes back without one. Random is the profile's color none (a
new profile's), for which each game gives the player a color no one else
has (`network_server_manager.c`'s `get_unique_random_color`); the PC
version's picker had no row for it. The classic engine keeps its own screen
and the original pictures.

## Add Player

`screens/add_player.xml` is the new menus' ADD PLAYER, in place of the
classic one's (the next START of another controller, then the profile
list of `main_menu/multiplayer_type_select/lobby/player_profile_screen`):
the lobby's ADD PLAYER (X) and a game under way's (its preview's) open it,
and so does the Start (or A on START GAME) of a controller not yet in the
game, which joins on it at once. It does what the Xbox's Select Profile of
four (`4way_start2join_screen`, Press A to Join) did, in the tall layout:
ADD PLAYER in the band with how many more may join, a row for each of this
machine's four controllers (`add.players`: its profile's color, P1 to P4,
its profile, or Press A to join, or No controller, and IN THE GAME,
CHOOSING or READY), and in the panel the chosen row (the controller that
pressed last): its player, its profile, its Spartan in the profile's color,
what its controller does next, and DONE once every one joining is ready.

Its keys are taken by controller (`screen_places`' `key`,
`pc_menu_add_player_key`), before its focus: a controller not in the game
joins with A or START (several may, together), steps its own profile with
left and right (the D-pad, the stick), is ready with A and goes back a step
with B; once all are ready, A or START of one of them, DONE, or a player's
START makes each of them a player of the game on its profile
(`ui_widget_port_multiplayer_player`, which the lobby's "net splitscreen
prejoin players" adds; the preview's JOIN GAME takes them all) and goes
back. A joining controller's other keys do nothing, nor move the screen's
focus, which is the players' in the game, as are its B and BACK (those
joining let go of). While it is open, one gamepad sharing player 1's
controller with the keyboard has its own (`pc_menu_split_players`), and
joins as P2. The classic engine keeps the classic way.

## Select Profile

`screens/profile_select.xml` takes the place of the profile manager
(`main_menu/profile_manager/player_profile_manager`, Settings' MANAGE
PROFILES): SELECT PROFILE in the band with player 1's profile at its right,
the profiles and the row that makes one in the table (`profiles`), and in
the panel the chosen row's name, its Spartan in its color (`<spartan
color="profile.color">` in the panel, live as the focus moves; on the row
that makes one, none, as the generated screen showed no picture there), its
controls, best difficulty (its line gone until the profile has finished a
level, as the generated screen hid it) and current level. BACK is at the
bar's left, OK, CREATE NEW and DELETE at its right, as Campaign's. A (OK)
chooses the row's profile and goes back (on the row that makes one, it asks
first), Y (CREATE NEW) makes one, X (DELETE, not on the row that makes one)
deletes the chosen one, B goes back. The list's model is the generated screen's (menu_functions.c's
`profile_list`: the profiles read, the row chosen, its scroll), and the
actions run the generated screen's functions and open its screens. The
classic engine keeps its own screen and the original pictures.

## Leaving an editor with changes not saved

The gametype editor (Edit Gametypes' EDIT, a generated screen) takes B, Back
and its CANCEL with `port edit leave` (`tools/port_settings.py`'s
`WIDGET_PATCHES`, `menu_functions.c`'s `edit_leave_ask`): with nothing
changed it goes back, as before; with changes it opens
`screens/save_changes.xml`, a pop-up of its own (`screens/save_changes`, a
stand-in in `blocks.xml`), the editor kept in the history. SAVE saves them as
the editor's OK does (a built-in gametype asks for a new name first, the
editor kept for its OK), DON'T SAVE lets them go, and CANCEL (or B) goes back
to the editor, the changes kept (`pc_menu_edit_leave`). Another generated
editor opts in with the same handler. The editor's Step 1 (its GAME TYPE)
shows the gametype's own type while its BACK is focused, not the PC version's
picture of a SAVE CHANGES question, which looked like one BACK asked (#901).

## The pause menu

`screens/pause.xml` (a multiplayer game's) and `screens/pause_campaign.xml`
(a campaign's, single player's or network co-op's) take the place of the
map's pause screens under the new engine: the game opens them where it
would open the map's (`ui_widget.c`'s `ui_check_for_pause_game` asks
`menu_screen_pause_name`), as a stand-in of `screens/blocks.xml` that pauses
the game as the map's screen would (`screens/pause`, `screens/pause_campaign`)
or not (a network game's: `screens/pause_live`, `screens/pause_campaign_live`).
They look as the map's do as the port has patched them (`menu_tags.c`'s
`pause_patch` and `pause_campaign_patch`), drawn with the same pictures,
fonts and the map's legend row, and have the same rows and actions:

- multiplayer: RESUME GAME, SETTINGS, the host's HOST CONTROLS, END GAME or
  CHAPTERS, LEAVE GAME;
- campaign: RESUME GAME, SETTINGS, network co-op's FRIENDLY FIRE and its
  host's HOST CONTROLS, REVERT TO SAVED, RESTART LEVEL, SAVE AND QUIT, and
  the mission objectives beside them.

Each opens on RESUME GAME. In split screen the one screen, made for the
menus' 640 by 480, is scaled into the player's view (`menu_view.c`), in place
of the Xbox's own 2-player and 4-player layouts. The classic engine keeps the
map's screens.

## The main menu: four doors

`screens/main.xml`: CAMPAIGN, VERSUS, SETTINGS and QUIT under the title art
(and VR TUTORIAL first in the headset), each focused item's hint under it,
and the button-hint bar at the foot of the screen.

The main menu no longer offers the PC version's MULTIPLAYER and PRO 2V2
items. Under the new engine, these are unreachable from the menus until later
steps rebuild them:

- the Multiplayer hub itself, and its ONLINE NOW and BEST FOR YOU panel;
- LOCAL NETWORK (the local network's games) and JOIN BY LINK. A link opened
  from outside the game still joins;
- CREATE GAME: hosting on the Internet, on the local network, or offline (a
  game of this machine alone, with bots);
- EDIT GAMETYPES (also reached from Server Setup, which hosting opens, so
  that is unreachable too);
- PARTY and FRIENDS from the hub. The Party screen still opens over the main
  menu when a party's game is followed;
- PRO 2V2's own screen (matchmaking a 2v2, hosting one). Its games stay
  findable through the server browser's `pro_2v2` game type filter.

Co-op stays reachable: CAMPAIGN, then CO-OP, opens co-op's screen (HOST,
JOIN and SPLIT SCREEN: "Co-op", above), as the server browser's CO-OP tab
does.

There are two temporary ways in. `display.menu_engine = "classic"` has the old
main menu. `debug.menu_open =
main_menu/multiplayer_type_select/multiplayer_type_select_screen` starts on
the Multiplayer hub, from which everything above is reached.
