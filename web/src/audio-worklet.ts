class PcmPlayerProcessor extends AudioWorkletProcessor {
  private queue: Float32Array[] = [];
  private readPos = 0;

  constructor() {
    super();
    this.port.onmessage = (ev) => {
      if (ev.data?.type === "pcm") {
        this.queue.push(ev.data.samples as Float32Array);
      } else if (ev.data?.type === "clear") {
        this.queue = [];
        this.readPos = 0;
      }
    };
  }

  process(_inputs: Float32Array[][], outputs: Float32Array[][]): boolean {
    const outL = outputs[0]?.[0];
    const outR = outputs[0]?.[1] ?? outL;
    if (!outL) return true;
    for (let i = 0; i < outL.length; i++) {
      if (this.queue.length === 0) {
        outL[i] = 0;
        if (outR) outR[i] = 0;
        continue;
      }
      const cur = this.queue[0]!;
      outL[i] = cur[this.readPos++] ?? 0;
      outR![i] = cur[this.readPos++] ?? 0;
      if (this.readPos >= cur.length) {
        this.queue.shift();
        this.readPos = 0;
      }
    }
    return true;
  }
}

registerProcessor("pcm-player", PcmPlayerProcessor);
