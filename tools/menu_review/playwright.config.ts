// The review app driven in a real browser (Playwright's own Chromium, headless), on a made-up gallery.
import { defineConfig, devices } from "@playwright/test";

const port = 8797;

export default defineConfig({
  testDir: "tests",
  timeout: 30000,
  workers: 1,
  reporter: [["list"]],
  use: { baseURL: `http://127.0.0.1:${port}/`, headless: true, viewport: { width: 1400, height: 900 } },
  projects: [{ name: "chromium", use: { ...devices["Desktop Chrome"], viewport: { width: 1400, height: 900 } } }],
  webServer: {
    command: `python3 tests/fixture.py tests/.fixture && python3 ../menu_review.py serve --host 127.0.0.1 --port ${port} ` +
      "--secret test --build tests/.fixture/build --notes tests/.fixture/notes.jsonl",
    url: `http://127.0.0.1:${port}/login`,
    reuseExistingServer: false,
    timeout: 20000,
  },
});
