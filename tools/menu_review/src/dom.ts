// A little DOM: elements made with their attributes and children, and the author's name kept.

type Child = Node | string | null | undefined | false;

export function h<K extends keyof HTMLElementTagNameMap>(
  tag: K, attributes: Record<string, unknown> = {}, ...content: Child[]): HTMLElementTagNameMap[K] {
  const element = document.createElement(tag);
  for (const [key, value] of Object.entries(attributes)) {
    if (value === undefined || value === null || value === false) continue;
    if (key.startsWith("on") && typeof value === "function") {
      element.addEventListener(key.slice(2), value as EventListener);
    } else if (key === "class") {
      element.className = String(value);
    } else if (key === "dataset") {
      Object.assign(element.dataset, value);
    } else if (key in element && typeof value !== "string") {
      (element as unknown as Record<string, unknown>)[key] = value;
    } else {
      element.setAttribute(key, value === true ? "" : String(value));
    }
  }
  for (const child of content) {
    if (child === null || child === undefined || child === false) continue;
    element.append(typeof child === "string" ? document.createTextNode(child) : child);
  }
  return element;
}

export const SVG = "http://www.w3.org/2000/svg";

export function svg(tag: string, attributes: Record<string, string | number> = {}): SVGElement {
  const element = document.createElementNS(SVG, tag);
  for (const [key, value] of Object.entries(attributes)) element.setAttribute(key, String(value));
  return element;
}

export function author(): string {
  try {
    return localStorage.getItem("menu-review-author") || "";
  } catch {
    return "";
  }
}

export function setAuthor(name: string): void {
  try {
    localStorage.setItem("menu-review-author", name);
  } catch {
    /* (private browsing: kept for this page only) */
  }
}

export function when(time: string | undefined): string {
  if (!time) return "";
  const date = new Date(time);
  return isNaN(date.getTime()) ? time : date.toLocaleString(undefined, { dateStyle: "medium", timeStyle: "short" });
}

export const STATUS_WORDS: Record<string, string> = {
  open: "Open", in_progress: "In progress", fixed: "Ready for review", done: "Ready for review", closed: "Closed",
};

/** children added, those that are not there left out */
export function add(parent: Element, ...content: Child[]): void {
  for (const child of content) {
    if (child === null || child === undefined || child === false) continue;
    parent.append(typeof child === "string" ? document.createTextNode(child) : child);
  }
}
