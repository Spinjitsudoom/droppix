# Lessons index

## How to use

1. Grep this file for a failure tag (e.g. `silent-failure`, `oom`) or subsystem tag (e.g. `encoder`, `protocol`).
2. Open **only** the matching detail file(s). Do not load every lesson.

## Tag taxonomy

- **Subsystem:** `host`, `android`, `client`, `protocol`, `encoder`, `evdi`, `input`, `transport`, `gui`, `packaging`, `desktop-backend`, `audio`, `tls`
- **Failure class:** `silent-failure`, `data-loss`, `performance`, `oom`, `truncation`, `wrong-answer`, `regression`, `flake`, `gotcha`
- **Severity:** `critical`, `high`, `medium`, `low`
- **Doc type:** `lesson`, `constraint`, `gotcha`

## Lessons

| ID | Title | Tags | Severity | Date | File |
|---|---|---|---|---|---|
| L-2026-07-18-ghost-ws-stream | Video keeps playing after disconnect (orphaned WebSocket) | client, transport, silent-failure | high | 2026-07-18 | [ghost-ws-stream.md](ghost-ws-stream.md) |
| L-2026-07-18-delta-drop-corruption | Shaky/corrupted video from dropping delta frames | client, encoder, wrong-answer, performance | high | 2026-07-18 | [delta-drop-corruption.md](delta-drop-corruption.md) |
| G-2026-07-18-playwright-autoplay | Playwright Chromium never suspends AudioContext; `await resume()` hangs connect under real autoplay | client, audio, flake | high | 2026-07-18 | [playwright-autoplay.md](playwright-autoplay.md) |
| L-2026-07-18-mock-overlay-lipsync | A/V drift when re-encoding movie with burned-in overlay (preset/backpressure) | encoder, audio, performance | medium | 2026-07-18 | [mock-overlay-lipsync.md](mock-overlay-lipsync.md) |

## Related external docs

- [../ARCHITECTURE.md](../ARCHITECTURE.md) — system architecture
- [../STATUS.md](../STATUS.md) — feature / design status
- [../WIRE.md](../WIRE.md) — current protocol
- [../../scratchpad.md](../../scratchpad.md) — session memory
- [../README.md](../README.md) — docs hub

## Adding a new lesson

Copy [`_TEMPLATE.md`](_TEMPLATE.md). Use IDs:

- `L-YYYY-MM-DD-shortslug` — fixed mistakes
- `C-YYYY-MM-DD-shortslug` — hard constraints
- `G-YYYY-MM-DD-shortslug` — gotchas

Add one row to the table above. Keep the detail file short; link to code/commits.
