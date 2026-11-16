/*
KEYBOARD_WALK.H

The keys' Walk (controls.walk, input.walk, input.walk_speed): how far the
movement keys push the player while it is held (keyboard_walk.c).
*/

#ifndef KEYBOARD_WALK_H
#define KEYBOARD_WALK_H

/* the throttle the movement keys give the local player while Walk is held
(0 to 1, a stick's length), on the 127 steps a network game's throttle is
sent in: input.walk_speed's percentage, or ("tracker") the fastest that
stays under the motion trackers' threshold (keyboard_walk_tracker_throttle);
1, no change, in a vehicle's seat, with no unit or no map */
real keyboard_walk_throttle(short local_player_index);

/* the throttle under which a biped whose fastest speed (in world units a
second) is fastest_speed stays off the motion trackers, whose threshold is
on the square of a velocity in world units a 30 Hz tick (motion_sensor.c's
should_draw_object): a little below the threshold's velocity (the margin),
rounded down to the 127 steps */
real keyboard_walk_tracker_throttle(real velocity_sensitivity, real fastest_speed);

#endif
