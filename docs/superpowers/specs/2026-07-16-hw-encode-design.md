# Hardware video encoding (VAAPI / NVENC + software fallback)

**Date:** 2026-07-16
**Status:** Shipped on master (NVENC → VAAPI → software AutoEncoder).
**Roadmap:** tier T3 "Hardware video encoding" (HOST) — implemented. Remaining optional polish: expose `--encoder` in the host GUI.

## Summary

GPU H.264 encoders (NVENC + VAAPI) sit behind the existing `Encoder` interface, chosen at runtime with a clean fallback to software x264. A factory tries **NVENC → VAAPI → software** (overridable with `--encoder`), keeping the first that initializes. This offloads encoding from the CPU with no change to the client — the stream stays plain H.264 with in-band SPS/PPS. **Host-only. No protocol/client/Android change.**

## Decisions

| Question | Decision |
| --- | --- |
| Backends | **NVENC** (`h264_nvenc`) + **VAAPI** (`h264_vaapi`) + existing **software** (`libx264`). |
| Selection | Factory tries **NVENC → VAAPI → software**; first whose `open()` succeeds wins. `--encoder <auto\|nvenc\|vaapi\|software>` forces one (default `auto`). |
| Frame path | Capture gives BGRA CPU frames (`Frame.bgra`). All encoders `sws` BGRA→NV12; NVENC hands the CPU NV12 to `h264_nvenc` (it uploads internally); VAAPI uploads NV12 to a VAAPI surface. |
| VAAPI device | Try the default VAAPI device, then probe `/dev/dri/renderD12*` for a node whose VAAPI advertises the **H264 encode entrypoint** (skips this host's default NVIDIA-NVDEC decode-only backend → picks the Intel iGPU). |
| Headers/latency | Low-latency CBR; **in-band SPS/PPS on every IDR** (no global header) — matches x264 `repeat-headers`; client decode unchanged. |
| Client | None. The stream is codec-agnostic H.264. |

## Architecture (`host/src/`)

- **`Encoder` interface unchanged** (`open`/`extradata`/`encode`/`flush`).
- **New `NvencEncoder : Encoder`** and **`VaapiEncoder : Encoder`** (FFmpeg, like `SoftwareEncoder`).
- **`make_encoder(pref)` factory** (`encoder_factory.{h,cpp}`) → `std::unique_ptr<Encoder>`. It returns an **`AutoEncoder`** wrapper (also an `Encoder`) that, on `open(w,h,fps,bitrate)`, constructs+opens candidates in preference order and delegates the rest to the winner. If `pref` names a specific backend, only that one is tried (no fallback masking a forced choice — a forced `open()` failure is fatal so misconfig is visible; `auto` falls through to software).
- **`stream_main`**: replace `SoftwareEncoder enc;` with `auto enc = droppix::make_encoder(pref); ... StreamDaemon(..., *enc, ...)`. Parse `--encoder <val>` → `pref`. `StreamDaemon` is unchanged (takes `Encoder&`).
- **Shared `bgra_to_nv12` helper** (`nv12_convert.{h,cpp}` or a small shared unit): the `sws_getContext(BGRA→NV12)` + `sws_scale` currently inline in `SoftwareEncoder`, factored out and reused by all three so the conversion lives in one place.

## `NvencEncoder`

- `open()`: `avcodec_find_encoder_by_name("h264_nvenc")`; `pix_fmt = AV_PIX_FMT_NV12` (system memory — nvenc uploads internally); low-latency opts: `preset=p4` (or `p1` for lowest latency), `tune=ll` (low-latency), `zerolatency=1`, `delay=0`, `rc=cbr`, `bit_rate`/`rc_max_rate`/`rc_buffer_size` as `SoftwareEncoder` sets them. Ensure headers are **in-band** (do NOT set `AV_CODEC_FLAG_GLOBAL_HEADER`; set `-forced-idr`/repeat-headers equivalent so SPS/PPS ride each IDR). `avcodec_open2` failure → `open()` returns false (so the factory falls back).
- `encode()`: `bgra_to_nv12` into the CPU NV12 frame → `avcodec_send_frame` → drain packets (same as `SoftwareEncoder::encode`/`drain`).
- `extradata()`/`flush()`/dtor: mirror `SoftwareEncoder`.

## `VaapiEncoder`

- `open()`:
  1. **Device:** `av_hwdevice_ctx_create(&dev, AV_HWDEVICE_TYPE_VAAPI, nullptr, ...)` (default), then a probe: for each `/dev/dri/renderD128..renderD135`, create a VAAPI device on it and check (via `vaQueryConfigEntrypoints` / an ffmpeg constraints query, or simply attempting `avcodec_open2` on `h264_vaapi`) whether H264 **encode** is supported; keep the first that works. This skips the NVDEC (decode-only) default and selects the Intel iGPU node.
  2. **Frames context:** `av_hwframe_ctx_alloc` with `format = AV_PIX_FMT_VAAPI`, `sw_format = AV_PIX_FMT_NV12`, w/h; `av_hwframe_ctx_init`.
  3. **Codec:** `h264_vaapi`; `ctx->pix_fmt = AV_PIX_FMT_VAAPI`; `ctx->hw_device_ctx`/`hw_frames_ctx` set; low-latency CBR opts (`rc_mode=CBR`, `low_power=1` where supported); in-band headers. `avcodec_open2` failure → `open()` false.
- `encode()`: `bgra_to_nv12` into a CPU NV12 frame → `av_hwframe_get_buffer` a VAAPI frame → `av_hwframe_transfer_data(vaapi_frame, nv12_cpu, 0)` → `avcodec_send_frame(vaapi_frame)` → drain.
- `extradata()`/`flush()`/dtor: mirror `SoftwareEncoder`; free the hw device/frames contexts.

## CLI + build

- `stream_main` gains `--encoder <auto|nvenc|vaapi|software>` (default `auto`). The GUI/args_builder does NOT need to expose it now (host default `auto` is correct); it can be added later. The streamer logs the chosen backend (`encoder: using nvenc` / `vaapi (renderD128)` / `software`).
- CMake: the new sources join the existing FFmpeg-linked host target. NVENC/VAAPI symbols come from the same `libavcodec` already linked (runtime availability is what `open()` probes) — no new link deps beyond what FFmpeg pulls.

## Testing

- **Factory selection (unit-tested, no GPU):** a pure `select_encoder_order(pref)` returning the ordered backend list — `auto`→[nvenc,vaapi,software], `nvenc`→[nvenc], etc. — asserted directly. And an `AutoEncoder` fallback test using **fake `Encoder`s** whose `open()` returns false/true: prove `auto` cascades nvenc→vaapi→software and stops at the first success; prove a forced backend does NOT fall back.
- **`bgra_to_nv12` helper:** a small unit test on a tiny known BGRA buffer → NV12 plane sizes/values (or at least that it runs and produces the right plane dimensions for a 2×2/16×16 frame).
- **On-device (host):** run the streamer; the log names the opened backend (`nvenc` on this host); the tablet streams normally; CPU usage is markedly lower than x264 for the same resolution/fps. `--encoder vaapi` selects the Intel iGPU (renderD128) and streams; `--encoder software` still works (fallback intact). Decoding on the tablet is unchanged (in-band SPS/PPS).

## Out of scope

- Zero-copy capture→GPU (evdi provides CPU frames; the per-frame `sws`+upload stays, but the encode moves to the GPU — the win).
- QSV / AMF / other backends; HEVC / AV1.
- Exposing the encoder choice in the host GUI (host default `auto` suffices; a later toggle if wanted).
- No protocol/client/Android change.
