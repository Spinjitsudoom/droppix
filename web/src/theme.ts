import { loadSettings, saveSettings } from "./settings.ts";
export type Theme = "dark" | "light";
export function nextTheme(t: Theme): Theme { return t === "dark" ? "light" : "dark"; }
export function applyTheme(t: Theme): void {
  document.documentElement.setAttribute("data-theme", t);
}
export function initTheme(): Theme {
  const t = loadSettings().theme;
  applyTheme(t);
  return t;
}
export function setTheme(t: Theme): void {
  applyTheme(t);
  const s = loadSettings(); s.theme = t; saveSettings(s);
}
