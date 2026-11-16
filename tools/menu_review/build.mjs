// The client built into static/app.js (committed, so that the app needs no Node to run).
import { build } from "esbuild";

await build({
  entryPoints: ["src/main.ts"],
  bundle: true,
  format: "iife",
  target: "es2020",
  outfile: "static/app.js",
  legalComments: "none",
  banner: { js: "/* The menu review app's client, built from tools/menu_review/src by build.mjs: do not edit. */" },
});
