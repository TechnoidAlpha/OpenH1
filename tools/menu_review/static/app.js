/* The menu review app's client, built from tools/menu_review/src by build.mjs: do not edit. */
"use strict";
(() => {
  // src/api.ts
  async function call(path, body) {
    const response = await fetch(path, body === void 0 ? {} : {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body)
    });
    if (response.status === 401) {
      location.href = "login";
      throw new Error("sign in");
    }
    const value = await response.json();
    if (value && value.error) throw new Error(value.error);
    return value;
  }
  var screens = /* @__PURE__ */ new Map();
  var api = {
    meta: () => call("api/runs"),
    screens: (run) => call(
      `api/screens?run=${encodeURIComponent(run)}`
    ),
    /** a screen's data, fetched once (neighbours prefetched ahead of a switch) */
    screen(run, name) {
      const key = `${run}
${name}`;
      let found = screens.get(key);
      if (!found) {
        found = call(`api/screen?run=${encodeURIComponent(run)}&screen=${encodeURIComponent(name)}`);
        found.catch(() => screens.delete(key));
        screens.set(key, found);
      }
      return found;
    },
    notes: () => call("api/notes"),
    addNote: (note) => call("api/notes", note),
    change: (id, change) => call(`api/notes/${id}`, change),
    render: () => call("api/render", {})
  };
  var preloaded = /* @__PURE__ */ new Set();
  function preload(url) {
    if (!url || preloaded.has(url)) return;
    preloaded.add(url);
    const image = new Image();
    image.decoding = "async";
    image.src = url;
  }

  // src/dom.ts
  function h(tag, attributes = {}, ...content) {
    const element = document.createElement(tag);
    for (const [key, value] of Object.entries(attributes)) {
      if (value === void 0 || value === null || value === false) continue;
      if (key.startsWith("on") && typeof value === "function") {
        element.addEventListener(key.slice(2), value);
      } else if (key === "class") {
        element.className = String(value);
      } else if (key === "dataset") {
        Object.assign(element.dataset, value);
      } else if (key in element && typeof value !== "string") {
        element[key] = value;
      } else {
        element.setAttribute(key, value === true ? "" : String(value));
      }
    }
    for (const child of content) {
      if (child === null || child === void 0 || child === false) continue;
      element.append(typeof child === "string" ? document.createTextNode(child) : child);
    }
    return element;
  }
  var SVG = "http://www.w3.org/2000/svg";
  function svg(tag, attributes = {}) {
    const element = document.createElementNS(SVG, tag);
    for (const [key, value] of Object.entries(attributes)) element.setAttribute(key, String(value));
    return element;
  }
  function author() {
    try {
      return localStorage.getItem("menu-review-author") || "";
    } catch {
      return "";
    }
  }
  function setAuthor(name) {
    try {
      localStorage.setItem("menu-review-author", name);
    } catch {
    }
  }
  function when(time) {
    if (!time) return "";
    const date = new Date(time);
    return isNaN(date.getTime()) ? time : date.toLocaleString(void 0, { dateStyle: "medium", timeStyle: "short" });
  }
  var STATUS_WORDS = {
    open: "Open",
    in_progress: "In progress",
    fixed: "Ready for review",
    done: "Ready for review",
    closed: "Closed"
  };
  function add(parent, ...content) {
    for (const child of content) {
      if (child === null || child === void 0 || child === false) continue;
      parent.append(typeof child === "string" ? document.createTextNode(child) : child);
    }
  }

  // src/gallery.ts
  async function galleryView(root2, meta2, run) {
    const data = await api.screens(run);
    const runs = h("select", { "aria-label": "Gallery", onchange: (event) => {
      location.hash = `#/g/${encodeURIComponent(event.target.value)}`;
    } }, ...meta2.runs.map((each) => h(
      "option",
      { value: each.name, selected: each.name === run },
      each.name + (each.compared ? " (before | after)" : "")
    )));
    const filter = h("input", { type: "search", placeholder: "Find a screen", "aria-label": "Find a screen" });
    const groups = /* @__PURE__ */ new Map();
    for (const screen of data.screens) {
      const card = h(
        "a",
        {
          class: "card",
          href: `#/s/${encodeURIComponent(run)}/${encodeURIComponent(screen.name)}/${screen.variants[0]}`,
          "data-name": screen.name.toLowerCase(),
          onmouseenter: () => api.screen(run, screen.name)
        },
        h(
          "div",
          { class: "card-title" },
          h("span", {}, screen.label),
          screen.open ? h("span", { class: "badge open-count", title: "Open notes" }, String(screen.open)) : null
        ),
        h("div", { class: "thumbs" }, ...screen.variants.map((variant) => h("img", {
          src: screen.thumbs[variant],
          alt: variant,
          title: variant,
          loading: "lazy",
          decoding: "async"
        }))),
        h("div", { class: "dim" }, `${screen.file} \xB7 ${screen.generator || "by hand"}`)
      );
      if (!groups.has(screen.menu)) groups.set(screen.menu, []);
      groups.get(screen.menu).push(card);
    }
    const sections = [...groups.entries()].map(([menu, cards]) => h(
      "section",
      { class: "group" },
      h("h2", {}, menu.charAt(0).toUpperCase() + menu.slice(1)),
      h("div", { class: "grid" }, ...cards)
    ));
    filter.addEventListener("input", () => {
      const words = filter.value.toLowerCase();
      for (const card of root2.querySelectorAll(".card")) card.hidden = !card.dataset.name.includes(words);
    });
    root2.replaceChildren(h(
      "main",
      { class: "page" },
      h(
        "div",
        { class: "filters" },
        runs,
        filter,
        h("span", { class: "dim" }, `${data.screens.length} screens${data.compared ? " \xB7 before | after: notes go on the after" : ""}`)
      ),
      ...sections
    ));
    for (const screen of data.screens.slice(0, 4)) api.screen(run, screen.name).then((found) => {
      for (const variant of Object.values(found.variants)) preload(variant.image);
    }).catch(() => void 0);
  }

  // src/geometry.ts
  var WIDGET_TYPES = ["container", "text", "spinner", "list", "model", "movie", "custom"];
  function toMenu(layout, size, x, y) {
    const [x0, y0, x1, y1] = layout.screen;
    return [x0 + x * (x1 - x0) / size[0], y0 + y * (y1 - y0) / size[1]];
  }
  function toPicture(layout, size, x, y) {
    const [x0, y0, x1, y1] = layout.screen;
    return [(x - x0) * size[0] / (x1 - x0), (y - y0) * size[1] / (y1 - y0)];
  }
  function boxOnPicture(layout, size, box) {
    const [ax, ay] = toPicture(layout, size, box[0], box[1]);
    const [bx, by] = toPicture(layout, size, box[2], box[3]);
    return [ax, ay, bx - ax, by - ay];
  }
  function pixelsPerUnit(layout, size) {
    return size[0] / (layout.screen[2] - layout.screen[0]);
  }
  function widgets(layout) {
    return layout.items.filter((item) => item.kind === "widget");
  }
  function texts(layout) {
    return layout.items.filter((item) => item.kind === "text");
  }
  function glyphs(layout) {
    return layout.items.filter((item) => item.kind === "glyph");
  }
  function screenWide(widget) {
    return widget.box[2] - widget.box[0] >= 640 && widget.box[3] - widget.box[1] >= 480;
  }
  function contains(box, x, y) {
    return x >= box[0] && x < box[2] && y >= box[1] && y < box[3];
  }
  function sensible(layout, pictured) {
    const found = /* @__PURE__ */ new Set();
    for (const item of layout.items) {
      if (item.kind === "text" || item.kind === "glyph" || item.kind === "target") found.add(item.widget);
    }
    for (const widget of widgets(layout)) {
      if (pictured.has(widget.name)) found.add(widget.id);
    }
    return found;
  }
  function widgetAt(layout, x, y, shows) {
    const all = widgets(layout);
    let fallback = null;
    for (let index = all.length - 1; index >= 0; index--) {
      const widget = all[index];
      if (!contains(widget.box, x, y) || screenWide(widget) || widget.box[2] <= widget.box[0]) continue;
      if (!shows || shows.has(widget.id)) return widget;
      if (!fallback) fallback = widget;
    }
    return fallback;
  }
  function byId(layout) {
    return new Map(widgets(layout).map((widget) => [widget.id, widget]));
  }
  function children(layout, id) {
    return widgets(layout).filter((widget) => widget.parent === id);
  }
  function childAt(layout, id, x, y) {
    const found = children(layout, id).filter((widget) => contains(widget.box, x, y));
    return found.length ? found[found.length - 1] : null;
  }
  function parentOf(layout, widget) {
    const parent = byId(layout).get(widget.parent);
    return parent && !screenWide(parent) ? parent : null;
  }
  function named(layout, name) {
    let found = null;
    for (const widget of widgets(layout)) if (widget.name === name) found = widget;
    return found;
  }
  function pathOf(layout, widget) {
    const all = byId(layout);
    const path = [];
    let step = widget;
    while (step && path.length < 32) {
      path.unshift(step.name);
      step = all.get(step.parent);
    }
    return path;
  }
  function shortName(name) {
    const parts = name.split("\\");
    return parts[parts.length - 1];
  }
  function union(boxes) {
    return [
      Math.min(...boxes.map((box) => box[0])),
      Math.min(...boxes.map((box) => box[1])),
      Math.max(...boxes.map((box) => box[2])),
      Math.max(...boxes.map((box) => box[3]))
    ];
  }
  function snap(moving, others, threshold, edges = "xyXY") {
    const box = [...moving];
    const guides = [];
    for (const axis of ["x", "y"]) {
      const low = axis === "x" ? 0 : 1;
      const high = axis === "x" ? 2 : 3;
      const lines = [];
      for (const other of others) lines.push(other[low], other[high], (other[low] + other[high]) / 2);
      const moveLow = edges.includes(axis);
      const moveHigh = edges.includes(axis.toUpperCase());
      const mine = [];
      if (moveLow) mine.push([box[low], 0]);
      if (moveHigh) mine.push([box[high], 1]);
      if (moveLow && moveHigh) mine.push([(box[low] + box[high]) / 2, 2]);
      let best = null;
      for (const [value, which] of mine) {
        for (const line of lines) {
          const difference = line - value;
          if (Math.abs(difference) <= threshold && (!best || Math.abs(difference) < Math.abs(best[0]))) {
            best = [difference, line, which];
          }
        }
      }
      if (best) {
        if (moveLow && moveHigh) {
          box[low] += best[0];
          box[high] += best[0];
        } else if (moveLow) box[low] += best[0];
        else box[high] += best[0];
        guides.push({ axis, at: best[1] });
      }
    }
    return { box, guides };
  }
  function redlines(a, b) {
    const lines = [];
    const middleY = (Math.max(a[1], b[1]) + Math.min(a[3], b[3])) / 2;
    const middleX = (Math.max(a[0], b[0]) + Math.min(a[2], b[2])) / 2;
    const overlapX = a[0] < b[2] && b[0] < a[2];
    const overlapY = a[1] < b[3] && b[1] < a[3];
    const y = overlapY ? middleY : (a[1] + a[3]) / 2;
    const x = overlapX ? middleX : (a[0] + a[2]) / 2;
    if (a[2] <= b[0]) lines.push({ from: [a[2], y], to: [b[0], y], length: b[0] - a[2] });
    else if (b[2] <= a[0]) lines.push({ from: [b[2], y], to: [a[0], y], length: a[0] - b[2] });
    if (a[3] <= b[1]) lines.push({ from: [x, a[3]], to: [x, b[1]], length: b[1] - a[3] });
    else if (b[3] <= a[1]) lines.push({ from: [x, b[3]], to: [x, a[1]], length: a[1] - b[3] });
    if (overlapX && overlapY) {
      const [inner, outer] = a[0] >= b[0] && a[2] <= b[2] && a[1] >= b[1] && a[3] <= b[3] ? [a, b] : [b, a];
      const cy = (inner[1] + inner[3]) / 2;
      const cx = (inner[0] + inner[2]) / 2;
      lines.push({ from: [outer[0], cy], to: [inner[0], cy], length: inner[0] - outer[0] });
      lines.push({ from: [inner[2], cy], to: [outer[2], cy], length: outer[2] - inner[2] });
      lines.push({ from: [cx, outer[1]], to: [cx, inner[1]], length: inner[1] - outer[1] });
      lines.push({ from: [cx, inner[3]], to: [cx, outer[3]], length: outer[3] - inner[3] });
    }
    return lines.filter((line) => Math.abs(line.length) > 0.01);
  }
  function round1(value) {
    return Math.round(value * 10) / 10;
  }

  // src/notes.ts
  function editsWords(note) {
    const words = [];
    const box = note.edits?.box;
    if (box) {
      const d = box.delta;
      words.push(`Move ${sign(d[0])}, ${sign(d[1])}; ${sign(d[2])} wide, ${sign(d[3])} tall (to ${box.to.join(", ")})`);
    }
    const text = note.edits?.text;
    if (text) words.push(`"${text.old}" \u2192 "${text.new}"`);
    return words;
  }
  function sign(value) {
    return (value > 0 ? "+" : "") + value;
  }
  function noteCard(note, options) {
    const kind = note.kind === "question" ? "Question" : "Fix";
    const where = note.widget ? shortName(note.widget) : note.area ? "an area" : "the screen";
    const reply = h("textarea", { class: "reply", rows: 2, placeholder: note.kind === "question" ? "Answer\u2026" : "Reply\u2026" });
    const status = (value, words, needsReply = false) => h("button", {
      class: "small",
      "data-action": value,
      onclick: async () => {
        if (needsReply && !reply.value.trim()) {
          reply.focus();
          reply.placeholder = "Say why it is reopened\u2026";
          return;
        }
        const changed = await api.change(note.id, { status: value, reply: reply.value.trim() || void 0, author: author() });
        options.onChanged(changed);
      }
    }, words);
    const buttons = [];
    if (note.status === "open") buttons.push(status("in_progress", "Start"), status("closed", "Resolve"));
    if (note.status === "in_progress") buttons.push(status("open", "Reopen"), status("closed", "Resolve"));
    if (note.status === "fixed" || note.status === "done") {
      buttons.push(status("closed", "Approve"), status("open", "Reopen with reply", true));
    }
    if (note.status === "closed") buttons.push(status("open", "Reopen"));
    const thread = (note.thread || []).map((entry) => h(
      "div",
      { class: "reply-item" },
      h("b", {}, entry.author || "someone"),
      " ",
      h("span", { class: "dim" }, when(entry.time)),
      h("div", {}, entry.text)
    ));
    const pair = note.before_after ? h(
      "div",
      { class: "pair" },
      h("figure", {}, h("img", { src: note.before_after.before, alt: "before", loading: "lazy" }), h("figcaption", {}, "before")),
      h("figure", {}, h("img", { src: note.before_after.after, alt: "after", loading: "lazy" }), h("figcaption", {}, "after"))
    ) : null;
    const card = h(
      "article",
      { class: `note status-${note.status}`, "data-note": note.id },
      h(
        "header",
        {},
        h("span", { class: `badge kind-${note.kind || "fix"}` }, kind),
        h("span", { class: "badge status" }, STATUS_WORDS[note.status] || note.status),
        h("button", { class: "link", onclick: () => options.onSelect?.(note), title: "Show it on the screen" }, where),
        h("span", { class: "dim" }, ` \xB7 ${note.author || (note.origin === "web" ? "reviewer" : "studio")} \xB7 ${when(note.time)}`)
      ),
      h("p", { class: "words" }, note.note),
      ...editsWords(note).map((words) => h("p", { class: "edit-words" }, words)),
      !options.compact && note.screen ? h("p", { class: "dim" }, `${note.screen} \xB7 ${note.variant}${note.scope && note.scope !== "variant" ? ` \xB7 ${note.scope === "screen" ? "all variants" : "everywhere like this"}` : ""}${note.commit ? ` \xB7 fixed in ${note.commit.slice(0, 9)}` : ""}`) : null,
      pair,
      !options.compact && note.picture_url && !pair ? h("img", { class: "note-picture", src: note.picture_url, alt: "", loading: "lazy" }) : null,
      thread.length ? h("div", { class: "thread" }, ...thread) : null,
      h(
        "div",
        { class: "reply-row" },
        reply,
        h("button", { class: "small", onclick: async () => {
          if (!reply.value.trim()) return;
          options.onChanged(await api.change(note.id, { reply: reply.value.trim(), author: author() }));
        } }, "Reply")
      ),
      h("div", { class: "actions" }, ...buttons)
    );
    return card;
  }
  function composer(draft, meta2, done) {
    const words = h("textarea", { rows: 3, placeholder: "What should change?", maxlength: 2e3 });
    const name = h("input", { placeholder: "Your name", value: author(), autocomplete: "name" });
    const kind = h(
      "select",
      { "aria-label": "Kind" },
      h("option", { value: "fix" }, "Fix"),
      h("option", { value: "question" }, "Question")
    );
    const scope = h(
      "select",
      { "aria-label": "Scope" },
      h("option", { value: "variant" }, "This variant"),
      h("option", { value: "screen" }, "All variants of this screen"),
      h("option", { value: "everywhere" }, "Everywhere like this")
    );
    const message = h("span", { class: "dim" });
    const save = h("button", { class: "primary", type: "submit" }, "Save note");
    const form = h(
      "form",
      { class: "composer", onsubmit: async (event) => {
        event.preventDefault();
        if (!words.value.trim() && !draft.edits) {
          words.focus();
          return;
        }
        setAuthor(name.value.trim());
        save.disabled = true;
        try {
          const note = await api.addNote({
            run: draft.run,
            screen: draft.screen,
            variant: draft.variant,
            side: "after",
            kind: draft.target,
            widget: draft.widget,
            x: draft.x,
            y: draft.y,
            x1: draft.x1,
            y1: draft.y1,
            edits: draft.edits,
            note: words.value.trim(),
            author: name.value.trim(),
            note_kind: kind.value,
            scope: scope.value
          });
          done(note);
        } catch (error) {
          message.textContent = String(error);
          save.disabled = false;
        }
      } },
      h("div", { class: "composer-title" }, h("b", {}, "Note on "), draft.label),
      draft.edits ? h("p", { class: "edit-words" }, "With the edits proposed") : null,
      words,
      h("div", { class: "row" }, kind, scope),
      h(
        "div",
        { class: "row" },
        name,
        save,
        h("button", { type: "button", onclick: () => done(null) }, "Cancel")
      ),
      message
    );
    void meta2;
    setTimeout(() => words.focus(), 0);
    form.addEventListener("keydown", (event) => {
      if (event.key === "Escape") {
        event.stopPropagation();
        done(null);
      }
      if (event.key === "Enter" && (event.metaKey || event.ctrlKey)) form.requestSubmit();
      event.stopPropagation();
    });
    return form;
  }
  async function notesView(root2, meta2, query) {
    let notes = await api.notes();
    const status = h(
      "select",
      { "aria-label": "Status" },
      ...[
        ["active", "Open and in progress"],
        ["open", "Open"],
        ["in_progress", "In progress"],
        ["fixed", "Ready for review"],
        ["closed", "Closed"],
        ["all", "All"]
      ].map(([value, words]) => h("option", { value, selected: value === (query.get("status") || "active") }, words))
    );
    const screen = h("input", { placeholder: "Screen", value: query.get("screen") || "" });
    const variant = h(
      "select",
      { "aria-label": "Variant" },
      h("option", { value: "" }, "Any variant"),
      ...meta2.variants.map((value) => h("option", { value, selected: value === query.get("variant") }, value))
    );
    const who = h("input", { placeholder: "Author", value: query.get("author") || "" });
    const list = h("div", { class: "note-list" });
    const rendering = h("span", { class: "dim" }, meta2.render || "");
    const draw = () => {
      const chosen = notes.filter((note) => {
        const wanted = status.value;
        const statusOk = wanted === "all" || (wanted === "active" ? ["open", "in_progress"].includes(note.status) : wanted === "fixed" ? ["fixed", "done"].includes(note.status) : note.status === wanted);
        return statusOk && (!screen.value || note.screen.includes(screen.value)) && (!variant.value || note.variant === variant.value) && (!who.value || (note.author || "studio").toLowerCase().includes(who.value.toLowerCase()));
      }).reverse();
      list.replaceChildren(...chosen.map((note) => noteCard(note, {
        onChanged: (changed) => {
          notes = notes.map((other) => other.id === changed.id ? { ...other, ...changed } : other);
          draw();
        },
        onSelect: (selected) => {
          location.hash = `#/s/latest/${encodeURIComponent(selected.screen)}/${selected.variant}?note=${selected.id}`;
        }
      })));
      if (!chosen.length) list.append(h("p", { class: "dim" }, "No notes here."));
      const counts = { open: 0, ready: 0 };
      for (const note of notes) {
        if (note.status === "open" || note.status === "in_progress") counts.open++;
        if (note.status === "fixed" || note.status === "done") counts.ready++;
      }
      heading.textContent = `Notes \xB7 ${counts.open} open \xB7 ${counts.ready} ready for review`;
    };
    const heading = h("h1", {});
    for (const control of [status, screen, variant, who]) control.addEventListener("input", draw);
    root2.replaceChildren(h(
      "main",
      { class: "page" },
      heading,
      h("div", { class: "filters" }, status, screen, variant, who),
      h("div", { class: "filters" }, h("button", { onclick: async () => {
        rendering.textContent = (await api.render()).render || "";
      } }, "Render the noted screens again"), rendering),
      list
    ));
    draw();
  }

  // src/screen.ts
  var HANDLES = ["nw", "n", "ne", "e", "se", "s", "sw", "w"];
  var VARIANT_WORDS = {
    "keyboard-16x9": "Keys 16:9",
    "controller-16x10": "Pad 16:10",
    "touch-phone": "Touch",
    "keyboard-4x3": "Keys 4:3",
    "keyboard-21x9": "Keys 21:9"
  };
  var LONG_PRESS = 550;
  var ZOOM_MIN = 0.05;
  var ZOOM_MAX = 16;
  var ScreenView = class {
    constructor(root2, meta2) {
      this.run = "";
      this.screenName = "";
      this.variant = "";
      this.layout = null;
      this.size = [1, 1];
      this.shows = /* @__PURE__ */ new Set();
      this.notes = [];
      this.screens = [];
      // the view: the picture's pixels at zoom, from the viewport's corner
      this.zoom = 1;
      this.x = 0;
      this.y = 0;
      this.placed = false;
      // the selection, by widgets' names (so that it follows them to another variant)
      this.selected = [];
      this.hover = null;
      this.alt = false;
      this.space = false;
      this.mode = "select";
      this.compare = "after";
      this.showBefore = false;
      this.swipe = 50;
      this.onion = 0.5;
      // the edits proposed, by widget, and their history
      this.proposals = /* @__PURE__ */ new Map();
      this.undoStack = [];
      this.redoStack = [];
      this.guides = [];
      this.live = null;
      this.areaDrag = null;
      // the pointers down, and what they are doing
      this.pointers = /* @__PURE__ */ new Map();
      this.gesture = null;
      this.gestureStart = null;
      this.longPress = 0;
      this.tab = "inspect";
      this.keys = (event) => this.key(event);
      this.keysUp = (event) => this.keyUp(event);
      this.resize = () => this.applyView();
      // the elements
      this.el = {
        view: h("div", { class: "screen-view" }),
        title: h("div", { class: "title" }),
        variants: h("div", { class: "segmented variants" }),
        modes: h("div", { class: "segmented modes" }),
        zoomText: h("button", { class: "zoom-text", title: "Fit (Shift+1); 100% (Shift+0)" }),
        compares: h("div", { class: "segmented compares" }),
        slider: h("input", { type: "range", min: 0, max: 100, class: "compare-slider", "aria-label": "Swipe or onion" }),
        area: h("section", { class: "canvas-area" }),
        side: h("div", { class: "viewport side-before", hidden: true }),
        sideStage: h("div", { class: "stage" }),
        sideImage: h("img", { alt: "before", draggable: false }),
        viewport: h("div", { class: "viewport main", tabindex: 0 }),
        stage: h("div", { class: "stage" }),
        image: h("img", { alt: "", draggable: false }),
        before: h("img", { alt: "before", class: "before-overlay", draggable: false, hidden: true }),
        overlay: svg("svg", { class: "overlay" }),
        hud: h("div", { class: "hud" }),
        sheet: h("div", { class: "composer-host" }),
        panel: h("aside", { class: "panel" }),
        tabs: h("div", { class: "tabs" }),
        content: h("div", { class: "panel-content" }),
        status: h("div", { class: "statusbar dim" })
      };
      this.root = root2;
      this.meta = meta2;
      const e = this.el;
      e.stage.append(e.image, e.before, e.overlay);
      e.viewport.append(e.stage);
      e.sideStage.append(e.sideImage);
      e.side.append(e.sideStage);
      e.area.append(e.side, e.viewport, e.hud, e.sheet);
      e.panel.append(e.tabs, e.content);
      const bar = h(
        "div",
        { class: "toolbar" },
        h("button", { title: "Previous screen (K)", onclick: () => this.step(-1) }, "\u2039"),
        e.title,
        h("button", { title: "Next screen (J)", onclick: () => this.step(1) }, "\u203A"),
        e.variants,
        e.modes,
        h(
          "div",
          { class: "segmented" },
          h("button", { title: "Zoom out (-)", onclick: () => this.zoomBy(1 / 1.25) }, "\u2212"),
          e.zoomText,
          h("button", { title: "Zoom in (+)", onclick: () => this.zoomBy(1.25) }, "+")
        ),
        e.compares,
        e.slider
      );
      e.zoomText.addEventListener("click", () => this.zoom < this.fitZoom() * 1.01 ? this.actual() : this.fit());
      e.slider.addEventListener("input", () => {
        if (this.compare === "swipe") this.swipe = Number(e.slider.value);
        else this.onion = Number(e.slider.value) / 100;
        this.applyCompare();
      });
      e.view.append(bar, h("div", { class: "workspace" }, e.area, e.panel), e.status);
      this.bindPointers();
    }
    // ---------- showing a screen
    async show(run, screen, variant, note) {
      if (!this.root.contains(this.el.view)) {
        this.root.replaceChildren(this.el.view);
        document.addEventListener("keydown", this.keys);
        document.addEventListener("keyup", this.keysUp);
        window.addEventListener("resize", this.resize);
      }
      const sameScreen = run === this.run && screen === this.screenName;
      const data = await api.screen(run, screen);
      const names = Object.keys(data.variants);
      const chosen = variant && data.variants[variant] ? variant : names[0];
      const before = this.layout ? { ppu: pixelsPerUnit(this.layout, this.size), centre: this.centreInMenu() } : null;
      if (!sameScreen) {
        this.proposals.clear();
        this.undoStack = [];
        this.redoStack = [];
        if (!note) this.selected = [];
      }
      this.run = run;
      this.screenName = screen;
      this.variant = chosen;
      this.data = data;
      this.v = data.variants[chosen];
      this.layout = this.v.layout;
      this.size = this.v.size;
      const pictured = new Set(Object.entries(data.widgets).filter(([, found]) => found.attributes.bitmap).map(([name]) => "pc\\" + name.replace(/\//g, "\\")));
      this.shows = this.layout ? sensible(this.layout, pictured) : /* @__PURE__ */ new Set();
      this.selected = this.selected.filter((name) => this.layout && named(this.layout, name));
      this.el.image.src = this.v.image;
      this.el.image.width = this.size[0];
      this.el.image.height = this.size[1];
      this.el.before.src = this.v.before || "";
      this.el.sideImage.src = this.v.before || "";
      for (const element of [this.el.stage, this.el.sideStage]) {
        element.style.width = `${this.size[0]}px`;
        element.style.height = `${this.size[1]}px`;
      }
      this.el.overlay.setAttribute("viewBox", `0 0 ${this.size[0]} ${this.size[1]}`);
      this.el.overlay.setAttribute("width", String(this.size[0]));
      this.el.overlay.setAttribute("height", String(this.size[1]));
      if (!this.v.before) {
        this.compare = "after";
        this.showBefore = false;
      }
      this.notes = await api.notes();
      this.toolbar();
      if (sameScreen && before && this.layout && this.placed) {
        this.zoom *= before.ppu / pixelsPerUnit(this.layout, this.size);
        const [px, py] = toPicture(this.layout, this.size, before.centre[0], before.centre[1]);
        const rect = this.el.viewport.getBoundingClientRect();
        this.x = rect.width / 2 - px * this.zoom;
        this.y = rect.height / 2 - py * this.zoom;
        this.applyView();
      } else {
        requestAnimationFrame(() => this.fit());
      }
      if (note) {
        const found = this.notes.find((each) => each.id === note);
        if (found?.widget) this.selected = [found.widget];
        this.tab = "comments";
      }
      this.applyCompare();
      this.panel();
      this.draw();
      this.prefetch();
    }
    /** a point in the menus' units as the client's pixels (the browser tests') */
    clientPoint(mx, my) {
      const rect = this.el.viewport.getBoundingClientRect();
      const [px, py] = toPicture(this.layout, this.size, mx, my);
      return [rect.left + this.x + px * this.zoom, rect.top + this.y + py * this.zoom];
    }
    close() {
      document.removeEventListener("keydown", this.keys);
      document.removeEventListener("keyup", this.keysUp);
      window.removeEventListener("resize", this.resize);
    }
    href(variant = this.variant, screen = this.screenName) {
      return `#/s/${encodeURIComponent(this.run)}/${encodeURIComponent(screen)}/${variant}`;
    }
    async prefetch() {
      if (!this.screens.length) this.screens = (await api.screens(this.run)).screens.map((screen) => screen.name);
      for (const variant of Object.values(this.data.variants)) preload(variant.image);
      const index = this.screens.indexOf(this.screenName);
      for (const neighbour of [this.screens[index - 1], this.screens[index + 1]]) {
        if (!neighbour) continue;
        api.screen(this.run, neighbour).then((data) => preload((data.variants[this.variant] || Object.values(data.variants)[0])?.image)).catch(() => void 0);
      }
    }
    async step(by) {
      if (!this.screens.length) this.screens = (await api.screens(this.run)).screens.map((screen) => screen.name);
      const index = this.screens.indexOf(this.screenName);
      const next = this.screens[(index + by + this.screens.length) % this.screens.length];
      if (next) location.hash = this.href(this.variant, next);
    }
    toolbar() {
      const e = this.el;
      e.title.replaceChildren(h("b", {}, this.data.label), h("span", { class: "dim" }, ` ${this.data.file}`));
      e.variants.replaceChildren(...Object.keys(this.data.variants).map((variant) => h("a", {
        href: this.href(variant),
        class: variant === this.variant ? "on" : "",
        title: `${variant} ([ and ])`,
        "data-variant": variant
      }, VARIANT_WORDS[variant] || variant)));
      const modes = [["select", "Select", "V"], ["edit", "Edit", "E"], ["comment", "Comment", "C"]];
      e.modes.replaceChildren(...modes.map(([mode, words, key]) => h("button", {
        class: mode === this.mode ? "on" : "",
        "data-mode": mode,
        title: `${words} (${key})`,
        onclick: () => this.setMode(mode)
      }, words)));
      const compares = [["after", "After"], ["side", "Side by side"], ["swipe", "Swipe"], ["onion", "Onion"]];
      e.compares.hidden = !this.v.before;
      e.compares.replaceChildren(...compares.map(([compare, words]) => h("button", {
        class: compare === this.compare ? "on" : "",
        "data-compare": compare,
        onclick: () => {
          this.compare = compare;
          this.toolbar();
          this.applyCompare();
        }
      }, words)), h("button", { class: this.showBefore ? "on" : "", title: "Before (B)", onclick: () => this.toggleBefore() }, "B"));
      e.slider.hidden = !(this.v.before && (this.compare === "swipe" || this.compare === "onion"));
      e.slider.value = String(this.compare === "swipe" ? this.swipe : Math.round(this.onion * 100));
    }
    setMode(mode) {
      this.mode = this.mode === mode && mode !== "select" ? "select" : mode;
      this.el.viewport.dataset.mode = this.mode;
      this.toolbar();
      this.draw();
    }
    toggleBefore() {
      if (!this.v.before) return;
      this.showBefore = !this.showBefore;
      this.toolbar();
      this.applyCompare();
    }
    applyCompare() {
      const e = this.el;
      const has = Boolean(this.v.before);
      e.side.hidden = !(has && this.compare === "side");
      e.area.classList.toggle("split", !e.side.hidden);
      e.before.hidden = !(has && (this.compare === "swipe" || this.compare === "onion" || this.showBefore));
      e.before.style.clipPath = has && this.compare === "swipe" && !this.showBefore ? `inset(0 ${100 - this.swipe}% 0 0)` : "";
      e.before.style.opacity = has && this.compare === "onion" && !this.showBefore ? String(this.onion) : "1";
      e.viewport.dataset.before = this.showBefore ? "shown" : "";
      this.applyView();
    }
    // ---------- the view
    fitZoom() {
      const rect = this.el.viewport.getBoundingClientRect();
      return Math.max(ZOOM_MIN, Math.min(rect.width / this.size[0], rect.height / this.size[1]) * 0.96);
    }
    fit() {
      const rect = this.el.viewport.getBoundingClientRect();
      this.zoom = this.fitZoom();
      this.x = (rect.width - this.size[0] * this.zoom) / 2;
      this.y = (rect.height - this.size[1] * this.zoom) / 2;
      this.placed = true;
      this.el.viewport.dataset.placed = "fit";
      this.applyView();
    }
    actual() {
      this.zoomAt(1, this.el.viewport.clientWidth / 2, this.el.viewport.clientHeight / 2);
    }
    zoomBy(factor) {
      this.zoomAt(this.zoom * factor, this.el.viewport.clientWidth / 2, this.el.viewport.clientHeight / 2);
    }
    /** zoomed about a point of the viewport, which stays where it is */
    zoomAt(zoom, vx, vy) {
      const next = Math.min(ZOOM_MAX, Math.max(ZOOM_MIN, zoom));
      this.x = vx - (vx - this.x) * next / this.zoom;
      this.y = vy - (vy - this.y) * next / this.zoom;
      this.zoom = next;
      this.placed = true;
      this.applyView();
    }
    applyView() {
      const transform = `translate(${this.x}px, ${this.y}px) scale(${this.zoom})`;
      this.el.stage.style.transform = transform;
      this.el.sideStage.style.transform = transform;
      const crisp = this.zoom > 1.001;
      this.el.stage.classList.toggle("crisp", crisp);
      this.el.sideStage.classList.toggle("crisp", crisp);
      this.el.viewport.dataset.zoom = String(round1(this.zoom * 100) / 100);
      this.el.zoomText.textContent = `${Math.round(this.zoom * 100)}%`;
      this.drawHud();
    }
    centreInMenu() {
      const rect = this.el.viewport.getBoundingClientRect();
      const px = (rect.width / 2 - this.x) / this.zoom;
      const py = (rect.height / 2 - this.y) / this.zoom;
      return this.layout ? toMenu(this.layout, this.size, px, py) : [320, 240];
    }
    /** a client point as the picture's pixels */
    picturePoint(clientX, clientY) {
      const rect = this.el.viewport.getBoundingClientRect();
      return [(clientX - rect.left - this.x) / this.zoom, (clientY - rect.top - this.y) / this.zoom];
    }
    menuPoint(clientX, clientY) {
      const [px, py] = this.picturePoint(clientX, clientY);
      return this.layout ? toMenu(this.layout, this.size, px, py) : [px, py];
    }
    /** a box in the menus' units in the viewport's pixels: [x, y, width, height] */
    onViewport(box) {
      const [px, py, pw, ph] = boxOnPicture(this.layout, this.size, box);
      return [this.x + px * this.zoom, this.y + py * this.zoom, pw * this.zoom, ph * this.zoom];
    }
    unitsPerScreenPixel() {
      return this.layout ? 1 / (pixelsPerUnit(this.layout, this.size) * this.zoom) : 1;
    }
    // ---------- the selection
    primary() {
      const name = this.selected[this.selected.length - 1];
      return name && this.layout ? named(this.layout, name) : null;
    }
    boxOf(widget) {
      return this.live && this.live.name === widget.name ? this.live.box : this.proposals.get(widget.name)?.box || widget.box;
    }
    select(names) {
      this.selected = names;
      this.panel();
      this.draw();
      this.el.content.querySelector(".layer.selected")?.scrollIntoView({ block: "nearest" });
    }
    clickAt(clientX, clientY, shift) {
      if (!this.layout) return;
      const [mx, my] = this.menuPoint(clientX, clientY);
      const widget = widgetAt(this.layout, mx, my, this.shows);
      if (this.mode === "comment") return this.openComposer(widget, mx, my);
      if (!widget) return this.select(shift ? this.selected : []);
      if (shift) {
        const without = this.selected.filter((name) => name !== widget.name);
        this.select(without.length === this.selected.length ? [...this.selected, widget.name] : without);
      } else {
        this.select([widget.name]);
      }
    }
    doubleClick(clientX, clientY) {
      if (!this.layout) return;
      const [mx, my] = this.menuPoint(clientX, clientY);
      const current = this.primary();
      if (current) {
        const child = childAt(this.layout, current.id, mx, my);
        if (child) return this.select([child.name]);
        if (texts(this.layout).some((text) => text.widget === current.id && text.text.trim()) && contains(this.boxOf(current), mx, my)) {
          return this.editWords(current);
        }
      }
      const widget = widgetAt(this.layout, mx, my, this.shows);
      if (widget) this.select([widget.name]);
    }
    toParent() {
      const current = this.primary();
      if (!current || !this.layout) return this.select([]);
      const parent = parentOf(this.layout, current);
      this.select(parent ? [parent.name] : []);
    }
    // ---------- proposed edits
    snapshot() {
      return JSON.stringify([...this.proposals.entries()]);
    }
    remember() {
      this.undoStack.push(this.snapshot());
      if (this.undoStack.length > 200) this.undoStack.shift();
      this.redoStack = [];
    }
    restore(state) {
      this.proposals = new Map(JSON.parse(state));
      this.panel();
      this.draw();
    }
    undo() {
      const state = this.undoStack.pop();
      if (state === void 0) return;
      this.redoStack.push(this.snapshot());
      this.restore(state);
    }
    redo() {
      const state = this.redoStack.pop();
      if (state === void 0) return;
      this.undoStack.push(this.snapshot());
      this.restore(state);
    }
    propose(name, change) {
      const widget = this.layout && named(this.layout, name);
      const next = { ...this.proposals.get(name) || {}, ...change };
      if (next.box && widget && next.box.every((value, index) => Math.abs(value - widget.box[index]) < 0.05)) delete next.box;
      if (next.text !== void 0 && widget && next.text === this.wordsOf(widget)) delete next.text;
      if (next.box || next.text !== void 0) this.proposals.set(name, next);
      else this.proposals.delete(name);
    }
    nudge(dx, dy) {
      const widget = this.primary();
      if (!widget) return;
      this.remember();
      const box = this.boxOf(widget);
      this.propose(widget.name, { box: [box[0] + dx, box[1] + dy, box[2] + dx, box[3] + dy] });
      this.panel();
      this.draw();
    }
    wordsOf(widget) {
      if (!this.layout) return "";
      return texts(this.layout).filter((text) => text.widget === widget.id).map((text) => text.text.trim()).filter(Boolean).join(" ");
    }
    /** the words of a label edited in place, as a proposal */
    editWords(widget) {
      const [vx, vy, vw, vh] = this.onViewport(this.boxOf(widget));
      const old = this.proposals.get(widget.name)?.text ?? this.wordsOf(widget);
      const input = h("input", { class: "word-editor", value: old, "aria-label": "The label's words" });
      Object.assign(input.style, {
        left: `${vx}px`,
        top: `${vy}px`,
        width: `${Math.max(vw, 80)}px`,
        height: `${Math.max(vh, 22)}px`,
        fontSize: `${Math.max(11, vh * 0.6)}px`
      });
      const finish = (keep) => {
        if (!input.isConnected) return;
        input.remove();
        if (keep && input.value !== old) {
          this.remember();
          this.propose(widget.name, { text: input.value });
          this.panel();
          this.draw();
        }
        this.el.viewport.focus();
      };
      input.addEventListener("keydown", (event) => {
        event.stopPropagation();
        if (event.key === "Enter") finish(true);
        if (event.key === "Escape") finish(false);
      });
      input.addEventListener("blur", () => finish(true));
      this.el.hud.append(input);
      input.focus();
      input.select();
    }
    /** the box a handle or a move makes of the box it started from, snapped to the others' edges and centres */
    dragged(start2, edge, dx, dy, widget) {
      const box = [...start2];
      let edges = "";
      if (edge === "move") {
        box[0] += dx;
        box[2] += dx;
        box[1] += dy;
        box[3] += dy;
        edges = "xyXY";
      } else {
        if (edge.includes("w")) {
          box[0] = Math.min(box[0] + dx, box[2] - 1);
          edges += "x";
        }
        if (edge.includes("e")) {
          box[2] = Math.max(box[2] + dx, box[0] + 1);
          edges += "X";
        }
        if (edge.includes("n")) {
          box[1] = Math.min(box[1] + dy, box[3] - 1);
          edges += "y";
        }
        if (edge.includes("s")) {
          box[3] = Math.max(box[3] + dy, box[1] + 1);
          edges += "Y";
        }
      }
      const others = widgets(this.layout).filter((other) => other.name !== widget.name && !screenWide(other) && !pathOf(this.layout, other).includes(widget.name) && other.box[2] > other.box[0]).map((other) => this.boxOf(other));
      const snapped = snap(box, others, 6 * this.unitsPerScreenPixel(), edges);
      snapped.box = snapped.box.map(round1);
      return snapped;
    }
    handleAt(clientX, clientY) {
      const widget = this.primary();
      if (!widget || this.mode !== "edit" || !this.layout) return null;
      const rect = this.el.viewport.getBoundingClientRect();
      const vx = clientX - rect.left;
      const vy = clientY - rect.top;
      const [x, y, w, hgt] = this.onViewport(this.boxOf(widget));
      const spots = {
        nw: [x, y],
        n: [x + w / 2, y],
        ne: [x + w, y],
        e: [x + w, y + hgt / 2],
        se: [x + w, y + hgt],
        s: [x + w / 2, y + hgt],
        sw: [x, y + hgt],
        w: [x, y + hgt / 2]
      };
      for (const [name, [sx, sy]] of Object.entries(spots)) if (Math.abs(vx - sx) <= 9 && Math.abs(vy - sy) <= 9) return name;
      return vx >= x && vx <= x + w && vy >= y && vy <= y + hgt ? "move" : null;
    }
    // ---------- pointers: mouse, pen and fingers alike
    bindPointers() {
      const view = this.el.viewport;
      view.addEventListener("pointerdown", (event) => this.down(event));
      view.addEventListener("pointermove", (event) => this.moved(event));
      view.addEventListener("pointerup", (event) => this.up(event));
      view.addEventListener("pointercancel", (event) => this.cancelPointer(event));
      view.addEventListener("pointerleave", () => {
        if (!this.pointers.size) {
          this.hover = null;
          this.draw();
        }
      });
      view.addEventListener("dblclick", (event) => this.doubleClick(event.clientX, event.clientY));
      view.addEventListener("contextmenu", (event) => event.preventDefault());
      view.addEventListener("wheel", (event) => {
        event.preventDefault();
        const rect = view.getBoundingClientRect();
        if (event.ctrlKey || event.metaKey) {
          this.zoomAt(this.zoom * Math.exp(-event.deltaY * 0.01), event.clientX - rect.left, event.clientY - rect.top);
        } else {
          this.x -= event.deltaX;
          this.y -= event.deltaY;
          this.placed = true;
          this.applyView();
        }
      }, { passive: false });
    }
    down(event) {
      this.el.viewport.focus({ preventScroll: true });
      try {
        this.el.viewport.setPointerCapture(event.pointerId);
      } catch {
      }
      this.pointers.set(event.pointerId, {
        x: event.clientX,
        y: event.clientY,
        startX: event.clientX,
        startY: event.clientY,
        time: performance.now(),
        type: event.pointerType
      });
      clearTimeout(this.longPress);
      if (this.pointers.size === 2) {
        const [a, b] = [...this.pointers.values()];
        const rect = this.el.viewport.getBoundingClientRect();
        this.gesture = "pinch";
        this.live = null;
        this.gestureStart = {
          x: (a.x + b.x) / 2 - rect.left,
          y: (a.y + b.y) / 2 - rect.top,
          zoom: this.zoom,
          px: this.x,
          py: this.y,
          distance: Math.hypot(a.x - b.x, a.y - b.y),
          edge: "",
          box: null
        };
        return;
      }
      if (this.pointers.size > 2) return;
      const [px, py] = this.picturePoint(event.clientX, event.clientY);
      const outside = px < 0 || py < 0 || px > this.size[0] || py > this.size[1];
      const handle = this.handleAt(event.clientX, event.clientY);
      const widget = this.primary();
      this.gestureStart = {
        x: event.clientX,
        y: event.clientY,
        zoom: this.zoom,
        px: this.x,
        py: this.y,
        distance: 0,
        edge: handle || "",
        box: widget ? this.boxOf(widget) : null
      };
      if (event.button === 1 || this.space || outside) this.gesture = "pan";
      else if (handle && widget) this.gesture = handle === "move" ? "move" : "resize";
      else if (this.mode === "comment") this.gesture = "comment";
      else this.gesture = "tap";
      if (event.pointerType === "touch" && (this.gesture === "tap" || this.gesture === "comment")) {
        this.longPress = window.setTimeout(() => {
          const pointer = this.pointers.get(event.pointerId);
          if (!pointer || this.gesture === "pan" || this.pointers.size !== 1) return;
          this.gesture = null;
          const [mx, my] = this.menuPoint(pointer.x, pointer.y);
          this.openComposer(this.layout ? widgetAt(this.layout, mx, my, this.shows) : null, mx, my);
        }, LONG_PRESS);
      }
    }
    moved(event) {
      const pointer = this.pointers.get(event.pointerId);
      this.alt = event.altKey;
      if (!pointer) {
        if (event.pointerType === "mouse" && this.layout && !this.space) {
          const [mx, my] = this.menuPoint(event.clientX, event.clientY);
          const found = widgetAt(this.layout, mx, my, this.shows);
          if (found?.name !== this.hover?.name) {
            this.hover = found;
            this.draw();
          } else if (this.alt) this.drawHud();
          const handle = this.handleAt(event.clientX, event.clientY);
          this.el.viewport.style.cursor = this.space ? "grab" : handle && handle !== "move" ? `${handle}-resize` : handle === "move" ? "move" : "";
        }
        return;
      }
      pointer.x = event.clientX;
      pointer.y = event.clientY;
      const start2 = this.gestureStart;
      if (!start2) return;
      if (this.gesture === "pinch" && this.pointers.size >= 2) {
        const [a, b] = [...this.pointers.values()];
        const rect = this.el.viewport.getBoundingClientRect();
        const mx = (a.x + b.x) / 2 - rect.left;
        const my = (a.y + b.y) / 2 - rect.top;
        const zoom = Math.min(ZOOM_MAX, Math.max(ZOOM_MIN, start2.zoom * (Math.hypot(a.x - b.x, a.y - b.y) / Math.max(start2.distance, 1))));
        this.x = mx - (start2.x - start2.px) * zoom / start2.zoom;
        this.y = my - (start2.y - start2.py) * zoom / start2.zoom;
        this.zoom = zoom;
        this.placed = true;
        this.applyView();
        return;
      }
      const dx = event.clientX - start2.x;
      const dy = event.clientY - start2.y;
      const far = Math.hypot(dx, dy) > (pointer.type === "touch" ? 8 : 4);
      if (far) clearTimeout(this.longPress);
      if (this.gesture === "tap" && far) this.gesture = "pan";
      if (this.gesture === "comment" && far) this.gesture = pointer.type === "touch" ? "pan" : "area";
      if (this.gesture === "pan") {
        this.x = start2.px + dx;
        this.y = start2.py + dy;
        this.placed = true;
        this.applyView();
      } else if ((this.gesture === "move" || this.gesture === "resize") && start2.box && far) {
        const widget = this.primary();
        const units = this.unitsPerScreenPixel();
        const result = this.dragged(start2.box, this.gesture === "move" ? "move" : start2.edge, dx * units, dy * units, widget);
        this.live = { name: widget.name, box: result.box };
        this.guides = result.guides;
        this.draw();
      } else if (this.gesture === "area") {
        const [ax, ay] = this.picturePoint(start2.x, start2.y);
        const [bx, by] = this.picturePoint(event.clientX, event.clientY);
        this.areaDrag = [Math.min(ax, bx), Math.min(ay, by), Math.max(ax, bx), Math.max(ay, by)];
        this.draw();
      }
    }
    up(event) {
      const pointer = this.pointers.get(event.pointerId);
      this.pointers.delete(event.pointerId);
      clearTimeout(this.longPress);
      if (!pointer) return;
      const gesture = this.gesture;
      if (this.pointers.size) {
        if (gesture === "pinch") {
          const [other] = [...this.pointers.values()];
          this.gesture = "pan";
          this.gestureStart = { x: other.x, y: other.y, zoom: this.zoom, px: this.x, py: this.y, distance: 0, edge: "", box: null };
        }
        return;
      }
      this.gesture = null;
      if (gesture === "tap" || gesture === "comment") {
        this.clickAt(event.clientX, event.clientY, event.shiftKey);
      } else if ((gesture === "move" || gesture === "resize") && this.live) {
        this.remember();
        this.propose(this.live.name, { box: this.live.box });
        this.live = null;
        this.guides = [];
        this.panel();
        this.draw();
      } else if (gesture === "move" || gesture === "resize") {
        this.clickAt(event.clientX, event.clientY, event.shiftKey);
      } else if (gesture === "area" && this.areaDrag && this.layout) {
        const [ax, ay, bx, by] = this.areaDrag;
        this.areaDrag = null;
        this.draw();
        if (bx - ax > 4 && by - ay > 4) this.openArea([ax, ay, bx, by]);
      }
    }
    cancelPointer(event) {
      this.pointers.delete(event.pointerId);
      clearTimeout(this.longPress);
      this.gesture = null;
      this.live = null;
      this.areaDrag = null;
      this.draw();
    }
    // ---------- keys
    key(event) {
      const target = event.target;
      if (target && (target.tagName === "INPUT" || target.tagName === "TEXTAREA" || target.tagName === "SELECT")) return;
      const command = event.metaKey || event.ctrlKey;
      if (event.key === "Alt") {
        this.alt = true;
        this.drawHud();
        return;
      }
      if (event.key === " ") {
        this.space = true;
        this.el.viewport.style.cursor = "grab";
        event.preventDefault();
        return;
      }
      if (command && event.key.toLowerCase() === "z") {
        event.preventDefault();
        if (event.shiftKey) this.redo();
        else this.undo();
        return;
      }
      if (command && event.key.toLowerCase() === "y") {
        event.preventDefault();
        this.redo();
        return;
      }
      if (command) return;
      const step = event.shiftKey ? 10 : 1;
      switch (event.key) {
        case "Escape":
          if (this.el.sheet.childElementCount) this.closeComposer();
          else this.toParent();
          break;
        case "v":
        case "V":
          this.setMode("select");
          break;
        case "e":
        case "E":
          this.setMode("edit");
          break;
        case "c":
        case "C":
          this.setMode("comment");
          break;
        case "b":
        case "B":
          this.toggleBefore();
          break;
        case "+":
        case "=":
          this.zoomBy(1.25);
          break;
        case "-":
        case "_":
          this.zoomBy(1 / 1.25);
          break;
        case "!":
          this.fit();
          break;
        case ")":
          this.actual();
          break;
        case "[":
        case "]": {
          const names = Object.keys(this.data.variants);
          const index = names.indexOf(this.variant) + (event.key === "]" ? 1 : -1);
          location.hash = this.href(names[(index + names.length) % names.length]);
          break;
        }
        case "j":
        case "J":
        case "PageDown":
          void this.step(1);
          break;
        case "k":
        case "K":
        case "PageUp":
          void this.step(-1);
          break;
        case "ArrowLeft":
          if (this.mode === "edit") this.nudge(-step, 0);
          else return;
          break;
        case "ArrowRight":
          if (this.mode === "edit") this.nudge(step, 0);
          else return;
          break;
        case "ArrowUp":
          if (this.mode === "edit") this.nudge(0, -step);
          else return;
          break;
        case "ArrowDown":
          if (this.mode === "edit") this.nudge(0, step);
          else return;
          break;
        case "Enter":
          if (this.primary() && this.mode === "edit") this.editWords(this.primary());
          else return;
          break;
        default:
          if (event.shiftKey && event.code === "Digit1") this.fit();
          else if (event.shiftKey && event.code === "Digit0") this.actual();
          else return;
      }
      event.preventDefault();
    }
    keyUp(event) {
      if (event.key === "Alt") {
        this.alt = false;
        this.drawHud();
      }
      if (event.key === " ") {
        this.space = false;
        this.el.viewport.style.cursor = "";
      }
    }
    // ---------- comments
    draftFor(widget, label, mx, my) {
      const draft = {
        run: this.run,
        screen: this.screenName,
        variant: this.variant,
        target: widget ? "widget" : "screen",
        widget: widget?.name ?? null,
        label
      };
      if (mx !== void 0 && my !== void 0 && this.layout) {
        const [px, py] = toPicture(this.layout, this.size, mx, my);
        draft.x = px;
        draft.y = py;
      }
      if (widget) {
        const proposal = this.proposals.get(widget.name);
        if (proposal) {
          draft.edits = {};
          if (proposal.box) draft.edits.box = { to: proposal.box };
          if (proposal.text !== void 0) draft.edits.text = { old: this.wordsOf(widget), new: proposal.text };
        }
      }
      return draft;
    }
    openComposer(widget, mx, my) {
      if (widget && !this.selected.includes(widget.name)) this.select([widget.name]);
      const label = widget ? shortName(widget.name) : "the screen as a whole";
      this.showComposer(this.draftFor(widget, label, mx, my));
    }
    openArea(area) {
      this.showComposer({
        run: this.run,
        screen: this.screenName,
        variant: this.variant,
        target: "area",
        widget: null,
        x: area[0],
        y: area[1],
        x1: area[2],
        y1: area[3],
        label: "an area"
      });
    }
    showComposer(draft) {
      this.el.sheet.replaceChildren(composer(draft, this.meta, (note) => {
        this.closeComposer();
        if (!note) return;
        this.notes.push(note);
        if (draft.widget && draft.edits) {
          this.proposals.delete(draft.widget);
        }
        this.tab = "comments";
        this.panel();
        this.draw();
      }));
      this.el.sheet.classList.add("open");
    }
    closeComposer() {
      this.el.sheet.replaceChildren();
      this.el.sheet.classList.remove("open");
      this.el.viewport.focus({ preventScroll: true });
    }
    /** the notes on this screen in this variant (or all its variants, or everywhere, by their scope) */
    screenNotes() {
      return this.notes.filter((note) => note.screen === this.screenName && (note.variant === this.variant || note.scope && note.scope !== "variant"));
    }
    // ---------- drawing
    draw() {
      const overlay = this.el.overlay;
      overlay.replaceChildren();
      if (!this.layout) {
        this.drawHud();
        return;
      }
      const rect = (box, kind) => {
        const [x, y, w, hgt] = boxOnPicture(this.layout, this.size, box);
        overlay.append(svg("rect", {
          x,
          y,
          width: Math.max(w, 0.5),
          height: Math.max(hgt, 0.5),
          class: kind,
          "vector-effect": "non-scaling-stroke"
        }));
      };
      for (const note of this.screenNotes()) {
        if (note.status === "closed") continue;
        if (note.area) rect(note.area, "note-area");
      }
      for (const [name, proposal] of this.proposals) {
        const widget = named(this.layout, name);
        if (widget && proposal.box && this.live?.name !== name) {
          rect(widget.box, "origin");
          rect(proposal.box, "proposed");
        }
      }
      if (this.live) {
        const widget = named(this.layout, this.live.name);
        if (widget) rect(widget.box, "origin");
        rect(this.live.box, "proposed live");
      }
      if (this.hover && !this.selected.includes(this.hover.name)) rect(this.boxOf(this.hover), "hover");
      for (const name of this.selected) {
        const widget = named(this.layout, name);
        if (widget) rect(this.boxOf(widget), "selected");
      }
      if (this.selected.length > 1) {
        const boxes = this.selected.map((name) => named(this.layout, name)).filter((w) => !!w).map((w) => this.boxOf(w));
        if (boxes.length) rect(union(boxes), "group");
      }
      for (const guide of this.guides) {
        const [ax, ay] = toPicture(
          this.layout,
          this.size,
          guide.axis === "x" ? guide.at : this.layout.screen[0],
          guide.axis === "y" ? guide.at : this.layout.screen[1]
        );
        const line = guide.axis === "x" ? { x1: ax, y1: 0, x2: ax, y2: this.size[1] } : { x1: 0, y1: ay, x2: this.size[0], y2: ay };
        overlay.append(svg("line", { ...line, class: "guide", "vector-effect": "non-scaling-stroke" }));
      }
      if (this.areaDrag) {
        const [x0, y0, x1, y1] = this.areaDrag;
        overlay.append(svg("rect", { x: x0, y: y0, width: x1 - x0, height: y1 - y0, class: "area-drag", "vector-effect": "non-scaling-stroke" }));
      }
      this.drawHud();
    }
    /** what is drawn at the screen's own size over the canvas: the handles, the readout, the redlines' numbers,
     * the notes' pins */
    drawHud() {
      const hud = this.el.hud;
      for (const child of [...hud.children]) if (!child.classList.contains("word-editor")) child.remove();
      if (!this.layout) return;
      const widget = this.primary();
      if (widget && this.mode === "edit") {
        const [x, y, w, hgt] = this.onViewport(this.boxOf(widget));
        const spots = {
          nw: [x, y],
          n: [x + w / 2, y],
          ne: [x + w, y],
          e: [x + w, y + hgt / 2],
          se: [x + w, y + hgt],
          s: [x + w / 2, y + hgt],
          sw: [x, y + hgt],
          w: [x, y + hgt / 2]
        };
        for (const name of HANDLES) {
          const [sx, sy] = spots[name];
          hud.append(h("div", { class: `handle handle-${name}`, style: `left:${sx - 4}px;top:${sy - 4}px` }));
        }
      }
      if (widget && (this.mode === "edit" || this.live || this.proposals.has(widget.name))) {
        const box = this.boxOf(widget);
        const [x, y, , hgt] = this.onViewport(box);
        const delta = this.live || this.proposals.get(widget.name)?.box ? ` \xB7 \u0394 ${signed(box[0] - widget.box[0])}, ${signed(box[1] - widget.box[1])}, ${signed(box[2] - box[0] - (widget.box[2] - widget.box[0]))} w, ${signed(box[3] - box[1] - (widget.box[3] - widget.box[1]))} h` : "";
        hud.append(h(
          "div",
          { class: "readout", style: `left:${x}px;top:${y + hgt + 6}px` },
          `${round1(box[0])}, ${round1(box[1])} \xB7 ${round1(box[2] - box[0])} \xD7 ${round1(box[3] - box[1])}${delta}`
        ));
      }
      if (widget && this.alt && this.hover && this.hover.name !== widget.name) {
        const lines = redlines(this.boxOf(widget), this.boxOf(this.hover));
        const overlay = this.el.overlay;
        for (const line of overlay.querySelectorAll(".redline")) line.remove();
        for (const red of lines) {
          const [ax, ay] = toPicture(this.layout, this.size, red.from[0], red.from[1]);
          const [bx, by] = toPicture(this.layout, this.size, red.to[0], red.to[1]);
          overlay.append(svg("line", { x1: ax, y1: ay, x2: bx, y2: by, class: "redline", "vector-effect": "non-scaling-stroke" }));
          hud.append(h(
            "div",
            { class: "redline-label", style: `left:${this.x + (ax + bx) / 2 * this.zoom}px;top:${this.y + (ay + by) / 2 * this.zoom}px` },
            String(round1(Math.abs(red.length)))
          ));
        }
      } else {
        for (const line of this.el.overlay.querySelectorAll(".redline")) line.remove();
      }
      const pinned = /* @__PURE__ */ new Map();
      for (const note of this.screenNotes()) {
        if (note.status === "closed" || !note.widget) continue;
        if (!pinned.has(note.widget)) pinned.set(note.widget, []);
        pinned.get(note.widget).push(note);
      }
      for (const [name, notes] of pinned) {
        const found = named(this.layout, name);
        if (!found) continue;
        const [x, y, w] = this.onViewport(found.box);
        hud.append(h("button", {
          class: `pin${notes.some((note) => note.status === "fixed" || note.status === "done") ? " ready" : ""}`,
          style: `left:${x + w - 10}px;top:${y - 10}px`,
          title: notes.map((note) => note.note).join("\n"),
          onclick: () => {
            this.tab = "comments";
            this.select([name]);
          }
        }, String(notes.length)));
      }
    }
    // ---------- the panel: layers, inspector, comments
    panel() {
      const tabs = [["layers", "Layers"], ["inspect", "Inspect"], ["comments", `Comments (${this.screenNotes().filter((note) => note.status !== "closed").length})`]];
      this.el.tabs.replaceChildren(...tabs.map(([tab, words]) => h("button", {
        class: tab === this.tab ? "on" : "",
        "data-tab": tab,
        onclick: () => {
          this.tab = tab;
          this.panel();
        }
      }, words)));
      if (this.tab === "layers") this.el.content.replaceChildren(this.layers());
      else if (this.tab === "comments") this.el.content.replaceChildren(this.comments());
      else this.el.content.replaceChildren(this.inspector());
      this.el.status.textContent = `${this.screenName} \xB7 ${this.variant} \xB7 ${this.mode}${this.proposals.size ? ` \xB7 ${this.proposals.size} edit${this.proposals.size > 1 ? "s" : ""} proposed` : ""}`;
    }
    layers() {
      const list = h("div", { class: "layers" });
      if (!this.layout) return list;
      const all = widgets(this.layout);
      const depth = /* @__PURE__ */ new Map();
      for (const widget of all) depth.set(widget.id, (depth.get(widget.parent) ?? -1) + 1);
      for (const widget of all) {
        const row = h("div", {
          class: `layer${this.selected.includes(widget.name) ? " selected" : ""}${this.proposals.has(widget.name) ? " edited" : ""}`,
          style: `padding-left:${8 + (depth.get(widget.id) || 0) * 12}px`,
          title: widget.name,
          "data-name": widget.name,
          onclick: (event) => {
            if (event.shiftKey) {
              const without = this.selected.filter((name) => name !== widget.name);
              this.select(without.length === this.selected.length ? [...this.selected, widget.name] : without);
            } else this.select([widget.name]);
          },
          onmouseenter: () => {
            this.hover = widget;
            this.draw();
          },
          onmouseleave: () => {
            this.hover = null;
            this.draw();
          }
        }, h("span", { class: "layer-type" }, (WIDGET_TYPES[widget.type] || "?").slice(0, 4)), shortName(widget.name));
        list.append(row);
      }
      return list;
    }
    inspector() {
      const box = h("div", { class: "inspector" });
      if (!this.layout) return h("div", { class: "inspector" }, h("p", { class: "dim" }, "This picture has no layout."));
      const widget = this.primary();
      if (!widget) {
        add(
          box,
          h("h3", {}, this.data.label),
          h("p", { class: "dim" }, `${this.data.name}`),
          h(
            "dl",
            {},
            h("dt", {}, "File"),
            h("dd", {}, this.data.file),
            h("dt", {}, "Generator"),
            h("dd", {}, this.data.generator || "by hand"),
            h("dt", {}, "Screen"),
            h("dd", {}, `${this.layout.screen[2] - this.layout.screen[0]} \xD7 480 units \xB7 ${this.size[0]} \xD7 ${this.size[1]} px`)
          ),
          this.data.rules.length ? h("div", {}, h("h4", {}, "Style rules here"), h("ul", {}, ...this.data.rules.map((rule) => h("li", {}, rule)))) : null,
          h("p", { class: "dim" }, "Click a widget to inspect it; double-click to go into it, Esc to go out; Shift-click adds. Alt shows distances. E edits, C comments."),
          h("button", { onclick: () => this.openComposer(null) }, "Comment on the screen")
        );
        return box;
      }
      if (this.selected.length > 1) {
        const boxes = this.selected.map((name) => named(this.layout, name)).filter((w) => !!w).map((w) => this.boxOf(w));
        const all = union(boxes);
        box.append(
          h("h3", {}, `${this.selected.length} widgets`),
          h("p", { class: "dim" }, this.selected.map(shortName).join(", ")),
          h("dl", {}, h("dt", {}, "Together"), h("dd", {}, `${round1(all[0])}, ${round1(all[1])} \xB7 ${round1(all[2] - all[0])} \xD7 ${round1(all[3] - all[1])}`))
        );
      }
      const current = this.boxOf(widget);
      const [px, py, pw, ph] = boxOnPicture(this.layout, this.size, current);
      const key = widget.name.replace(/^pc\\/, "").replace(/\\/g, "/");
      const source = this.data.widgets[key];
      const own = texts(this.layout).filter((text) => text.widget === widget.id && text.text.trim());
      const marks = glyphs(this.layout).filter((glyph) => glyph.widget === widget.id);
      const proposal = this.proposals.get(widget.name);
      const attributes = source?.attributes || {};
      const place = source?.place;
      add(
        box,
        h("h3", {}, shortName(widget.name)),
        h("p", { class: "dim path" }, pathOf(this.layout, widget).map(shortName).join(" \u203A ")),
        h(
          "dl",
          {},
          h("dt", {}, "Kind"),
          h("dd", {}, WIDGET_TYPES[widget.type] || String(widget.type)),
          h("dt", {}, "Box"),
          h("dd", {}, `x ${round1(current[0])}  y ${round1(current[1])}  w ${round1(current[2] - current[0])}  h ${round1(current[3] - current[1])}`),
          h("dt", {}, "Pixels"),
          h("dd", {}, `x ${Math.round(px)}  y ${Math.round(py)}  w ${Math.round(pw)}  h ${Math.round(ph)}`),
          attributes.font ? h("dt", {}, "Font") : null,
          attributes.font ? h("dd", {}, `${attributes.font}${attributes.color ? ` \xB7 ${attributes.color}` : ""}${attributes.align ? ` \xB7 ${attributes.align}` : ""}`) : null,
          attributes.bitmap ? h("dt", {}, "Picture") : null,
          attributes.bitmap ? h("dd", {}, attributes.bitmap) : null,
          own.length ? h("dt", {}, "Words") : null,
          own.length ? h("dd", {}, ...own.map((text) => h("div", { class: "words" }, `\u201C${text.text.trim()}\u201D${text.scale !== 1 ? ` (scaled ${text.scale})` : ""}`))) : null,
          marks.length ? h("dt", {}, "Glyphs") : null,
          marks.length ? h("dd", {}, marks.map((glyph) => `${glyph.key ? "key" : "button"} ${glyph.name}`).join(", ")) : null,
          h("dt", {}, "Source"),
          h(
            "dd",
            { class: "source" },
            place ? place.where : widget.source || "unknown",
            place && place.calls.length > 1 ? h("div", { class: "dim" }, place.calls.map((call2) => place.lines[call2] ? `${call2}:${place.lines[call2]}` : call2).join(" \u203A ")) : null,
            place?.tag ? h("div", { class: "dim" }, `from ${place.tag}`) : null
          ),
          h("dt", {}, "File"),
          h("dd", {}, source?.file || widget.file || "")
        ),
        proposal ? h(
          "div",
          { class: "proposal" },
          h("h4", {}, "Proposed"),
          proposal.box ? h("p", {}, `Box \u2192 ${proposal.box.map(round1).join(", ")} (\u0394 ${signed(proposal.box[0] - widget.box[0])}, ${signed(proposal.box[1] - widget.box[1])}, ${signed(proposal.box[2] - proposal.box[0] - (widget.box[2] - widget.box[0]))} w, ${signed(proposal.box[3] - proposal.box[1] - (widget.box[3] - widget.box[1]))} h)`) : null,
          proposal.text !== void 0 ? h("p", {}, `Words \u2192 \u201C${proposal.text}\u201D`) : null,
          h(
            "div",
            { class: "row" },
            h("button", { class: "primary", onclick: () => this.openComposer(widget) }, "Save as a note"),
            h("button", { onclick: () => {
              this.remember();
              this.proposals.delete(widget.name);
              this.panel();
              this.draw();
            } }, "Discard")
          )
        ) : null,
        h(
          "div",
          { class: "row" },
          h("button", { onclick: () => this.openComposer(widget) }, "Comment"),
          h("button", { onclick: () => this.setMode("edit"), title: "Drag or resize the box, arrows nudge it, double-click edits words" }, "Propose an edit"),
          h("button", { onclick: () => navigator.clipboard?.writeText(place?.where || widget.name) }, "Copy source")
        )
      );
      return box;
    }
    comments() {
      const host = h("div", { class: "comments" });
      const filter = h(
        "select",
        { "aria-label": "Status" },
        ...[["active", "Open and in progress"], ["fixed", "Ready for review"], ["closed", "Closed"], ["all", "All"]].map(([value, words]) => h("option", { value }, words))
      );
      const who = h("input", { placeholder: "Author", "aria-label": "Author" });
      const list = h("div", { class: "note-list" });
      const draw = () => {
        const notes = this.screenNotes().filter((note) => {
          const wanted = filter.value;
          const statusOk = wanted === "all" || (wanted === "active" ? ["open", "in_progress"].includes(note.status) : wanted === "fixed" ? ["fixed", "done"].includes(note.status) : note.status === wanted);
          return statusOk && (!who.value || (note.author || "studio").toLowerCase().includes(who.value.toLowerCase()));
        }).reverse();
        list.replaceChildren(...notes.map((note) => noteCard(note, {
          compact: true,
          onChanged: (changed) => {
            this.notes = this.notes.map((other) => other.id === changed.id ? { ...other, ...changed } : other);
            this.draw();
            draw();
            this.el.tabs.querySelector('[data-tab="comments"]').textContent = `Comments (${this.screenNotes().filter((each) => each.status !== "closed").length})`;
          },
          onSelect: (selected) => {
            if (selected.widget && this.layout && named(this.layout, selected.widget)) {
              this.selected = [selected.widget];
              this.draw();
            }
          }
        })));
        if (!notes.length) list.append(h("p", { class: "dim" }, "No notes here. C, then a click, leaves one."));
      };
      filter.addEventListener("input", draw);
      who.addEventListener("input", draw);
      host.append(h("div", { class: "filters" }, filter, who), list);
      draw();
      return host;
    }
  };
  function signed(value) {
    const rounded = round1(value);
    return (rounded > 0 ? "+" : "") + rounded;
  }

  // src/main.ts
  var meta;
  var screenView = null;
  var root = h("div", { id: "view" });
  function nav() {
    return h(
      "nav",
      { class: "top" },
      h("a", { href: "#/", class: "brand" }, "Menu review"),
      h("a", { href: "#/" }, "Gallery"),
      h("a", { href: "#/notes" }, "Notes")
    );
  }
  async function route() {
    const [path, search] = location.hash.slice(1).split("?");
    const parts = (path || "/").split("/").filter(Boolean).map(decodeURIComponent);
    const query = new URLSearchParams(search || "");
    document.body.classList.toggle("on-screen", parts[0] === "s");
    if (parts[0] === "s" && parts.length >= 3) {
      if (!screenView) screenView = new ScreenView(root, meta);
      await screenView.show(parts[1], parts[2], parts[3], query.get("note"));
      return;
    }
    screenView?.close();
    screenView = null;
    if (parts[0] === "notes") return notesView(root, meta, query);
    return galleryView(root, meta, parts[0] === "g" && parts[1] ? parts[1] : meta.runs[0]?.name || "latest");
  }
  async function start() {
    document.body.append(nav(), root);
    meta = await api.meta();
    if (!meta.runs.length) {
      root.replaceChildren(h(
        "main",
        { class: "page" },
        h("h1", {}, "No gallery yet"),
        h("p", {}, "Make one: python tools/menu_gallery.py")
      ));
      return;
    }
    window.addEventListener("hashchange", () => void route());
    await route();
  }
  window.menuReview = { view: () => screenView };
  void start();
})();
