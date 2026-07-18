import { defineConfig } from "@playwright/test";

const PORT = process.env.PORT || "8443";
const BASE = process.env.DROPPIX_MOCK_URL || `https://localhost:${PORT}`;

export default defineConfig({
  testDir: "./e2e",
  timeout: 60_000,
  expect: { timeout: 15_000 },
  fullyParallel: false,
  workers: 1,
  reporter: [["list"]],
  use: {
    baseURL: BASE,
    ignoreHTTPSErrors: true,
    trace: "retain-on-failure",
    screenshot: "only-on-failure",
    video: "off",
  },
  // Assume mock host already running (`npm start`). Set DROPPIX_MOCK_START=1 to spawn it.
  webServer: process.env.DROPPIX_MOCK_START
    ? {
        command: "node src/server.mjs",
        url: `${BASE}/health`,
        reuseExistingServer: true,
        ignoreHTTPSErrors: true,
        timeout: 30_000,
      }
    : undefined,
});
