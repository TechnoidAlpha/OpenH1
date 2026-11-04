# Fonts

The fonts the game's text is drawn with, at the display's resolution, in
place of the bitmap fonts of the maps (`port/linux/src/text_hires.c`).
`fonts.json` says which font draws each of the maps' font tags.

| File | Draws | From |
| --- | --- | --- |
| `Overpass-900.ttf` | `ui\large_ui`, `ui\interstate` | Overpass, weight 900 (Black) |
| `Overpass-750.ttf` | `ui\small_ui` | Overpass, weight 750 |
| `Hack-Bold.ttf` | `old tags\internal system plain`, the console's (and the debug text's) | Hack 3.003, Bold, unmodified |
| `OpenCE-Regular.ttf` | the menus' titles, as pictures (`port/assets/titles`); the menus' heading font, `pc\heading` (the campaign's missions' names) | OpenCE: Newtown, respaced for this project (`tools/title_font.py`) |
| `Newtown-Regular.ttf` | (OpenCE's source) | Newtown by Roger White, unmodified |

The maps' fonts are Interstate (Tobias Frere-Jones, The Font Bureau), a
commercial typeface that cannot be shipped. Overpass (Red Hat, Delve
Withrington) follows the same Highway Gothic road-sign alphabet and is
under the SIL Open Font License 1.1 (`Overpass-OFL.txt`), without a
Reserved Font Name. Matched against the maps' glyphs, its weights 900 and 750 overlap
them as much as Interstate Bold does, at the same letter widths.

The menus' titles are pictures of text in the maps, set in another
commercial typeface. OpenCE (Open Community Edition) stands in for it:
Newtown, a typeface in the same style by Roger White (1994), which its author
released into the public domain (`Newtown-LICENSE.txt` quotes his statement
and where it is archived), respaced for this project. Only its spacing is
changed, its outlines are Newtown's: each letter's side spaces and the
kerning pairs are measured from where the letters sit in the maps' titles.
OpenCE is under the SIL Open Font License 1.1 (`OpenCE-OFL.txt`), without a
Reserved Font Name. `tools/title_assets.py` draws the titles with it, and
the builds embed those pictures; the builds embed it too, for the menus'
heading font (`pc\heading`, a copy of `ui\large_ui`'s tag the menus add:
`port/linux/game/menu_tags.c`), which the high-res text draws in it at the
display's resolution, so the release packages carry `OpenCE-OFL.txt`. To
measure its spacing again and rebuild it:

    python3 tools/title_font.py --measure    # paste the values into tools/title_font.py
    python3 tools/title_font.py
    python3 tools/title_assets.py --map assets/maps/ui.map

The Overpass files are static instances of Google Fonts' variable Overpass
(`ofl/overpass/Overpass[wght].ttf`, google/fonts commit
9710da1eacb3be272583c3224dcb70f9da6eadbb), made with fontTools:

    python3 -m fontTools.varLib.instancer 'Overpass[wght].ttf' wght=900 -o Overpass-900.ttf
    python3 -m fontTools.varLib.instancer 'Overpass[wght].ttf' wght=750 -o Overpass-750.ttf

The console's font in the maps, `old tags\internal system plain`, is a
small fixed-width bitmap font. Hack (Source Foundry,
https://github.com/source-foundry/Hack) is a monospaced typeface made for
code and terminals; its Bold matches the weight of the bitmap's strokes.
`Hack-Bold.ttf` is the file of the release v3.003
(`Hack-v3.003-ttf.tar.gz`, SHA-256
`7f592d091cbd2472f39af96e6c280cde3c27cab50a9c88746feb42918aa1888f`),
unchanged. Hack is under the MIT License, and the Bitstream Vera License
for the Bitstream Vera Sans Mono outlines it builds on (DejaVu's are in the
public domain); `Hack-LICENSE.txt` is the project's `LICENSE.md`.

The builds embed the Overpass and Hack files (`tools/embed_assets.py`),
and the release packages carry `Overpass-OFL.txt` and `Hack-LICENSE.txt`,
as the licenses ask.
