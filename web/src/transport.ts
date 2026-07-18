import {
  MsgType,
  encodeHello,
  frameMessage,
  parseFrame,
  decodeConfig,
  decodeVideo,
  decodeOverlay,
  kProtocolVersion,
} from "./protocol.ts";

export interface TransportHandlers {
  onConfig: (w: number, h: number, fps: number) => void;
  onVideo: (ptsUs: bigint, keyframe: boolean, nal: Uint8Array) => void;
  onAudio: (pcm: Uint8Array) => void;
  onOverlay: (show: boolean) => void;
  onClose: (reason: string) => void;
  onStatus: (msg: string) => void;
}

export class Transport {
  private ws: WebSocket | null = null;
  private pingTimer: number | null = null;

  constructor(private handlers: TransportHandlers) {}

  connect(hello: {
    width: number;
    height: number;
    density: number;
    name: string;
    id: string;
    fps: number;
    audioWanted: number;
    bitrateKbps: number;
  }): void {
    this.close();
    const proto = location.protocol === "https:" ? "wss:" : "ws:";
    const url = `${proto}//${location.host}/ws`;
    this.handlers.onStatus(`Connecting ${url}`);
    const ws = new WebSocket(url);
    ws.binaryType = "arraybuffer";
    this.ws = ws;

    ws.onopen = () => {
      if (this.ws !== ws) return;
      const body = encodeHello(
        kProtocolVersion,
        hello.width,
        hello.height,
        hello.density,
        hello.name,
        hello.id,
        hello.fps,
        hello.audioWanted,
        0,
        hello.bitrateKbps,
      );
      ws.send(frameMessage(MsgType.Hello, body));
      this.handlers.onStatus("Connected - waiting for CONFIG");
      this.pingTimer = window.setInterval(() => {
        this.send(MsgType.Ping, new Uint8Array());
      }, 2000);
    };

    ws.onmessage = (ev) => {
      // Stale socket (replaced or closed): drop its frames on the floor.
      if (this.ws !== ws) return;
      const parsed = parseFrame(ev.data as ArrayBuffer);
      if (!parsed) return;
      switch (parsed.type) {
        case MsgType.Config: {
          const c = decodeConfig(parsed.body);
          if (c) this.handlers.onConfig(c.width, c.height, c.fps);
          break;
        }
        case MsgType.Video: {
          const v = decodeVideo(parsed.body);
          if (v) this.handlers.onVideo(v.ptsUs, v.keyframe, v.nal);
          break;
        }
        case MsgType.Audio:
          this.handlers.onAudio(parsed.body);
          break;
        case MsgType.Overlay:
          this.handlers.onOverlay(decodeOverlay(parsed.body) !== 0);
          break;
        case MsgType.Ping:
          this.send(MsgType.Pong, parsed.body);
          break;
        case MsgType.Pong:
          break;
        case MsgType.Bye:
          this.handlers.onClose("host bye");
          this.close();
          break;
        default:
          break;
      }
    };

    ws.onerror = () => {
      if (this.ws === ws) this.handlers.onStatus("WebSocket error");
    };
    ws.onclose = () => {
      // Only report the close if this socket is still the active one;
      // sockets discarded via close() were already handled by the caller.
      if (this.ws !== ws) return;
      this.ws = null;
      this.clearPing();
      this.handlers.onClose("socket closed");
    };
  }

  send(type: MsgType, body: Uint8Array): void {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return;
    this.ws.send(frameMessage(type, body));
  }

  close(): void {
    this.clearPing();
    const ws = this.ws;
    this.ws = null; // detach first so late events from this socket are ignored
    if (ws) {
      try {
        if (ws.readyState === WebSocket.OPEN) {
          ws.send(frameMessage(MsgType.Bye, new Uint8Array()));
        }
      } catch {
        /* ignore */
      }
      try {
        ws.close();
      } catch {
        /* ignore */
      }
    }
  }

  private clearPing(): void {
    if (this.pingTimer != null) {
      clearInterval(this.pingTimer);
      this.pingTimer = null;
    }
  }
}
