/*
CAMERA_POSE.H

The camera put at a place and facing from the console (camera_pose.c).
*/

#ifndef CAMERA_POSE_H
#define CAMERA_POSE_H

/* "<x> <y> <z> <yaw> <pitch> <roll> <field of view>": world units, and
degrees (the field of view the game's own, across at 4:3); the script's
camera is taken and put there at once. "off" gives the camera back */
void camera_pose_set(char const *pose);

/* an object of a tag the map has, put at "<tag> <x> <y> <z> <yaw> [<pitch>
<roll>]", or a unit made and put in a seat of the last one placed ("<tag>
seat <label>"); either with "color <r> <g> <b>" (0 to 1: its change
colours). "clear" deletes those placed */
void camera_pose_place_object(char const *line);

/* the scenario's places (players' starts, cutscene flags and cameras, the
AI's firing positions and starts) in the loaded structure, written to a
file: "<kind> <x> <y> <z> <yaw> <pitch> <roll> <field of view>" (degrees);
then every cutscene camera outside the structure ("camera_outside", its name
last), and the scenery, vehicles, bipeds and machines placed ("object", its
tag's name last), for seeds of a view where no one stands */
void camera_pose_write_places(char const *path);

#endif
