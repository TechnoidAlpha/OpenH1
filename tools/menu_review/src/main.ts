// The menu review app (tools/menu_review.py serves it and its API): the gallery, a screen's inspector, the notes.
import { api, Meta } from "./api";
import { h } from "./dom";
import { galleryView } from "./gallery";
import { notesView } from "./notes";
import { ScreenView } from "./screen";

let meta: Meta;
let screenView: ScreenView | null = null;
const root = h("div", { id: "view" });

function nav(): HTMLElement {
  return h("nav", { class: "top" }, h("a", { href: "#/", class: "brand" }, "Menu review"),
    h("a", { href: "#/" }, "Gallery"), h("a", { href: "#/notes" }, "Notes"));
}

async function route(): Promise<void> {
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

async function start(): Promise<void> {
  document.body.append(nav(), root);
  meta = await api.meta();
  if (!meta.runs.length) {
    root.replaceChildren(h("main", { class: "page" }, h("h1", {}, "No gallery yet"),
      h("p", {}, "Make one: python tools/menu_gallery.py")));
    return;
  }
  window.addEventListener("hashchange", () => void route());
  await route();
}

// (the browser tests' way in: tests/review.spec.ts)
(window as unknown as { menuReview: unknown }).menuReview = { view: () => screenView };

void start();
