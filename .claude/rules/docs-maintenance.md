---
description: Keep living droppix docs current whenever code or protocol changes
alwaysApply: true
---

# Docs maintenance (droppix)

Living docs are part of every feature/protocol/packaging change. Do not end a turn with shipped code and stale STATUS/README/WIRE.

Read [`scratchpad.md`](../../scratchpad.md) first, then [`docs/STATUS.md`](../../docs/STATUS.md) before trusting any design-spec Status line.

## Living docs (must stay current)

| Doc | Update when… |
|---|---|
| [`docs/STATUS.md`](../../docs/STATUS.md) | Feature ships, partials, or roadmap changes; bump **Last verified** + commit/date |
| [`docs/WIRE.md`](../../docs/WIRE.md) | Any wire change: `MsgType`, HELLO version/fields, framing, pairing/TLS assumptions |
| [`README.md`](../../README.md) | User-visible capability, layout path, build/req, or packaging story changes |
| [`docs/README.md`](../../docs/README.md) | Docs hub / open-roadmap summary changes |
| [`scratchpad.md`](../../scratchpad.md) | Session memory: architecture, decisions, active work |
| [`client/README.md`](../../client/README.md) | Desktop-client role, build, or packaging changes |
| [`docs/lessons/`](../../docs/lessons/) | Non-obvious bug, silent failure, constraint, or vendor gotcha |

## Historical docs

| Path | Rule |
|---|---|
| `docs/superpowers/specs/*-design.md` | On ship: `**Status:** Shipped on master (YYYY-MM-DD).` Optional short Update note. Do not erase original decisions. |
| `docs/superpowers/plans/*.md` | Build journals - do not mass-rewrite completed checklists. |

## Protocol changes (hard rule)

If you touch `host/src/protocol.{h,cpp}` or Android/client protocol codecs:

1. Keep C++ / Kotlin / desktop client codecs byte-identical.
2. Update shared test vectors / protocol tests.
3. Update `docs/WIRE.md` and STATUS protocol row in the same change.
4. Bump `kProtocolVersion` when the HELLO/wire shape changes; document back-compat.

## End-of-turn checklist

```
[ ] Code + tests for the change
[ ] docs/STATUS.md row + Last verified (if feature/roadmap)
[ ] docs/WIRE.md (if protocol)
[ ] README.md (if user-facing)
[ ] Spec Status header → Shipped/Partial (if design existed)
[ ] scratchpad.md session note (if architecture/state changed)
[ ] docs/lessons/ entry (if non-obvious bug/constraint/gotcha)
```
