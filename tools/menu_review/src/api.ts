// The server's JSON API (tools/menu_review.py).
import type { Box, Layout, Size } from "./geometry";

export interface Run {
  name: string;
  compared: boolean;
}

export interface ScreenSummary {
  name: string;
  label: string;
  menu: string;
  file: string;
  generator: string;
  variants: string[];
  open: number;
  thumbs: Record<string, string>;
}

export interface VariantData {
  image: string;
  size: Size;
  layout: Layout | null;
  before: string | null;
  before_layout: Layout | null;
}

export interface WidgetSource {
  file: string;
  generator: string;
  source: string;
  attributes: Record<string, string>;
  place: { file: string; calls: string[]; lines: Record<string, number>; where: string; tag: string };
}

export interface ScreenData {
  name: string;
  label: string;
  file: string;
  generator: string;
  run: string;
  variants: Record<string, VariantData>;
  widgets: Record<string, WidgetSource>;
  rules: string[];
}

export interface Reply {
  author: string;
  time: string;
  text: string;
}

export interface Note {
  id: string;
  status: string;
  kind?: string;
  scope?: string;
  time: string;
  note: string;
  screen: string;
  widget: string | null;
  widget_path?: string[];
  widget_text?: string;
  box?: Box | null;
  area?: Box;
  variant: string;
  author?: string;
  origin?: string;
  source?: string;
  thread?: Reply[];
  edits?: { box?: { from: Box; to: Box; delta: Box }; text?: { old: string; new: string } };
  commit?: string;
  picture_url?: string;
  before_after?: { run: string; before: string; after: string };
}

export interface Meta {
  runs: Run[];
  render: string;
  variants: string[];
  scopes: Record<string, string>;
  kinds: string[];
}

async function call<T>(path: string, body?: unknown): Promise<T> {
  const response = await fetch(path, body === undefined ? {} : {
    method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body),
  });
  if (response.status === 401) {
    location.href = "login";
    throw new Error("sign in");
  }
  const value = await response.json();
  if (value && value.error) throw new Error(value.error);
  return value as T;
}

const screens = new Map<string, Promise<ScreenData>>();

export const api = {
  meta: () => call<Meta>("api/runs"),
  screens: (run: string) => call<{ run: string; compared: boolean; screens: ScreenSummary[] }>(
    `api/screens?run=${encodeURIComponent(run)}`),
  /** a screen's data, fetched once (neighbours prefetched ahead of a switch) */
  screen(run: string, name: string): Promise<ScreenData> {
    const key = `${run}\n${name}`;
    let found = screens.get(key);
    if (!found) {
      found = call<ScreenData>(`api/screen?run=${encodeURIComponent(run)}&screen=${encodeURIComponent(name)}`);
      found.catch(() => screens.delete(key));
      screens.set(key, found);
    }
    return found;
  },
  notes: () => call<Note[]>("api/notes"),
  addNote: (note: Record<string, unknown>) => call<Note>("api/notes", note),
  change: (id: string, change: Record<string, unknown>) => call<Note>(`api/notes/${id}`, change),
  render: () => call<{ render: string }>("api/render", {}),
};

/** a picture fetched ahead, so that a switch to it draws at once */
const preloaded = new Set<string>();
export function preload(url: string | null | undefined): void {
  if (!url || preloaded.has(url)) return;
  preloaded.add(url);
  const image = new Image();
  image.decoding = "async";
  image.src = url;
}
