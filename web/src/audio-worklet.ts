/**
 * Ring-buffer PCM player with prebuffering: waits for ~120ms of audio before
 * starting (and after every underrun) so bursty network delivery stays gapless.
 */
class PcmPlayerProcessor extends AudioWorkletProcessor {
  private queue: Float32Array[] = [];
  private readPos = 0;
  private queuedFrames = 0;
  private started = false;
  // 60ms prebuffer: enough to ride localhost jitter, small enough to keep
  // audio from lagging video (lipsync). Re-armed after every underrun.
  private static readonly PREBUFFER_FRAMES = 48000 * 0.06;

  constructor() {
    super();
    this.port.onmessage = (ev) => {
      if (ev.data?.type === "pcm") {
        const samples = ev.data.samples as Float32Array;
        this.queue.push(samples);
        this.queuedFrames += samples.length / 2;
      } else if (ev.data?.type === "clear") {
        this.queue = [];
        this.readPos = 0;
        this.queuedFrames = 0;
        this.started = false;
      }
    };
  }

  process(_inputs: Float32Array[][], outputs: Float32Array[][]): boolean {
    const outL = outputs[0]?.[0];
    const outR = outputs[0]?.[1] ?? outL;
    if (!outL) return true;

    if (!this.started) {
      if (this.queuedFrames >= PcmPlayerProcessor.PREBUFFER_FRAMES) {
        this.started = true;
      } else {
        outL.fill(0);
        if (outR && outR !== outL) outR.fill(0);
        return true;
      }
    }

    for (let i = 0; i < outL.length; i++) {
      if (this.queue.length === 0) {
        // Underrun: go back to prebuffering instead of crackling.
        outL[i] = 0;
        if (outR) outR[i] = 0;
        this.started = false;
        continue;
      }
      const cur = this.queue[0]!;
      outL[i] = cur[this.readPos++] ?? 0;
      outR![i] = cur[this.readPos++] ?? 0;
      this.queuedFrames--;
      if (this.readPos >= cur.length) {
        this.queue.shift();
        this.readPos = 0;
      }
    }
    return true;
  }
}

registerProcessor("pcm-player", PcmPlayerProcessor);
