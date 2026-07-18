export function isStandaloneDisplay(): boolean {
  return (
    window.matchMedia("(display-mode: standalone)").matches ||
    window.matchMedia("(display-mode: fullscreen)").matches ||
    // @ts-expect-error iOS Safari
    (typeof navigator !== "undefined" && navigator.standalone === true)
  );
}

export async function enterFullscreen(el: HTMLElement): Promise<void> {
  if (!document.fullscreenElement) await el.requestFullscreen?.();
}

export async function exitFullscreen(): Promise<void> {
  if (document.fullscreenElement) await document.exitFullscreen?.();
}

export function toggleFullscreen(el: HTMLElement): void {
  if (document.fullscreenElement) void exitFullscreen();
  else void enterFullscreen(el);
}
