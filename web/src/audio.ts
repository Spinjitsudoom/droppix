/** Host format: 48000 Hz, s16le, stereo interleaved. */
export const AUDIO_RATE = 48000;
export const AUDIO_CHANNELS = 2;

export class AudioPlayer {
  private ctx: AudioContext | null = null;
  private node: AudioWorkletNode | null = null;
  private muted = false;
  private ready = false;

  async unlock(): Promise<void> {
    try {
      if (!this.ctx) {
        this.ctx = new AudioContext({ sampleRate: AUDIO_RATE });
        await this.ctx.audioWorklet.addModule(new URL("./audio-worklet.js", import.meta.url));
        this.node = new AudioWorkletNode(this.ctx, "pcm-player", {
          numberOfInputs: 0,
          numberOfOutputs: 1,
          outputChannelCount: [2],
        });
        this.node.connect(this.ctx.destination);
      }
      if (this.ctx.state === "suspended") await this.ctx.resume();
      this.ready = true;
    } catch (e) {
      console.warn("audio init failed; video-only", e);
      this.ready = false;
    }
  }

  setMuted(m: boolean) {
    this.muted = m;
    if (m) this.node?.port.postMessage({ type: "clear" });
  }

  submit(pcm: Uint8Array) {
    if (!this.ready || this.muted || !this.node) return;
    if (pcm.length < 4) return;
    const samples = new Float32Array(pcm.length / 2);
    const view = new DataView(pcm.buffer, pcm.byteOffset, pcm.byteLength);
    for (let i = 0; i < samples.length; i++) {
      samples[i] = view.getInt16(i * 2, true) / 32768;
    }
    this.node.port.postMessage({ type: "pcm", samples }, [samples.buffer]);
  }

  close() {
    try {
      this.node?.disconnect();
      void this.ctx?.close();
    } catch {
      /* ignore */
    }
    this.node = null;
    this.ctx = null;
    this.ready = false;
  }
}
