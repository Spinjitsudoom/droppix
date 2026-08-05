/**
 * The disconnected-state connect card: a single Connect button + a status line.
 * No PIN — the web client is served BY the host over its own HTTPS, so loading
 * this page already means you reached the right PC; the host's approve-device
 * prompt is the actual pairing gate. The page auto-connects on load (see main.ts);
 * this button is for reconnecting after a drop.
 */
export class ConnectView {
  private btn: HTMLButtonElement;
  private statusEl: HTMLElement;

  constructor(private onConnect: () => void) {
    this.btn = document.getElementById("btn-connect") as HTMLButtonElement;
    this.statusEl = document.getElementById("c-status")!;
    this.btn.disabled = false;
    this.btn.addEventListener("click", () => this.onConnect());
  }

  setStatus(msg: string): void {
    this.statusEl.textContent = msg;
  }
  showError(msg: string): void {
    this.statusEl.textContent = msg;
  }
  reset(): void {
    /* nothing to reset without a PIN entry */
  }
}
