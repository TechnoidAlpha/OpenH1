# Lucide icons

Seven icons of [Lucide](https://lucide.dev) 1.53.0 (ISC licence: `LICENSE`),
as `icons/` has them at the `1.53.0` tag of
https://github.com/lucide-icons/lucide:

- `volume-2.svg`: voice chat's speaker, of a player who talks;
- `volume-x.svg`: ... of a player muted;
- `triangle-alert.svg`: the Mods menu's sign of a mod that loads on the
  wrong side of a mod it asks to load before or after;
- `circle-x.svg`: ... of a mod that cannot be played, or that another mod
  says it cannot be played with;
- `star.svg`: the server browsers' favorite games;
- `search.svg`: the server browsers' searches of the SERVER and MAP columns;
- `x.svg`: ... the clearing of a search.

`tools/ce_menus.py` draws the first two, white, as the menus' bitmap
`voice/speaker` (port/linux/game/network_voice.c tints it), the next two,
amber and red, as `mods/status` and the smaller `mods/status_small`, and
the star, gold, grey and outlined, as `favorite_star`, and the search's
magnifying glass and X, blue, as `search_icon` and `search_clear`, and
larger for the touchscreen's browser, as `touch/search_icon` and
`touch/search_clear` (`tools/port_settings.py`). Credited in `AUTHORS.md`.
