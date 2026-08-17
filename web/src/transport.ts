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
  // Web-path PIN pairing: the socket is open but the host holds the stream until the
  // client submits the code shown on the PC. onAwaitingPin fires when the prompt should
  // show; onPinRejected reports remaining tries; onPaired fires when the code is accepted.
  onAwaitingPin?: () => void;
  onPinRejected?: (triesLeft: number) => void;
  onPaired?: () => void;
  /**
   * Settings the HOST had stored for this client, delivered right after pairing.
   *
   * The handler should merge and persist them, then update the transport's hello args —
   * HELLO is sent immediately afterwards and carries fps/resolution/audio.
   */
  onClientSettings?: (json: string) => void;
}

type HelloArgs = {
  width: number;
  height: number;
  density: number;
  name: string;
  id: string;
  fps: number;
  audioWanted: number;
  bitrateKbps: number;
  wallCol: number;
  wallRow: number;
  pinRequired: boolean;
};

export class Transport {
  private ws: WebSocket | null = null;
  private pingTimer: number | null = null;
  private hello: HelloArgs | null = null;
  private paired = false;
  private helloTimer: number | null = null;

  constructor(private handlers: TransportHandlers) {}

  connect(hello: HelloArgs): void {
    this.close();
    this.hello = hello;
    this.paired = false;
    const proto = location.protocol === "https:" ? "wss:" : "ws:";
    const url = `${proto}//${location.host}/ws`;
    this.handlers.onStatus(`Connecting ${url}`);
    const ws = new WebSocket(url);
    ws.binaryType = "arraybuffer";
    this.ws = ws;

    ws.onopen = () => {
      if (this.ws !== ws) return;
      if (hello.pinRequired) {
        // Hold HELLO until the user enters the code the host shows; sendHello() runs on
        // an accepted PairResult. The page is "live" here — just waiting to be confirmed.
        this.handlers.onStatus("Connected — enter the code shown on your PC");
        this.handlers.onAwaitingPin?.();
      } else {
        this.sendHello();
      }
    };

    ws.onmessage = (ev) => {
      // Stale socket (replaced or closed): drop its frames on the floor.
      if (this.ws !== ws) return;
      const parsed = parseFrame(ev.data as ArrayBuffer);
      if (!parsed) return;
      if (!this.paired && parsed.type === MsgType.PairResult) {
        const ok = parsed.body[0] === 1;
        const triesLeft = parsed.body[1] ?? 0;
        if (ok) {
          this.paired = true;
          this.handlers.onPaired?.();
          // HELLO waits for the host's stored settings: fps/resolution/audio are carried in
          // it, so sending it now would spend this whole session on stale values. The host
          // always sends the frame (even "{}"), and the timer is only a guard against a host
          // that does not — never the normal path.
          this.helloTimer = window.setTimeout(() => {
            this.helloTimer = null;
            this.sendHello();
          }, 1500);
        } else {
          this.handlers.onPinRejected?.(triesLeft);
        }
        return;
      }
      if (this.paired && parsed.type === MsgType.ClientSettings) {
        // Settings the host was holding for us. Apply before HELLO so this session already
        // uses them; the handler re-reads storage and may replace `this.hello`.
        try {
          const json = new TextDecoder().decode(parsed.body);
          this.handlers.onClientSettings?.(json);
        } catch {
          /* malformed blob must not block the stream */
        }
        if (this.helloTimer !== null) {
          clearTimeout(this.helloTimer);
          this.helloTimer = null;
        }
        this.sendHello();
        return;
      }
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

  /** Send the code the user typed (read off the host screen) for host verification. */
  submitPin(code: string): void {
    this.send(MsgType.Pair, new TextEncoder().encode(code));
  }

  /** Ask the host to persist these settings on our behalf. Safe before/after HELLO. */
  saveSettingsOnHost(json: string): void {
    this.send(MsgType.ClientSettings, new TextEncoder().encode(json));
  }

  /** Replace the HELLO args (used after host settings arrive, before HELLO is sent). */
  setHello(h: HelloArgs): void {
    this.hello = h;
  }

  private sendHello(): void {
    const h = this.hello;
    if (!h || !this.ws) return;
    const body = encodeHello(
      kProtocolVersion,
      h.width,
      h.height,
      h.density,
      h.name,
      h.id,
      h.fps,
      h.audioWanted,
      0,
      h.bitrateKbps,
      h.wallCol,
      h.wallRow,
    );
    this.ws.send(frameMessage(MsgType.Hello, body));
    this.handlers.onStatus("Connected - waiting for CONFIG");
    this.pingTimer = window.setInterval(() => {
      this.send(MsgType.Ping, new Uint8Array());
    }, 2000);
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
