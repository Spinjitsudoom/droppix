import type { FitMode } from "./fit.ts";

/**
 * Common surface implemented by both video render paths so main.ts can pick one at
 * connect time without caring which:
 *  - VideoPipeline    — WebCodecs decode → Canvas 2D (works everywhere; canvas copy
 *                       is costly on low-end GPUs).
 *  - MseVideoPipeline — fMP4 mux → <video> via MSE (native hardware decode + compositor).
 *
 * The instrumentation getters back the stats overlay; MSE reports approximations
 * (e.g. paintQueue as seconds buffered ahead) where an exact frame count isn't available.
 */
export interface VideoRenderer {
  submit(keyframe: boolean, nal: Uint8Array, ptsUs: bigint): void;
  setFit(mode: FitMode): void;
  setAdjust(flip: boolean, brightness: number, contrast: number): void;
  /** Presentation clock (audio wire PTS) for the canvas path; ignored by MSE. */
  setClock(fn: (() => number | null) | null): void;
  close(): void;
  readonly size: { w: number; h: number };
  readonly currentFps: number;
  readonly inFps: number;
  readonly decodeQueue: number;
  readonly paintQueue: number;
  readonly stats: { received: number; painted: number; fps: number; lastError: string };
  readonly hasPainted: boolean;
}
