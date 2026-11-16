/*
MENU_ENGINE.H

The port's menu engine (menu_engine.c, docs/menu_engine.md): the generated
screens of the PC menus (port/assets/menus) run from their model, under
display.menu_engine "new". A screen is one widget of the game's, its
stand-in, which ui_widget.c opens, keeps in its history and closes as its
own; the engine lays out, draws and takes the events of what is in it.
These are ui_widget.c's calls into the engine, and the engine's into it.
*/

#ifndef MENU_ENGINE_H
#define MENU_ENGINE_H

struct widget_instance;
struct event_record;

/* ---------- ui_widget.c's calls into the engine */

/* a screen opened (a widget with no parent): the engine takes it if it is a
generated screen and display.menu_engine is "new" */
void menu_engine_screen_opened(struct widget_instance *screen);
/* a screen closed: the engine lets it go */
void menu_engine_screen_closed(struct widget_instance *screen);
/* the screen drawn by the engine: FALSE if it is not the engine's (or the
engine is drawing it now), for ui_widget.c to draw */
boolean menu_engine_screen_draw(struct widget_instance *screen, rectangle2d *clip, point2d offset, boolean focus,
	boolean nifty);
/* whether the engine runs the screen (a generated or hand-written screen,
laid out for the menus' 640 by 480) */
boolean menu_engine_screen_owned(struct widget_instance const *screen);
/* an event for the screen taken by the engine (*deleted: the screen is gone
after it): FALSE if the screen is not the engine's */
boolean menu_engine_screen_event(struct widget_instance *screen, struct event_record *event, boolean *deleted);

/* ---------- ui_widget.c's for the engine (port: in ui_widget.c) */

/* one widget drawn as ui_widget.c draws it, but not its children: its game
data inputs run, its picture, text, spinner or list drawn, its box and
targets noted. FALSE if it is not shown; else *children_offset is where its
children are drawn from and *children whether they are drawn */
boolean ui_widget_port_render_node(struct widget_instance *widget, rectangle2d *clip, point2d offset, boolean focus,
	boolean nifty, point2d *children_offset, boolean *children);
/* an event processed by ui_widget.c's own code (the widget and those it
passes it to) */
void ui_widget_port_process_event(struct widget_instance *widget, struct event_record *event, boolean *deleted);
/* what the engine takes its screens' events by: a widget's definition's
flags, its event handlers (their count, each one's event type, one run), and
ui_widget.c's steps: whether a widget takes a controller's events, the
screen before, the focus given to a widget, a list's items' frames brought
up to date, the time a controller's D-pad direction last moved the menus,
the menus' clock, the pointer no longer what moved the focus last, and
whether no screen is open */
long ui_widget_port_definition_flags(struct widget_instance const *widget);
long ui_widget_port_handler_count(struct widget_instance const *widget);
short ui_widget_port_handler_event_type(struct widget_instance const *widget, long index);
void ui_widget_port_handler_dispatch(struct widget_instance *widget, struct event_record *event, long index,
	boolean *deleted);
/* (an event handler of the game's not of the widget's definition: the
model's, which menu_tags.c makes) */
void ui_widget_port_handler_run(struct widget_instance *widget, struct event_record *event, void *handler,
	boolean *deleted);
boolean ui_widget_port_takes_events(struct widget_instance const *widget, short controller_index);
void ui_widget_port_go_back(struct widget_instance *widget);
void ui_widget_port_give_focus(struct widget_instance *widget, struct widget_instance *focus);
void ui_widget_port_lists_update(struct widget_instance *widget);
unsigned long *ui_widget_port_dpad_time(short controller_index, short direction);
unsigned long ui_widget_port_milliseconds(void);
void ui_widget_port_pointer_not_last(void);
boolean ui_widget_port_no_screens(void);

/* ---------- the widgets' definitions (ui_widget.c, menu_tags.c) */

/* whether the engine draws the menus' files' widgets from the model's looks
(display.menu_engine "new") */
boolean menu_engine_looks(void);
/* a widget's definition as the game's widgets read it: the engine's, made
from the widget's look in the model (menu_tags.c's pc_menu_look_definition),
where it draws the widget; else the widget's tag */
void *menu_engine_definition(long tag_index);

/* ---------- the menus' functions' (menu_functions.c) */

/* a settings row's help and note (after a "|"), from the model, where the
engine runs its screen (*found: the model has the row) */
wchar_t const *menu_engine_row_help(struct widget_instance *row, boolean *found);

/* the menus' model (NULL: none); the model's screen of a widget's screen,
and the model's row of a settings row (op_<key>), on either engine (NULL:
the model has none) */
struct halo_menu_model;
struct halo_menu_model_screen;
struct halo_menu_model_row;
struct halo_menu_model const *menu_engine_model(void);
struct halo_menu_model_screen const *menu_engine_model_screen_of(struct widget_instance const *widget);
struct halo_menu_model_row const *menu_engine_model_row_of(struct widget_instance const *row);
/* a widget's role in its look in the model (model/look's role=: what the
game's code finds it by), NULL if it has none */
char const *menu_engine_look_role(struct widget_instance const *widget);

/* whether the engine lays out what is in the widget (a settings tab's or
page's rows) from the model */
boolean menu_engine_lays_out(struct widget_instance *widget);
/* the geometry the settings screen of a widget is laid out in: its own (the
tall layout's) where the engine runs it, else the model's first (NULL: no
model) */
struct halo_menu_model_geometry;
struct halo_menu_model_geometry const *menu_engine_settings_geometry(struct widget_instance *widget);
/* where a geometry's rows or panel end (its rows_bottom or panel_bottom):
the tall layout's (a geometry that scrolls) the footer's lift higher on a
touchscreen (ui_widget_port_foot_lift), the footer raised there */
long menu_engine_geometry_bottom(struct halo_menu_model_geometry const *geometry, long bottom);

/* ---------- Settings' search, the engine's alone (menu_search.c; the menus'
functions' settings_search_*) */

/* whether the widget's screen is the search's */
boolean menu_engine_search_screen(struct widget_instance const *widget);
/* the rows of every tab and page that match what is typed (shown: whether
one may be found, its conditions), made in the search's list (a widget of
its screen) from their own pages' widgets, as many as fit, again only if what
is typed has changed: their count */
typedef int (*menu_engine_search_shown)(void *context, struct halo_menu_model_screen const *screen,
	struct halo_menu_model_row const *row);
long menu_engine_search_find(struct widget_instance *list, char const *typed, menu_engine_search_shown shown,
	void *context);
/* how many rows match in all (more than are made: the words under them say
so), the index'th row made (NULL past the last), and the group'th group's
crumb (the way to its rows, NULL past the last) */
long menu_engine_search_matched(void);
struct widget_instance *menu_engine_search_found(long index);
struct halo_menu_model_screen const *menu_engine_search_found_screen(long index);
char const *menu_engine_search_crumb(long group);

/* ---------- the hand-written screens (menu_screen.c, docs/menu_screens.md) */

/* a screen opened: its widgets made from the hand-written screen that takes
its place, if one does (TRUE), in place of its own */
boolean menu_screen_opened(struct widget_instance *screen);
/* the screen let go of (closed) */
void menu_screen_closed(struct widget_instance *screen);
/* before the screen is drawn: made again if its file was read again, its
focused item's hint shown */
void menu_screen_frame(struct widget_instance *screen);
/* a widget of a hand-written screen about to be drawn (at offset, its
parent's place), shown or not: what the screen draws in its place in the
widgets' order (its <spartan>'s Spartans' view, in the picture's) */
void menu_screen_widget_drawing(struct widget_instance const *widget, point2d offset);
/* B or Back on the screen: its back action run (TRUE), else FALSE for the
engine to take the event */
boolean menu_screen_back(struct widget_instance *screen, struct event_record *event, boolean *deleted);
/* a building block's text colour, where it is not its definition's (a
focused item's): TRUE if it is */
boolean menu_screen_text_color(struct widget_instance const *widget, real_argb_color *color);
/* a hand-written screen's block drawn otherwise than its definition: an
item or a header as its file's picture (*bitmap_tag; NONE, the definition's
own), a card and its parts scaled to fill their room; *bounds comes in as
the definition's and goes out as the box it is drawn in. FALSE for a widget
drawn as its definition has it */
boolean menu_screen_item_picture(struct widget_instance const *widget, long *bitmap_tag, rectangle2d *bounds);
/* whether a hand-written screen's picture is shrunk whole to the box it is
drawn in (what its widget's own size shows of it), not cut to it: the
original picture in a tall panel's <spartan> line, smaller than the
picture */
boolean menu_screen_picture_stretched(struct widget_instance const *widget);
/* the screen a pause menu opens as (name: the map's pause screen's tag):
under the menu engine, the hand-written pause menu that takes its place,
else name */
char const *menu_screen_pause_name(char const *name);
/* "port screen do", an item's A: its action run */
boolean menu_screen_do(struct widget_instance *item, struct event_record *event, boolean *deleted);
/* "port screen key", a button bar prompt's A (a click, a tap): its key
pressed (accept: the item chosen's action) */
boolean menu_screen_key(struct widget_instance *prompt, struct event_record *event, boolean *deleted, short controller);
/* "port screen left", "port screen right" (step -1, 1): left or right on a
tall screen's row (its table's left or right) or panel's choice */
boolean menu_screen_step(struct widget_instance *widget, struct event_record *event, boolean *deleted, short step);

#endif
