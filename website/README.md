# goath1.com

The project's site: the landing page (`public/index.html`), the guide to
the game data (`public/maps.html`, served at `/maps`), the landing page's
pictures and clips (`public/media/`), its fonts (`public/fonts/`), the logo
(`public/brand/`) and the icons (`public/favicon.svg`, `favicon-16.png`,
`favicon-32.png`, `apple-touch-icon.png`).

It is a Cloudflare Worker of static assets, `openh1` (the name it was
created with, kept so that deploying updates it rather than making a new
one), on the custom domains `goath1.com` and `www.goath1.com`, and still
`openh1.com` and `www.openh1.com`, which serve the same site
(`cloudflare.config.ts`), in Paul's Cloudflare account. To publish a
change, from this folder:

```sh
npm install                                   # once: the Cloudflare CLI, Wrangler and Node 22
PATH="$PWD/node_modules/.bin:$PATH" npx cf deploy
```

The CLI needs Node 22.18 or later to load `cloudflare.config.ts`;
`node_modules/.bin` puts the `node` package's first, as the system's may be
older.

To preview the page, serve `public/` (`python3 -m http.server -d public`)
and open `http://localhost:8000/`. Opened as a file, the fonts may not load.

## The page

The look is "Boot Glow": green on black, after the Xbox era's boot glow and
the plasma's green, with wide Archivo for the titles, Inter for reading and
Overpass Mono (the family of the port's HUD font) for labels. The colours
are the custom properties at the top of the style sheet.

From the top:

- **The hero**: a muted loop of the game (`media/hero.mp4`, `hero.webm`,
  `hero-poster.webp`) under the title. It starts once the page has loaded,
  pauses off screen, has a Pause button, and a visitor whose system asks
  for less motion sees the poster only.
- **The headline features**: 128 players, online co-op, the Steam Frame
  build, 64-bit, Vulkan and Metal, and 3D audio, for any player.
- **The picture sections**, each a comparison of the same moment captured
  twice, the Original (as the game drew it) and GOAT H1:
  - 01, dynamic lights: a wipe, with callouts on the GOAT H1 side;
  - 02, anti-aliasing: a lens, GOAT H1 inside it;
  - 03, the plasma glow: two clips side by side, with a numbered legend;
  - 04, frames between ticks: two clips side by side;
  and two of GOAT H1's options, each the same frame with it off and on:
  - 05, ambient occlusion: a wipe of two stills, Off and On, with a
    numbered legend and where the setting is;
  - 06, a wide field of view: two stills side by side, Normal and Panini,
    with a numbered legend and where the setting is.
  The wipes and the lens play one short demo the first time they scroll into
  view (none for less motion), and stop as soon as the visitor touches them.
- **08, 3D audio**: a wireframe head drawn with three.js (r128, MIT,
  `public/vendor/three-r128.min.js`, as cdnjs and npm's `three@0.128.0`
  publish `three.min.js`, SHA-256
  `9274bbcec8d96168626c732b5d31c775aa8cfb7eaa0599bec0c175908a2c1ce2`), served from the site
  as the fonts are, and loaded only as the section nears the screen. A sound
  source circles the head, turns amber and muffled behind a wall, and the
  readout shows its direction, the ear delay and each ear's level. Callouts
  name what the game's mixer does; "Listen (headphones)" plays a tone placed
  by the browser's own HRTF panner (3D) or by an equal-power pan of the
  source's left and right in the view (Stereo), a switch beside it
  crossfading the two, and the caption says it is only a hint.
  It draws only on screen with the tab shown; for less motion, one still
  frame (it turns by dragging); without WebGL, the poster
  (`media/audio-head-poster.webp`, a frame of the scene) and the callouts.
- **07, Responsive** (before the 3D audio on the page): a headline and a line in plain words, always shown,
  then the race: one click slowed 25 times on one time axis (0 to 60 ms).
  "You click" at the left; in each lane, the Original's (grey) and GOAT H1
  120 Tick's (green, "Faster"), a dot travels from the click to a screen,
  the game's frame, which shows the shot when the dot arrives, at the
  lane's measured median (47.7 and 5.9 ms), over faint marks at its steps
  (30 and 120 a second); a bracket under the Original's lane measures the
  wait, "42 ms you'd be waiting". It plays when it comes into view, holds
  the result 1.5 s and plays again (a Pause button; for less motion, the
  end, still). "Try it at real speed" opens the click-it-yourself demo: two
  panels that answer one press at those medians. Under it, where the time
  goes: each stage's median of `tools/latency_summary.py` as a stacked bar.
  Change the numbers with a new measurement only.
- **VR**, **Platforms**, **Under the hood** (the details for purists, in a
  dense list), **Bring your own game**, **Get it** (the downloads), and the
  footer.
- **Get it**: a card for each machine. Windows and the Steam Deck link the
  latest release's zip (`releases/latest/download/…`) with short install
  steps, and so does the Mac (signed and notarized: `OpenH1.dmg`), and
  Android (`OpenH1-Android.apk`, signed with the project's key since #92).
  The Steam Frame (until it has a simpler install) says
  "Coming soon". The script puts the visitor's machine
  first, and marks it, when it has a build (an Android phone's browser
  says Linux too, so Android is asked first).

Rules for the content:

- Download links only for builds someone who isn't a developer can install:
  Windows, the Mac, the Steam Deck and Android for now.
- Every claim is in this repository's READMEs. Check a new one there first.
- Every picture or clip is a real capture of the game in GOAT H1, captioned
  with what it compares. The one drawing is the 3D audio's head, whose
  caption says its sound is the browser's, not the game's. No Microsoft or
  Halo branding, logos or art.
- The footer keeps the credit to OpenCE and the disclaimer: an unofficial
  fan project, not endorsed by or affiliated with Microsoft; Halo is
  Microsoft's trademark; no game data included.

## Get your maps

`public/maps.html`, at `/maps` (Workers' static assets serve `maps.html`
there, and send `/maps.html` to it: no configuration), is the guide to the
step that most often stops a new player: what the game data is, how to make
a disc image of your own disc, how each build takes it, and what to do when
it goes wrong. Each download card, "Bring your own game", and the game
data section of the repository's README link to it. Previewed with
`python3 -m http.server`, open it as `/maps.html`.

Its style is the landing page's: the rules it uses are copied from
`index.html`'s style sheet as they are (the tokens, the header, the cards,
the dense list, the footer), so a change to one goes in the other. Its
claims are the READMEs' and the code's (`port/linux/src/sdl_platform.c`,
`tools/linux_install.sh`, `port/macos/host_app.m`, the Android
`LauncherActivity.java`); the dumping tools it names are linked to their
own projects only, never to a site that offers the game.

## The join page

`public/join/index.html`, at `/join/`, is where the game's COPY INVITE
sends friends (`P2P_JOIN_PAGE` in `port/linux/src/p2p.h`; "Invites from
anywhere" in `port/linux/README.md`): chat apps such as Steam's make
`https://` addresses clickable, not `openh1://` links. The invite's code is
after `#` (`#party=<32 hex digits>` for a party, `#join=<64>` for a game),
which a browser never sends to the site; the page also takes an older
address's `?join=` and `?party=`. The page opens the `openh1://` link at once
(the game is its handler on every system, the Mac and the iPhone too), shows
the link to copy into JOIN BY LINK, and, when the game did not open (the page
kept the focus) or the code is missing, the downloads, as the home page's
(the visitor's machine first). Its style is the landing page's, its rules copied as
`maps.html`'s are; it is named from the site's root (`/fonts/...`), as it
is in a folder. `tools/test_join_page.py` checks how it reads the links
(with Node, when there is one) and that it loads nothing from other sites.
Preview it at `http://localhost:8000/join/#party=00112233445566778899aabbccddeeff`.

## Pictures and clips

`public/media/`:

| Files | Where | What |
| --- | --- | --- |
| `hero-720.*`, `hero-1080.*`, `hero-1440.*`, `hero-poster.webp` | The hero | Six multiplayer moments cut together (`capture/`, "hero, 2026-10-20"): a Warthog's splatter on Blood Gulch, a pistol triple on Sidewinder, a needler on Prisoner, a sticky on Hang 'Em High, rockets on Chill Out and Damnation. Enhanced at 60 fps, HUD on, filmed at 3840 × 2160; MP4 and WebM at 1280, 1920 and 2560 wide (1.6, 3.2 and 5.5 Mbit/s, a keyframe each second), the page choosing by the screen (a phone the smallest); 21 s, hard cuts; the poster its first frame |
| `lights-original.*`, `lights-openh1.*` | 01 | Rat Race, a frag grenade at a spot already scorched (`capture/`): the Classic preset at 640 × 480 a frame a tick, and Enhanced at 60 fps between ticks. 4 s, 1920 × 1440 (from 3840 × 2880 masters), a loop without a fade, MP4 and WebM with posters |
| `edges-original.*`, `edges-openh1.*` | 02 | Blood Gulch's cliffs against the sky, a slow strafe out and back (`capture/`): the Classic preset at 640 × 480 and Enhanced (SMAA). A quarter of the frame (320 × 180 of the Original's pixels) at 1280 × 720, 4× by nearest neighbour, shown with `image-rendering: pixelated`; 5.3 s, a loop without a fade |
| `glow-original.*`, `glow-openh1.*` | 03 | Rat Race, the plasma rifle fired down a dark corridor, a burst before the clip leaving the scorch marks (`capture/`): the Classic preset a frame a tick, and Enhanced at 60 fps between ticks. 4 s, 1280 × 960, a loop without a fade |
| `strafe-original.*`, `strafe-openh1.*` | 04 | Rat Race, a strafe past pillars and back to where it began (`capture/`): the Classic preset at one frame a tick, and Enhanced at 60 fps between ticks. 3.9 s, 1280 × 720 (a 16:9 crop of 4:3 masters), a loop without a fade |
| `ao-off.webp`, `ao-on.webp` | 05 | Blood Gulch, a Warthog, two players and a Ghost placed by a base, the world stopped, one camera (`capture/`, "ambient occlusion and Panini, 2026-11-03"): Enhanced with `display.ambient_occlusion` off, then `"strong"`, the same frame. Drawn at 2560 × 1440, 1920 × 1080, about 240 KB each |
| `panini-off.webp`, `panini-on.webp` | 06 | Blood Gulch from a player's start, the player's own view at a field of view of 150 (the same section): `display.panini` 0, then 1 (vertical 0), the same frame. Drawn at 2560 × 1440, 1920 × 1080, under 90 KB each |
| `latency-pistol-idle.webp`, `latency-pistol-fire.webp` | 07 | Blood Gulch, a local SWAT game, the pistol fired once (`capture/`, "latency, 2026-10-24"): the frame before the shot and the one with the muzzle flash at its brightest, Enhanced. A 16:9 crop of the lower middle of a 1280 × 720 window (drawn at 2560 × 1440), 640 × 360, about 30 KB each |

Each comparison is a pair of layers, `.layer.before` (the Original) under
`.layer.after` (GOAT H1), each holding an `<img>` or a `<video>`. A wipe of
an option off and on names its sides in `data-before` and `data-after`,
which the slider's spoken value reads. A video
needs `muted loop playsinline preload="none"`, a `poster`, and
`data-webm` / `data-mp4`: the script adds the sources when the pair comes
into view, plays the pair together and keeps the second in step with the
first. How they are captured is in `capture/README.md`.

Callouts are `<span class="co">` in the GOAT H1 layer: `--x` and `--y` put
the pin on the spot it names (in percent of the picture), `--len` is the
leader's length, `west` puts the label to the left of its pin and `down`
below it. On a phone the pins become numbers, and the `<ol class="legend">`
under the picture says what each one is: keep the two in the same order.

Budgets: a still at most 300 KB, a comparison clip at most 1 MB a file,
the hero at most 4 MB, a clip fetched only on a press at most 2.5 MB a file. Everything below the hero loads lazily.

## Fonts

`fonts/Archivo-Latin.woff2`, `fonts/Inter-Latin.woff2` and
`fonts/OverpassMono-Latin.woff2` are the Latin subsets of Archivo (its
width and weight axes), Inter and Overpass Mono, from Google Fonts, under
the SIL Open Font License 1.1 (`fonts/LICENSES.txt`, linked from the
footer). They are served from this site: the page loads nothing from
other sites, and has no trackers or analytics.

## The logo

The logo is "Tail Code" (round 5, concept 01, `website/brand/logo-r5` on
the `logo-r5` branch): the name in light, wide caps, then H1 knocked out of
a green plate with one clipped corner. `brand/goath1-lockup.svg` (GOAT, then
the plate) is the header's and the footer's wordmark, and
`brand/openh1-mark.svg` the plate alone, which is also the app's icon.
`brand/openh1-lockup.svg`, the OPEN lockup, stays for what outside the site
still draws with it (the Steam artwork, `port/assets/steam/art.html`, and the
Mac disk image's background, `port/macos/dmg/background.html`); the mark
keeps its file name, as `tools/app_icon.py` and `AUTHORS.md` name it.
`goath1-lockup.svg` is outlined Archivo (the site's own), GOAT at width 125
and weight 300 tracked 0.06 em, with the plate copied unchanged from the OPEN
lockup. `favicon.svg` is the plate on a dark tile; `favicon-16.png`,
`favicon-32.png` and `apple-touch-icon.png` (180 px) are it drawn at those
sizes. The link preview, `og:image`, is `media/og.jpg` (1200 × 630): the
logo and the headline over a capture of Blood Gulch, from `og/home.html`
(`og/render.sh home`). The maps guide's, `media/og-maps.jpg`, is drawn: the
logo, its headline and a disc, from `og/maps.html` (`og/render.sh maps`).
