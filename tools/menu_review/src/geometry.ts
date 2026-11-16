// The layouts' geometry: a picture's pixels and the menus' units (debug.menu_layout's JSON:
// port/linux/game/menu_layout.c), the widget at a point, the tree, snapping and redlines.

export type Box = [number, number, number, number];

export interface Widget {
  kind: "widget";
  id: number;
  parent: number;
  name: string;
  type: number;
  box: Box;
  focused?: boolean;
  source?: string;
  file?: string;
}

export interface TextItem {
  kind: "text";
  widget: number;
  text: string;
  box: Box;
  clip: Box;
  extent: Box | null;
  scale: number;
}

export interface GlyphItem {
  kind: "glyph";
  widget: number;
  key: boolean;
  name: string;
  box: Box;
}

export interface Layout {
  screen: Box;
  screen_name: string;
  prompts: string;
  touch_menus: boolean;
  items: Array<Widget | TextItem | GlyphItem | { kind: "target"; widget: number; target: number; box: Box }>;
}

export type Size = [number, number];

export const WIDGET_TYPES = ["container", "text", "spinner", "list", "model", "movie", "custom"];

/** a point of a picture (its pixels) in the menus' units */
export function toMenu(layout: Layout, size: Size, x: number, y: number): [number, number] {
  const [x0, y0, x1, y1] = layout.screen;
  return [x0 + (x * (x1 - x0)) / size[0], y0 + (y * (y1 - y0)) / size[1]];
}

/** a point in the menus' units on the picture */
export function toPicture(layout: Layout, size: Size, x: number, y: number): [number, number] {
  const [x0, y0, x1, y1] = layout.screen;
  return [((x - x0) * size[0]) / (x1 - x0), ((y - y0) * size[1]) / (y1 - y0)];
}

/** a box in the menus' units on the picture: [x, y, width, height] in its pixels */
export function boxOnPicture(layout: Layout, size: Size, box: Box): Box {
  const [ax, ay] = toPicture(layout, size, box[0], box[1]);
  const [bx, by] = toPicture(layout, size, box[2], box[3]);
  return [ax, ay, bx - ax, by - ay];
}

/** the picture's pixels to a menu unit, across */
export function pixelsPerUnit(layout: Layout, size: Size): number {
  return size[0] / (layout.screen[2] - layout.screen[0]);
}

export function widgets(layout: Layout): Widget[] {
  return layout.items.filter((item): item is Widget => item.kind === "widget");
}

export function texts(layout: Layout): TextItem[] {
  return layout.items.filter((item): item is TextItem => item.kind === "text");
}

export function glyphs(layout: Layout): GlyphItem[] {
  return layout.items.filter((item): item is GlyphItem => item.kind === "glyph");
}

/** a widget over the whole screen: the screen's own (a click there is on the screen) */
export function screenWide(widget: Widget): boolean {
  return widget.box[2] - widget.box[0] >= 640 && widget.box[3] - widget.box[1] >= 480;
}

export function contains(box: Box, x: number, y: number): boolean {
  return x >= box[0] && x < box[2] && y >= box[1] && y < box[3];
}

/** the widgets that show something of their own (words, a glyph, a place to click, or a picture): those a
 * click picks before a container */
export function sensible(layout: Layout, pictured: Set<string>): Set<number> {
  const found = new Set<number>();
  for (const item of layout.items) {
    if (item.kind === "text" || item.kind === "glyph" || item.kind === "target") found.add(item.widget);
  }
  for (const widget of widgets(layout)) {
    if (pictured.has(widget.name)) found.add(widget.id);
  }
  return found;
}

/** the widget a click at a point picks: the deepest that shows something there (the last drawn that holds it),
 * else the deepest of any; those over the whole screen passed over; null for the screen */
export function widgetAt(layout: Layout, x: number, y: number, shows?: Set<number>): Widget | null {
  const all = widgets(layout);
  let fallback: Widget | null = null;
  for (let index = all.length - 1; index >= 0; index--) {
    const widget = all[index];
    if (!contains(widget.box, x, y) || screenWide(widget) || widget.box[2] <= widget.box[0]) continue;
    if (!shows || shows.has(widget.id)) return widget;
    if (!fallback) fallback = widget;
  }
  return fallback;
}

export function byId(layout: Layout): Map<number, Widget> {
  return new Map(widgets(layout).map((widget) => [widget.id, widget]));
}

/** a widget's direct children */
export function children(layout: Layout, id: number): Widget[] {
  return widgets(layout).filter((widget) => widget.parent === id);
}

/** double-click's drill: the child of a widget at a point (the last drawn), or null */
export function childAt(layout: Layout, id: number, x: number, y: number): Widget | null {
  const found = children(layout, id).filter((widget) => contains(widget.box, x, y));
  return found.length ? found[found.length - 1] : null;
}

/** a widget's parent that is not the screen's own, or null */
export function parentOf(layout: Layout, widget: Widget): Widget | null {
  const parent = byId(layout).get(widget.parent);
  return parent && !screenWide(parent) ? parent : null;
}

export function named(layout: Layout, name: string): Widget | null {
  let found: Widget | null = null;
  for (const widget of widgets(layout)) if (widget.name === name) found = widget;
  return found;
}

/** the names down to a widget from the screen */
export function pathOf(layout: Layout, widget: Widget): string[] {
  const all = byId(layout);
  const path: string[] = [];
  let step: Widget | undefined = widget;
  while (step && path.length < 32) {
    path.unshift(step.name);
    step = all.get(step.parent);
  }
  return path;
}

export function shortName(name: string): string {
  const parts = name.split("\\");
  return parts[parts.length - 1];
}

export function union(boxes: Box[]): Box {
  return [
    Math.min(...boxes.map((box) => box[0])),
    Math.min(...boxes.map((box) => box[1])),
    Math.max(...boxes.map((box) => box[2])),
    Math.max(...boxes.map((box) => box[3])),
  ];
}

export interface Guide {
  axis: "x" | "y";
  at: number;
}

/** a box being moved (or an edge of it, resized) snapped to the others' edges and centres within a threshold
 * (the menus' units): the box snapped and the guides it lines up on */
export function snap(moving: Box, others: Box[], threshold: number, edges = "xyXY"): { box: Box; guides: Guide[] } {
  const box: Box = [...moving] as Box;
  const guides: Guide[] = [];
  for (const axis of ["x", "y"] as const) {
    const low = axis === "x" ? 0 : 1;
    const high = axis === "x" ? 2 : 3;
    const lines: number[] = [];
    for (const other of others) lines.push(other[low], other[high], (other[low] + other[high]) / 2);
    const moveLow = edges.includes(axis);
    const moveHigh = edges.includes(axis.toUpperCase());
    const mine: Array<[number, number]> = []; // [its value, which: 0 low, 1 high, 2 centre]
    if (moveLow) mine.push([box[low], 0]);
    if (moveHigh) mine.push([box[high], 1]);
    if (moveLow && moveHigh) mine.push([(box[low] + box[high]) / 2, 2]);
    let best: [number, number, number] | null = null; // [difference, line, which]
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

export interface Redline {
  from: [number, number];
  to: [number, number];
  length: number;
}

/** the distances between a selection and another widget (the menus' units): the gaps between them where they
 * are apart, else from each of the inner's edges to the outer's */
export function redlines(a: Box, b: Box): Redline[] {
  const lines: Redline[] = [];
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

export function round1(value: number): number {
  return Math.round(value * 10) / 10;
}
