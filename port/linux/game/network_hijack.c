/*
NETWORK_HIJACK.C

The host's word on vehicle hijacks (vehicle_hijack.c) to its OpenH1 clients
(_distributed_message_vehicle_hijack, 144, reliably; OpenH1 protocol
version 31):

- a hijack begun on the host, as its hold ended: the jacker's unit, the
  rider's, the vehicle and the seat. Each client plays it from then for its
  own units, which are its own to move: its player's leap and the seat
  they take, its player thrown out; for the others it plays the blow. The
  seats and the units' places reach it from the host as every seat's does
  (network_objects_set_seat).
- a hijack called off on the host (the vehicle flipped or died, a unit died
  or left), which the client lets go of.
- the rule changed during a game (Host Controls' VEHICLE HIJACKING:
  network_standard_set_vehicle_hijack), in place of the record's word
  (_network_game_vehicle_hijack_bit), which a machine that joins later
  takes.

A Legacy game's host sends none (it plays no hijack), nor does any host to
an OpenCE machine (network_standard_machine_is_openh1); a client takes them
only from its host's stream, in an OpenH1 game under way.
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "main/main.h"
#include "networking/network_game_globals.h"
#include "network_distributed.h"
#include "network_standard.h"
#include "network_hijack.h"
#include "vehicle_hijack.h"

/* ---------- constants */

enum
{
	/* a machine takes its own format alone */
	VEHICLE_HIJACK_FORMAT = 1,
	MAXIMUM_HIJACK_MACHINES = HALO_PORT_MAXIMUM_NETWORK_MACHINES,
};

/* what the message says */
enum
{
	_vehicle_hijack_message_start = 0,
	_vehicle_hijack_message_cancel,
	_vehicle_hijack_message_rule,
	NUMBER_OF_VEHICLE_HIJACK_MESSAGES,
};

/* ---------- structures */

/* on the wire, the host's to each OpenH1 client, reliably: what happened
(_vehicle_hijack_message_*), the rule (a rule's), the seat and the units
(a start's; a cancel's jacker), by their datum indices, which every
machine shares */
struct distributed_vehicle_hijack
{
	byte format;
	byte kind;
	byte rule;
	byte pad;
	short seat_index;
	short pad2;
	long jacker_unit_index;
	long rider_unit_index;
	long vehicle_index;
};

typedef char distributed_vehicle_hijack_size_assert[sizeof(struct distributed_vehicle_hijack) == 20 ? 1 : -1];

/* ---------- private code */

/* whether this machine hosts an OpenH1 game under way */
static boolean network_hijack_hosting(
	void)
{
	return game_connection() == _game_connection_network_server && network_standard_hosting() &&
		game_in_progress();
}

static void network_hijack_send(
	struct distributed_vehicle_hijack const *hijack)
{
	struct
	{
		struct distributed_message_header header;
		struct distributed_vehicle_hijack hijack;
	} message;
	long machines[MAXIMUM_HIJACK_MACHINES];
	short count;
	short index;

	if (!network_hijack_hosting())
		return;
	count = distributed_client_machines(machines, MAXIMUM_HIJACK_MACHINES);
	csmemset(&message, 0, sizeof(message));
	message.hijack = *hijack;
	message.hijack.format = VEHICLE_HIJACK_FORMAT;
	for (index = 0; index < count; index++)
	{
		if (network_standard_machine_is_openh1(machines[index]))
		{
			distributed_send_to_machine_reliably(machines[index], &message, _distributed_message_vehicle_hijack, 1,
				(word)sizeof(message));
		}
	}
}

/* ---------- public code */

void network_hijack_send_start(
	long jacker_unit_index,
	long rider_unit_index,
	long vehicle_index,
	short seat_index)
{
	struct distributed_vehicle_hijack hijack;

	csmemset(&hijack, 0, sizeof(hijack));
	hijack.kind = _vehicle_hijack_message_start;
	hijack.rule = TRUE;
	hijack.seat_index = seat_index;
	hijack.jacker_unit_index = jacker_unit_index;
	hijack.rider_unit_index = rider_unit_index;
	hijack.vehicle_index = vehicle_index;
	network_hijack_send(&hijack);
}

void network_hijack_send_cancel(
	long jacker_unit_index)
{
	struct distributed_vehicle_hijack hijack;

	csmemset(&hijack, 0, sizeof(hijack));
	hijack.kind = _vehicle_hijack_message_cancel;
	hijack.seat_index = NONE;
	hijack.jacker_unit_index = jacker_unit_index;
	hijack.rider_unit_index = NONE;
	hijack.vehicle_index = NONE;
	network_hijack_send(&hijack);
}

void network_hijack_send_rule(
	boolean on)
{
	struct distributed_vehicle_hijack hijack;

	csmemset(&hijack, 0, sizeof(hijack));
	hijack.kind = _vehicle_hijack_message_rule;
	hijack.rule = on ? TRUE : FALSE;
	hijack.seat_index = NONE;
	hijack.jacker_unit_index = NONE;
	hijack.rider_unit_index = NONE;
	hijack.vehicle_index = NONE;
	network_hijack_send(&hijack);
}

boolean network_hijack_handles_message(
	word const *message,
	word size)
{
	struct distributed_message_header header;

	if (size < sizeof(header))
		return FALSE;
	csmemcpy(&header, message, sizeof(header));
	return header.type == _distributed_message_vehicle_hijack;
}

/* (the host's alone, over its stream, whole, in an OpenH1 game under way) */
void network_hijack_handle_message(
	long machine_index,
	word const *message,
	word size,
	boolean from_stream)
{
	struct distributed_message_header header;
	struct distributed_vehicle_hijack hijack;

	if (machine_index != NONE || !from_stream || game_connection() != _game_connection_network_client ||
		!network_standard_agreed_rate() || !game_in_progress() ||
		size != sizeof(header) + sizeof(hijack))
	{
		return;
	}
	csmemcpy(&header, message, sizeof(header));
	csmemcpy(&hijack, (byte const *)message + sizeof(header), sizeof(hijack));
	if (header.count != 1 || hijack.format != VEHICLE_HIJACK_FORMAT || hijack.kind >= NUMBER_OF_VEHICLE_HIJACK_MESSAGES)
		return;
	switch (hijack.kind)
	{
	case _vehicle_hijack_message_start:
		if (distributed_object_index_valid(hijack.jacker_unit_index) &&
			distributed_object_index_valid(hijack.rider_unit_index) &&
			distributed_object_index_valid(hijack.vehicle_index))
		{
			vehicle_hijack_heard(hijack.jacker_unit_index, hijack.rider_unit_index, hijack.vehicle_index,
				hijack.seat_index);
		}
		break;
	case _vehicle_hijack_message_cancel:
		if (distributed_object_index_valid(hijack.jacker_unit_index))
			vehicle_hijack_heard_cancel(hijack.jacker_unit_index);
		break;
	case _vehicle_hijack_message_rule:
		network_standard_vehicle_hijack_heard(hijack.rule != 0);
		error(_error_log, "hijack: the host turned vehicle hijacking %s", hijack.rule ? "on" : "off");
		break;
	}
}
