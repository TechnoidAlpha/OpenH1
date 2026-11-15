/*
HIT_SOUNDS.C (test)

The real hit sounds (hit_sounds.c's hit_sounds_note_damage, the beep's
buffers and spacing) and network_standard.c's rule of the host's
(network_standard_hit_sounds_allowed, the record's stamp and an OpenCE
machine's copy of it) over a fake game: a beep for damage this machine's
player dealt another player, as the deciding machine dealt it; none for
themselves, for no damage (a teammate whom friendly fire spares), for a
bot's or another machine's player, with the setting off, or in a game whose
host forbids them. test_hit_sounds.py takes the code (under_test.inc) and
the record's bit and the module's state (config.inc).
*/

#include "harness.h"

#define SET_FLAG(f, b, v) ((v) ? ((f) |= (unsigned)FLAG(b)) : ((f) &= (unsigned)~FLAG(b)))
#define csmemcpy memcpy
#define csmemset memset

enum
{
	_game_connection_local = 0,
	_game_connection_network_client,
	_game_connection_network_server,
	_game_connection_film_playback,
	_object_being_damaged_body_depleted_bit = 0,
	/* the players: this machine's, another machine's, a bot, this machine's second (split screen) */
	LOCAL = 0x10000,
	REMOTE = 0x20001,
	BOT = 0x30002,
	LOCAL_2 = 0x40003,
	GONE = 0x50004,
};

/* DirectSound's buffers (port/linux/src/dsound_sdl.c's): each beep played, and which buffer */
#define WAVE_FORMAT_PCM 1
#define DSBCAPS_LOCDEFER 0x00040000
typedef struct { unsigned short wFormatTag, nChannels; unsigned long nSamplesPerSec, nAvgBytesPerSec;
	unsigned short nBlockAlign, wBitsPerSample, cbSize; } WAVEFORMATEX;
typedef struct { unsigned long dwSize, dwFlags, dwBufferBytes; WAVEFORMATEX *lpwfxFormat; } DSBUFFERDESC;
typedef struct IDirectSoundBuffer { int number; long volume; long plays; unsigned long size; } IDirectSoundBuffer;
static IDirectSoundBuffer the_buffers[8];
static int buffers_made;
static long plays;
static int last_played;
static long DirectSoundCreateBuffer(DSBUFFERDESC const *description, IDirectSoundBuffer **buffer)
{
	CHECK(description->lpwfxFormat->wFormatTag == WAVE_FORMAT_PCM && description->lpwfxFormat->wBitsPerSample == 16,
		"the beep's buffer is not 16-bit PCM");
	CHECK(buffers_made < 8, "too many buffers made");
	the_buffers[buffers_made].number = buffers_made;
	*buffer = &the_buffers[buffers_made++];
	return 0;
}
static long IDirectSoundBuffer_SetBufferData(IDirectSoundBuffer *buffer, void *data, unsigned long size)
{
	buffer->size = size;
	return 0;
}
static long IDirectSoundBuffer_SetVolume(IDirectSoundBuffer *buffer, long volume) { buffer->volume = volume; return 0; }
static long IDirectSoundBuffer_SetCurrentPosition(IDirectSoundBuffer *buffer, unsigned long at) { return 0; }
static long IDirectSoundBuffer_Play(IDirectSoundBuffer *buffer, unsigned long a, unsigned long b, unsigned long flags)
{
	CHECK(buffer->size > 0, "a beep played from an empty buffer");
	buffer->plays++;
	plays++;
	last_played = buffer->number;
	return 0;
}

#define HIT_SOUND_RATE 48000
#define HIT_SOUND_SAMPLES 4
static short const hit_sound_pcm[HIT_SOUND_SAMPLES] = { 0, 100, -100, 0 };

/* the players: who is this machine's, who is a bot */
struct player_datum { short local_player_index; };
static struct player_datum players[5] = { { 0 }, { NONE }, { NONE }, { 1 }, { NONE } };
static struct player_datum *player_try_and_get(long index)
{
	switch (index)
	{
	case LOCAL: return &players[0];
	case REMOTE: return &players[1];
	case BOT: return &players[2];
	case LOCAL_2: return &players[3];
	}
	return NULL;
}
static int bots_player_is_bot(long index) { return index == BOT; }

/* the clock, the settings */
static unsigned long now = 1000;
static unsigned long system_milliseconds(void) { return now; }
static boolean hit_sounds_setting = TRUE;
/* (the second split screen player's own audio.hit_sounds) */
static boolean hit_sounds_setting_2 = TRUE;
static boolean allow_setting = TRUE;
static unsigned long changes = 1;
/* player_settings.h's: a reader's slots, a local player's own setting */
#define PLAYER_SETTINGS_SLOTS 5
#define PLAYER_SETTINGS_SLOT(local_player_index) \
	((local_player_index) >= 0 && (local_player_index) < PLAYER_SETTINGS_SLOTS - 1 ? \
		(local_player_index) : PLAYER_SETTINGS_SLOTS - 1)
static int config_player_boolean(int local_player, char const *name)
{
	CHECK(!strcmp(name, "audio.hit_sounds"), "an unexpected player's setting %s", name);
	return local_player == 1 ? hit_sounds_setting_2 : hit_sounds_setting;
}
static int config_boolean(char const *name)
{
	if (!strcmp(name, "network.allow_hit_sounds"))
		return allow_setting;
	if (!strcmp(name, "game.auto_pickup") || !strcmp(name, "game.enhanced_animations"))
		return TRUE;
	CHECK(FALSE, "an unexpected setting %s", name);
	return FALSE;
}
static unsigned long config_changes(void) { return changes; }
static long notes;
static void network_test_note_hit_sound(long attacker, long victim) { notes++; }

/* the game: its connection, network_standard.c's state, the record */
struct network_game
{
	byte cooperative_flags;
	byte standard_rate;
	byte standard_flags;
};
static short connection;
static struct
{
	boolean hosting;
	long hosting_rate;
	boolean hosting_hold_to_pick_up;
	boolean hosting_classic_turning;
	boolean hosting_no_hit_sounds;
	boolean hosting_no_direct_camera;
	boolean joined;
} network_standard_globals;
static struct network_game record;
static short game_connection(void) { return connection; }
static struct network_game *network_game_get_game(void) { return &record; }
static long network_standard_server_rate(boolean in_lobby) { return network_standard_globals.hosting ? 60 : 0; }
static boolean network_standard_hit_sounds_allowed(void);
/* (no Pro 2v2 game: test_pro_rules.py's) */
static boolean network_standard_pro_hosting(void) { return FALSE; }

#include "config.inc"
#include "under_test.inc"

/* a hit of the shield, the time moved on past the spacing first */
static long hit(long attacker, long victim, real shield, real body, unsigned long flags)
{
	long before = plays;

	now += 1000;
	hit_sounds_note_damage(attacker, victim, shield, body, flags);
	return plays - before;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	/* a game of this machine's own: this machine's players' damage of another player */
	CASE("local")
	{
		CHECK(hit(LOCAL, REMOTE, 0.2f, 0.f, 0) == 1, "no beep for a shield hit on another player");
		CHECK(hit(LOCAL, BOT, 0.f, 0.3f, 0) == 1, "no beep for a body hit on a bot");
		CHECK(hit(LOCAL_2, LOCAL, 0.1f, 0.f, 0) == 1, "no beep for a split screen player's hit on the other");
		CHECK(hit(LOCAL, REMOTE, 0.f, 0.f, FLAG(_object_being_damaged_body_depleted_bit)) == 1,
			"no beep for a kill outright");
		CHECK(notes == 4, "the test log has %ld beeps of 4", notes);
		CHECK(the_buffers[last_played].volume == HIT_SOUND_VOLUME, "the beep's volume is not HIT_SOUND_VOLUME");
		return 0;
	}
	/* none for a player's own damage, no damage, another machine's or a bot's player, no victim */
	CASE("not-ours")
	{
		CHECK(hit(LOCAL, LOCAL, 0.2f, 0.2f, 0) == 0, "a beep for the player's damage of themselves");
		CHECK(hit(LOCAL, REMOTE, 0.f, 0.f, 0) == 0, "a beep for no damage (a teammate friendly fire spares)");
		CHECK(hit(REMOTE, LOCAL, 0.2f, 0.f, 0) == 0, "a beep for another machine's player's hit");
		CHECK(hit(BOT, REMOTE, 0.2f, 0.f, 0) == 0, "a beep for a bot's hit");
		CHECK(hit(LOCAL, NONE, 0.2f, 0.f, 0) == 0, "a beep for a hit on no player (a vehicle, an actor)");
		CHECK(hit(NONE, REMOTE, 0.2f, 0.f, 0) == 0, "a beep for no player's damage");
		CHECK(hit(LOCAL, GONE, 0.2f, 0.f, 0) == 0, "a beep for a player who is not there");
		CHECK(hit(GONE, REMOTE, 0.2f, 0.f, 0) == 0, "a beep for a player who is not there");
		CHECK(hit(LOCAL, REMOTE, -0.2f, -0.1f, 0) == 0, "a beep for less than no damage");
		return 0;
	}
	/* the setting, read again as it changes */
	CASE("setting")
	{
		hit_sounds_setting = FALSE;
		CHECK(hit(LOCAL, REMOTE, 0.2f, 0.f, 0) == 0, "a beep with audio.hit_sounds off");
		hit_sounds_setting = TRUE;
		changes++;
		CHECK(hit(LOCAL, REMOTE, 0.2f, 0.f, 0) == 1, "no beep once audio.hit_sounds is on");
		/* (each split screen player's own) */
		hit_sounds_setting_2 = FALSE;
		changes++;
		CHECK(hit(LOCAL_2, REMOTE, 0.2f, 0.f, 0) == 0, "a beep for a player whose audio.hit_sounds is off");
		CHECK(hit(LOCAL, REMOTE, 0.2f, 0.f, 0) == 1, "no beep for a player whose audio.hit_sounds is on");
		CHECK(buffers_made == HIT_SOUND_BUFFERS, "the buffers were not made once (%d)", buffers_made);
		return 0;
	}
	/* hits closer together than the spacing beep once; each beep its own buffer */
	CASE("spacing")
	{
		int first;

		CHECK(hit(LOCAL, REMOTE, 0.2f, 0.f, 0) == 1, "no beep");
		first = last_played;
		hit_sounds_note_damage(LOCAL, REMOTE, 0.2f, 0.f, 0);
		now += HIT_SOUND_SPACING_MILLISECONDS - 1;
		hit_sounds_note_damage(LOCAL, BOT, 0.2f, 0.f, 0);
		CHECK(plays == 1, "hits within the spacing beeped %ld times", plays);
		now += 1;
		hit_sounds_note_damage(LOCAL, REMOTE, 0.2f, 0.f, 0);
		CHECK(plays == 2, "a hit past the spacing did not beep");
		CHECK(last_played != first, "a beep cut the last short, in its buffer");
		return 0;
	}
	/* an OpenH1 host that forbids them: none for its players, the record says so, read in the lobby */
	CASE("host")
	{
		connection = _game_connection_network_server;
		network_standard_globals.hosting = TRUE;
		network_standard_stamp_game(&record, TRUE);
		CHECK(hit(LOCAL, REMOTE, 0.2f, 0.f, 0) == 1, "no beep on a host that allows them");
		CHECK(!TEST_FLAG(record.standard_flags, _network_game_no_hit_sounds_bit), "the record forbids them");
		allow_setting = FALSE;
		changes++;
		network_standard_stamp_game(&record, FALSE);
		CHECK(hit(LOCAL, REMOTE, 0.2f, 0.f, 0) == 1, "the host's rule changed during the game");
		network_standard_stamp_game(&record, TRUE);
		CHECK(TEST_FLAG(record.standard_flags, _network_game_no_hit_sounds_bit), "the record does not forbid them");
		CHECK(hit(LOCAL, REMOTE, 0.2f, 0.f, 0) == 0, "a beep on a host that forbids them");
		return 0;
	}
	/* an OpenCE machine's copy of the record has no bit of OpenH1's; a Legacy host's record none */
	CASE("records")
	{
		struct network_game const *copy;

		allow_setting = FALSE;
		network_standard_globals.hosting = TRUE;
		network_standard_stamp_game(&record, TRUE);
		copy = network_standard_game_for_machine(&record, TRUE);
		CHECK(!TEST_FLAG(copy->standard_flags, _network_game_no_hit_sounds_bit),
			"an OpenCE machine's record forbids them");
		network_standard_globals.hosting = FALSE;
		network_standard_stamp_game(&record, TRUE);
		CHECK(!TEST_FLAG(record.standard_flags, _network_game_no_hit_sounds_bit), "a Legacy host's record forbids them");
		/* (a Legacy host forbids them for its own players) */
		connection = _game_connection_network_server;
		CHECK(hit(LOCAL, REMOTE, 0.2f, 0.f, 0) == 0, "a beep on a Legacy host that forbids them");
		return 0;
	}
	/* a client: its host's word, whatever its own; a Legacy host's, none to say */
	CASE("client")
	{
		connection = _game_connection_network_client;
		allow_setting = FALSE;
		network_standard_globals.joined = TRUE;
		CHECK(hit(LOCAL, REMOTE, 0.2f, 0.f, 0) == 1, "no beep on a client of a host that allows them");
		SET_FLAG(record.standard_flags, _network_game_no_hit_sounds_bit, TRUE);
		allow_setting = TRUE;
		CHECK(hit(LOCAL, REMOTE, 0.2f, 0.f, 0) == 0, "a beep on a client of a host that forbids them");
		network_standard_globals.joined = FALSE;
		CHECK(hit(LOCAL, REMOTE, 0.2f, 0.f, 0) == 1, "no beep in a Legacy game");
		return 0;
	}
	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
