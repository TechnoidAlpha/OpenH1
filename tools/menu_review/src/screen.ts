// A screen's inspector: its picture on a canvas to zoom and pan, the widgets selected from the layout (hover,
// click, double-click to drill in, Esc to the parent, shift to add), the layers, the inspector, redlines,
// proposed edits (move, resize with snapping, nudge, words in place, undo), comments pinned to widgets, and the
// before | after (side by side, swipe, onion skin).
import { api, Meta, Note, preload, ScreenData, VariantData } from "./api";
import { add, h, svg } from "./dom";
import {
  Box, Layout, Size, Widget, WIDGET_TYPES, boxOnPicture, childAt, contains, glyphs, named, parentOf, pathOf,
  pixelsPerUnit, redlines, round1, screenWide, sensible, shortName, snap, texts, toMenu, toPicture, union,
  widgetAt, widgets, Guide,
} from "./geometry";
import { composer, Draft, noteCard } from "./notes";

type Mode = "select" | "edit" | "comment";
type Compare = "after" | "side" | "swipe" | "onion";
type Gesture = null | "tap" | "pan" | "move" | "resize" | "comment" | "area" | "pinch";

interface Proposal {
  box?: Box;
  text?: string;
}

const HANDLES = ["nw", "n", "ne", "e", "se", "s", "sw", "w"];
const VARIANT_WORDS: Record<string, string> = {
  "keyboard-16x9": "Keys 16:9", "controller-16x10": "Pad 16:10", "touch-phone": "Touch", "keyboard-4x3": "Keys 4:3",
  "keyboard-21x9": "Keys 21:9",
};
const LONG_PRESS = 550;
const ZOOM_MIN = 0.05;
const ZOOM_MAX = 16;

export class ScreenView {
  private meta: Meta;
  private root: HTMLElement;
  private run = "";
  private screenName = "";
  private variant = "";
  private data!: ScreenData;
  private v!: VariantData;
  private layout: Layout | null = null;
  private size: Size = [1, 1];
  private shows = new Set<number>();
  private notes: Note[] = [];
  private screens: string[] = [];
  // the view: the picture's pixels at zoom, from the viewport's corner
  private zoom = 1;
  private x = 0;
  private y = 0;
  private placed = false;
  // the selection, by widgets' names (so that it follows them to another variant)
  private selected: string[] = [];
  private hover: Widget | null = null;
  private alt = false;
  private space = false;
  private mode: Mode = "select";
  private compare: Compare = "after";
  private showBefore = false;
  private swipe = 50;
  private onion = 0.5;
  // the edits proposed, by widget, and their history
  private proposals = new Map<string, Proposal>();
  private undoStack: string[] = [];
  private redoStack: string[] = [];
  private guides: Guide[] = [];
  private live: { name: string; box: Box } | null = null;
  private areaDrag: Box | null = null;
  // the pointers down, and what they are doing
  private pointers = new Map<number, { x: number; y: number; startX: number; startY: number; time: number; type: string }>();
  private gesture: Gesture = null;
  private gestureStart: { x: number; y: number; zoom: number; px: number; py: number; distance: number; edge: string; box: Box | null } | null = null;
  private longPress = 0;
  private tab = "inspect";
  private keys = (event: KeyboardEvent) => this.key(event);
  private keysUp = (event: KeyboardEvent) => this.keyUp(event);
  private resize = () => this.applyView();
  // the elements
  private el = {
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
    overlay: svg("svg", { class: "overlay" }) as SVGSVGElement,
    hud: h("div", { class: "hud" }),
    sheet: h("div", { class: "composer-host" }),
    panel: h("aside", { class: "panel" }),
    tabs: h("div", { class: "tabs" }),
    content: h("div", { class: "panel-content" }),
    status: h("div", { class: "statusbar dim" }),
  };

  constructor(root: HTMLElement, meta: Meta) {
    this.root = root;
    this.meta = meta;
    const e = this.el;
    e.stage.append(e.image, e.before, e.overlay);
    e.viewport.append(e.stage);
    e.sideStage.append(e.sideImage);
    e.side.append(e.sideStage);
    e.area.append(e.side, e.viewport, e.hud, e.sheet);
    e.panel.append(e.tabs, e.content);
    const bar = h("div", { class: "toolbar" },
      h("button", { title: "Previous screen (K)", onclick: () => this.step(-1) }, "‹"),
      e.title,
      h("button", { title: "Next screen (J)", onclick: () => this.step(1) }, "›"),
      e.variants, e.modes,
      h("div", { class: "segmented" },
        h("button", { title: "Zoom out (-)", onclick: () => this.zoomBy(1 / 1.25) }, "−"), e.zoomText,
        h("button", { title: "Zoom in (+)", onclick: () => this.zoomBy(1.25) }, "+")),
      e.compares, e.slider);
    e.zoomText.addEventListener("click", () => (this.zoom < this.fitZoom() * 1.01 ? this.actual() : this.fit()));
    e.slider.addEventListener("input", () => {
      if (this.compare === "swipe") this.swipe = Number(e.slider.value);
      else this.onion = Number(e.slider.value) / 100;
      this.applyCompare();
    });
    e.view.append(bar, h("div", { class: "workspace" }, e.area, e.panel), e.status);
    this.bindPointers();
  }

  // ---------- showing a screen

  async show(run: string, screen: string, variant?: string, note?: string | null): Promise<void> {
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
    const pictured = new Set(Object.entries(data.widgets).filter(([, found]) => found.attributes.bitmap)
      .map(([name]) => "pc\\" + name.replace(/\//g, "\\")));
    this.shows = this.layout ? sensible(this.layout, pictured) : new Set();
    // (the selection's names kept: those this variant has)
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
    // (the view kept across variants: the same place in the menus at the same size)
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
  clientPoint(mx: number, my: number): [number, number] {
    const rect = this.el.viewport.getBoundingClientRect();
    const [px, py] = toPicture(this.layout!, this.size, mx, my);
    return [rect.left + this.x + px * this.zoom, rect.top + this.y + py * this.zoom];
  }

  close(): void {
    document.removeEventListener("keydown", this.keys);
    document.removeEventListener("keyup", this.keysUp);
    window.removeEventListener("resize", this.resize);
  }

  private href(variant = this.variant, screen = this.screenName): string {
    return `#/s/${encodeURIComponent(this.run)}/${encodeURIComponent(screen)}/${variant}`;
  }

  private async prefetch(): Promise<void> {
    if (!this.screens.length) this.screens = (await api.screens(this.run)).screens.map((screen) => screen.name);
    for (const variant of Object.values(this.data.variants)) preload(variant.image);
    const index = this.screens.indexOf(this.screenName);
    for (const neighbour of [this.screens[index - 1], this.screens[index + 1]]) {
      if (!neighbour) continue;
      api.screen(this.run, neighbour).then((data) => preload((data.variants[this.variant] || Object.values(data.variants)[0])?.image))
        .catch(() => undefined);
    }
  }

  private async step(by: number): Promise<void> {
    if (!this.screens.length) this.screens = (await api.screens(this.run)).screens.map((screen) => screen.name);
    const index = this.screens.indexOf(this.screenName);
    const next = this.screens[(index + by + this.screens.length) % this.screens.length];
    if (next) location.hash = this.href(this.variant, next);
  }

  private toolbar(): void {
    const e = this.el;
    e.title.replaceChildren(h("b", {}, this.data.label), h("span", { class: "dim" }, ` ${this.data.file}`));
    e.variants.replaceChildren(...Object.keys(this.data.variants).map((variant) => h("a", {
      href: this.href(variant), class: variant === this.variant ? "on" : "", title: `${variant} ([ and ])`, "data-variant": variant,
    }, VARIANT_WORDS[variant] || variant)));
    const modes: Array<[Mode, string, string]> = [["select", "Select", "V"], ["edit", "Edit", "E"], ["comment", "Comment", "C"]];
    e.modes.replaceChildren(...modes.map(([mode, words, key]) => h("button", {
      class: mode === this.mode ? "on" : "", "data-mode": mode, title: `${words} (${key})`, onclick: () => this.setMode(mode),
    }, words)));
    const compares: Array<[Compare, string]> = [["after", "After"], ["side", "Side by side"], ["swipe", "Swipe"], ["onion", "Onion"]];
    e.compares.hidden = !this.v.before;
    e.compares.replaceChildren(...compares.map(([compare, words]) => h("button", {
      class: compare === this.compare ? "on" : "", "data-compare": compare, onclick: () => {
        this.compare = compare;
        this.toolbar();
        this.applyCompare();
      },
    }, words)), h("button", { class: this.showBefore ? "on" : "", title: "Before (B)", onclick: () => this.toggleBefore() }, "B"));
    e.slider.hidden = !(this.v.before && (this.compare === "swipe" || this.compare === "onion"));
    e.slider.value = String(this.compare === "swipe" ? this.swipe : Math.round(this.onion * 100));
  }

  private setMode(mode: Mode): void {
    this.mode = this.mode === mode && mode !== "select" ? "select" : mode;
    this.el.viewport.dataset.mode = this.mode;
    this.toolbar();
    this.draw();
  }

  private toggleBefore(): void {
    if (!this.v.before) return;
    this.showBefore = !this.showBefore;
    this.toolbar();
    this.applyCompare();
  }

  private applyCompare(): void {
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

  private fitZoom(): number {
    const rect = this.el.viewport.getBoundingClientRect();
    return Math.max(ZOOM_MIN, Math.min(rect.width / this.size[0], rect.height / this.size[1]) * 0.96);
  }

  fit(): void {
    const rect = this.el.viewport.getBoundingClientRect();
    this.zoom = this.fitZoom();
    this.x = (rect.width - this.size[0] * this.zoom) / 2;
    this.y = (rect.height - this.size[1] * this.zoom) / 2;
    this.placed = true;
    this.el.viewport.dataset.placed = "fit";
    this.applyView();
  }

  private actual(): void {
    this.zoomAt(1, this.el.viewport.clientWidth / 2, this.el.viewport.clientHeight / 2);
  }

  private zoomBy(factor: number): void {
    this.zoomAt(this.zoom * factor, this.el.viewport.clientWidth / 2, this.el.viewport.clientHeight / 2);
  }

  /** zoomed about a point of the viewport, which stays where it is */
  private zoomAt(zoom: number, vx: number, vy: number): void {
    const next = Math.min(ZOOM_MAX, Math.max(ZOOM_MIN, zoom));
    this.x = vx - ((vx - this.x) * next) / this.zoom;
    this.y = vy - ((vy - this.y) * next) / this.zoom;
    this.zoom = next;
    this.placed = true;
    this.applyView();
  }

  private applyView(): void {
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

  private centreInMenu(): [number, number] {
    const rect = this.el.viewport.getBoundingClientRect();
    const px = (rect.width / 2 - this.x) / this.zoom;
    const py = (rect.height / 2 - this.y) / this.zoom;
    return this.layout ? toMenu(this.layout, this.size, px, py) : [320, 240];
  }

  /** a client point as the picture's pixels */
  private picturePoint(clientX: number, clientY: number): [number, number] {
    const rect = this.el.viewport.getBoundingClientRect();
    return [(clientX - rect.left - this.x) / this.zoom, (clientY - rect.top - this.y) / this.zoom];
  }

  private menuPoint(clientX: number, clientY: number): [number, number] {
    const [px, py] = this.picturePoint(clientX, clientY);
    return this.layout ? toMenu(this.layout, this.size, px, py) : [px, py];
  }

  /** a box in the menus' units in the viewport's pixels: [x, y, width, height] */
  private onViewport(box: Box): Box {
    const [px, py, pw, ph] = boxOnPicture(this.layout!, this.size, box);
    return [this.x + px * this.zoom, this.y + py * this.zoom, pw * this.zoom, ph * this.zoom];
  }

  private unitsPerScreenPixel(): number {
    return this.layout ? 1 / (pixelsPerUnit(this.layout, this.size) * this.zoom) : 1;
  }

  // ---------- the selection

  private primary(): Widget | null {
    const name = this.selected[this.selected.length - 1];
    return name && this.layout ? named(this.layout, name) : null;
  }

  private boxOf(widget: Widget): Box {
    return this.live && this.live.name === widget.name ? this.live.box : this.proposals.get(widget.name)?.box || widget.box;
  }

  select(names: string[]): void {
    this.selected = names;
    this.panel();
    this.draw();
    this.el.content.querySelector<HTMLElement>(".layer.selected")?.scrollIntoView({ block: "nearest" });
  }

  private clickAt(clientX: number, clientY: number, shift: boolean): void {
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

  private doubleClick(clientX: number, clientY: number): void {
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

  private toParent(): void {
    const current = this.primary();
    if (!current || !this.layout) return this.select([]);
    const parent = parentOf(this.layout, current);
    this.select(parent ? [parent.name] : []);
  }

  // ---------- proposed edits

  private snapshot(): string {
    return JSON.stringify([...this.proposals.entries()]);
  }

  private remember(): void {
    this.undoStack.push(this.snapshot());
    if (this.undoStack.length > 200) this.undoStack.shift();
    this.redoStack = [];
  }

  private restore(state: string): void {
    this.proposals = new Map(JSON.parse(state));
    this.panel();
    this.draw();
  }

  private undo(): void {
    const state = this.undoStack.pop();
    if (state === undefined) return;
    this.redoStack.push(this.snapshot());
    this.restore(state);
  }

  private redo(): void {
    const state = this.redoStack.pop();
    if (state === undefined) return;
    this.undoStack.push(this.snapshot());
    this.restore(state);
  }

  private propose(name: string, change: Proposal): void {
    const widget = this.layout && named(this.layout, name);
    const next = { ...(this.proposals.get(name) || {}), ...change };
    if (next.box && widget && next.box.every((value, index) => Math.abs(value - widget.box[index]) < 0.05)) delete next.box;
    if (next.text !== undefined && widget && next.text === this.wordsOf(widget)) delete next.text;
    if (next.box || next.text !== undefined) this.proposals.set(name, next);
    else this.proposals.delete(name);
  }

  private nudge(dx: number, dy: number): void {
    const widget = this.primary();
    if (!widget) return;
    this.remember();
    const box = this.boxOf(widget);
    this.propose(widget.name, { box: [box[0] + dx, box[1] + dy, box[2] + dx, box[3] + dy] });
    this.panel();
    this.draw();
  }

  private wordsOf(widget: Widget): string {
    if (!this.layout) return "";
    return texts(this.layout).filter((text) => text.widget === widget.id).map((text) => text.text.trim()).filter(Boolean).join(" ");
  }

  /** the words of a label edited in place, as a proposal */
  private editWords(widget: Widget): void {
    const [vx, vy, vw, vh] = this.onViewport(this.boxOf(widget));
    const old = this.proposals.get(widget.name)?.text ?? this.wordsOf(widget);
    const input = h("input", { class: "word-editor", value: old, "aria-label": "The label's words" });
    Object.assign(input.style, { left: `${vx}px`, top: `${vy}px`, width: `${Math.max(vw, 80)}px`, height: `${Math.max(vh, 22)}px`,
      fontSize: `${Math.max(11, vh * 0.6)}px` });
    const finish = (keep: boolean) => {
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
  private dragged(start: Box, edge: string, dx: number, dy: number, widget: Widget): { box: Box; guides: Guide[] } {
    const box: Box = [...start] as Box;
    let edges = "";
    if (edge === "move") {
      box[0] += dx; box[2] += dx; box[1] += dy; box[3] += dy;
      edges = "xyXY";
    } else {
      if (edge.includes("w")) { box[0] = Math.min(box[0] + dx, box[2] - 1); edges += "x"; }
      if (edge.includes("e")) { box[2] = Math.max(box[2] + dx, box[0] + 1); edges += "X"; }
      if (edge.includes("n")) { box[1] = Math.min(box[1] + dy, box[3] - 1); edges += "y"; }
      if (edge.includes("s")) { box[3] = Math.max(box[3] + dy, box[1] + 1); edges += "Y"; }
    }
    const others = widgets(this.layout!).filter((other) => other.name !== widget.name && !screenWide(other) &&
      !pathOf(this.layout!, other).includes(widget.name) && other.box[2] > other.box[0]).map((other) => this.boxOf(other));
    const snapped = snap(box, others, 6 * this.unitsPerScreenPixel(), edges);
    snapped.box = snapped.box.map(round1) as Box;
    return snapped;
  }

  private handleAt(clientX: number, clientY: number): string | null {
    const widget = this.primary();
    if (!widget || this.mode !== "edit" || !this.layout) return null;
    const rect = this.el.viewport.getBoundingClientRect();
    const vx = clientX - rect.left;
    const vy = clientY - rect.top;
    const [x, y, w, hgt] = this.onViewport(this.boxOf(widget));
    const spots: Record<string, [number, number]> = {
      nw: [x, y], n: [x + w / 2, y], ne: [x + w, y], e: [x + w, y + hgt / 2], se: [x + w, y + hgt], s: [x + w / 2, y + hgt],
      sw: [x, y + hgt], w: [x, y + hgt / 2],
    };
    for (const [name, [sx, sy]] of Object.entries(spots)) if (Math.abs(vx - sx) <= 9 && Math.abs(vy - sy) <= 9) return name;
    return vx >= x && vx <= x + w && vy >= y && vy <= y + hgt ? "move" : null;
  }

  // ---------- pointers: mouse, pen and fingers alike

  private bindPointers(): void {
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

  private down(event: PointerEvent): void {
    this.el.viewport.focus({ preventScroll: true });
    try {
      this.el.viewport.setPointerCapture(event.pointerId);
    } catch {
      /* (a pointer the browser does not know as down: an event made by a test) */
    }
    this.pointers.set(event.pointerId, { x: event.clientX, y: event.clientY, startX: event.clientX, startY: event.clientY,
      time: performance.now(), type: event.pointerType });
    clearTimeout(this.longPress);
    if (this.pointers.size === 2) {
      const [a, b] = [...this.pointers.values()];
      const rect = this.el.viewport.getBoundingClientRect();
      this.gesture = "pinch";
      this.live = null;
      this.gestureStart = { x: (a.x + b.x) / 2 - rect.left, y: (a.y + b.y) / 2 - rect.top, zoom: this.zoom, px: this.x, py: this.y,
        distance: Math.hypot(a.x - b.x, a.y - b.y), edge: "", box: null };
      return;
    }
    if (this.pointers.size > 2) return;
    const [px, py] = this.picturePoint(event.clientX, event.clientY);
    const outside = px < 0 || py < 0 || px > this.size[0] || py > this.size[1];
    const handle = this.handleAt(event.clientX, event.clientY);
    const widget = this.primary();
    this.gestureStart = { x: event.clientX, y: event.clientY, zoom: this.zoom, px: this.x, py: this.y, distance: 0,
      edge: handle || "", box: widget ? this.boxOf(widget) : null };
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

  private moved(event: PointerEvent): void {
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
    const start = this.gestureStart;
    if (!start) return;
    if (this.gesture === "pinch" && this.pointers.size >= 2) {
      const [a, b] = [...this.pointers.values()];
      const rect = this.el.viewport.getBoundingClientRect();
      const mx = (a.x + b.x) / 2 - rect.left;
      const my = (a.y + b.y) / 2 - rect.top;
      const zoom = Math.min(ZOOM_MAX, Math.max(ZOOM_MIN, start.zoom * (Math.hypot(a.x - b.x, a.y - b.y) / Math.max(start.distance, 1))));
      this.x = mx - ((start.x - start.px) * zoom) / start.zoom;
      this.y = my - ((start.y - start.py) * zoom) / start.zoom;
      this.zoom = zoom;
      this.placed = true;
      this.applyView();
      return;
    }
    const dx = event.clientX - start.x;
    const dy = event.clientY - start.y;
    const far = Math.hypot(dx, dy) > (pointer.type === "touch" ? 8 : 4);
    if (far) clearTimeout(this.longPress);
    if (this.gesture === "tap" && far) this.gesture = "pan";
    if (this.gesture === "comment" && far) this.gesture = pointer.type === "touch" ? "pan" : "area";
    if (this.gesture === "pan") {
      this.x = start.px + dx;
      this.y = start.py + dy;
      this.placed = true;
      this.applyView();
    } else if ((this.gesture === "move" || this.gesture === "resize") && start.box && far) {
      const widget = this.primary()!;
      const units = this.unitsPerScreenPixel();
      const result = this.dragged(start.box, this.gesture === "move" ? "move" : start.edge, dx * units, dy * units, widget);
      this.live = { name: widget.name, box: result.box };
      this.guides = result.guides;
      this.draw();
    } else if (this.gesture === "area") {
      const [ax, ay] = this.picturePoint(start.x, start.y);
      const [bx, by] = this.picturePoint(event.clientX, event.clientY);
      this.areaDrag = [Math.min(ax, bx), Math.min(ay, by), Math.max(ax, bx), Math.max(ay, by)];
      this.draw();
    }
  }

  private up(event: PointerEvent): void {
    const pointer = this.pointers.get(event.pointerId);
    this.pointers.delete(event.pointerId);
    clearTimeout(this.longPress);
    if (!pointer) return;
    const gesture = this.gesture;
    if (this.pointers.size) {
      // (one finger of a pinch lifted: the other pans from here)
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
      // (a click on the selection in Edit: the selection under it)
      this.clickAt(event.clientX, event.clientY, event.shiftKey);
    } else if (gesture === "area" && this.areaDrag && this.layout) {
      const [ax, ay, bx, by] = this.areaDrag;
      this.areaDrag = null;
      this.draw();
      if (bx - ax > 4 && by - ay > 4) this.openArea([ax, ay, bx, by]);
    }
  }

  private cancelPointer(event: PointerEvent): void {
    this.pointers.delete(event.pointerId);
    clearTimeout(this.longPress);
    this.gesture = null;
    this.live = null;
    this.areaDrag = null;
    this.draw();
  }

  // ---------- keys

  private key(event: KeyboardEvent): void {
    const target = event.target as HTMLElement;
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
      case "v": case "V": this.setMode("select"); break;
      case "e": case "E": this.setMode("edit"); break;
      case "c": case "C": this.setMode("comment"); break;
      case "b": case "B": this.toggleBefore(); break;
      case "+": case "=": this.zoomBy(1.25); break;
      case "-": case "_": this.zoomBy(1 / 1.25); break;
      case "!": this.fit(); break;
      case ")": this.actual(); break;
      case "[": case "]": {
        const names = Object.keys(this.data.variants);
        const index = names.indexOf(this.variant) + (event.key === "]" ? 1 : -1);
        location.hash = this.href(names[(index + names.length) % names.length]);
        break;
      }
      case "j": case "J": case "PageDown": void this.step(1); break;
      case "k": case "K": case "PageUp": void this.step(-1); break;
      case "ArrowLeft": if (this.mode === "edit") this.nudge(-step, 0); else return; break;
      case "ArrowRight": if (this.mode === "edit") this.nudge(step, 0); else return; break;
      case "ArrowUp": if (this.mode === "edit") this.nudge(0, -step); else return; break;
      case "ArrowDown": if (this.mode === "edit") this.nudge(0, step); else return; break;
      case "Enter":
        if (this.primary() && this.mode === "edit") this.editWords(this.primary()!);
        else return;
        break;
      default:
        // (Shift+1 and Shift+0 by the key's code, on any layout)
        if (event.shiftKey && event.code === "Digit1") this.fit();
        else if (event.shiftKey && event.code === "Digit0") this.actual();
        else return;
    }
    event.preventDefault();
  }

  private keyUp(event: KeyboardEvent): void {
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

  private draftFor(widget: Widget | null, label: string, mx?: number, my?: number): Draft {
    const draft: Draft = { run: this.run, screen: this.screenName, variant: this.variant, target: widget ? "widget" : "screen",
      widget: widget?.name ?? null, label };
    if (mx !== undefined && my !== undefined && this.layout) {
      const [px, py] = toPicture(this.layout, this.size, mx, my);
      draft.x = px;
      draft.y = py;
    }
    if (widget) {
      const proposal = this.proposals.get(widget.name);
      if (proposal) {
        draft.edits = {};
        if (proposal.box) draft.edits.box = { to: proposal.box };
        if (proposal.text !== undefined) draft.edits.text = { old: this.wordsOf(widget), new: proposal.text };
      }
    }
    return draft;
  }

  openComposer(widget: Widget | null, mx?: number, my?: number): void {
    if (widget && !this.selected.includes(widget.name)) this.select([widget.name]);
    const label = widget ? shortName(widget.name) : "the screen as a whole";
    this.showComposer(this.draftFor(widget, label, mx, my));
  }

  private openArea(area: Box): void {
    this.showComposer({ run: this.run, screen: this.screenName, variant: this.variant, target: "area", widget: null,
      x: area[0], y: area[1], x1: area[2], y1: area[3], label: "an area" });
  }

  private showComposer(draft: Draft): void {
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

  private closeComposer(): void {
    this.el.sheet.replaceChildren();
    this.el.sheet.classList.remove("open");
    this.el.viewport.focus({ preventScroll: true });
  }

  /** the notes on this screen in this variant (or all its variants, or everywhere, by their scope) */
  private screenNotes(): Note[] {
    return this.notes.filter((note) => note.screen === this.screenName &&
      (note.variant === this.variant || (note.scope && note.scope !== "variant")));
  }

  // ---------- drawing

  draw(): void {
    const overlay = this.el.overlay;
    overlay.replaceChildren();
    if (!this.layout) {
      this.drawHud();
      return;
    }
    const rect = (box: Box, kind: string) => {
      const [x, y, w, hgt] = boxOnPicture(this.layout!, this.size, box);
      overlay.append(svg("rect", { x, y, width: Math.max(w, 0.5), height: Math.max(hgt, 0.5), class: kind,
        "vector-effect": "non-scaling-stroke" }));
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
      const boxes = this.selected.map((name) => named(this.layout!, name)).filter((w): w is Widget => !!w).map((w) => this.boxOf(w));
      if (boxes.length) rect(union(boxes), "group");
    }
    for (const guide of this.guides) {
      const [ax, ay] = toPicture(this.layout, this.size, guide.axis === "x" ? guide.at : this.layout.screen[0],
        guide.axis === "y" ? guide.at : this.layout.screen[1]);
      const line = guide.axis === "x"
        ? { x1: ax, y1: 0, x2: ax, y2: this.size[1] } : { x1: 0, y1: ay, x2: this.size[0], y2: ay };
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
  private drawHud(): void {
    const hud = this.el.hud;
    for (const child of [...hud.children]) if (!child.classList.contains("word-editor")) child.remove();
    if (!this.layout) return;
    const widget = this.primary();
    if (widget && this.mode === "edit") {
      const [x, y, w, hgt] = this.onViewport(this.boxOf(widget));
      const spots: Record<string, [number, number]> = {
        nw: [x, y], n: [x + w / 2, y], ne: [x + w, y], e: [x + w, y + hgt / 2], se: [x + w, y + hgt], s: [x + w / 2, y + hgt],
        sw: [x, y + hgt], w: [x, y + hgt / 2],
      };
      for (const name of HANDLES) {
        const [sx, sy] = spots[name];
        hud.append(h("div", { class: `handle handle-${name}`, style: `left:${sx - 4}px;top:${sy - 4}px` }));
      }
    }
    if (widget && (this.mode === "edit" || this.live || this.proposals.has(widget.name))) {
      const box = this.boxOf(widget);
      const [x, y, , hgt] = this.onViewport(box);
      const delta = this.live || this.proposals.get(widget.name)?.box
        ? ` · Δ ${signed(box[0] - widget.box[0])}, ${signed(box[1] - widget.box[1])}, ${signed(box[2] - box[0] - (widget.box[2] - widget.box[0]))} w, ${signed(box[3] - box[1] - (widget.box[3] - widget.box[1]))} h`
        : "";
      hud.append(h("div", { class: "readout", style: `left:${x}px;top:${y + hgt + 6}px` },
        `${round1(box[0])}, ${round1(box[1])} · ${round1(box[2] - box[0])} × ${round1(box[3] - box[1])}${delta}`));
    }
    if (widget && this.alt && this.hover && this.hover.name !== widget.name) {
      const lines = redlines(this.boxOf(widget), this.boxOf(this.hover));
      const overlay = this.el.overlay;
      for (const line of overlay.querySelectorAll(".redline")) line.remove();
      for (const red of lines) {
        const [ax, ay] = toPicture(this.layout, this.size, red.from[0], red.from[1]);
        const [bx, by] = toPicture(this.layout, this.size, red.to[0], red.to[1]);
        overlay.append(svg("line", { x1: ax, y1: ay, x2: bx, y2: by, class: "redline", "vector-effect": "non-scaling-stroke" }));
        hud.append(h("div", { class: "redline-label", style: `left:${this.x + ((ax + bx) / 2) * this.zoom}px;top:${this.y + ((ay + by) / 2) * this.zoom}px` },
          String(round1(Math.abs(red.length)))));
      }
    } else {
      for (const line of this.el.overlay.querySelectorAll(".redline")) line.remove();
    }
    // (the notes' pins, on their widgets: they follow them to another variant)
    const pinned = new Map<string, Note[]>();
    for (const note of this.screenNotes()) {
      if (note.status === "closed" || !note.widget) continue;
      if (!pinned.has(note.widget)) pinned.set(note.widget, []);
      pinned.get(note.widget)!.push(note);
    }
    for (const [name, notes] of pinned) {
      const found = named(this.layout, name);
      if (!found) continue;
      const [x, y, w] = this.onViewport(found.box);
      hud.append(h("button", { class: `pin${notes.some((note) => note.status === "fixed" || note.status === "done") ? " ready" : ""}`,
        style: `left:${x + w - 10}px;top:${y - 10}px`, title: notes.map((note) => note.note).join("\n"),
        onclick: () => { this.tab = "comments"; this.select([name]); } }, String(notes.length)));
    }
  }

  // ---------- the panel: layers, inspector, comments

  panel(): void {
    const tabs: Array<[string, string]> = [["layers", "Layers"], ["inspect", "Inspect"], ["comments", `Comments (${this.screenNotes().filter((note) => note.status !== "closed").length})`]];
    this.el.tabs.replaceChildren(...tabs.map(([tab, words]) => h("button", { class: tab === this.tab ? "on" : "", "data-tab": tab,
      onclick: () => { this.tab = tab; this.panel(); } }, words)));
    if (this.tab === "layers") this.el.content.replaceChildren(this.layers());
    else if (this.tab === "comments") this.el.content.replaceChildren(this.comments());
    else this.el.content.replaceChildren(this.inspector());
    this.el.status.textContent = `${this.screenName} · ${this.variant} · ${this.mode}${this.proposals.size ? ` · ${this.proposals.size} edit${this.proposals.size > 1 ? "s" : ""} proposed` : ""}`;
  }

  private layers(): HTMLElement {
    const list = h("div", { class: "layers" });
    if (!this.layout) return list;
    const all = widgets(this.layout);
    const depth = new Map<number, number>();
    for (const widget of all) depth.set(widget.id, (depth.get(widget.parent) ?? -1) + 1);
    for (const widget of all) {
      const row = h("div", {
        class: `layer${this.selected.includes(widget.name) ? " selected" : ""}${this.proposals.has(widget.name) ? " edited" : ""}`,
        style: `padding-left:${8 + (depth.get(widget.id) || 0) * 12}px`, title: widget.name, "data-name": widget.name,
        onclick: (event: MouseEvent) => {
          if (event.shiftKey) {
            const without = this.selected.filter((name) => name !== widget.name);
            this.select(without.length === this.selected.length ? [...this.selected, widget.name] : without);
          } else this.select([widget.name]);
        },
        onmouseenter: () => { this.hover = widget; this.draw(); },
        onmouseleave: () => { this.hover = null; this.draw(); },
      }, h("span", { class: "layer-type" }, (WIDGET_TYPES[widget.type] || "?").slice(0, 4)), shortName(widget.name));
      list.append(row);
    }
    return list;
  }

  private inspector(): HTMLElement {
    const box = h("div", { class: "inspector" });
    if (!this.layout) return h("div", { class: "inspector" }, h("p", { class: "dim" }, "This picture has no layout."));
    const widget = this.primary();
    if (!widget) {
      add(box, h("h3", {}, this.data.label), h("p", { class: "dim" }, `${this.data.name}`),
        h("dl", {}, h("dt", {}, "File"), h("dd", {}, this.data.file), h("dt", {}, "Generator"), h("dd", {}, this.data.generator || "by hand"),
          h("dt", {}, "Screen"), h("dd", {}, `${this.layout.screen[2] - this.layout.screen[0]} × 480 units · ${this.size[0]} × ${this.size[1]} px`)),
        this.data.rules.length ? h("div", {}, h("h4", {}, "Style rules here"), h("ul", {}, ...this.data.rules.map((rule) => h("li", {}, rule)))) : null,
        h("p", { class: "dim" }, "Click a widget to inspect it; double-click to go into it, Esc to go out; Shift-click adds. Alt shows distances. E edits, C comments."),
        h("button", { onclick: () => this.openComposer(null) }, "Comment on the screen"));
      return box;
    }
    if (this.selected.length > 1) {
      const boxes = this.selected.map((name) => named(this.layout!, name)).filter((w): w is Widget => !!w).map((w) => this.boxOf(w));
      const all = union(boxes);
      box.append(h("h3", {}, `${this.selected.length} widgets`), h("p", { class: "dim" }, this.selected.map(shortName).join(", ")),
        h("dl", {}, h("dt", {}, "Together"), h("dd", {}, `${round1(all[0])}, ${round1(all[1])} · ${round1(all[2] - all[0])} × ${round1(all[3] - all[1])}`)));
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
    add(box,
      h("h3", {}, shortName(widget.name)),
      h("p", { class: "dim path" }, pathOf(this.layout, widget).map(shortName).join(" › ")),
      h("dl", {},
        h("dt", {}, "Kind"), h("dd", {}, WIDGET_TYPES[widget.type] || String(widget.type)),
        h("dt", {}, "Box"), h("dd", {}, `x ${round1(current[0])}  y ${round1(current[1])}  w ${round1(current[2] - current[0])}  h ${round1(current[3] - current[1])}`),
        h("dt", {}, "Pixels"), h("dd", {}, `x ${Math.round(px)}  y ${Math.round(py)}  w ${Math.round(pw)}  h ${Math.round(ph)}`),
        attributes.font ? h("dt", {}, "Font") : null, attributes.font ? h("dd", {}, `${attributes.font}${attributes.color ? ` · ${attributes.color}` : ""}${attributes.align ? ` · ${attributes.align}` : ""}`) : null,
        attributes.bitmap ? h("dt", {}, "Picture") : null, attributes.bitmap ? h("dd", {}, attributes.bitmap) : null,
        own.length ? h("dt", {}, "Words") : null, own.length ? h("dd", {}, ...own.map((text) => h("div", { class: "words" }, `“${text.text.trim()}”${text.scale !== 1 ? ` (scaled ${text.scale})` : ""}`))) : null,
        marks.length ? h("dt", {}, "Glyphs") : null, marks.length ? h("dd", {}, marks.map((glyph) => `${glyph.key ? "key" : "button"} ${glyph.name}`).join(", ")) : null,
        h("dt", {}, "Source"), h("dd", { class: "source" }, place ? place.where : widget.source || "unknown",
          place && place.calls.length > 1 ? h("div", { class: "dim" }, place.calls.map((call) => place.lines[call] ? `${call}:${place.lines[call]}` : call).join(" › ")) : null,
          place?.tag ? h("div", { class: "dim" }, `from ${place.tag}`) : null),
        h("dt", {}, "File"), h("dd", {}, source?.file || widget.file || "")),
      proposal ? h("div", { class: "proposal" }, h("h4", {}, "Proposed"),
        proposal.box ? h("p", {}, `Box → ${proposal.box.map(round1).join(", ")} (Δ ${signed(proposal.box[0] - widget.box[0])}, ${signed(proposal.box[1] - widget.box[1])}, ${signed(proposal.box[2] - proposal.box[0] - (widget.box[2] - widget.box[0]))} w, ${signed(proposal.box[3] - proposal.box[1] - (widget.box[3] - widget.box[1]))} h)`) : null,
        proposal.text !== undefined ? h("p", {}, `Words → “${proposal.text}”`) : null,
        h("div", { class: "row" },
          h("button", { class: "primary", onclick: () => this.openComposer(widget) }, "Save as a note"),
          h("button", { onclick: () => { this.remember(); this.proposals.delete(widget.name); this.panel(); this.draw(); } }, "Discard"))) : null,
      h("div", { class: "row" },
        h("button", { onclick: () => this.openComposer(widget) }, "Comment"),
        h("button", { onclick: () => this.setMode("edit"), title: "Drag or resize the box, arrows nudge it, double-click edits words" }, "Propose an edit"),
        h("button", { onclick: () => navigator.clipboard?.writeText(place?.where || widget.name) }, "Copy source")));
    return box;
  }

  private comments(): HTMLElement {
    const host = h("div", { class: "comments" });
    const filter = h("select", { "aria-label": "Status" },
      ...[["active", "Open and in progress"], ["fixed", "Ready for review"], ["closed", "Closed"], ["all", "All"]].map(([value, words]) =>
        h("option", { value }, words)));
    const who = h("input", { placeholder: "Author", "aria-label": "Author" });
    const list = h("div", { class: "note-list" });
    const draw = () => {
      const notes = this.screenNotes().filter((note) => {
        const wanted = filter.value;
        const statusOk = wanted === "all" || (wanted === "active" ? ["open", "in_progress"].includes(note.status)
          : wanted === "fixed" ? ["fixed", "done"].includes(note.status) : note.status === wanted);
        return statusOk && (!who.value || (note.author || "studio").toLowerCase().includes(who.value.toLowerCase()));
      }).reverse();
      list.replaceChildren(...notes.map((note) => noteCard(note, {
        compact: true,
        onChanged: (changed) => {
          this.notes = this.notes.map((other) => (other.id === changed.id ? { ...other, ...changed } : other));
          this.draw();
          draw();
          this.el.tabs.querySelector('[data-tab="comments"]')!.textContent =
            `Comments (${this.screenNotes().filter((each) => each.status !== "closed").length})`;
        },
        onSelect: (selected) => {
          if (selected.widget && this.layout && named(this.layout, selected.widget)) {
            this.selected = [selected.widget];
            this.draw();
          }
        },
      })));
      if (!notes.length) list.append(h("p", { class: "dim" }, "No notes here. C, then a click, leaves one."));
    };
    filter.addEventListener("input", draw);
    who.addEventListener("input", draw);
    host.append(h("div", { class: "filters" }, filter, who), list);
    draw();
    return host;
  }
}

function signed(value: number): string {
  const rounded = round1(value);
  return (rounded > 0 ? "+" : "") + rounded;
}
