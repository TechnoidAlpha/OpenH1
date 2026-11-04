# Menu pictures' poses

The camera's place for each menu picture the game draws again from the player's own maps (`port/linux/README.md`,
"Menu pictures"): numbers only, never a picture. `tools/embed_assets.py` embeds every `.json` file here; a pose is
added by dropping its file in and building.

A file is named for ui.map's frame it stands for: `sp_levels_<frame>.json` (a mission's picture) or
`mp_map_grafix_<frame>.json` (a multiplayer map's). `tools/preview_rerender` finds a pose by matching the original
picture, and `slim_pose.py` writes the file:

| Key | Contents |
| --- | --- |
| `scenario` | The map (`levels\a10\a10`); its file (`a10`) must be in `maps.json` |
| `bsp` | The structure the camera is in (always switched to: a map may start in another) |
| `aspect` | The picture's width over its height before it was squeezed to 140x114 |
| `pose` | The camera: `x`, `y`, `z` (world units), `yaw`, `pitch`, `roll`, `vfov` (degrees; the vertical field of view, under 90 degrees as the game's across at 4:3) |
| `objects` | Optional: objects placed (`debug_object_place`): `tag`, `x`, `y`, `z`, `yaw`, `pitch`, `roll`, or `tag` and `seat` (in the last one placed), either with `color` |
| `prepare` | Optional: console lines run before the objects (`object_destroy_containing pelican`) |
| `settle_seconds` | Optional: how long the world runs for the objects to settle (a vehicle comes to rest, a rider takes its seat): 3 by default; 0 leaves them as placed |
| `colour` | Optional: `"curve"` takes the picture's colours toward the original's (`tools/preview_rerender/final.py`'s curve), where that looks closer; `"raw"` (the default) keeps them as rendered |
| `picture`, `score` | Notes: what it is, and how close the search came (0 to 1) |

`maps.json` has the Xbox maps' cache file header checksums: a map with any other (a modded map) keeps its original
picture.
