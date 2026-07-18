// src/audio-worklet.ts
var PcmPlayerProcessor = class _PcmPlayerProcessor extends AudioWorkletProcessor {
  queue = [];
  readPos = 0;
  queuedFrames = 0;
  started = false;
  // 60ms prebuffer: enough to ride localhost jitter, small enough to keep
  // audio from lagging video (lipsync). Re-armed after every underrun.
  static PREBUFFER_FRAMES = 48e3 * 0.06;
  constructor() {
    super();
    this.port.onmessage = (ev) => {
      if (ev.data?.type === "pcm") {
        const samples = ev.data.samples;
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
  process(_inputs, outputs) {
    const outL = outputs[0]?.[0];
    const outR = outputs[0]?.[1] ?? outL;
    if (!outL) return true;
    if (!this.started) {
      if (this.queuedFrames >= _PcmPlayerProcessor.PREBUFFER_FRAMES) {
        this.started = true;
      } else {
        outL.fill(0);
        if (outR && outR !== outL) outR.fill(0);
        return true;
      }
    }
    for (let i = 0; i < outL.length; i++) {
      if (this.queue.length === 0) {
        outL[i] = 0;
        if (outR) outR[i] = 0;
        this.started = false;
        continue;
      }
      const cur = this.queue[0];
      outL[i] = cur[this.readPos++] ?? 0;
      outR[i] = cur[this.readPos++] ?? 0;
      this.queuedFrames--;
      if (this.readPos >= cur.length) {
        this.queue.shift();
        this.readPos = 0;
      }
    }
    return true;
  }
};
registerProcessor("pcm-player", PcmPlayerProcessor);
//# sourceMappingURL=audio-worklet.js.map
