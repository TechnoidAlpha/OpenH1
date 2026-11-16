/*
MENU_SEARCH.C

Settings' search, the menu engine's (docs/menu_engine.md, "Deliberate
differences"; tools/port_settings.py's SETTINGS_SEARCH): the rows of the
menus' model that match what is typed, best first. The words typed are
compared, without case, with the words of each row's label, the other words
it is found by (its search: "fov" for FIELD OF VIEW), its choices' words
(BORDERLESS) and its help's: a whole word first, then the start of one, then
a part of a label's (SYNC in VSYNC). Every word typed must find something of
the row; the row's score is what they find added up, a label's matches above
the others', a help's least. A word is letters and digits: ANTI-ALIASING is
the words ANTI and ALIASING, and ANTIALIASING too.

What the rows are is the model's alone (halo_menus.h); menu_engine.c makes
the rows found and lays them out, and menu_functions.c takes what is typed.
*/

#include "halo_menus.h"
#include "menu_search.h"

#include <string.h>

/* the longest word compared, and the most words typed */
#define SEARCH_WORD_LENGTH 48
#define SEARCH_WORDS 8

static char search_lower(char character)
{
	return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

static int search_letter(char character)
{
	return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
		(character >= '0' && character <= '9');
}

/* the next word of text from *at, in lower case (a run of letters and
digits; joined, the letters and digits of a run of other characters but
spaces: ANTI-ALIASING's ANTIALIASING): its length, 0 past the last */
static int search_next_word(char const **at, char *word, int joined)
{
	char const *text = *at;
	int length = 0;

	if (!text)
		return 0;
	if (joined)
	{
		while (*text == ' ')
			text++;
		for (; *text && *text != ' '; text++)
		{
			if (search_letter(*text) && length < SEARCH_WORD_LENGTH - 1)
				word[length++] = search_lower(*text);
		}
	}
	else
	{
		while (*text && !search_letter(*text))
			text++;
		for (; *text && search_letter(*text); text++)
		{
			if (length < SEARCH_WORD_LENGTH - 1)
				word[length++] = search_lower(*text);
		}
	}
	word[length] = 0;
	*at = text;
	return length || *text ? (length ? length : -1) : 0;
}

/* how a word typed matches the words of a text: 2 one of them, 1 the start
of one, 0 neither */
static int search_words_match(char const *text, char const *typed)
{
	char word[SEARCH_WORD_LENGTH];
	size_t typed_length = strlen(typed);
	int best = 0;
	int joined;

	for (joined = 0; joined < 2 && best < 2; joined++)
	{
		char const *at = text;
		int length;

		while ((length = search_next_word(&at, word, joined)) != 0 && best < 2)
		{
			if (length < 0)
				continue;
			if (!strcmp(word, typed))
				best = 2;
			else if (!strncmp(word, typed, typed_length))
				best = 1;
		}
	}
	return best;
}

/* whether a word typed is a part of one of the text's words (joined: VSYNC,
ANTIALIASING) */
static int search_part_of(char const *text, char const *typed)
{
	char word[SEARCH_WORD_LENGTH];
	char const *at = text;
	int length;

	while ((length = search_next_word(&at, word, 1)) != 0)
	{
		if (length > 0 && strstr(word, typed))
			return 1;
	}
	return 0;
}

/* how well the row matches one word typed: the best of what it finds */
static long search_word_score(struct halo_menu_model_row const *row, char const *typed)
{
	size_t length = strlen(typed);
	int match;

	if ((match = search_words_match(row->label, typed)) != 0)
		return match == 2 ? MENU_SEARCH_LABEL_WORD : MENU_SEARCH_LABEL_START;
	if (row->search && (match = search_words_match(row->search, typed)) != 0)
		return match == 2 ? MENU_SEARCH_OTHER_WORD : MENU_SEARCH_OTHER_START;
	if (length >= MENU_SEARCH_LEAST_HELP && search_part_of(row->label, typed))
		return MENU_SEARCH_LABEL_PART;
	if (row->search_choices && (match = search_words_match(row->search_choices, typed)) != 0)
		return match == 2 ? MENU_SEARCH_CHOICE_WORD : MENU_SEARCH_CHOICE_START;
	if (length >= MENU_SEARCH_LEAST_HELP)
	{
		match = row->help ? search_words_match(row->help, typed) : 0;
		if (!match && row->note)
			match = search_words_match(row->note, typed);
		if (match)
			return match == 2 ? MENU_SEARCH_HELP_WORD : MENU_SEARCH_HELP_START;
	}
	return 0;
}

long menu_search_score(struct halo_menu_model_row const *row, char const *typed)
{
	char words[SEARCH_WORDS][SEARCH_WORD_LENGTH];
	char first[SEARCH_WORD_LENGTH];
	char const *at = typed;
	long score = 0;
	int count = 0, length, index;

	if (!row || !row->label || !typed)
		return 0;
	while (count < SEARCH_WORDS && (length = search_next_word(&at, words[count], 0)) != 0)
	{
		if (length > 0)
			count++;
	}
	for (index = 0; index < count; index++)
	{
		long found = search_word_score(row, words[index]);

		if (!found)
			return 0;
		score += found;
	}
	/* (a label that starts with the first word typed: FRAME RATE & SYNC
	before SHOW FRAME RATE for "frame") */
	at = row->label;
	if (count && search_next_word(&at, first, 0) > 0 && !strncmp(first, words[0], strlen(words[0])))
		score += MENU_SEARCH_LABEL_FIRST;
	return count ? score : 0;
}

/* whether a row is one a search finds: a choice's, a slider's or a link's of
a tab or a page, with a label of its own (not Key Bindings' actions, nor a
status, nor a label the game writes) */
static int search_findable(struct halo_menu_model_screen const *screen, struct halo_menu_model_row const *row)
{
	if (!screen->kind || strcmp(screen->kind, "settings") || !screen->layout ||
		(strcmp(screen->layout, "tab") && strcmp(screen->layout, "page")))
	{
		return 0;
	}
	if (!row->label || !row->label[0] || row->named)
		return 0;
	return !strcmp(row->kind, "choice") || !strcmp(row->kind, "slider") || !strcmp(row->kind, "link");
}

long menu_search_rank(struct halo_menu_model const *model, char const *typed, menu_search_shown shown,
	void *context, struct menu_search_found *found, long maximum)
{
	long matched = 0, screen_index;

	if (!model || !typed)
		return 0;
	for (screen_index = 0; screen_index < model->screen_count; screen_index++)
	{
		struct halo_menu_model_screen const *screen = &model->screens[screen_index];
		long index;

		for (index = 0; index < screen->row_count; index++)
		{
			struct halo_menu_model_row const *row = &model->rows[screen->first_row + index];
			struct menu_search_found entry;
			long place;

			if (!search_findable(screen, row))
				continue;
			entry.score = menu_search_score(row, typed);
			if (!entry.score || (shown && !shown(context, screen, row)))
				continue;
			entry.row = screen->first_row + index;
			entry.screen = screen_index;
			matched++;
			/* (in its place among those found, after the equal: the
			model's order) */
			for (place = matched - 1 < maximum ? matched - 1 : maximum; place > 0 &&
				found[place - 1].score < entry.score; place--)
			{
				if (place < maximum)
					found[place] = found[place - 1];
			}
			if (place < maximum)
				found[place] = entry;
		}
	}
	return matched;
}

struct halo_menu_model_row const *menu_search_hint(struct halo_menu_model const *model,
	struct halo_menu_model_screen const *screen, char const *typed)
{
	char word[SEARCH_WORD_LENGTH];
	long index;

	if (!model || !screen || !typed)
		return NULL;
	for (index = 0; index < screen->row_count; index++)
	{
		struct halo_menu_model_row const *row = &model->rows[screen->first_row + index];
		char const *at = typed;
		int length;

		if (strcmp(row->kind, "hint") || !row->label)
			continue;
		while ((length = search_next_word(&at, word, 0)) != 0)
		{
			if (length > 0 && search_words_match(row->label, word) == 2)
				return row;
		}
	}
	return NULL;
}
