# Fonts

The fonts the game's text is drawn with, at the display's resolution, in
place of the bitmap fonts of the maps (`port/linux/src/text_hires.c`).
`fonts.json` says which font draws each of the maps' font tags.

| File | Draws | From |
| --- | --- | --- |
| `CyborgInterfaceGothic-Regular.ttf` | `ui\large_ui`, `ui\interstate`, `ui\small_ui` | Cyborg Interface Gothic by fizzy (below) |
| `Overpass-900.ttf` | what Cyborg Interface Gothic lacks, for `ui\large_ui` and `ui\interstate` | Overpass, weight 900 (Black) |
| `Overpass-750.ttf` | what Cyborg Interface Gothic lacks, for `ui\small_ui` | Overpass, weight 750 |
| `Hack-Bold.ttf` | `old tags\internal system plain`, the console's (and the debug text's) | Hack 3.003, Bold, unmodified |
| `OpenCE-Regular.ttf` | the menus' titles, as pictures (`port/assets/titles`); the menus' heading font, `pc\heading`; the loading screen's caption ("MENU PICTURES · 4 OF 23"), at the screen's pixels | OpenCE: Newtown, respaced for this project (`tools/title_font.py`) |
| `Newtown-Regular.ttf` | (OpenCE's source) | Newtown by Roger White, unmodified |
| `msdf_atlas.zlib` | the fonts above as distance fields, which the high-res text is drawn from | made from them by `tools/msdf_atlas.py` |

The maps' fonts are Interstate (Tobias Frere-Jones, The Font Bureau), a
commercial typeface that cannot be shipped.

Cyborg Interface Gothic was made by fizzy (._o. on ChupathingyCE's Discord
server) to match the look of Halo's interface font: many of its glyphs are
derived from Overpass, the others drawn by them. It is under the SIL Open
Font License 1.1, as its author confirmed (`CyborgInterfaceGothic-OFL.txt`,
with Overpass's copyright and theirs), without a Reserved Font Name
(Overpass has none). The file is ChupathingyCE's
(ChupathingyCE/chupathingyce#324, SHA-256
`508519e64cb6a5a07a13be29162cfd35eef5c15d96b40f676a8161ffd00de1f9`), its
names set there with fontTools as agreed with its author, its outlines
fizzy's. Matched against the Xbox maps' font tags there (each glyph's
coverage at the tag's capital height, as `text_hires.c` sizes it), it
overlaps `ui\large_ui` and `ui\interstate` better than Overpass 900 does
and `ui\small_ui` as well as Overpass 750 does.

It has 240 characters: ASCII and Latin-1 but the soft hyphen, and a few
more. A character it lacks (the soft hyphen, the low double quote, the euro
sign, letters past Latin-1) is drawn with Overpass, its fallback in
`fonts.json`, sized to the same tag the same way. Overpass (Red Hat, Delve
Withrington) follows the same Highway Gothic road-sign alphabet and is
under the SIL Open Font License 1.1 (`Overpass-OFL.txt`), without a
Reserved Font Name. Matched against the maps' glyphs, its weights 900 and 750 overlap
them as much as Interstate Bold does, at the same letter widths. The HUD's
pictures with words (`tools/hud_assets.py`) and the Android and iOS touch
controls' captions are set in it.

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

The builds embed the fonts `fonts.json` names and their fallbacks
(`tools/embed_assets.py`), and every release package, the dedicated
server's included, carries `CyborgInterfaceGothic-OFL.txt`,
`Overpass-OFL.txt`, `OpenCE-OFL.txt` and `Hack-LICENSE.txt`, as the
licenses ask, and `Newtown-LICENSE.txt`, the statement `OpenCE-OFL.txt`
names (`tools/linux_package.py`'s `notices`, `tools/frame_package.py`).

`msdf_atlas.zlib` is the fonts' characters (ASCII, Latin-1 and Windows-1252's
punctuation and letters) as multi-channel signed distance fields, which
the high-res text is drawn from (`port/linux/src/text_hires.c`;
the fields' code is msdfgen's translated to C:
`port/third_party/msdfgen`). It is derived from the fonts, so it is under
their licences, as they are (the SIL Open Font License 1.1 for Cyborg
Interface Gothic, Overpass and OpenCE; the MIT and Bitstream Vera licenses
for Hack), which the releases carry already. To make it again after a font
changes (`tools/test_text_msdf.py` checks it is what the fonts make):

    python3 tools/msdf_atlas.py
