/*
NETWORK_HIJACK.H

The host's word on vehicle hijacks to its OpenH1 clients (network_hijack.c).
*/

#ifndef NETWORK_HIJACK_H
#define NETWORK_HIJACK_H

/* ---------- prototypes/NETWORK_HIJACK.C */

/* the host (of an OpenH1 game): a hijack begun, called off, and the rule
changed during a game (Host Controls' VEHICLE HIJACKING), each to every
OpenH1 client, reliably; nothing from a client or a game of this machine's
own */
void network_hijack_send_start(long jacker_unit_index, long rider_unit_index, long vehicle_index, short seat_index);
void network_hijack_send_cancel(long jacker_unit_index);
void network_hijack_send_rule(boolean on);
/* a client: the host's message (network_distributed.c) */
boolean network_hijack_handles_message(word const *message, word size);
void network_hijack_handle_message(long machine_index, word const *message, word size, boolean from_stream);

#endif
