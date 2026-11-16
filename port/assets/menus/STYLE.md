# The menus' style guide

Rules for the menus that the review notes have settled (and those the
project's rules already make, in AGENTS.md's "Menus"). An agent fixing a
note that states a general preference adds it here as a rule
(`python tools/menu_notes.py rule "<scope>" "<rule>"`), and each brief
(`python tools/menu_notes.py brief`) quotes the rules that apply to the
note's screen.

A rule is a line `- (scope) rule`: the scope is `everywhere`, or a pattern
of screens' names as the menu files name them
(`main_menu/settings_select/*`, `*/server_settings/*`).

## Rules

- (everywhere) Keep Halo Custom Edition's look and layout where it works: its palette, type, framed panels and sounds; modernise the structure, the information and the wording, not the look.
- (everywhere) Plain names: say what a thing does, not what it is called inside the game.
- (everywhere) Every label, value and prompt fits its box in every variant (keyboard 16:9, controller 16:10, touch on a phone), with the keyboard's keys and the controller's pictures alike; `python tools/menu_gallery.py --check` finds none cut.
- (everywhere) A help or description of more than a line wraps in its box (text_flags wrap) and fits it at 4:3 too.
- (everywhere) A footer's prompts leave room for the controller's pictures of triggers and sticks (a word and a picture: RIGHT and the trigger) on wide screens.
- (everywhere) Controls are sized for a finger and a controller as well as the mouse.
- (main_menu/settings_select/*) A setting's values are short enough for its spinner's 140 units; shorten the words (STRONG & WORSE) before widening the column.
