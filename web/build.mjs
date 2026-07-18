import * as esbuild from "esbuild";
import { cpSync, mkdirSync, rmSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = dirname(fileURLToPath(import.meta.url));
const dist = join(root, "dist");
const watch = process.argv.includes("--watch");

rmSync(dist, { recursive: true, force: true });
mkdirSync(dist, { recursive: true });
cpSync(join(root, "public"), dist, { recursive: true });

const ctx = await esbuild.context({
  entryPoints: [join(root, "src/main.ts"), join(root, "src/audio-worklet.ts")],
  bundle: true,
  outdir: dist,
  format: "esm",
  target: "es2022",
  sourcemap: true,
  entryNames: "[name]",
  loader: { ".css": "css" },
});

if (watch) {
  await ctx.watch();
  console.log("watching web/");
} else {
  await ctx.rebuild();
  await ctx.dispose();
  console.log("built web/dist");
}
