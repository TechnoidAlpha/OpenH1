# The menu review app's client

`tools/menu_review.py` serves the menu galleries and the review notes as a JSON
API; this folder is its client: an inspector for the menus' pictures in the
manner of a design tool's.

- `src/`: the TypeScript (`main.ts` the routes, `gallery.ts`, `screen.ts` the
  canvas and inspector, `notes.ts`, `geometry.ts` the layouts' geometry,
  snapping and redlines, `api.ts`).
- `static/`: what is served: `index.html`, `app.css`, and `app.js`, built from
  `src/` and committed, so that running the app needs no Node.
- `tests/`: Playwright's tests in its own Chromium, headless, on a made-up
  gallery (`tests/fixture.py`: plain rectangles, none of the game's pictures).

Build and test (Node 20 or later):

```sh
cd tools/menu_review
npm ci                 # the locked versions (package-lock.json)
npm run build          # type-checked, then static/app.js
npx playwright install chromium
npx playwright test
```

Its only dependencies are for building and testing, none shipped: esbuild
(MIT), TypeScript (Apache 2.0) and Playwright (Apache 2.0).

## Using it

On a screen:

| | |
| --- | --- |
| Click | select the widget under the pointer (the deepest that shows something) |
| Double-click | go into the selection's child under the pointer; on a label, edit its words |
| Esc | the selection's parent |
| Shift-click | add to the selection (in the layers too) |
| Alt | the distances between the selection and the widget under the pointer |
| V, E, C | select, edit (drag or resize the box, snapped to the others; arrows nudge, Shift by 10), comment |
| Cmd/Ctrl-Z, Shift-Cmd/Ctrl-Z | undo, redo the edits proposed |
| + and -, Cmd/Ctrl-scroll, pinch | zoom; Shift+1 fits, Shift+0 is 100% |
| Space-drag, scroll, two fingers, a drag off the picture | pan |
| [ and ] | the other variants, keeping the selection and the view |
| J and K | the next and previous screen |
| B | the before, in a --compare run (side by side, swipe and onion skin in the bar) |
| A long press (a finger) | a comment on the widget |

Edits are proposed, never applied: they are saved on a note (its box's delta
in the menus' units, a label's old and new words) for the agent who fixes it.
