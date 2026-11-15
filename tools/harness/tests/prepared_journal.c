/* A map's preparation's record (port/linux/src/d3d8_device.c's journal,
port/linux/game/render_prepared.c): the entries read back are the entries
recorded, a repeat once, in their order, with the textures and pages between
them; pages recorded under one platform's page size (4 KB, 16 KB on Apple's
hardware) are played back under the other's as exactly the pages that hold
them; malformed words are refused; a record whose draws the played back ones
do not all have under its own derivation, or that is another form's or
map's, is refused, and its key holds no setting; one record, made under any
settings, played back under any others, makes every draw those settings
would have walked. */
#include "harness.h"

#include <stdint.h>

typedef unsigned long DWORD;
typedef int BOOL;
#define D3DTSS_MAXSTAGES 4
#define D3DRS_MAX 144
#define D3DTSS_MAX 32

/* FNV-1a, 64 bits (xgpu_manifest.c's) */
static uint64_t xgpu_manifest_hash(const void *data, size_t size, uint64_t hash)
{
	const unsigned char *bytes = data;
	size_t index;

	for (index = 0; index < size; index++)
	{
		hash ^= bytes[index];
		hash *= 1099511628211ULL;
	}
	return hash;
}
#define XGPU_MANIFEST_HASH_START 14695981039346656037ULL

#include "journal.inc"

static struct
{
	uint32_t *words;
	unsigned long count, capacity;
	BOOL failed;
	struct journal_state last;
	uint64_t seen[JOURNAL_SEEN_SLOTS];
	unsigned long seen_count;
} journal;

#include "journal_code.inc"
#include "prepared.inc"

#define POOL 40
#define ENTRIES 3000
#define PAGE_COUNT 4096
/* (the window's: platform.h's PLATFORM_CONTIGUOUS_SIZE) */
#define WINDOW_SIZE 0x20000000UL

static struct journal_state pool[POOL];
static struct { unsigned long op; int state; } expected[ENTRIES];
static int expected_count;
static uint32_t textures_expected[ENTRIES][2];
static int textures_expected_count;

static struct { unsigned long op; struct journal_state state; } read_entries[ENTRIES];
static int read_count;
static uint32_t textures_read[ENTRIES][2];
static int textures_read_count;
static unsigned long pages_read[16][2];
static int pages_read_count;

static void on_entry(unsigned long op, const struct journal_state *state)
{
	CHECK(read_count < ENTRIES, "too many entries");
	read_entries[read_count].op = op;
	read_entries[read_count].state = *state;
	read_count++;
}

static void on_texture(uint32_t tag_index, uint32_t bitmap_index)
{
	textures_read[textures_read_count][0] = tag_index;
	textures_read[textures_read_count][1] = bitmap_index;
	textures_read_count++;
}

static void on_pages(unsigned long first, unsigned long end)
{
	pages_read[pages_read_count][0] = first;
	pages_read[pages_read_count][1] = end;
	pages_read_count++;
}

static const struct journal_reader reader = { on_entry, on_texture, on_pages };

static void texture_note(uint32_t tag_index, uint32_t bitmap_index)
{
	CHECK(journal_reserve(3), "room");
	journal.words[journal.count++] = JOURNAL_TEXTURE;
	journal.words[journal.count++] = tag_index;
	journal.words[journal.count++] = bitmap_index;
	textures_expected[textures_expected_count][0] = tag_index;
	textures_expected[textures_expected_count][1] = bitmap_index;
	textures_expected_count++;
}

static int read_back(struct journal_state *state)
{
	memset(state, 0, sizeof(*state));
	read_count = textures_read_count = pages_read_count = 0;
	return journal_read(journal.words, journal.count, state, PAGE_COUNT * JOURNAL_PAGE_UNIT, JOURNAL_PAGE_UNIT, &reader);
}

/* states that differ in a few of their words, as one draw's from the next */
static void pool_make(void)
{
	int index, word;

	srand(7);
	for (index = 0; index < POOL; index++)
	{
		uint32_t *words = (uint32_t *)&pool[index];

		if (index)
			pool[index] = pool[rand() % index];
		for (word = 0; word < 1 + rand() % 6; word++)
			words[rand() % JOURNAL_STATE_WORDS] = (uint32_t)rand();
	}
}

static void record(void)
{
	int index, seen;

	memset(&journal, 0, sizeof(journal));
	pool_make();
	expected_count = textures_expected_count = 0;
	for (index = 0; index < ENTRIES; index++)
	{
		unsigned long op = JOURNAL_DRAW + (unsigned long)(rand() % 3);
		int state = rand() % POOL;

		if (index && rand() % 7 == 0)
			texture_note((uint32_t)rand(), (uint32_t)(rand() % 16));
		journal_append(op, &pool[state]);
		for (seen = 0; seen < expected_count && (expected[seen].op != op || expected[seen].state != state); seen++)
			;
		if (seen == expected_count)
		{
			expected[expected_count].op = op;
			expected[expected_count].state = state;
			expected_count++;
		}
	}
	/* (the pages last, as halo_gpu_journal_take writes them) */
	CHECK(journal_reserve(5), "room");
	journal.words[journal.count++] = JOURNAL_PAGES | 2u << 8;
	journal.words[journal.count++] = 3;
	journal.words[journal.count++] = 9;
	journal.words[journal.count++] = 100;
	journal.words[journal.count++] = 101;
}

static void case_roundtrip(void)
{
	struct journal_state state;
	int index;

	record();
	CHECK(read_back(&state), "the words read");
	CHECK(read_count == expected_count, "%d entries read, %d recorded apart", read_count, expected_count);
	for (index = 0; index < read_count; index++)
	{
		CHECK(read_entries[index].op == expected[index].op, "entry %d's type", index);
		CHECK(!memcmp(&read_entries[index].state, &pool[expected[index].state], sizeof(state)), "entry %d's state",
			index);
	}
	CHECK(textures_read_count == textures_expected_count, "%d textures read of %d", textures_read_count,
		textures_expected_count);
	CHECK(!memcmp(textures_read, textures_expected, sizeof(textures_read[0]) * textures_read_count), "the textures");
	CHECK(pages_read_count == 2 && pages_read[0][0] == 3 && pages_read[0][1] == 9 && pages_read[1][0] == 100,
		"the pages");
}

static void case_malformed(void)
{
	struct journal_state state;
	uint32_t saved;
	unsigned long count;

	record();
	count = journal.count;
	/* (cut short: in the first entry's pairs) */
	journal.count = 2;
	CHECK(!read_back(&state), "a journal cut short is refused");
	journal.count = count;
	/* (a type no entry has) */
	saved = journal.words[0];
	journal.words[0] = 0x33;
	journal.count = 1;
	CHECK(!read_back(&state), "an unknown type is refused");
	journal.words[0] = saved;
	journal.count = count;
	/* (a word past the state's) */
	saved = journal.words[1];
	journal.words[1] = JOURNAL_STATE_WORDS;
	CHECK(!read_back(&state), "a word past the state is refused");
	journal.words[1] = saved;
	/* (pages past the mirror's) */
	journal.words[count - 1] = PAGE_COUNT + 1;
	CHECK(!read_back(&state), "pages past the mirror are refused");
	journal.words[count - 1] = 101;
	CHECK(read_back(&state), "the journal as recorded is read");
}

/* ---------- the pages, from one platform's page size to another's

Pages set in a bitmap of one page size (journal_runs, as
halo_gpu_journal_take writes them) and read back under another's must be
given as exactly the pages of that size that share a byte with them, each
once, in order: among them two runs of 4 KB pages in one 16 KB page, runs
that start and end within a 16 KB page, and the high runs of a campaign
map's (a10's, near 0x0E700000 into the window, where 4 KB pages' numbers are
past the 16 KB pages' count), and the window's last page. */

#define SIZES 2
static const unsigned long page_sizes[SIZES] = { 0x1000, 0x4000 };
static unsigned char written[WINDOW_SIZE / 0x1000 / 8], wanted[WINDOW_SIZE / 0x1000 / 8],
	given[WINDOW_SIZE / 0x1000 / 8];
static unsigned long given_end, given_runs;
static int given_twice, given_backwards;

static void page_set(unsigned char *pages, unsigned long page)
{
	pages[page >> 3] |= (unsigned char)(1u << (page & 7));
}

static int page_is(const unsigned char *pages, unsigned long page)
{
	return (pages[page >> 3] >> (page & 7)) & 1;
}

static void on_pages_given(unsigned long first, unsigned long end)
{
	unsigned long page;

	if (first < given_end)
		given_backwards = 1;
	for (page = first; page < end; page++)
	{
		if (page_is(given, page))
			given_twice = 1;
		page_set(given, page);
	}
	given_end = end;
	given_runs++;
}

static const struct journal_reader page_reader = { on_entry, on_texture, on_pages_given };

static void case_pages(void)
{
	/* (in bytes into the window: [start, end) each) */
	static const unsigned long runs[][2] = {
		{ 0x0, 0x1000 }, { 0x2000, 0x3000 }, { 0x5000, 0x9000 }, { 0x13000, 0x20000 }, { 0x40000, 0x48000 },
		{ 0x691000, 0x6A1000 }, { 0x0E6C0000, 0x0E6C7000 }, { 0x0E6F1000, 0x0E6F3000 }, { 0x0E7AE000, 0x0E7AF000 },
		{ 0x1FFFF000, 0x20000000 },
	};
	int write, read, run;

	for (write = 0; write < SIZES; write++)
	{
		unsigned long write_size = page_sizes[write];

		for (read = 0; read < SIZES; read++)
		{
			unsigned long read_size = page_sizes[read], page;
			struct journal_state state;

			memset(written, 0, sizeof(written));
			memset(wanted, 0, sizeof(wanted));
			memset(given, 0, sizeof(given));
			given_end = given_runs = 0;
			given_twice = given_backwards = 0;
			for (run = 0; run < (int)(sizeof(runs) / sizeof(runs[0])); run++)
			{
				for (page = runs[run][0] / write_size; page < (runs[run][1] + write_size - 1) / write_size; page++)
					page_set(written, page);
			}
			for (page = 0; page < WINDOW_SIZE / write_size; page++)
			{
				unsigned long other;

				if (!page_is(written, page))
					continue;
				for (other = page * write_size / read_size; other < ((page + 1) * write_size + read_size - 1) / read_size;
					other++)
					page_set(wanted, other);
			}
			memset(&journal, 0, sizeof(journal));
			journal_runs(written, WINDOW_SIZE / write_size, write_size);
			CHECK(!journal.failed, "room");
			memset(&state, 0, sizeof(state));
			CHECK(journal_read(journal.words, journal.count, &state, WINDOW_SIZE, read_size, &page_reader),
				"written in %lu-byte pages, read in %lu-byte pages: refused as malformed", write_size, read_size);
			CHECK(!memcmp(given, wanted, WINDOW_SIZE / read_size / 8), "written in %lu-byte pages, read in "
				"%lu-byte pages: not the pages that hold them", write_size, read_size);
			CHECK(!given_twice && !given_backwards, "written in %lu-byte pages, read in %lu-byte pages: a page given "
				"twice or out of order", write_size, read_size);
			CHECK(given_runs > 0, "no pages given");
			free(journal.words);
		}
	}
	/* (a run past the window, in units) */
	memset(&journal, 0, sizeof(journal));
	CHECK(journal_reserve(3), "room");
	journal.words[journal.count++] = JOURNAL_PAGES | 1u << 8;
	journal.words[journal.count++] = WINDOW_SIZE / JOURNAL_PAGE_UNIT - 1;
	journal.words[journal.count++] = WINDOW_SIZE / JOURNAL_PAGE_UNIT + 1;
	{
		struct journal_state state;

		memset(&state, 0, sizeof(state));
		CHECK(!journal_read(journal.words, journal.count, &state, WINDOW_SIZE, 0x4000, &page_reader),
			"a run past the window is refused");
	}
	free(journal.words);
}

static void case_missing(void)
{
	unsigned long long recorded[] = { 1, 3, 5, 7 };
	unsigned long long more[] = { 1, 2, 3, 5, 7, 9 };
	unsigned long long fewer[] = { 1, 3, 7 };
	unsigned long long other[] = { 2, 4, 6, 8 };

	CHECK(render_prepared_draws_missing(recorded, 4, recorded, 4) == 0, "the same draws");
	CHECK(render_prepared_draws_missing(recorded, 4, more, 6) == 0, "more draws than the record's");
	CHECK(render_prepared_draws_missing(recorded, 4, fewer, 3) == 1, "a draw not made");
	CHECK(render_prepared_draws_missing(recorded, 4, other, 4) == 4, "none of the record's draws");
	CHECK(render_prepared_draws_missing(recorded, 4, NULL, 0) == 4, "no draws");
}

/* ---------- one record under every settings

A toy front end: a walk's draw is a game state (journal_state: its pipeline
in render_state[0], its HDR tag in hdr_kind, the enhanced lights' count and
its window's span as journal_capture notes them, whatever the settings: the
walk draws the union as it records). Its pipeline under settings is derived
as the front end derives it: the tag only with the HDR on, the lights only
enhanced, the world's light (read from the pre-pass in a window it finds
once a pixel: world_ao_key) only with display.ambient_occlusion "world".
Recorded under some settings (a draw journaled where it is new in its
derived pipeline or in journal_settings_blind_of's words:
preparation_seen), played back under others, the record's pipelines must
be the walk's under those. */

enum { SETTING_HDR = 1, SETTING_LIGHTS = 2, SETTING_WORLD = 4, SETTINGS = 8 };
#define WALK 400
#define DERIVED_MAXIMUM (WALK * 2)

struct derived { uint32_t pipeline, tag, lit, world; };

static struct journal_state walk[WALK];

static struct derived derive(const struct journal_state *state, int settings)
{
	struct derived derived;

	memset(&derived, 0, sizeof(derived));
	derived.pipeline = state->render_state[0];
	derived.tag = (settings & SETTING_HDR) ? state->hdr_kind : 0;
	derived.lit = (settings & SETTING_LIGHTS) && state->lights_count > 0;
	/* (the world's light, read from the pre-pass where found; a found
	window's first-person weapon none, but its twin made) */
	if ((settings & SETTING_WORLD) && state->world_span < 5)
		derived.world = (uint32_t[]){ 0, 1, 0, 2, 3 }[state->world_span];
	return derived;
}

static int derived_add(struct derived *set, int count, struct derived derived)
{
	int index;

	for (index = 0; index < count; index++)
	{
		if (!memcmp(&set[index], &derived, sizeof(derived)))
			return count;
	}
	set[count] = derived;
	return count + 1;
}

static int derived_has(const struct derived *set, int count, struct derived derived)
{
	int index;

	for (index = 0; index < count; index++)
	{
		if (!memcmp(&set[index], &derived, sizeof(derived)))
			return 1;
	}
	return 0;
}

static void walk_make(void)
{
	int index;

	srand(11);
	memset(walk, 0, sizeof(walk));
	for (index = 0; index < WALK; index++)
	{
		struct journal_state *state = &walk[index];

		state->render_state[0] = (uint32_t)(rand() % 24);
		state->hdr_kind = (uint32_t)(rand() % 3);
		state->lights_count = (uint32_t)(rand() % 2);
		/* (world_ao.h's WORLD_AO_SPAN_*: none, open, its first person, found, its first person) */
		state->world_span = (uint32_t)(rand() % 5);
	}
}

/* the walk's pipelines under settings */
static int walked(int settings, struct derived *set)
{
	int index, count = 0;

	for (index = 0; index < WALK; index++)
		count = derived_add(set, count, derive(&walk[index], settings));
	return count;
}

static struct journal_state recorded_entries[WALK];

/* the walk recorded under settings: the entries journaled */
static int recorded(int settings)
{
	struct { struct derived derived; unsigned long blind[3]; } seen[WALK], key;
	int index, other, seen_count = 0, count = 0;

	for (index = 0; index < WALK; index++)
	{
		const struct journal_state *state = &walk[index];

		memset(&key, 0, sizeof(key));
		key.derived = derive(state, settings);
		journal_settings_blind_of(key.blind, (int)state->hdr_category, (int)state->hdr_kind, (BOOL)state->hdr_view,
			(int)state->lights_count, (BOOL)state->lights_environment, (int)state->world_span);
		for (other = 0; other < seen_count && memcmp(&seen[other], &key, sizeof(key)); other++)
			;
		if (other < seen_count)
			continue;
		seen[seen_count++] = key;
		recorded_entries[count++] = *state;
	}
	return count;
}

static void case_settings(void)
{
	static struct derived wanted[DERIVED_MAXIMUM], made[DERIVED_MAXIMUM];
	int recorded_under, played_under, index, wanted_count, made_count, entries;

	walk_make();
	for (recorded_under = 0; recorded_under < SETTINGS; recorded_under++)
	{
		entries = recorded(recorded_under);
		for (played_under = 0; played_under < SETTINGS; played_under++)
		{
			wanted_count = walked(played_under, wanted);
			made_count = 0;
			for (index = 0; index < entries; index++)
			{
				const struct journal_state *state = &recorded_entries[index];

				made_count = derived_add(made, made_count, derive(state, played_under));
			}
			for (index = 0; index < wanted_count; index++)
				CHECK(derived_has(made, made_count, wanted[index]), "recorded under settings %d, played under %d: "
					"a pipeline the walk makes not made (%d of %d made)", recorded_under, played_under, made_count,
					wanted_count);
			for (index = 0; index < made_count; index++)
				CHECK(derived_has(wanted, wanted_count, made[index]), "recorded under settings %d, played under %d: "
					"a pipeline the walk does not make made", recorded_under, played_under);
		}
	}
}

/* a record's key as render_prepared.c makes it (prepared_choose): its form,
the journal's, the map's scenario, whether the frame draws mirrors and the
structure it starts in */
static unsigned long long record_key(char const *scenario, unsigned long journal_form, boolean mirrors,
	long structure_bsp_index)
{
	return prepared_key(prepared_key_of(scenario, journal_form), mirrors, structure_bsp_index);
}

/* the key: no setting in it, every part of it telling records apart, and
a record played back judged under its own derivation alone */
static void case_key(void)
{
	unsigned long long key = record_key("levels\\a10\\a10", 0x40000 | 400, TRUE, 0);
	struct prepared_header header;

	CHECK(key == record_key("levels\\a10\\a10", 0x40000 | 400, TRUE, 0), "the same map's the same");
	CHECK(key != record_key("levels\\b30\\b30", 0x40000 | 400, TRUE, 0), "another scenario's");
	CHECK(key != record_key("levels\\a10\\a10", 0x50000 | 400, TRUE, 0), "another journal form's");
	CHECK(key != record_key("levels\\a10\\a10", 0x40000 | 400, FALSE, 0), "without mirrors");
	CHECK(key != record_key("levels\\a10\\a10", 0x40000 | 400, TRUE, 1), "another structure's");
	memset(&header, 0, sizeof(header));
	header.magic = PREPARED_MAGIC;
	header.version = PREPARED_VERSION;
	header.header_size = sizeof(header);
	header.checksum = 0x12345678;
	header.key = key;
	header.word_count = 100;
	header.derivation = 0x99;
	CHECK(prepared_header_fits(&header, 0x12345678, key), "a record under any derivation fits");
	CHECK(!prepared_header_fits(&header, 0x12345678, record_key("levels\\a10\\a10", 0x50000 | 400, TRUE,
		0)), "a stale form's key is refused");
	CHECK(render_prepared_played(1, 0, 0, TRUE), "its own derivation's, every draw made");
	CHECK(!render_prepared_played(1, 0, 3, TRUE), "its own derivation's, draws not made");
	CHECK(render_prepared_played(1, 0, 3, FALSE), "another derivation's, its draws derived as others");
	CHECK(!render_prepared_played(1, 2, 0, FALSE), "objects not found, under another derivation");
	CHECK(!render_prepared_played(0, 0, 0, FALSE), "malformed, under another derivation");
}

static void case_stale(void)
{
	struct prepared_header header, changed;

	memset(&header, 0, sizeof(header));
	header.magic = PREPARED_MAGIC;
	header.version = PREPARED_VERSION;
	header.header_size = sizeof(header);
	header.checksum = 0x12345678;
	header.key = 0x1122334455667788ULL;
	header.word_count = 100;
	header.draw_count = 10;
	CHECK(prepared_header_fits(&header, 0x12345678, 0x1122334455667788ULL), "the record's own");
	changed = header;
	changed.version++;
	CHECK(!prepared_header_fits(&changed, 0x12345678, 0x1122334455667788ULL), "another form's");
	CHECK(!prepared_header_fits(&header, 0x12345679, 0x1122334455667788ULL), "another map's");
	CHECK(!prepared_header_fits(&header, 0x12345678, 0x1122334455667789ULL), "another build's or settings'");
	changed = header;
	changed.magic = 0;
	CHECK(!prepared_header_fits(&changed, 0x12345678, 0x1122334455667788ULL), "not a record");
	changed = header;
	changed.word_count = 0;
	CHECK(!prepared_header_fits(&changed, 0x12345678, 0x1122334455667788ULL), "an empty record");
	changed = header;
	changed.word_count = PREPARED_MAXIMUM_WORDS + 1;
	CHECK(!prepared_header_fits(&changed, 0x12345678, 0x1122334455667788ULL), "a record too large");
}

int main(int argc, char **argv)
{
	const char *which = argc > 1 ? argv[1] : "";

	if (!strcmp(which, "roundtrip"))
		case_roundtrip();
	else if (!strcmp(which, "malformed"))
		case_malformed();
	else if (!strcmp(which, "pages"))
		case_pages();
	else if (!strcmp(which, "missing"))
		case_missing();
	else if (!strcmp(which, "stale"))
		case_stale();
	else if (!strcmp(which, "settings"))
		case_settings();
	else if (!strcmp(which, "key"))
		case_key();
	else
		CHECK(0, "no case %s", which);
	return 0;
}
