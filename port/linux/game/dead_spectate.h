/*
DEAD_SPECTATE.H

A dead player watching a living teammate until they respawn
(dead_spectate.c).
*/

#ifndef __DEAD_SPECTATE_H
#define __DEAD_SPECTATE_H

/* the unit a dead local player watches, chosen and cycled (a press of A, or
the keyboard's jump key, goes to the next teammate): in network co-op
always, and with game.spectate_on_death "teammates" in local co-op and in
team games too; NONE while the player is alive, in a game with no
teammates to watch (free for all), or with nobody alive. Called once a
frame for each local player (director.c), which reads its press */
long dead_spectate_unit(
	short local_player_index);

/* the player a dead local player last watched, or NONE */
long dead_spectate_watched_player(
	short local_player_index);

/* whether a dead local player watching unit_index sees through its eyes
(game.spectate_on_death "teammates", the unit not in a seat whose camera
follows the vehicle, as its own player's would) rather than from behind */
boolean dead_spectate_through_eyes(
	long unit_index);

/* director.c's camera for a dead player looking out of a teammate's eyes */
void dead_spectate_camera_new(
	void *camera,
	short local_player_index,
	long unit_index);
/* the unit the camera looks out of */
long dead_spectate_camera_unit(
	void const *camera);
void dead_spectate_camera_update(
	void *camera,
	void *controls,
	void *result);

/* the unit a local player's view looks out of while dead (its body is
hidden from that view and its first-person weapon drawn), or NONE */
long dead_spectate_eyes_unit(
	short local_player_index);

/* the unit whose first-person weapon a local player's slot shows: its own,
or while it watches through a teammate's eyes, the teammate's
(first_person_weapons.c) */
long dead_spectate_first_person_weapon_unit(
	short local_player_index,
	long unit_index);

/* the HUD's lines while a local player watches through a teammate's eyes:
who, and the button for the next */
void dead_spectate_draw(
	short local_player_index);

#endif
