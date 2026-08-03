import { normalizePin, pinComplete } from "./pin.ts";

export class ConnectView {
  private inputs: HTMLInputElement[];
  private btn: HTMLButtonElement;
  private wrap: HTMLElement;
  private statusEl: HTMLElement;

  constructor(private onConnect: (code: string) => void) {
    this.wrap = document.getElementById("pin")!;
    this.inputs = [...this.wrap.querySelectorAll("input")] as HTMLInputElement[];
    this.btn = document.getElementById("btn-connect") as HTMLButtonElement;
    this.statusEl = document.getElementById("c-status")!;
    this.inputs.forEach((inp, i) => {
      inp.addEventListener("input", () => {
        inp.value = normalizePin(inp.value).slice(0, 1);
        inp.classList.toggle("filled", !!inp.value);
        this.wrap.classList.remove("err");
        if (inp.value && i < this.inputs.length - 1) this.inputs[i + 1]!.focus();
        this.sync();
      });
      inp.addEventListener("keydown", (e) => {
        if (e.key === "Backspace" && !inp.value && i > 0) this.inputs[i - 1]!.focus();
      });
    });
    this.btn.addEventListener("click", () => { if (!this.btn.disabled) this.onConnect(this.value()); });
    this.sync();
  }

  private value(): string { return normalizePin(this.inputs.map((i) => i.value).join("")); }
  private sync(): void { this.btn.disabled = !pinComplete(this.value()); }
  showError(msg: string): void { this.wrap.classList.add("err"); this.statusEl.textContent = msg; }
  reset(): void { this.inputs.forEach((i) => { i.value = ""; i.classList.remove("filled"); }); this.wrap.classList.remove("err"); this.sync(); }
  focus(): void { this.inputs[0]?.focus(); }
}
