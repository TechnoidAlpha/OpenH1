# The high-res HUD's art: Aerocatia's and Jessie's

## What is theirs

The PNGs in `art/` are Aerocatia's and Jessie's high-resolution HUD art from
[Halo PC Restored](https://github.com/Aerocatia/halopc-restored), release
r220 (its tag, commit 92dd5af2, of 27 January 2026, the latest release):
the HUD bitmaps of its tags (`tags/ui/hud/bitmaps`, the sniper rifle's
scope ladder in `tags/weapons/sniper rifle/bitmaps`, and the meters in the
Xbox channel order in `extra/xbox_order_hud_meters`), decoded as they are,
unchanged. Halo PC Restored's README credits its HUD's authors as Aerocatia
and Holy Crust (Jesse); OpenH1 credits them, as ChupathingyCE does, as
Aerocatia and Jessie.

The HUD textures in this folder that `tools/hud_assets.py` cuts from them
(`layout.json`: each cell with an `art` source) are their art, cut and
placed, at its own scale (4x the Xbox bitmaps), where the Xbox maps' HUD
sheets have each sprite: the meters, panels and outlines, the ammunition's
counters and icons, the reticles, the scopes and the sniper's scope ladder,
the waypoints, the damage arrows, the multiplayer icons, the message icons
(their pictures of the weapons and the skull, their buttons, sticks and
words), the reticle's warnings, and the vehicles' and passengers' seat
plates. The classic style's controller buttons
(`port/assets/buttons/classic_*`, `tools/button_assets.py`) are their
stencils of the A, B, X and Y buttons (from `hud_msg_icons_sm`) in each
button's colour.

## The permission

Their art is not under OpenH1's licence. It is used with Aerocatia's
permission, as ChupathingyCE's NOTICE (`port/assets/hud/NOTICE.md` in
[ChupathingyCE/chupathingyce](https://github.com/ChupathingyCE/chupathingyce))
records it for the same art:

> They are used with Aerocatia's permission, given on October 14, 2026
> ("it was released to be used"), on the condition that Aerocatia and
> Jessie are credited. Anyone reusing them should follow the same terms,
> or ask Aerocatia.

Aerocatia has confirmed to OpenH1's maintainers that OpenH1's use of the
HUD art is fine. The condition is met by this file and `AUTHORS.md`.

ChupathingyCE first fitted this art to the Xbox maps' sheets;
`tools/hud_assets.py` follows its way of cutting and placing it (its
`tools/hud_assets.py`, GPL-3.0-or-later).

## What is not theirs

Where Halo PC Restored has nothing larger than the Xbox bitmap, the
textures are drawn from the SVG redraws in `svg/` (`AUTHORS.md`): the health
meter's base (`hud_health_base__0.png`), the motion sensor's blips and
sweeper (`hud_sensor_blip__0.png`, `hud_sensor_blip_custom__0.png`,
`hud_sweeper__0.png`, `hud_sweeper_mask__0.png`), and the outlines the
upscaled seat plates of other languages' maps get
(`vehicle_wrench_outline__0.png`, `passenger_health_outline__0.png`). The
sniper's zoom labels (2x and 10x, in `sniper__hud_reticles_scope__0.png`)
are set in Cyborg Interface Gothic (`port/assets/fonts`), as the restored
ones say 2x and 8x (Halo PC's).
