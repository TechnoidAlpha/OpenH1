// goath1.com: the landing page (public/), static assets on Workers, behind
// src/worker.js, which sends openh1.com's pages (but its join page) to
// goath1.com
import { bindings, defineConfig } from "cf/config";

export default defineConfig({
  worker: {
    "name": "openh1",
    "compatibilityDate": "2026-10-06",
    "domains": ["goath1.com", "www.goath1.com", "openh1.com", "www.openh1.com"],
    "entrypoint": "src/worker.js",
    "env": { "ASSETS": bindings.assets() },
    "assets": { "runWorkerFirst": true }
  }
});
