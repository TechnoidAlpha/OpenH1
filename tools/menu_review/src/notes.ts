// The notes: a note's card (its thread, replies and status buttons), the composer, and the notes view.
import { api, Meta, Note } from "./api";
import { h, author, setAuthor, when, STATUS_WORDS } from "./dom";
import { shortName } from "./geometry";

export interface Draft {
  run: string;
  screen: string;
  variant: string;
  target: string; // widget, area, point, screen
  widget?: string | null;
  x?: number;
  y?: number;
  x1?: number;
  y1?: number;
  edits?: Record<string, unknown>;
  label: string;
}

/** the words of a note's edits */
export function editsWords(note: Note): string[] {
  const words: string[] = [];
  const box = note.edits?.box;
  if (box) {
    const d = box.delta;
    words.push(`Move ${sign(d[0])}, ${sign(d[1])}; ${sign(d[2])} wide, ${sign(d[3])} tall (to ${box.to.join(", ")})`);
  }
  const text = note.edits?.text;
  if (text) words.push(`"${text.old}" → "${text.new}"`);
  return words;
}

function sign(value: number): string {
  return (value > 0 ? "+" : "") + value;
}

export interface CardOptions {
  onChanged: (note: Note) => void;
  onSelect?: (note: Note) => void;
  compact?: boolean;
}

/** a note's card: its words, where, its thread and a reply box, and what can be done with it */
export function noteCard(note: Note, options: CardOptions): HTMLElement {
  const kind = note.kind === "question" ? "Question" : "Fix";
  const where = note.widget ? shortName(note.widget) : note.area ? "an area" : "the screen";
  const reply = h("textarea", { class: "reply", rows: 2, placeholder: note.kind === "question" ? "Answer…" : "Reply…" });
  const status = (value: string, words: string, needsReply = false) => h("button", {
    class: "small", "data-action": value,
    onclick: async () => {
      if (needsReply && !reply.value.trim()) {
        reply.focus();
        reply.placeholder = "Say why it is reopened…";
        return;
      }
      const changed = await api.change(note.id, { status: value, reply: reply.value.trim() || undefined, author: author() });
      options.onChanged(changed);
    },
  }, words);
  const buttons: HTMLElement[] = [];
  if (note.status === "open") buttons.push(status("in_progress", "Start"), status("closed", "Resolve"));
  if (note.status === "in_progress") buttons.push(status("open", "Reopen"), status("closed", "Resolve"));
  if (note.status === "fixed" || note.status === "done") {
    buttons.push(status("closed", "Approve"), status("open", "Reopen with reply", true));
  }
  if (note.status === "closed") buttons.push(status("open", "Reopen"));
  const thread = (note.thread || []).map((entry) => h("div", { class: "reply-item" },
    h("b", {}, entry.author || "someone"), " ", h("span", { class: "dim" }, when(entry.time)), h("div", {}, entry.text)));
  const pair = note.before_after ? h("div", { class: "pair" },
    h("figure", {}, h("img", { src: note.before_after.before, alt: "before", loading: "lazy" }), h("figcaption", {}, "before")),
    h("figure", {}, h("img", { src: note.before_after.after, alt: "after", loading: "lazy" }), h("figcaption", {}, "after"))) : null;
  const card = h("article", { class: `note status-${note.status}`, "data-note": note.id },
    h("header", {},
      h("span", { class: `badge kind-${note.kind || "fix"}` }, kind),
      h("span", { class: "badge status" }, STATUS_WORDS[note.status] || note.status),
      h("button", { class: "link", onclick: () => options.onSelect?.(note), title: "Show it on the screen" }, where),
      h("span", { class: "dim" }, ` · ${note.author || (note.origin === "web" ? "reviewer" : "studio")} · ${when(note.time)}`)),
    h("p", { class: "words" }, note.note),
    ...editsWords(note).map((words) => h("p", { class: "edit-words" }, words)),
    !options.compact && note.screen ? h("p", { class: "dim" }, `${note.screen} · ${note.variant}${note.scope && note.scope !== "variant" ? ` · ${note.scope === "screen" ? "all variants" : "everywhere like this"}` : ""}${note.commit ? ` · fixed in ${note.commit.slice(0, 9)}` : ""}`) : null,
    pair,
    !options.compact && note.picture_url && !pair ? h("img", { class: "note-picture", src: note.picture_url, alt: "", loading: "lazy" }) : null,
    thread.length ? h("div", { class: "thread" }, ...thread) : null,
    h("div", { class: "reply-row" }, reply,
      h("button", { class: "small", onclick: async () => {
        if (!reply.value.trim()) return;
        options.onChanged(await api.change(note.id, { reply: reply.value.trim(), author: author() }));
      } }, "Reply")),
    h("div", { class: "actions" }, ...buttons));
  return card;
}

/** the composer of a note: its kind, scope, words and author; saves it, with the draft's edits */
export function composer(draft: Draft, meta: Meta, done: (note: Note | null) => void): HTMLElement {
  const words = h("textarea", { rows: 3, placeholder: "What should change?", maxlength: 2000 });
  const name = h("input", { placeholder: "Your name", value: author(), autocomplete: "name" });
  const kind = h("select", { "aria-label": "Kind" },
    h("option", { value: "fix" }, "Fix"), h("option", { value: "question" }, "Question"));
  const scope = h("select", { "aria-label": "Scope" },
    h("option", { value: "variant" }, "This variant"),
    h("option", { value: "screen" }, "All variants of this screen"),
    h("option", { value: "everywhere" }, "Everywhere like this"));
  const message = h("span", { class: "dim" });
  const save = h("button", { class: "primary", type: "submit" }, "Save note");
  const form = h("form", { class: "composer", onsubmit: async (event: Event) => {
    event.preventDefault();
    if (!words.value.trim() && !draft.edits) {
      words.focus();
      return;
    }
    setAuthor(name.value.trim());
    save.disabled = true;
    try {
      const note = await api.addNote({
        run: draft.run, screen: draft.screen, variant: draft.variant, side: "after", kind: draft.target,
        widget: draft.widget, x: draft.x, y: draft.y, x1: draft.x1, y1: draft.y1, edits: draft.edits,
        note: words.value.trim(), author: name.value.trim(), note_kind: kind.value, scope: scope.value,
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
    h("div", { class: "row" }, name, save,
      h("button", { type: "button", onclick: () => done(null) }, "Cancel")),
    message);
  void meta;
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

/** the notes view: every note, by status, screen, variant and author; the render's button */
export async function notesView(root: HTMLElement, meta: Meta, query: URLSearchParams): Promise<void> {
  let notes = await api.notes();
  const status = h("select", { "aria-label": "Status" },
    ...[["active", "Open and in progress"], ["open", "Open"], ["in_progress", "In progress"],
      ["fixed", "Ready for review"], ["closed", "Closed"], ["all", "All"]].map(([value, words]) =>
      h("option", { value, selected: value === (query.get("status") || "active") }, words)));
  const screen = h("input", { placeholder: "Screen", value: query.get("screen") || "" });
  const variant = h("select", { "aria-label": "Variant" }, h("option", { value: "" }, "Any variant"),
    ...meta.variants.map((value) => h("option", { value, selected: value === query.get("variant") }, value)));
  const who = h("input", { placeholder: "Author", value: query.get("author") || "" });
  const list = h("div", { class: "note-list" });
  const rendering = h("span", { class: "dim" }, meta.render || "");
  const draw = () => {
    const chosen = notes.filter((note) => {
      const wanted = status.value;
      const statusOk = wanted === "all" || (wanted === "active" ? ["open", "in_progress"].includes(note.status)
        : wanted === "fixed" ? ["fixed", "done"].includes(note.status) : note.status === wanted);
      return statusOk && (!screen.value || note.screen.includes(screen.value)) &&
        (!variant.value || note.variant === variant.value) &&
        (!who.value || (note.author || "studio").toLowerCase().includes(who.value.toLowerCase()));
    }).reverse();
    list.replaceChildren(...chosen.map((note) => noteCard(note, {
      onChanged: (changed) => {
        notes = notes.map((other) => (other.id === changed.id ? { ...other, ...changed } : other));
        draw();
      },
      onSelect: (selected) => {
        location.hash = `#/s/latest/${encodeURIComponent(selected.screen)}/${selected.variant}?note=${selected.id}`;
      },
    })));
    if (!chosen.length) list.append(h("p", { class: "dim" }, "No notes here."));
    const counts = { open: 0, ready: 0 };
    for (const note of notes) {
      if (note.status === "open" || note.status === "in_progress") counts.open++;
      if (note.status === "fixed" || note.status === "done") counts.ready++;
    }
    heading.textContent = `Notes · ${counts.open} open · ${counts.ready} ready for review`;
  };
  const heading = h("h1", {});
  for (const control of [status, screen, variant, who]) control.addEventListener("input", draw);
  root.replaceChildren(h("main", { class: "page" }, heading,
    h("div", { class: "filters" }, status, screen, variant, who),
    h("div", { class: "filters" }, h("button", { onclick: async () => {
      rendering.textContent = (await api.render()).render || "";
    } }, "Render the noted screens again"), rendering),
    list));
  draw();
}
