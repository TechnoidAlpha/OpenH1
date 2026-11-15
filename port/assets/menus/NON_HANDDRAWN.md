# Menu pictures that are not redraws

The menus' bitmaps are drawn from the high-res redraws (`svg/`, from ui-svg-handmade) where there is
one. The PC version's pictures that have none (3D renders, screenshots and logos) are not in this
repository.

## From the Xbox's map

These have the same pictures, in the same order, in the Xbox's `ui.map`, which the menus draw them
from (the player's own; nothing is shipped). `mp_map_grafix`: the Xbox's has the 13 Xbox maps' pictures
(the PC version's first 13) and its "?"; the PC version's other 8 are its own maps'.

- `ui\shell\bitmaps\colors_sm`
- `ui\shell\bitmaps\mp_map_grafix`
- `ui\shell\bitmaps\sp_levels`
- `ui\shell\main_menu\difficulty_select\difficulty_options`
- `ui\shell\main_menu\difficulty_select\difficulty_options_small`
- `ui\shell\main_menu\halo_logo`
- `ui\shell\main_menu\settings_select\player_setup\player_profile_edit\color_edit\player_color_marine_large`

These frames are drawn from a frame of the Xbox map's bitmap of the same name, whose other frames differ:
the same picture, or for the profile settings the Xbox's picture for the same thing.

| The PC version's frame | The Xbox's frame |
| --- | --- |
| `shell/main_menu/multiplayer_type_select/mp_options__2.png` | `ui\shell\main_menu\multiplayer_type_select\mp_options` frame 3 |
| `shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__2.png` | `ui\shell\main_menu\settings_select\multiplayer_setup\playlist_edit\gametype_options` frame 2 |
| `shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__5.png` | `ui\shell\main_menu\settings_select\multiplayer_setup\playlist_edit\gametype_options` frame 4 |
| `shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__0.png` | `ui\shell\main_menu\settings_select\player_setup\player_profile_edit\profile_options` frame 0 |
| `shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__2.png` | `ui\shell\main_menu\settings_select\player_setup\player_profile_edit\profile_options` frame 1 |
| `shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__7.png` | `ui\shell\main_menu\settings_select\player_setup\player_profile_edit\profile_options` frame 3 |

These frames are added after a bitmap's, drawn scaled from the Xbox map's (the profile settings'
picture of Gamepad Setup: the Xbox's Controller Setup's pictures of the button settings).

| Our bitmap | The Xbox's frame |
| --- | --- |
| `main_menu/settings_select/player_setup/player_profile_edit/profile_options` | `ui\shell\main_menu\settings_select\player_setup\player_profile_edit\controller_edit\config_controller` frame 4 |
| `main_menu/settings_select/player_setup/player_profile_edit/profile_options` | `ui\shell\main_menu\settings_select\player_setup\player_profile_edit\controller_edit\config_controller` frame 5 |
| `main_menu/settings_select/player_setup/player_profile_edit/profile_options` | `ui\shell\main_menu\settings_select\player_setup\player_profile_edit\controller_edit\config_controller` frame 6 |
| `main_menu/settings_select/player_setup/player_profile_edit/profile_options` | `ui\shell\main_menu\settings_select\player_setup\player_profile_edit\controller_edit\config_controller` frame 7 |
| `main_menu/settings_select/player_setup/player_profile_edit/profile_options` | `ui\shell\main_menu\settings_select\player_setup\player_profile_edit\controller_edit\config_controller` frame 8 |

## Screenshots

Settings' Quality row shows these two small screenshots of the game, the same view drawn by each preset
(`port/assets/menus/stills`). They are this port's own drawing of the game: a local
Slayer game on Blood Gulch (`debug.network_test = "local:bloodgulch:slayer"`, `debug.random_seed = 7`,
four bots), the same frame of each run captured offscreen by the macOS build at 1600x1200
(`debug.screenshot_every`), cropped to 16:10, scaled to 288x180 (the menus' copies 264x165, three times their
size in the menu) and
reduced to 96 colors for size. The pictures of other rows' values are made the same way at the frame 12 s
into the game with the setting set, 320x200 (twice their size in the menu), reduced to 64 colors: the whole
view, or a part of it at 1:1 or larger. The menus ship them with their corners cut round, and the others'
framed (`ce/port/stills`, tools/ce_menus.py's `rounded_still`).

| File | Size | What it shows |
| --- | --- | --- |
| `stills/graphics_classic.png` | 88x55 | Quality CLASSIC (`display.graphics = "classic"`) |
| `stills/graphics_enhanced.png` | 88x55 | Quality ENHANCED (`display.graphics = "enhanced"`) |
| `stills/fov_default.png` | 160x100 | Field of view DEFAULT (`display.fov = 0`); HDR mode FAITHFUL |
| `stills/fov_100.png` | 160x100 | Field of view 100, for 95 to 115 |
| `stills/fov_130.png` | 160x100 | Field of view 130, for 120 to 160 |
| `stills/aa_off.png` | 160x100 | Anti-aliasing OFF, close on an edge (each pixel twice its size) |
| `stills/aa_smaa.png` | 160x100 | Anti-aliasing SMAA (and FXAA), the same; render resolution FULL |
| `stills/aa_msaa.png` | 160x100 | Anti-aliasing MSAA 4X (and MSAA's others, and render resolution above 100%), the same |
| `stills/render_xbox.png` | 160x100 | Render resolution XBOX 640X480, the same edge, its pixels as drawn |
| `stills/hud_high_res.png` | 160x100 | High-res HUD ON: the health and shields meters, close |
| `stills/hud_game.png` | 160x100 | High-res HUD OFF, the same |
| `stills/hdr_punchy.png` | 160x100 | HDR mode PUNCHY, approximately: the DEFAULT scene lifted in SDR, as no SDR picture can show HDR |
| `stills/buttons_classic.png` | 160x100 | Button style CLASSIC: A, B, X and Y, the HUD's own pictures (port/assets/buttons), not a screenshot |
| `stills/buttons_gems.png` | 160x100 | Button style GEMS, the same |
| `stills/buttons_dark_gems.png` | 160x100 | Button style DARK GEMS, the same |

## Not redrawn yet: drawn empty

The Xbox's map has none of these (or other pictures under the name), so each frame is drawn empty
(no picture shows there) until it is redrawn.

| File | Size | The PC version's picture |
| --- | --- | --- |
| `ce/shell/main_menu/multiplayer_type_select/mp_options__0.png` | 512x256 | `shell/main_menu/multiplayer_type_select/mp_options__0.png` |
| `ce/shell/main_menu/multiplayer_type_select/mp_options__1.png` | 512x256 | `shell/main_menu/multiplayer_type_select/mp_options__1.png` |
| `ce/shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__0.png` | 512x256 | `shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__0.png` |
| `ce/shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__3.png` | 512x256 | `shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__3.png` |
| `ce/shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__4.png` | 512x256 | `shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__4.png` |
| `ce/shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__6.png` | 512x256 | `shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__6.png` |
| `ce/shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__7.png` | 512x256 | `shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__7.png` |
| `ce/shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__1.png` | 512x256 | `shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__1.png` |
| `ce/shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__3.png` | 512x256 | `shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__3.png` |
| `ce/shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__4.png` | 512x256 | `shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__4.png` |
| `ce/shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__5.png` | 512x256 | `shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__5.png` |
| `ce/shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__6.png` | 512x256 | `shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__6.png` |
| `ce/shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__8.png` | 512x256 | `shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__8.png` |

13 of 542 frames are drawn empty.

The PC version's `ui\gamespy` and `ui\ticker` fonts are drawn with `ui\small_ui`, which
the Xbox's map has.
