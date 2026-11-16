/* The keyboard's layout types the characters (port/linux/src/sdl_platform.c's
ascii_from_key, #726): a key's character is the one its layout gives it with
Shift, Caps Lock and AltGr (SDL_GetKeyFromScancode's, here made up), where
that is ASCII, not a US keyboard's shift of the unshifted key: Shift and the
+ key type ? on a Nordic layout, Shift and 7 a /, AltGr and 2 an @. The keys
that type no character keep theirs (Return, Backspace, Tab, Escape), a
layout's character beyond ASCII falls back to the key's own, and a Ctrl
chord types its key (the layout's lookup leaves Ctrl out: the menus'
Ctrl+V; Windows reports AltGr as Ctrl and Alt). */
#include "harness.h"

typedef int BOOL;
typedef char CHAR;
typedef int SDL_Keycode;
typedef unsigned short SDL_Keymod;
#define SDL_KMOD_SHIFT 0x0003
#define SDL_KMOD_CTRL 0x00c0
#define SDL_KMOD_CAPS 0x2000
#define SDL_KMOD_MODE 0x4000
#define SDLK_RETURN 0x0000000d
#define SDLK_KP_ENTER 0x40000058
#define SDLK_BACKSPACE 0x00000008
#define SDLK_TAB 0x00000009
#define SDLK_ESCAPE 0x0000001b
#define SDLK_UNKNOWN 0

#include "under_test.inc"

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("nordic_shifted")
	{
		/* (the key right of 0: + unshifted, ? shifted) */
		CHECK(ascii_from_key('+', '?', SDL_KMOD_SHIFT) == '?', "Shift and + typed %c", ascii_from_key('+', '?', SDL_KMOD_SHIFT));
		CHECK(ascii_from_key('+', '+', 0) == '+', "+ alone not +");
		CHECK(ascii_from_key('7', '/', SDL_KMOD_SHIFT) == '/', "Shift and 7 not /");
		CHECK(ascii_from_key('2', '"', SDL_KMOD_SHIFT) == '"', "Shift and 2 not \"");
	}
	else CASE("altgr")
	{
		CHECK(ascii_from_key('2', '@', SDL_KMOD_MODE) == '@', "AltGr and 2 not @");
		CHECK(ascii_from_key('7', '{', SDL_KMOD_MODE) == '{', "AltGr and 7 not {");
	}
	else CASE("us")
	{
		CHECK(ascii_from_key('a', 'A', SDL_KMOD_SHIFT) == 'A', "Shift and a not A");
		CHECK(ascii_from_key('a', 'A', SDL_KMOD_CAPS) == 'A', "Caps Lock and a not A");
		CHECK(ascii_from_key('a', 'a', 0) == 'a', "a not a");
		CHECK(ascii_from_key('/', '?', SDL_KMOD_SHIFT) == '?', "US Shift and / not ?");
		CHECK(ascii_from_key('1', '!', SDL_KMOD_SHIFT) == '!', "US Shift and 1 not !");
	}
	else CASE("no_character")
	{
		CHECK(ascii_from_key(SDLK_RETURN, SDLK_RETURN, 0) == '\r', "Return not a return");
		CHECK(ascii_from_key(SDLK_BACKSPACE, SDLK_BACKSPACE, 0) == '\b', "Backspace not a backspace");
		CHECK(ascii_from_key(SDLK_TAB, SDLK_TAB, 0) == '\t', "Tab not a tab");
		CHECK(ascii_from_key(SDLK_ESCAPE, SDLK_ESCAPE, 0) == 0x1b, "Escape not an escape");
	}
	else CASE("beyond_ascii")
	{
		/* (a Nordic layout's ö: its key's own; an unknown one too) */
		CHECK(ascii_from_key(0xf6, 0xf6, 0) == 0, "a character beyond ASCII typed");
		CHECK(ascii_from_key('a', SDLK_UNKNOWN, 0) == 'a', "a key the layout has none for not its own");
	}
	else CASE("ctrl")
	{
		CHECK(ascii_from_key('v', 'v', SDL_KMOD_CTRL) == 'v', "Ctrl and v not v");
	}
	else
	{
		fprintf(stderr, "unknown case %s\n", case_name);
		return 2;
	}
	return 0;
}
