// The gallery: the screens grouped by menu, their variants side by side, their open notes' counts.
import { api, Meta, preload } from "./api";
import { h } from "./dom";

export async function galleryView(root: HTMLElement, meta: Meta, run: string): Promise<void> {
  const data = await api.screens(run);
  const runs = h("select", { "aria-label": "Gallery", onchange: (event: Event) => {
    location.hash = `#/g/${encodeURIComponent((event.target as HTMLSelectElement).value)}`;
  } }, ...meta.runs.map((each) => h("option", { value: each.name, selected: each.name === run },
    each.name + (each.compared ? " (before | after)" : ""))));
  const filter = h("input", { type: "search", placeholder: "Find a screen", "aria-label": "Find a screen" });
  const groups = new Map<string, HTMLElement[]>();
  for (const screen of data.screens) {
    const card = h("a", { class: "card", href: `#/s/${encodeURIComponent(run)}/${encodeURIComponent(screen.name)}/${screen.variants[0]}`,
      "data-name": screen.name.toLowerCase(), onmouseenter: () => api.screen(run, screen.name) },
      h("div", { class: "card-title" }, h("span", {}, screen.label),
        screen.open ? h("span", { class: "badge open-count", title: "Open notes" }, String(screen.open)) : null),
      h("div", { class: "thumbs" }, ...screen.variants.map((variant) => h("img", {
        src: screen.thumbs[variant], alt: variant, title: variant, loading: "lazy", decoding: "async" }))),
      h("div", { class: "dim" }, `${screen.file} · ${screen.generator || "by hand"}`));
    if (!groups.has(screen.menu)) groups.set(screen.menu, []);
    groups.get(screen.menu)!.push(card);
  }
  const sections = [...groups.entries()].map(([menu, cards]) => h("section", { class: "group" },
    h("h2", {}, menu.charAt(0).toUpperCase() + menu.slice(1)), h("div", { class: "grid" }, ...cards)));
  filter.addEventListener("input", () => {
    const words = filter.value.toLowerCase();
    for (const card of root.querySelectorAll<HTMLElement>(".card")) card.hidden = !card.dataset.name!.includes(words);
  });
  root.replaceChildren(h("main", { class: "page" },
    h("div", { class: "filters" }, runs, filter,
      h("span", { class: "dim" }, `${data.screens.length} screens${data.compared ? " · before | after: notes go on the after" : ""}`)),
    ...sections));
  // (the first screens' data ahead of a click)
  for (const screen of data.screens.slice(0, 4)) api.screen(run, screen.name).then((found) => {
    for (const variant of Object.values(found.variants)) preload(variant.image);
  }).catch(() => undefined);
}
