#include "harness.h"

typedef unsigned short word;
#define SET_FLAG(flags, bit, value) ((value) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define csmemset memset
#define TICKS_PER_SECOND 30
#define REPORT_MAXIMUM_AGE_TICKS (3 * TICKS_PER_SECOND)
#define PREDICTED_HIT_SLACK_TICKS (TICKS_PER_SECOND / 2)
#define MAXIMUM_WEAPONS_PER_UNIT 4
#define NUMBER_OF_UNIT_GRENADE_TYPES 2
#define NO_PLAYER 0xFF
#define DAMAGE_EFFECT_DEFINITION_TAG 0x6A706474
struct projectile_material_response_definition;
#include "config.inc"

/* the fake world: objects 1 (the target, player 0's) and 2 (the shooter,
this machine's player 1's) */
struct object_fields
{
	long parent_object_index; short type; word damage_flags;
	real shield_vitality, body_vitality, current_shield_damage, recent_shield_damage, current_body_damage, recent_body_damage;
};
struct unit_fields
{
	long player_index, weapon_object_indices[MAXIMUM_WEAPONS_PER_UNIT]; short current_weapon_index, flaming_death_delay;
	char grenade_counts[NUMBER_OF_UNIT_GRENADE_TYPES];
};
struct object_datum { long definition_index; struct object_fields object; struct unit_fields unit; };
struct unit_datum { long definition_index; struct object_fields object; struct unit_fields unit; };
struct player_datum { long unit_index; boolean quit_out_of_game, local; };
struct damage_effect_definition { struct { short category; } damage; };
struct object_definition { struct { struct { long index; } collision_model; } object; };
struct collision_model { struct { real body_destroyed_threshold; } resistance; };

static struct object_datum objects[4];
static struct player_datum players[2];
static struct damage_effect_definition damage_effect;
static struct object_definition object_definition;
static struct collision_model collision_model;
static long now, rate = 60, round_trip = 3;
static boolean coop, setting = TRUE, host_says_dead;
static byte host_killer = NO_PLAYER;
/* what object_cause_damage does to the target, and what the code under test did */
static real hit_shield, hit_body;
static int dealt, killed_announced, killed_victim, revived, attached, restored, deaths_set;
static long announced_score;
static boolean announced_score_said;

static unsigned long config_changes(void) { return 1; }
static int config_boolean(char const *name) { return setting; }
static long network_standard_agreed_rate(void) { return rate; }
static boolean network_coop_active(void) { return coop; }
static boolean distributed_player_is_local(long index) { return index >= 0 && index < 2 && players[index].local; }
static boolean tag_index_is_group(long index, unsigned long group) { return index == 7; }
static struct damage_effect_definition *damage_effect_definition_get(long index) { return &damage_effect; }
static void *object_try_and_get(long index) { return index >= 1 && index < 4 && objects[index].object.type >= 0 ? &objects[index] : NULL; }
static void *object_try_and_get_and_verify_type(long index, unsigned long mask)
{
	struct object_datum *object = object_try_and_get(index);
	return object && TEST_FLAG(mask, object->object.type) ? object : NULL;
}
static struct object_datum *object_get(long index) { CHECK(object_try_and_get(index), "bad object %ld", index); return &objects[index]; }
static struct unit_datum *unit_get(long index) { return (struct unit_datum *)object_get(index); }
static struct player_datum *player_try_and_get(long index) { return index >= 0 && index < 2 ? &players[index] : NULL; }
static long distributed_living_unit(struct player_datum *player) { return player->unit_index; }
static struct object_definition *object_definition_get(long index) { return &object_definition; }
static struct collision_model *collision_model_definition_get(long index) { return &collision_model; }
static long game_time_get(void) { return now; }
static real distributed_own_round_trip_ticks(void) { return (real)round_trip; }
static void network_test_note_hit(char const *what, long index) { }
static long distributed_player_from_byte(byte index) { return index == NO_PLAYER ? NONE : (long)index; }
static boolean distributed_get_death(short index, byte *killer, boolean *friendly_fire, boolean *by_vehicle)
{
	*killer = host_killer; *friendly_fire = FALSE; *by_vehicle = FALSE; return host_says_dead;
}
static void distributed_set_death(short index, byte killer, boolean friendly_fire, boolean by_vehicle)
{
	deaths_set++; host_killer = killer; host_says_dead = TRUE;
}
static void object_cause_damage(struct damage_data *damage, long index, short node, short region, short material,
	real_vector3d const *normal);
static void unit_port_revive(long index) { revived++; SET_FLAG(objects[index].object.damage_flags, _object_dead_bit, FALSE); }
static void network_objects_client_restore_weapons(long index, long const *weapons, short current)
{
	restored++; CHECK(weapons[0] == 3 && current == 0, "the weapons it held");
}
static void network_player_attach_unit(long player, long unit) { attached++; players[player].unit_index = unit; }
static boolean damage_replaying_kill;
static long damage_replaying_killer, damage_replaying_killer_score;
static void game_engine_player_killed(long killer, long object, long victim, boolean friendly_fire)
{
	killed_announced++; killed_victim = (int)victim;
	CHECK(damage_replaying_kill, "announced as the host's blow");
	announced_score_said = damage_replaying_killer != NONE; announced_score = damage_replaying_killer_score;
}
#include "under_test.inc"

/* the game's dealing, as far as the code under test sees it: through
network_damage_deals once (distributed_predicted_dealing), the target's
vitality down, dead at none left */
static void object_cause_damage(struct damage_data *damage, long index, short node, short region, short material,
	real_vector3d const *normal)
{
	struct object_datum *object = &objects[index];

	CHECK(network_damage_predicting(), "dealt as a hit shown");
	CHECK(distributed_predicted_dealing(index), "the hit dealt to the target");
	CHECK(!distributed_predicted_dealing(index), "dealt once");
	dealt++;
	object->object.shield_vitality = MAX(0.0f, object->object.shield_vitality - hit_shield);
	if (object->object.shield_vitality == 0.0f)
		SET_FLAG(object->object.damage_flags, _object_shield_depleted_bit, TRUE);
	object->object.current_shield_damage = 1.0f;
	object->object.body_vitality -= hit_body;
	if (object->object.body_vitality <= 0.0f)
	{
		SET_FLAG(object->object.damage_flags, _object_dead_bit, TRUE);
		players[0].unit_index = NONE;
		object->unit.weapon_object_indices[0] = NONE;
	}
}

static void setup(void)
{
	int index;

	memset(objects, 0, sizeof(objects));
	for (index = 0; index < 4; index++)
		objects[index].object.type = index == 3 ? _object_type_weapon : _object_type_biped;
	objects[1].object.shield_vitality = 1.0f; objects[1].object.body_vitality = 1.0f;
	objects[1].object.parent_object_index = NONE; objects[1].unit.player_index = 0;
	objects[1].unit.weapon_object_indices[0] = 3;
	for (index = 1; index < MAXIMUM_WEAPONS_PER_UNIT; index++)
		objects[1].unit.weapon_object_indices[index] = NONE;
	objects[2].unit.player_index = 1; objects[2].object.parent_object_index = NONE;
	players[0].unit_index = 1; players[0].local = FALSE;
	players[1].unit_index = 2; players[1].local = TRUE;
	damage_effect.damage.category = _damage_category_flame + 1;
	collision_model.resistance.body_destroyed_threshold = 0.0f;
	distributed_predicted_hits_reset();
}

static struct damage_data shot(void)
{
	struct damage_data damage;

	memset(&damage, 0, sizeof(damage));
	damage.definition_index = 7;
	damage.owner_player_index = 1;
	damage.owner_object_index = 2;
	return damage;
}

static boolean predict(real shield, real body)
{
	struct damage_data damage = shot();

	hit_shield = shield; hit_body = body;
	return distributed_predict_hit(&damage, 1, NONE, NONE, NONE, NULL);
}

/* the host's state of the target, as damage_set_network_state takes it */
static struct damage_network_state taken(real shield, real body)
{
	struct damage_network_state state, result;

	memset(&state, 0, sizeof(state));
	state.shield_vitality = shield; state.body_vitality = body;
	return network_damage_predicted_state(1, &state, &result) ? result : state;
}

static struct distributed_damage_event blow(byte killer, long score)
{
	struct distributed_damage_event event;

	memset(&event, 0, sizeof(event));
	event.kind = _damage_event_kill; event.object_index = 1; event.player_index = killer; event.killer_score = score;
	return event;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	long shown, answered, unanswered, kills, confirmed, refused;

	setup();
	CASE("hit-state")
	{
		struct damage_data damage = shot();
		struct damage_network_state state;

		CHECK(predict(0.6f, 0.0f) && dealt == 1, "a hit on another player shown");
		state = taken(1.0f, 1.0f);
		CHECK(state.shield_vitality > 0.39f && state.shield_vitality < 0.41f, "the host's stale state less the hit: %f",
			(double)state.shield_vitality);
		CHECK(state.current_shield_damage == 1.0f, "the flare kept");
		CHECK(distributed_predicted_hit_answered(1, &damage), "the host's damage of it answers it (not replayed)");
		CHECK(!distributed_predicted_hit_answered(1, &damage), "once");
		state = taken(0.4f, 1.0f);
		CHECK(state.shield_vitality > 0.39f && state.shield_vitality < 0.41f, "a state after it is the host's: %f",
			(double)state.shield_vitality);
		CHECK(taken(1.0f, 1.0f).shield_vitality == 1.0f, "nothing kept once taken");
	}
	CASE("hit-unanswered")
	{
		CHECK(predict(0.6f, 0.0f), "shown");
		now = round_trip + PREDICTED_HIT_SLACK_TICKS;
		distributed_predicted_hits_update();
		CHECK(taken(1.0f, 1.0f).shield_vitality < 1.0f, "kept for the round trip and a while");
		now++;
		distributed_predicted_hits_update();
		CHECK(taken(1.0f, 1.0f).shield_vitality == 1.0f, "then the host's");
		network_damage_prediction_statistics(&shown, &answered, &unanswered, &kills, &confirmed, &refused);
		CHECK(shown == 1 && answered == 0 && unanswered == 1, "counted unanswered");
	}
	CASE("kill-confirmed")
	{
		struct distributed_damage_event event = blow(1, 5);

		CHECK(predict(1.0f, 2.0f) && TEST_FLAG(objects[1].object.damage_flags, _object_dead_bit), "the death shown");
		CHECK(!killed_announced, "not announced before the host's word");
		now = 2;
		CHECK(distributed_predicted_kill_answered(&event), "the host's blow confirms it");
		CHECK(deaths_set == 1 && killed_announced == 1 && killed_victim == 0, "announced, of the victim");
		CHECK(announced_score_said && announced_score == 5, "with the blow's score");
		CHECK(!distributed_predicted_kill_answered(&event), "once (the blow sent again)");
		now = 100;
		distributed_predicted_hits_update();
		CHECK(!revived && killed_announced == 1, "nothing undone");
		network_damage_prediction_statistics(&shown, &answered, &unanswered, &kills, &confirmed, &refused);
		CHECK(kills == 1 && confirmed == 1 && refused == 0, "counted confirmed");
	}
	CASE("kill-host-state")
	{
		CHECK(predict(1.0f, 2.0f), "the death shown");
		host_says_dead = TRUE; host_killer = 1;
		now = 100;
		distributed_predicted_hits_update();
		CHECK(killed_announced == 1 && !revived && !announced_score_said, "its blow lost, the host's states have it dead");
	}
	CASE("kill-refused")
	{
		CHECK(predict(1.0f, 2.0f), "the death shown");
		now = round_trip + PREDICTED_HIT_SLACK_TICKS;
		distributed_predicted_hits_update();
		CHECK(!revived, "waited for");
		now++;
		distributed_predicted_hits_update();
		CHECK(revived == 1 && attached == 1 && restored == 1 && !killed_announced, "the unit stands again, its player's");
		CHECK(players[0].unit_index == 1, "the player's unit again");
		CHECK(objects[1].object.body_vitality == 1.0f && objects[1].object.shield_vitality == 1.0f &&
			!TEST_FLAG(objects[1].object.damage_flags, _object_shield_depleted_bit), "as it was");
		network_damage_prediction_statistics(&shown, &answered, &unanswered, &kills, &confirmed, &refused);
		CHECK(kills == 1 && confirmed == 0 && refused == 1, "counted refused");
	}
	CASE("killed-otherwise")
	{
		struct distributed_damage_event event = blow(0, 1);

		CHECK(predict(0.5f, 0.0f), "a hit shown");
		CHECK(!distributed_predicted_kill_answered(&event), "the host's blow of a living unit is replayed");
		CHECK(taken(1.0f, 1.0f).shield_vitality == 1.0f, "its hits forgotten");
	}
	CASE("not-shown")
	{
		rate = 0;
		CHECK(!predict(0.5f, 0.0f), "a Legacy game's");
		rate = 60; coop = TRUE;
		CHECK(!predict(0.5f, 0.0f), "co-op's");
		coop = FALSE; setting = FALSE;
		CHECK(!predict(0.5f, 0.0f), "network.predict_hits off");
		setting = TRUE; players[0].local = TRUE;
		CHECK(!predict(0.5f, 0.0f), "this machine's own player");
		players[0].local = FALSE; objects[1].object.parent_object_index = 2;
		CHECK(!predict(0.5f, 0.0f), "a rider");
		objects[1].object.parent_object_index = NONE; damage_effect.damage.category = _damage_category_flame;
		CHECK(!predict(0.5f, 0.0f), "fire");
		damage_effect.damage.category = 0; collision_model.resistance.body_destroyed_threshold = -0.5f;
		CHECK(!predict(0.5f, 0.0f), "a body that comes apart");
		CHECK(!dealt, "nothing dealt");
	}
	return 0;
}
